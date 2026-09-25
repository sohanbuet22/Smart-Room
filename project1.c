#define F_CPU 1000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdint.h>

/* =========================================================
   DHT11 PIN CONFIGURATION (PD6)
   ========================================================= */
#define DHT_PORT PORTD
#define DHT_DDR  DDRD
#define DHT_PIN  PIND
#define DHT_BIT  PD6

/* =========================================================
   ANALOG SENSORS
   ========================================================= */
#define LDR_ADC_CHANNEL 0   // LDR on ADC0 (PA0)
#define GAS_ADC_CHANNEL 1   // Gas sensor analog out on ADC1 (PA1)
#define GAS_THRESHOLD   400 // CALIBRATE THIS

/* =========================================================
   FLAME SENSOR + BUZZER
   ========================================================= */
#define FLAME_DDR   DDRD
#define FLAME_PORT  PORTD
#define FLAME_PIN   PIND
#define FLAME_BIT   PD4

#define BUZZER_DDR  DDRB
#define BUZZER_PORT PORTB
#define BUZZER_BIT  PB0

/* =========================================================
   SERVO (DOOR)
   ========================================================= */
#define SERVO_CLOSED        125
#define SERVO_OPEN          240
#define DOOR_CLOSE_DELAY    20    // ~2 seconds (loop delay is 100ms)

/* 1 = on fire/gas alarm the door opens even if it is locked (safety).
   Set to 0 if you want the lock to ALWAYS win. */
#define FIRE_OVERRIDE_LOCK  1

/* =========================================================
   BLUETOOTH (HC-05 / HC-06) on USART: RXD=PD0, TXD=PD1
   9600 baud, U2X=1 @ 1MHz -> UBRR = 12 (error ~0.2%)
   ========================================================= */
#define BT_UBRR 12

/* =========================================================
   VISITOR COUNTER & CONTROL VARIABLES
   ========================================================= */
volatile int count = 0;
volatile uint8_t door_activity = 0;

// --- Bluetooth manual-control state ---
volatile uint8_t light_manual = 0;   // 0 = AUTO, 1 = MANUAL
volatile uint8_t light_on     = 0;   // used in manual mode
volatile uint8_t light_level  = 9;   // 0..9 brightness (manual)

volatile uint8_t fan_manual   = 0;   // 0 = AUTO, 1 = MANUAL
volatile uint8_t fan_on       = 0;
volatile uint8_t fan_level    = 9;   // 0..9 speed (manual)

volatile uint8_t door_locked  = 0;   // 1 = locked (no auto open)
volatile uint8_t status_req   = 0;   // main loop sends status when set

/* =========================================================
   I2C LCD / PCF8574
   ========================================================= */
#define LCD_RS 0x01
#define LCD_RW 0x02
#define LCD_EN 0x04
#define LCD_BL 0x08

uint8_t lcd_address;

volatile uint8_t sensor1_triggered = 0;
volatile uint8_t sensor2_triggered = 0;

ISR(INT0_vect) {
    if (!door_locked) door_activity = 1;
    if (!sensor2_triggered) {
        sensor1_triggered = 1;
    } else if (sensor2_triggered) {
        if (count > 0) count--;
        sensor1_triggered = 0;
        sensor2_triggered = 0;
    }
}

ISR(INT1_vect) {
    if (!door_locked) door_activity = 1;
    if (!sensor1_triggered) {
        sensor2_triggered = 1;
    } else if (sensor1_triggered) {
        count++;
        sensor1_triggered = 0;
        sensor2_triggered = 0;
    }
}

/* =========================================================
   UART / BLUETOOTH
   ========================================================= */
void uart_init(void) {
    UCSRA = (1 << U2X);
    UBRRH = 0;
    UBRRL = BT_UBRR;
    UCSRB = (1 << RXEN) | (1 << TXEN) | (1 << RXCIE);
    UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0); // 8N1
}

void uart_putc(char c) {
    while (!(UCSRA & (1 << UDRE)));
    UDR = c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

void uart_num(uint16_t n) {
    char buf[6];
    uint8_t i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i) uart_putc(buf[--i]);
}

/*
   Commands (case-sensitive, single characters):
     L        light ON            l        light OFF
     B0..B9   light brightness    X        light back to AUTO
     F        fan ON              f        fan OFF
     S0..S9   fan speed           Y        fan back to AUTO
     A        light + fan AUTO
     D        door LOCK           U        door UNLOCK (auto mode)
     O        open door now (only if unlocked)
     ?        status
*/
ISR(USART_RXC_vect) {
    static char pending = 0;   // 'B' or 'S' waiting for a digit
    char c = UDR;

    if (c == '\r' || c == '\n' || c == ' ') return;

    if (pending) {
        char p = pending;
        pending = 0;
        if (c >= '0' && c <= '9') {
            uint8_t v = c - '0';
            if (p == 'B') {
                light_manual = 1;
                light_level  = v;
                light_on     = (v > 0);
            } else {
                fan_manual = 1;
                fan_level  = v;
                fan_on     = (v > 0);
            }
            status_req = 1;
            return;
        }
        // not a digit -> treat c as a fresh command below
    }

    switch (c) {
        case 'B':
        case 'S': pending = c; return;

        case 'L': light_manual = 1; light_on = 1;
                  if (light_level == 0) light_level = 9;
                  break;
        case 'l': light_manual = 1; light_on = 0; break;
        case 'X': light_manual = 0; break;

        case 'F': fan_manual = 1; fan_on = 1;
                  if (fan_level == 0) fan_level = 9;
                  break;
        case 'f': fan_manual = 1; fan_on = 0; break;
        case 'Y': fan_manual = 0; break;

        case 'A': light_manual = 0; fan_manual = 0; break;

        case 'D': door_locked = 1; break;
        case 'U': door_locked = 0; break;
        case 'O': if (!door_locked) door_activity = 1; break;

        case '?': break;
        default:  return;   // unknown char, ignore
    }
    status_req = 1;
}

void bt_send_status(uint8_t t, uint8_t h) {
    uart_puts("Light:");
    if (!light_manual)  uart_puts("AUTO");
    else if (light_on)  { uart_puts("ON B"); uart_num(light_level); }
    else                uart_puts("OFF");

    uart_puts(" | Fan:");
    if (!fan_manual)    uart_puts("AUTO");
    else if (fan_on)    { uart_puts("ON S"); uart_num(fan_level); }
    else                uart_puts("OFF");

    uart_puts(" | Door:");
    uart_puts(door_locked ? "LOCKED" : "AUTO");

    uart_puts(" | Cnt:");  uart_num(count);
    uart_puts(" | T:");    uart_num(t);
    uart_puts("C H:");     uart_num(h);
    uart_puts("%\r\n");
}

/* =========================================================
   ADC
   ========================================================= */
void adc_init(void) {
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

uint16_t adc_read(uint8_t channel) {
    ADMUX = (ADMUX & 0xF0) | (channel & 0x0F);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADC;
}

/* =========================================================
   SERVO
   ========================================================= */
void servo_init(void) {
    DDRD |= (1 << PD5);
    TCCR1A = (1 << WGM11) | (1 << COM1A1);
    ICR1  = 2499;
    OCR1A = SERVO_CLOSED;
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);
}

void servo_set(uint16_t ticks) {
    OCR1A = ticks;
}

/* =========================================================
   I2C
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
   LCD
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

    lcd_command(0x28);
    lcd_command(0x0C);
    lcd_command(0x06);
    lcd_command(0x01);
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
   DHT11
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
   ========================================================= */
int main(void) {
    uint8_t humidity = 0, temperature = 0;
    uint8_t found = 0;
    uint16_t dht_timer = 0;
    uint16_t timeout_counter = 0;
    uint16_t door_close_timer = 0;
    uint8_t door_is_open = 0;
    uint8_t prev_alarm = 0;

    // Disable JTAG
    MCUCSR = (1 << JTD);
    MCUCSR = (1 << JTD);

    // LED (OC0 on PB3) and Fan (OC2 on PD7) PWM outputs
    DDRB |= (1 << PB3);
    DDRD |= (1 << PD7);

    // Flame sensor input with pull-up
    FLAME_DDR  &= ~(1 << FLAME_BIT);
    FLAME_PORT |= (1 << FLAME_BIT);

    // Buzzer output, OFF
    BUZZER_DDR  |= (1 << BUZZER_BIT);
    BUZZER_PORT &= ~(1 << BUZZER_BIT);

    // Timer0: Fast PWM on OC0 (LED)
    TCCR0 = (1 << WGM00) | (1 << WGM01) | (1 << COM01) | (1 << CS01);
    OCR0 = 0;

    // Timer2: Fast PWM on OC2 (Fan)
    TCCR2 = (1 << WGM20) | (1 << WGM21) | (1 << COM21) | (1 << CS21);
    OCR2 = 0;

    // Servo (door)
    servo_init();

    // External interrupts INT0 / INT1 (falling edge)
    MCUCR |= (1 << ISC01) | (1 << ISC11);
    MCUCR &= ~((1 << ISC00) | (1 << ISC10));
    GICR  |= (1 << INT0) | (1 << INT1);

    // Bluetooth UART (RX interrupt enabled)
    uart_init();

    sei();

    i2c_init();
    adc_init();
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
    uart_puts("Smart Room Ready. Send ? for status\r\n");

    while (1) {
        /* ---------- 1a. LIGHT ---------- */
        if (light_manual) {
            if (light_on && light_level > 0)
                OCR0 = (uint8_t)(((uint16_t)light_level * 255) / 9);
            else
                OCR0 = 0;
        } else if (count > 0) {
            uint16_t ldr_val = adc_read(LDR_ADC_CHANNEL);
            OCR0 = 255 - (ldr_val >> 2);           // dark room -> bright
        } else {
            OCR0 = 0;
        }

        /* ---------- 1b. FAN ---------- */
        if (fan_manual) {
            if (fan_on && fan_level > 0)
                OCR2 = 60 + (fan_level - 1) * 24;  // level1=60 ... level9=252
            else
                OCR2 = 0;
        } else if (count > 0) {
            uint8_t temp_score  = (temperature > 32) ? 255 : (temperature > 28) ? 180 : (temperature > 24) ? 100 : 60;
            uint8_t hum_score   = (humidity > 70) ? 255 : (humidity > 50) ? 150 : 60;
            uint8_t count_score = (count >= 4) ? 255 : (count >= 2) ? 150 : 80;

            uint16_t fan_duty = (temp_score * 5 + hum_score * 3 + count_score * 2) / 10;
            if (fan_duty > 255) fan_duty = 255;
            if (fan_duty > 0 && fan_duty < 60) fan_duty = 60;

            OCR2 = (uint8_t)fan_duty;
        } else {
            OCR2 = 0;
        }

        /* ---------- 2. Sensor timeout ---------- */
        if (sensor1_triggered || sensor2_triggered) {
            timeout_counter++;
            if (timeout_counter > 30) {
                sensor1_triggered = 0;
                sensor2_triggered = 0;
                timeout_counter = 0;
            }
        } else {
            timeout_counter = 0;
        }

        /* ---------- 3. Gas & Flame safety ---------- */
        uint16_t gas_val = adc_read(GAS_ADC_CHANNEL);
        uint8_t flame_detected = !(FLAME_PIN & (1 << FLAME_BIT)); // active LOW
        uint8_t gas_detected   = (gas_val > GAS_THRESHOLD);
        uint8_t emergency      = (flame_detected || gas_detected);

        if (emergency) {
            BUZZER_PORT |= (1 << BUZZER_BIT);
        } else {
            BUZZER_PORT &= ~(1 << BUZZER_BIT);
        }

        // Tell the phone once when an alarm starts
        if (emergency && !prev_alarm) {
            uart_puts(flame_detected ? "ALERT: FIRE!\r\n" : "ALERT: GAS LEAK!\r\n");
        }
        prev_alarm = emergency;

        /* ---------- 4. Door control (servo) ---------- */
        if (FIRE_OVERRIDE_LOCK && emergency) {
            // Safety: open the door for evacuation even if locked
            servo_set(SERVO_OPEN);
            door_is_open = 1;
            door_close_timer = 0;
            door_activity = 0;
        } else if (door_locked) {
            // Locked: keep closed, ignore sensor activity
            servo_set(SERVO_CLOSED);
            door_is_open = 0;
            door_close_timer = 0;
            door_activity = 0;
        } else if (door_activity) {
            servo_set(SERVO_OPEN);
            door_is_open = 1;
            door_close_timer = 0;
            door_activity = 0;
        } else if (door_is_open) {
            door_close_timer++;
            if (door_close_timer > DOOR_CLOSE_DELAY) {
                servo_set(SERVO_CLOSED);
                door_is_open = 0;
                door_close_timer = 0;
            }
        }

        /* ---------- 5. DHT11 (~every 2 seconds) ---------- */
        if (dht_timer >= 20) {
            if (dht11_read(&humidity, &temperature)) {
                lcd_set_cursor(0, 0);
                lcd_string("T:");
                lcd_data('0' + (temperature / 10));
                lcd_data('0' + (temperature % 10));
                lcd_data(223); // degree symbol
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

        /* ---------- 6. LCD line 2 ---------- */
        lcd_set_cursor(1, 0);
        if (flame_detected) {
            lcd_string("!! FIRE ALERT !!");
        } else if (gas_detected) {
            lcd_string("!! GAS LEAK !!  ");
        } else {
            lcd_string("Count: ");
            lcd_number(count);
            lcd_string(door_locked ? " LOCK     " : "          ");
        }

        /* ---------- 7. Send status to phone if requested ---------- */
        if (status_req) {
            status_req = 0;
            bt_send_status(temperature, humidity);
        }

        _delay_ms(100);
        dht_timer++;
    }
}