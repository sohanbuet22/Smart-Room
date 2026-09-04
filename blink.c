#define F_CPU 1000000UL   // ATmega32 default fuse = 1 MHz internal oscillator
#include <avr/io.h>
#include <util/delay.h>

int main(void)
{
    DDRB |= (1 << PB0);   // PB0 as output

    while (1)
    {
        PORTB ^= (1 << PB0);  // Toggle LED
        _delay_ms(100);
    }
}