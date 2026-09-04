#define F_CPU 1000000UL
#include <avr/io.h>
#include <util/delay.h>

// ---------------- I2C (TWI) DRIVER ----------------
// ATmega32 hardware TWI pins: PC0 = SDA, PC1 = SCL (fixed, cannot be moved)

#define SCL_CLOCK 100000UL   // 100kHz standard I2C speed

void i2c_init(void)
{
    TWSR = 0x00;   // prescaler = 1
    TWBR = ((F_CPU / SCL_CLOCK) - 16) / 2;   // = 72 for 16MHz/100kHz
    TWCR = (1 << TWEN);   // enable TWI
}

void i2c_start(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

void i2c_stop(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    _delay_us(10);
}

void i2c_write(uint8_t data)
{
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

// ---------------- PCF8574 -> LCD (HD44780) DRIVER ----------------
// PCF8574 pin mapping (standard backpack):
// P0=D4  P1=D5  P2=D6  P3=D7  P4=RS  P5=RW  P6=EN  P7=Backlight

#define LCD_I2C_ADDR   0x27   // change to 0x3F if your backpack uses that address
#define LCD_BACKLIGHT  0x08   // P7 = 1 -> backlight on
#define LCD_EN         0x04   // P6
#define LCD_RW         0x02   // P5
#define LCD_RS         0x01   // P4

// Send one raw byte to the PCF8574 over I2C
void pcf8574_write(uint8_t data)
{
    i2c_start();
    i2c_write(LCD_I2C_ADDR << 1);   // address + write bit
    i2c_write(data);
    i2c_stop();
}

// Pulse EN high->low to latch a nibble into the LCD
void lcd_pulse_enable(uint8_t data)
{
    pcf8574_write(data | LCD_EN);
    _delay_us(1);
    pcf8574_write(data & ~LCD_EN);
    _delay_us(50);
}

// Send a 4-bit nibble (already positioned in upper nibble of 'nibble') with RS control
void lcd_send_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t data = (nibble & 0xF0) | LCD_BACKLIGHT;
    if (rs) data |= LCD_RS;

    pcf8574_write(data);
    lcd_pulse_enable(data);
}

void lcd_command(uint8_t cmd)
{
    lcd_send_nibble(cmd & 0xF0, 0);         // high nibble first
    lcd_send_nibble((cmd << 4) & 0xF0, 0);  // low nibble
    _delay_ms(2);
}

void lcd_data(uint8_t data)
{
    lcd_send_nibble(data & 0xF0, 1);
    lcd_send_nibble((data << 4) & 0xF0, 1);
    _delay_ms(2);
}

void lcd_set_cursor(uint8_t row, uint8_t col)
{
    uint8_t address;

    if (row == 0)
        address = 0x00 + col;   // Line 1 starts at 0x00
    else
        address = 0x40 + col;   // Line 2 starts at 0x40

    lcd_command(0x80 | address);  // "Set DDRAM address" command
}

void lcd_init(void)
{
    i2c_init();
    _delay_ms(50);   // wait for LCD power-up

    // Special init sequence to force 4-bit mode (HD44780 datasheet timing)
    lcd_send_nibble(0x30, 0);
    _delay_ms(5);
    lcd_send_nibble(0x30, 0);
    _delay_us(150);
    lcd_send_nibble(0x30, 0);
    _delay_us(150);
    lcd_send_nibble(0x20, 0);   // now set to 4-bit mode

    lcd_command(0x28);  // 4-bit mode, 2 lines, 5x8 font
    lcd_command(0x0C);  // display ON, cursor OFF
    lcd_command(0x06);  // auto-increment cursor
    lcd_command(0x01);  // clear display
    _delay_ms(2);
}

void lcd_string(const char *str)
{
    while (*str)
    {
        lcd_data(*str++);
    }
}

int main(void)
{
    lcd_init();

    lcd_set_cursor(0, 0);
    lcd_string("sohan!!");

    lcd_set_cursor(1, 0);
    lcd_string("CSE 316 Exp2");

    while (1)
    {
        // nothing else to do
    }
}
