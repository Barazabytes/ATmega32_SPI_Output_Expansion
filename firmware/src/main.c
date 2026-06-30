#define F_CPU 8000000UL   // clock speed = 8 MHz

#include <avr/io.h>
#include <util/delay.h>

// Useful constants
#define LATCH_PIN  PB4   // Latch
#define DATA_PIN   PB5   // DATA
#define CLOCK_PIN  PB7   // CLOCK

#define BUTTON1    PA0   // Sweep LEFT to RIGHT
#define BUTTON2    PA1   // Sweep RIGHT to LEFT
#define BUTTON3    PA2   // Blink all LEDs


// Set up the SPI hardware and the three pins we use to talk to the 74HC595
void setup_spi(void) {
    // Set the three pins we use to talk to the 74HC595 as OUTPUTS
    DDRB |= (1 << DATA_PIN) | (1 << CLOCK_PIN) | (1 << LATCH_PIN);

    // Start with latch pin LOW
    PORTB &= ~(1 << LATCH_PIN);

    // Turn on SPI, set as Master
    SPCR = (1 << SPE) | (1 << MSTR);
}

// Set up the three buttons as INPUTS
void setup_buttons(void) {
    // Clear the bits in DDRA to make these pins INPUTS
    DDRA &= ~((1 << BUTTON1) | (1 << BUTTON2) | (1 << BUTTON3));

    // Make sure internal pull-ups are OFF (we use external pull-downs)
    PORTA &= ~((1 << BUTTON1) | (1 << BUTTON2) | (1 << BUTTON3));
}

// Send a single byte to the 74HC595 via SPI
void send_byte(uint8_t data) {
    SPDR = data;                        // Start sending the byte
    while ((SPSR & (1 << SPIF)) == 0);  // Wait for the byte to finish sending
}


// Tell 74HC595 to release Data.
void pulse_latch(void) {
    PORTB |= (1 << LATCH_PIN);    // LATCH HIGH
    PORTB &= ~(1 << LATCH_PIN);   // LATCH LOW
}

// Show the intended patterns on LED
void show_pattern(uint8_t pattern) {
    send_byte(pattern);
    pulse_latch();
}

// Check if Button is pressed.
uint8_t button_is_pressed(uint8_t button_pin) {
    if (PINA & (1 << button_pin)) {
        return 1;   /* Button is pressed */
    } else {
        return 0;   /* Button is not pressed */
    }
}

// Wait until button is released.
void wait_until_released(void) {
    while (button_is_pressed(BUTTON1) || button_is_pressed(BUTTON2) || button_is_pressed(BUTTON3));
    _delay_ms(20);   // small delay (debounce)
}

// LED Animations..

// Turns LEDs on one at a time, from left to right 
void animation_left_to_right(void) {
    uint8_t pattern = 0b00000000;

    for (uint8_t i = 0; i < 8; i++) {
        pattern = (pattern >> 1) | 0b10000000;  // Turn ON led
        show_pattern(pattern);
        _delay_ms(150);                          // Small delay
    }
}

// Turns LEDs on one at a time, from right to left
void animation_right_to_left(void) {
    uint8_t pattern = 0b00000000;

    for (uint8_t i = 0; i < 8; i++) {
        pattern = (pattern << 1) | 0b00000001;  /* turn on one more LED */
        show_pattern(pattern);
        _delay_ms(150);                          /* wait before next step */
    }
}

// Blinks all 8 LEDs together, 5 times
void animation_blink(void) {
    for (uint8_t i = 0; i < 5; i++) {
        show_pattern(0b11111111);   /* all LEDs ON  */
        _delay_ms(300);
        show_pattern(0b00000000);   /* all LEDs OFF */
        _delay_ms(300);
    }
}


int main(void)
{
    // Run our setup functions.
    setup_spi();
    setup_buttons();

    // Make sure all LEDs start OFF
    show_pattern(0b00000000);

    while (1) {

        if (button_is_pressed(BUTTON1)) {
            show_pattern(0b00000000);   // Turn Off all LED's first.
            wait_until_released();
            animation_left_to_right();
        }
        else if (button_is_pressed(BUTTON2)) {
            show_pattern(0b00000000);
            wait_until_released();
            animation_right_to_left();
        }
        else if (button_is_pressed(BUTTON3)) {
            wait_until_released();
            animation_blink();
        }
    }

    return 0;
}
