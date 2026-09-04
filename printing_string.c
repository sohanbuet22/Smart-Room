// #define F_CPU 1000000UL
// #include <avr/io.h>
// #include <util/delay.h>

// #define LCD_PORT PORTC
// #define LCD_DDR  DDRC
// #define RS PC6
// #define EN PC7
// #define D4 PC0
// #define D5 PC1
// #define D6 PC2
// #define D7 PC3

// void lcd_pulse_enable(void)
// {
//     LCD_PORT |= (1 << EN);
//     _delay_us(1);
//     LCD_PORT &= ~(1 << EN);
//     _delay_us(100);
// }

// void lcd_send_nibble(uint8_t nibble)
// {
//     // Clear D4-D7 bits first
//     LCD_PORT &= ~((1<<D4)|(1<<D5)|(1<<D6)|(1<<D7));

//     if (nibble & 0x01) LCD_PORT |= (1 << D4);
//     if (nibble & 0x02) LCD_PORT |= (1 << D5);
//     if (nibble & 0x04) LCD_PORT |= (1 << D6);
//     if (nibble & 0x08) LCD_PORT |= (1 << D7);

//     lcd_pulse_enable();
// }

// void lcd_command(uint8_t cmd)
// {
//     LCD_PORT &= ~(1 << RS);       // RS = 0 for command
//     lcd_send_nibble(cmd >> 4);    // high nibble first
//     lcd_send_nibble(cmd & 0x0F);  // low nibble
//     _delay_ms(2);
// }

// void lcd_set_cursor(uint8_t row, uint8_t col)
// {
//     uint8_t address;

//     if (row == 0)
//         address = 0x00 + col;   // Line 1 starts at 0x00
//     else
//         address = 0x40 + col;   // Line 2 starts at 0x40

//     lcd_command(0x80 | address);  // 0x80 = "Set DDRAM address" command
// }

// void lcd_data(uint8_t data)
// {
//     LCD_PORT |= (1 << RS);        // RS = 1 for data
//     lcd_send_nibble(data >> 4);
//     lcd_send_nibble(data & 0x0F);
//     _delay_ms(2);
// }

// void lcd_init(void)
// {
//     LCD_DDR |= (1<<RS)|(1<<EN)|(1<<D4)|(1<<D5)|(1<<D6)|(1<<D7);
//     _delay_ms(20);   // wait for LCD power-up

//     // Special init sequence to force 4-bit mode
//     lcd_send_nibble(0x03);
//     _delay_ms(5);
//     lcd_send_nibble(0x03);
//     _delay_us(150);
//     lcd_send_nibble(0x03);
//     lcd_send_nibble(0x02);   // now set to 4-bit mode

//     lcd_command(0x28);  // 4-bit mode, 2 lines, 5x8 font
//     lcd_command(0x0C);  // display ON, cursor OFF
//     lcd_command(0x06);  // auto-increment cursor
//     lcd_command(0x01);  // clear display
//     _delay_ms(2);
// }

// void lcd_string(const char *str)
// {
//     while (*str)
//     {
//         lcd_data(*str++);
//     }
// }

// int main(void)
// {
//     MCUCSR = (1 << JTD);
//     MCUCSR = (1 << JTD);   // disable JTAG so PC2-PC5 work as normal I/O

//     lcd_init();

//     lcd_set_cursor(0,0);
//     lcd_string("sohan!!");


//     lcd_set_cursor(1,0);
//     lcd_string("CSE 316 Exp2");

//     while (1)
//     {
//         // nothing else to do
//     }
// }






#define F_CPU 1000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#define LCD_RS 0x01
#define LCD_RW 0x02
#define LCD_EN 0x04
#define LCD_BL 0x08

uint8_t lcd_address;

/* ---------------- I2C ---------------- */

void i2c_init(void)
{
    TWSR = 0x00;
    TWBR = 0;              // ~62.5 kHz at 1 MHz
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

/* Check whether an I2C device exists */
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

/* ---------------- LCD ---------------- */

void lcd_write_port(uint8_t data)
{
    i2c_start();

    i2c_write((lcd_address << 1) | 0);
    i2c_write(data);

    i2c_stop();
}

void lcd_pulse(uint8_t data)
{
    lcd_write_port(data | LCD_EN | LCD_BL);

    _delay_us(1);

    lcd_write_port((data & ~LCD_EN) | LCD_BL);

    _delay_us(100);
}

void lcd_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t data = 0;

    data = (nibble & 0x0F) << 4;

    if (rs)
        data |= LCD_RS;

    /* RW = 0 */
    data |= LCD_BL;

    lcd_pulse(data);
}

void lcd_command(uint8_t command)
{
    lcd_nibble(command >> 4, 0);
    lcd_nibble(command & 0x0F, 0);

    _delay_ms(2);
}

void lcd_data(uint8_t data)
{
    lcd_nibble(data >> 4, 1);
    lcd_nibble(data & 0x0F, 1);

    _delay_us(50);
}

void lcd_init(void)
{
    _delay_ms(50);

    lcd_nibble(0x03, 0);
    _delay_ms(5);

    lcd_nibble(0x03, 0);
    _delay_us(150);

    lcd_nibble(0x03, 0);
    _delay_us(150);

    lcd_nibble(0x02, 0);
    _delay_us(150);

    lcd_command(0x28);     // 4-bit, 2-line
    lcd_command(0x0C);     // display ON
    lcd_command(0x06);     // entry mode
    lcd_command(0x01);     // clear

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

/* ---------------- MAIN ---------------- */

int main(void)
{
    /* LED on PD0 for debugging */
    DDRD |= (1 << PD0);
    PORTD &= ~(1 << PD0);

    i2c_init();

    /*
       Try every possible PCF8574T address
       from 0x20 to 0x27.
    */

    for (uint8_t addr = 0x20; addr <= 0x27; addr++)
    {
        if (i2c_device_exists(addr))
        {
            lcd_address = addr;

            /*
             * Device found.
             * Turn LED ON.
             */
            PORTD |= (1 << PD0);

            lcd_init();

            lcd_set_cursor(0, 0);
            lcd_string("HELLO  MOSADDEK");

            lcd_set_cursor(1, 0);
            lcd_string("I2C WORKING!");

            while (1);
        }
    }

    /*
       No device found.
       Blink LED continuously.
    */
    while (1)
    {
        PORTD ^= (1 << PD0);
        _delay_ms(300);
    }
}