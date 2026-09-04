#define F_CPU 1000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>


/* =========================================================
   DHT11
   ========================================================= */

#define DHT_PORT PORTD
#define DHT_DDR  DDRD
#define DHT_PIN  PIND
#define DHT_BIT  PD2


/* =========================================================
   I2C LCD / PCF8574
   ========================================================= */

#define LCD_RS 0x01
#define LCD_RW 0x02
#define LCD_EN 0x04
#define LCD_BL 0x08

uint8_t lcd_address;


/* =========================================================
   I2C FUNCTIONS
   ========================================================= */

void i2c_init(void)
{
    TWSR = 0x00;

    /*
       F_CPU = 1 MHz
       TWBR = 0

       I2C frequency ≈ 62.5 kHz
    */

    TWBR = 0;

    TWCR = (1 << TWEN);
}


uint8_t i2c_start(void)
{
    TWCR = (1 << TWINT) |
           (1 << TWSTA) |
           (1 << TWEN);

    while (!(TWCR & (1 << TWINT)));

    return (TWSR & 0xF8);
}


uint8_t i2c_write(uint8_t data)
{
    TWDR = data;

    TWCR = (1 << TWINT) |
           (1 << TWEN);

    while (!(TWCR & (1 << TWINT)));

    return (TWSR & 0xF8);
}


void i2c_stop(void)
{
    TWCR = (1 << TWINT) |
           (1 << TWSTO) |
           (1 << TWEN);

    _delay_us(10);
}


uint8_t i2c_device_exists(uint8_t address)
{
    uint8_t status;

    i2c_start();

    status = i2c_write((address << 1) | 0);

    i2c_stop();

    /*
       0x18 = SLA+W transmitted,
       ACK received
    */

    if (status == 0x18)
        return 1;

    return 0;
}


/* =========================================================
   LCD FUNCTIONS
   ========================================================= */

void lcd_write_port(uint8_t data)
{
    i2c_start();

    i2c_write((lcd_address << 1) | 0);

    i2c_write(data);

    i2c_stop();
}


void lcd_pulse(uint8_t data)
{
    /*
       EN = HIGH
    */

    lcd_write_port(data | LCD_EN | LCD_BL);

    _delay_us(1);

    /*
       EN = LOW
    */

    lcd_write_port((data & ~LCD_EN) | LCD_BL);

    _delay_us(100);
}


void lcd_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t data = 0;

    /*
       PCF8574 P4-P7 → LCD D4-D7

       nibble 0000xxxx
       becomes  xxxx0000
    */

    data = (nibble & 0x0F) << 4;

    /*
       RS = 1 → data
       RS = 0 → command
    */

    if (rs)
        data |= LCD_RS;

    /*
       RW = 0 → write
    */

    data |= LCD_BL;

    lcd_pulse(data);
}


void lcd_command(uint8_t command)
{
    /*
       High nibble
    */

    lcd_nibble(command >> 4, 0);

    /*
       Low nibble
    */

    lcd_nibble(command & 0x0F, 0);

    _delay_ms(2);
}


void lcd_data(uint8_t data)
{
    /*
       High nibble
    */

    lcd_nibble(data >> 4, 1);

    /*
       Low nibble
    */

    lcd_nibble(data & 0x0F, 1);

    _delay_us(50);
}


void lcd_init(void)
{
    _delay_ms(50);

    /*
       Force LCD into 4-bit mode
    */

    lcd_nibble(0x03, 0);

    _delay_ms(5);

    lcd_nibble(0x03, 0);

    _delay_us(150);

    lcd_nibble(0x03, 0);

    _delay_us(150);

    lcd_nibble(0x02, 0);

    _delay_us(150);


    /*
       4-bit mode
       2 lines
       5x8 font
    */

    lcd_command(0x28);


    /*
       Display ON
       Cursor OFF
    */

    lcd_command(0x0C);


    /*
       Cursor automatically moves right
    */

    lcd_command(0x06);


    /*
       Clear display
    */

    lcd_command(0x01);

    _delay_ms(2);
}


void lcd_set_cursor(uint8_t row, uint8_t col)
{
    uint8_t address;

    if (row == 0)
        address = 0x00 + col;
    else
        address = 0x40 + col;

    lcd_command(0x80 | address);
}


void lcd_string(const char *str)
{
    while (*str)
    {
        lcd_data(*str);
        str++;
    }
}

/* =========================================================
   DHT11 START SIGNAL
   ========================================================= */

void dht_request(void)
{
    DHT_DDR  |= (1 << DHT_BIT);    /* set pin as output */
    DHT_PORT &= ~(1 << DHT_BIT);   /* pull line LOW */
    _delay_ms(20);                 /* hold LOW for >=18ms (start pulse) */
    DHT_PORT |= (1 << DHT_BIT);    /* release line HIGH */
}


/* =========================================================
   DHT11 RESPONSE CHECK
   (waits through the 80us LOW + 80us HIGH ack pulses)
   ========================================================= */

uint8_t dht_response(void)
{
    uint16_t timeout = 0;

    DHT_DDR &= ~(1 << DHT_BIT);    /* release line, set as input */

    /* wait while line is HIGH (end of MCU's release pulse) */
    while (DHT_PIN & (1 << DHT_BIT))
    {
        _delay_us(1);
        if (++timeout > 100) return 0;
    }

    timeout = 0;
    /* wait while line is LOW (DHT11's 80us LOW ack) */
    while (!(DHT_PIN & (1 << DHT_BIT)))
    {
        _delay_us(1);
        if (++timeout > 100) return 0;
    }

    timeout = 0;
    /* wait while line is HIGH (DHT11's 80us HIGH ack) */
    while (DHT_PIN & (1 << DHT_BIT))
    {
        _delay_us(1);
        if (++timeout > 100) return 0;
    }

    return 1;   /* response received successfully */
}


/* =========================================================
   RECEIVE ONE BYTE
   (fixed 30us-delay-then-sample method, MSB first)
   ========================================================= */

uint8_t dht_receive_byte(void)
{
    uint8_t byte = 0;
    uint16_t timeout;

    for (uint8_t i = 0; i < 8; i++)
    {
        /* each bit starts with ~50us LOW - wait for it to end */
        timeout = 0;
        while (!(DHT_PIN & (1 << DHT_BIT)))
        {
            _delay_us(1);
            if (++timeout > 100) return 0;
        }

        /*
           Now line is HIGH. Wait 30us, then sample once.
           If still HIGH after 30us -> bit is 1 (long ~70us pulse)
           If already LOW           -> bit is 0 (short ~26-28us pulse)
        */
        _delay_us(30);

        if (DHT_PIN & (1 << DHT_BIT))
            byte = (byte << 1) | 0x01;   /* logic HIGH -> bit 1 */
        else
            byte = (byte << 1);          /* logic LOW  -> bit 0 */

        /* wait for the HIGH pulse to finish before next bit */
        timeout = 0;
        while (DHT_PIN & (1 << DHT_BIT))
        {
            _delay_us(1);
            if (++timeout > 100) break;
        }
    }

    return byte;
}


/* =========================================================
   DHT11 FULL READ
   ========================================================= */

uint8_t dht11_read(uint8_t *humidity, uint8_t *temperature)
{
    uint8_t I_RH, D_RH, I_Temp, D_Temp, CheckSum;

    dht_request();

    if (!dht_response())
        return 0;

    I_RH      = dht_receive_byte();
    D_RH      = dht_receive_byte();
    I_Temp    = dht_receive_byte();
    D_Temp    = dht_receive_byte();
    CheckSum  = dht_receive_byte();

    if ((uint8_t)(I_RH + D_RH + I_Temp + D_Temp) != CheckSum)
        return 0;

    *humidity    = I_RH;
    *temperature = I_Temp;

    return 1;
}


/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    uint8_t found = 0;

    uint8_t humidity;
    uint8_t temperature;


    /* -----------------------------------------------------
       Disable JTAG
       ----------------------------------------------------- */

    MCUCSR = (1 << JTD);
    MCUCSR = (1 << JTD);


    /* -----------------------------------------------------
       Initialize I2C
       ----------------------------------------------------- */

    i2c_init();


    /* -----------------------------------------------------
       Search for PCF8574 LCD backpack
       ----------------------------------------------------- */

    for (uint8_t addr = 0x20;
         addr <= 0x27;
         addr++)
    {
        if (i2c_device_exists(addr))
        {
            lcd_address = addr;

            found = 1;

            break;
        }
    }


    /* -----------------------------------------------------
       No LCD found
       ----------------------------------------------------- */

    if (!found)
    {
        /*
           Stay here if LCD backpack isn't detected.
        */

        while (1)
        {
        }
    }


    /* -----------------------------------------------------
       Initialize LCD
       ----------------------------------------------------- */

    lcd_init();


    /* -----------------------------------------------------
       Initial message
       ----------------------------------------------------- */

    lcd_set_cursor(0, 0);
    lcd_string("Temperature:");

    lcd_set_cursor(1, 0);
    lcd_string("Humidity:");


    /* -----------------------------------------------------
       Main loop
       ----------------------------------------------------- */

    while (1)
    {
        /*
           Read DHT11
        */

        if (dht11_read(&humidity, &temperature))
        {
            /*
               First line:

               Temp: 25 C
            */

            lcd_set_cursor(0, 0);

            lcd_string("Temp: ");

            lcd_data('0' + (temperature / 10));

            lcd_data('0' + (temperature % 10));

            lcd_string(" C    ");


            /*
               Second line:

               Hum: 60 %
            */

            lcd_set_cursor(1, 0);

            lcd_string("Hum: ");

            lcd_data('0' + (humidity / 10));

            lcd_data('0' + (humidity % 10));

            lcd_string(" %    ");
        }
        else
        {
            /*
               DHT11 communication failed
            */

            lcd_set_cursor(0, 0);
            lcd_string("DHT11 ERROR     ");

            lcd_set_cursor(1, 0);
            lcd_string("Check sensor    ");
        }


        /*
           DHT11 should not be read continuously.

           Wait 2 seconds before next measurement.
        */

        _delay_ms(2000);
    }
}