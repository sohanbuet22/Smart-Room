/*
#define F_CPU 1000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>

#define LCD_PORT PORTC
#define LCD_DDR DDRC
#define RS PC6
#define EN PC7
#define D4 PC0
#define D5 PC1
#define D6 PC2
#define D7 PC3

void lcd_pulse_enable(void)
{
    LCD_PORT |= (1 << EN);
    _delay_us(1);
    LCD_PORT &= ~(1 << EN);
    _delay_us(100);
}

void lcd_send_nibble(uint8_t nibble)
{
    // Clear D4-D7 bits first
    LCD_PORT &= ~((1 << D4) | (1 << D5) | (1 << D6) | (1 << D7));

    if (nibble & 0x01)
        LCD_PORT |= (1 << D4);
    if (nibble & 0x02)
        LCD_PORT |= (1 << D5);
    if (nibble & 0x04)
        LCD_PORT |= (1 << D6);
    if (nibble & 0x08)
        LCD_PORT |= (1 << D7);

    lcd_pulse_enable();
}

void lcd_command(uint8_t cmd)
{
    LCD_PORT &= ~(1 << RS);      // RS = 0 for command
    lcd_send_nibble(cmd >> 4);   // high nibble first
    lcd_send_nibble(cmd & 0x0F); // low nibble
    _delay_ms(2);
}

void lcd_set_cursor(uint8_t row, uint8_t col)
{
    uint8_t address;

    if (row == 0)
        address = 0x00 + col; // Line 1 starts at 0x00
    else
        address = 0x40 + col; // Line 2 starts at 0x40

    lcd_command(0x80 | address); // 0x80 = "Set DDRAM address" command
}

void lcd_data(uint8_t data)
{
    LCD_PORT |= (1 << RS); // RS = 1 for data
    lcd_send_nibble(data >> 4);
    lcd_send_nibble(data & 0x0F);
    _delay_ms(2);
}

void lcd_init(void)
{
    LCD_DDR |= (1 << RS) | (1 << EN) | (1 << D4) | (1 << D5) | (1 << D6) | (1 << D7);
    _delay_ms(20); // wait for LCD power-up

    // Special init sequence to force 4-bit mode
    lcd_send_nibble(0x03);
    _delay_ms(5);
    lcd_send_nibble(0x03);
    _delay_us(150);
    lcd_send_nibble(0x03);
    lcd_send_nibble(0x02); // now set to 4-bit mode

    lcd_command(0x28); // 4-bit mode, 2 lines, 5x8 font
    lcd_command(0x0C); // display ON, cursor OFF
    lcd_command(0x06); // auto-increment cursor
    lcd_command(0x01); // clear display
    _delay_ms(2);
}

void lcd_string(const char *str)
{
    while (*str)
    {
        lcd_data(*str++);
    }
}

void adc_init(void)
{
    ADMUX = (1 << REFS0);                                  // AVCC as reference, right-adjusted, channel 0 (PA0)
    ADCSRA = (1 << ADEN)                                   // Enable ADC
             | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); // Prescaler = 128
}

uint16_t adc_read(uint8_t channel)
{
    ADMUX = (ADMUX & 0xF0) | (channel & 0x0F); // select channel, keep REFS bits
    ADCSRA |= (1 << ADSC);                     // start conversion
    while (ADCSRA & (1 << ADSC))
        ;       // wait until conversion completes
    return ADC; // 10-bit result
}

void float_to_string(uint16_t adc_val, char *buffer)
{
    // voltage in millivolts: adc_val * 5000 / 1023
    uint32_t mv = ((uint32_t)adc_val * 5000UL) / 1023UL;

    uint16_t volts = mv / 1000;
    uint16_t decimals = (mv % 1000) / 10; // 2 decimal places

    buffer[0] = '0' + volts;
    buffer[1] = '.';
    buffer[2] = '0' + (decimals / 10);
    buffer[3] = '0' + (decimals % 10);
    buffer[4] = 'V';
    buffer[5] = '\0';
}

int main(void)
{
    MCUCSR = (1 << JTD);
    MCUCSR = (1 << JTD); // disable JTAG for PORTC I/O

    lcd_init();
    adc_init();

    lcd_set_cursor(0, 0);
    lcd_string("Voltage:");

    char voltage_str[6];

    while (1)
    {
        uint16_t adc_val = adc_read(0);   // read PA0
        float_to_string(adc_val, voltage_str);

        lcd_set_cursor(1, 0);
        lcd_string("        ");   // clear old value with spaces (8 chars)
        lcd_set_cursor(1, 0);
        lcd_string(voltage_str);

        _delay_ms(300);   // small delay so display doesn't flicker

        
    }
}


*/


#define F_CPU 1000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

/* =========================================================
   PCF8574 -> LCD bit mapping
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

    // I2C clock approximately 62.5 kHz at 1 MHz CPU clock
    TWBR = 0;

    // Enable TWI
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
    // EN = 1
    lcd_write_port(data | LCD_EN | LCD_BL);

    _delay_us(1);

    // EN = 0
    lcd_write_port((data & ~LCD_EN) | LCD_BL);

    _delay_us(100);
}


void lcd_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t data = 0;

    /*
       Put nibble on PCF8574 P4-P7

       nibble:
       xxxx

       becomes:

       xxxx0000
    */

    data = (nibble & 0x0F) << 4;

    // RS = 1 means data
    if (rs)
        data |= LCD_RS;

    // Keep LCD backlight ON
    data |= LCD_BL;

    lcd_pulse(data);
}


void lcd_command(uint8_t command)
{
    // Send high nibble
    lcd_nibble(command >> 4, 0);

    // Send low nibble
    lcd_nibble(command & 0x0F, 0);

    _delay_ms(2);
}


void lcd_data(uint8_t data)
{
    // High nibble
    lcd_nibble(data >> 4, 1);

    // Low nibble
    lcd_nibble(data & 0x0F, 1);

    _delay_us(50);
}


void lcd_init(void)
{
    _delay_ms(50);

    // Force LCD into 4-bit mode

    lcd_nibble(0x03, 0);
    _delay_ms(5);

    lcd_nibble(0x03, 0);
    _delay_us(150);

    lcd_nibble(0x03, 0);
    _delay_us(150);

    lcd_nibble(0x02, 0);
    _delay_us(150);

    // 4-bit, 2-line, 5x8 font
    lcd_command(0x28);

    // Display ON, cursor OFF
    lcd_command(0x0C);

    // Cursor moves right
    lcd_command(0x06);

    // Clear display
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
   ADC FUNCTIONS
   ========================================================= */

void adc_init(void)
{
    /*
       REFS1:0 = 01
       AVCC is used as ADC reference.

       ADLAR = 0
       Result is right adjusted.

       MUX = 0000
       ADC0 = PA0
    */

    ADMUX = (1 << REFS0);

    /*
       ADEN = ADC Enable

       ADPS2:0 = 111
       Prescaler = 128

       At F_CPU = 1 MHz:

       ADC clock = 1 MHz / 128
                 = 7812.5 Hz
    */

    ADCSRA = (1 << ADEN) |
             (1 << ADPS2) |
             (1 << ADPS1) |
             (1 << ADPS0);
}


uint16_t adc_read(uint8_t channel)
{
    /*
       Keep REFS bits.
       Change only ADC channel.
    */

    ADMUX = (ADMUX & 0xF0) |
            (channel & 0x0F);

    /*
       Start ADC conversion
    */

    ADCSRA |= (1 << ADSC);

    /*
       Wait until conversion finishes
    */

    while (ADCSRA & (1 << ADSC));

    /*
       Return 10-bit ADC result
       Range: 0 - 1023
    */

    return ADC;
}


/* =========================================================
   ADC -> VOLTAGE
   ========================================================= */

void voltage_to_string(uint16_t adc_value, char *buffer)
{
    // Actual reference/supply voltage = 3.33 V
    uint32_t mv = ((uint32_t)adc_value * 3330UL) / 1023UL;

    uint16_t volts = mv / 1000;
    uint16_t decimals = (mv % 1000) / 10;

    buffer[0] = '0' + volts;
    buffer[1] = '.';
    buffer[2] = '0' + (decimals / 10);
    buffer[3] = '0' + (decimals % 10);
    buffer[4] = 'V';
    buffer[5] = '\0';
}


/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    /* -----------------------------------------
       Debug LED on PD0
       ----------------------------------------- */

    DDRD |= (1 << PD0);

    PORTD &= ~(1 << PD0);


    /* -----------------------------------------
       Initialize I2C
       ----------------------------------------- */

    i2c_init();


    /* -----------------------------------------
       Find PCF8574
       ----------------------------------------- */

    for (uint8_t addr = 0x20;
         addr <= 0x27;
         addr++)
    {
        if (i2c_device_exists(addr))
        {
            /*
               Found I2C device
            */

            lcd_address = addr;

            /*
               Turn debug LED ON
            */

            PORTD |= (1 << PD0);

            break;
        }
    }


    /*
       If no I2C device was found,
       lcd_address will remain 0.

       Blink LED forever.
    */

    if (lcd_address == 0)
    {
        while (1)
        {
            PORTD ^= (1 << PD0);
            _delay_ms(300);
        }
    }


    /* -----------------------------------------
       Initialize LCD
       ----------------------------------------- */

    lcd_init();


    /* -----------------------------------------
       Initialize ADC
       ----------------------------------------- */

    adc_init();


    /* -----------------------------------------
       Static LCD text
       ----------------------------------------- */

    lcd_set_cursor(0, 0);
    lcd_string("Pot Voltage:");


    char voltage_str[6];


    /* -----------------------------------------
       Main loop
       ----------------------------------------- */

    while (1)
    {
        /*
           Read potentiometer voltage
           from PA0 / ADC0
        */

        uint16_t adc_value = adc_read(0);


        /*
           Convert ADC value to:

           X.XXV
        */

        voltage_to_string(adc_value, voltage_str);


        /*
           Move to second LCD line
        */

        lcd_set_cursor(1, 0);


        /*
           Remove previous voltage
        */

        lcd_string("                ");


        /*
           Go back to beginning of second line
        */

        lcd_set_cursor(1, 0);


        /*
           Print new voltage
        */

        lcd_string(voltage_str);


        /*
           Update every 300 ms
        */

        _delay_ms(300);
    }
}