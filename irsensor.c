#include<stdio.h>
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

volatile int count = 0;
volatile uint8_t flag1 = 0, flag2 = 0;

void relay_init(void) {
    DDRB |= (1 << PB0); // Set PB0 as output for relay driver
    PORTB &= ~(1 << PB0); // Initially turn OFF fan/light
}

// External Interrupt 0 (Sensor 1 - Outer Sensor)
ISR(INT0_vect) {
    if (flag2 == 1) { // If Sensor 2 was triggered first (Exiting)
        if (count > 0) count--;
        flag2 = 0;
    } else {
        flag1 = 1; // Mark Sensor 1 triggered (Entering sequence started)
    }
    _delay_ms(300); // Debounce delay
}

// External Interrupt 1 (Sensor 2 - Inner Sensor)
ISR(INT1_vect) {
    if (flag1 == 1) { // If Sensor 1 was triggered first (Entering)
        count++;
        flag1 = 0;
    } else {
        flag2 = 1; // Mark Sensor 2 triggered (Exiting sequence started)
    }
    _delay_ms(300); // Debounce delay
}

int main(void) {
    relay_init();
    
    // Configure INT0 and INT1 to trigger on Falling Edge (Active LOW IR sensor output)
    MCUCR |= (1 << ISC01) | (1 << ISC11);
    MCUCR &= ~((1 << ISC00) | (1 << ISC10));
    GICR |= (1 << INT0) | (1 << INT1);
    
    sei(); // Enable global interrupts

    while (1) {
        // Control Load based on people count
        if (count > 0) {
            PORTB |= (1 << PB0);  // Turn ON Fan and Light
        } else {
            PORTB &= ~(1 << PB0); // Turn OFF Fan and Light
        }
        
        // Timeout resets in case someone triggers one sensor without crossing completely
        _delay_ms(100);
    }
}