#define F_CPU 1000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdint.h>

/* =========================================================
   DHT11 PIN CONFIGURATION (UPDATED TO PD6)
   ========================================================= */
#define DHT_PORT PORTD
#define DHT_DDR  DDRD
#define DHT_PIN  PIND
#define DHT_BIT  PD6

/* =========================================================
   VISITOR COUNTER & LOAD CONTROL VARIABLES
   ========================================================= */
volatile int count = 0;
volatile uint8_t flag1 = 0, flag2 = 0;

/* =========================================================
   I2C LCD / PCF8574 CONFIGURATION
   ========================================================= */
#define LCD_RS 0x01
#define LCD_RW 0x02
#define LCD_EN 0x04
#define LCD_BL 0x08

uint8_t lcd_address;

// Flag to record trigger order
volatile uint8_t sensor1_triggered = 0;
volatile uint8_t sensor2_triggered = 0;

ISR(INT0_vect) {
    if (!sensor2_triggered) {
        sensor1_triggered = 1; // Entry sequence started
    } else if (sensor2_triggered) {
        // Sensor 2 was triggered first, now Sensor 1 triggers -> EXIT COMPLETE
        if (count > 0) count--;
        sensor1_triggered = 0;
        sensor2_triggered = 0;
    }
}

ISR(INT1_vect) {
    if (!sensor1_triggered) {
        sensor2_triggered = 1; // Exit sequence started
    } else if (sensor1_triggered) {
        // Sensor 1 was triggered first, now Sensor 2 triggers -> ENTRY COMPLETE
        count++;
        sensor1_triggered = 0;
        sensor2_triggered = 0;
    }
}

/* =========================================================
   I2C FUNCTIONS
   ========================================================= */
void i2c_init(void) {
    TWSR = 0x00;
    TWBR = 0;
    TWCR = (1 << TWEN);
}

uint8_t i2c_start(void) {
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
    return (TWSR & 0xF8);
}

uint8_t i2c_write(uint8_t data) {
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
    return (TWSR & 0xF8);
}

void i2c_stop(void) {
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    _delay_us(10);
}

uint8_t i2c_device_exists(uint8_t address) {
    uint8_t status;
    i2c_start();
    status = i2c_write((address << 1) | 0);
    i2c_stop();
    return (status == 0x18);
}

/* =========================================================
   LCD FUNCTIONS
   ========================================================= */
void lcd_write_port(uint8_t data) {
    i2c_start();
    i2c_write((lcd_address << 1) | 0);
    i2c_write(data);
    i2c_stop();
}

void lcd_pulse(uint8_t data) {
    lcd_write_port(data | LCD_EN | LCD_BL);
    _delay_us(1);
    lcd_write_port((data & ~LCD_EN) | LCD_BL);
    _delay_us(100);
}

void lcd_nibble(uint8_t nibble, uint8_t rs) {
    uint8_t data = (nibble & 0x0F) << 4;
    if (rs) data |= LCD_RS;
    data |= LCD_BL;
    lcd_pulse(data);
}

void lcd_command(uint8_t command) {
    lcd_nibble(command >> 4, 0);
    lcd_nibble(command & 0x0F, 0);
    _delay_ms(2);
}

void lcd_data(uint8_t data) {
    lcd_nibble(data >> 4, 1);
    lcd_nibble(data & 0x0F, 1);
    _delay_us(50);
}

void lcd_init(void) {
    _delay_ms(50);
    lcd_nibble(0x03, 0);
    _delay_ms(5);
    lcd_nibble(0x03, 0);
    _delay_us(150);
    lcd_nibble(0x03, 0);
    _delay_us(150);
    lcd_nibble(0x02, 0);
    _delay_us(150);

    lcd_command(0x28); // 4-bit mode, 2 lines, 5x8 font
    lcd_command(0x0C); // Display ON, Cursor OFF
    lcd_command(0x06); // Entry mode
    lcd_command(0x01); // Clear display
    _delay_ms(2);
}

void lcd_set_cursor(uint8_t row, uint8_t col) {
    uint8_t address = (row == 0) ? (0x00 + col) : (0x40 + col);
    lcd_command(0x80 | address);
}

void lcd_string(const char *str) {
    while (*str) {
        lcd_data(*str);
        str++;
    }
}

/* Helper to print integers on LCD */
void lcd_number(int num) {
    char buf[10];
    int i = 0;

    if (num == 0) {
        lcd_data('0');
        return;
    }
    while (num > 0) {
        buf[i++] = (num % 10) + '0';
        num /= 10;
    }
    while (i > 0) {
        lcd_data(buf[--i]);
    }
}

/* =========================================================
   DHT11 DRIVER FUNCTIONS
   ========================================================= */
void dht_request(void) {
    DHT_DDR  |= (1 << DHT_BIT);
    DHT_PORT &= ~(1 << DHT_BIT);
    _delay_ms(20);
    DHT_PORT |= (1 << DHT_BIT);
}

uint8_t dht_response(void) {
    uint16_t timeout = 0;
    DHT_DDR &= ~(1 << DHT_BIT);

    while (DHT_PIN & (1 << DHT_BIT)) {
        _delay_us(1);
        if (++timeout > 100) return 0;
    }
    timeout = 0;
    while (!(DHT_PIN & (1 << DHT_BIT))) {
        _delay_us(1);
        if (++timeout > 100) return 0;
    }
    timeout = 0;
    while (DHT_PIN & (1 << DHT_BIT)) {
        _delay_us(1);
        if (++timeout > 100) return 0;
    }
    return 1;
}

uint8_t dht_receive_byte(void) {
    uint8_t byte = 0;
    uint16_t timeout;

    for (uint8_t i = 0; i < 8; i++) {
        timeout = 0;
        while (!(DHT_PIN & (1 << DHT_BIT))) {
            _delay_us(1);
            if (++timeout > 100) return 0;
        }

        _delay_us(30);

        if (DHT_PIN & (1 << DHT_BIT))
            byte = (byte << 1) | 0x01;
        else
            byte = (byte << 1);

        timeout = 0;
        while (DHT_PIN & (1 << DHT_BIT)) {
            _delay_us(1);
            if (++timeout > 100) break;
        }
    }
    return byte;
}

uint8_t dht11_read(uint8_t *humidity, uint8_t *temperature) {
    uint8_t I_RH, D_RH, I_Temp, D_Temp, CheckSum;

    dht_request();
    if (!dht_response()) return 0;

    I_RH     = dht_receive_byte();
    D_RH     = dht_receive_byte();
    I_Temp   = dht_receive_byte();
    D_Temp   = dht_receive_byte();
    CheckSum = dht_receive_byte();

    if ((uint8_t)(I_RH + D_RH + I_Temp + D_Temp) != CheckSum)
        return 0;

    *humidity    = I_RH;
    *temperature = I_Temp;
    return 1;
}

/* =========================================================
   MAIN APPLICATION
   ========================================================= 
========================================================= */
int main(void) {
    uint8_t humidity = 0, temperature = 0;
    uint8_t found = 0;
    uint16_t dht_timer = 0;
    uint16_t timeout_counter = 0;

    // Disable JTAG
    MCUCSR = (1 << JTD);
    MCUCSR = (1 << JTD);

    // Relay/LED output setup (PB0)
    DDRB |= (1 << PB0);
    PORTB &= ~(1 << PB0);

    // External Interrupts (INT0 on PD2, INT1 on PD3)
    // ISC01=1, ISC00=0 (Falling Edge for INT0)
    // ISC11=1, ISC10=0 (Falling Edge for INT1)
    MCUCR |= (1 << ISC01) | (1 << ISC11);
    MCUCR &= ~((1 << ISC00) | (1 << ISC10));
    GICR  |= (1 << INT0) | (1 << INT1);

    // Enable Global Interrupts
    sei();

    // Initialize I2C and find LCD
    i2c_init();
    for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
        if (i2c_device_exists(addr)) {
            lcd_address = addr;
            found = 1;
            break;
        }
    }

    if (!found) {
        while (1); // Halt if LCD not found
    }

    lcd_init();

    while (1) {
        // 1. Control Load (Fan/Light/LED) based on Count
        if (count > 0) {
            PORTB |= (1 << PB0);
        } else {
            PORTB &= ~(1 << PB0);
        }

        // 2. Timeout logic: Reset flags if someone stands in front of 1 sensor without entering
        if (sensor1_triggered || sensor2_triggered) {
            timeout_counter++;
            if (timeout_counter > 30) { // ~3 seconds timeout
                sensor1_triggered = 0;
                sensor2_triggered = 0;
                timeout_counter = 0;
            }
        } else {
            timeout_counter = 0;
        }

        // 3. Read DHT11 non-blockingly (~every 2 seconds)
        if (dht_timer >= 20) {
            if (dht11_read(&humidity, &temperature)) {
                lcd_set_cursor(0, 0);
                lcd_string("T:");
                lcd_data('0' + (temperature / 10));
                lcd_data('0' + (temperature % 10));
                lcd_data(223); // Degree symbol
                lcd_string("C H:");
                lcd_data('0' + (humidity / 10));
                lcd_data('0' + (humidity % 10));
                lcd_string("%   ");
            } else {
                lcd_set_cursor(0, 0);
                lcd_string("DHT Error       ");
            }
            dht_timer = 0;
        }

        // 4. Update Count Display Line
        lcd_set_cursor(1, 0);
        lcd_string("Count: ");
        lcd_number(count);
        lcd_string("     "); 

        _delay_ms(100);
        dht_timer++;
    }
}