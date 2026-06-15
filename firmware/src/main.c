/*
 * ATmega32 – 74HC595 LED Control via Hardware SPI
 * ================================================
 * Hardware SPI master drives a 74HC595 shift register
 * that controls 8 LEDs.
 *
 * Pin Mapping
 * -----------
 *  PA0 – Button 1 (LEFT-to-RIGHT sweep)
 *  PA1 – Button 2 (RIGHT-to-LEFT sweep)
 *  PA2 – Button 3 (BLINK all LEDs)
 *  PB4 – LATCH (STC / ST_CP)   → 74HC595 pin 12
 *  PB5 – MOSI  (DS  / SER)     → 74HC595 pin 14
 *  PB7 – SCK   (SHC / SH_CP)   → 74HC595 pin 11
 *
 * Button wiring: each switch connects its pin to +5 V;
 * pull-down resistors hold the lines LOW when released.
 * A press drives the pin HIGH (active-HIGH logic).
 *
 * Behaviour
 * ---------
 *  Power-on / Reset  : all 8 LEDs OFF
 *  Button 1 pressed  : LEDs light one-by-one, left → right
 *  Button 2 pressed  : LEDs light one-by-one, right → left
 *  Button 3 pressed  : all LEDs blink together (5 times)
 *  (after any animation the LEDs stay in their final state
 *   until the next button press or reset)
 */

#define F_CPU 8000000UL          /* adjust to your crystal */

#include <avr/io.h>
#include <util/delay.h>

/* ── Port / pin aliases ─────────────────────────────────── */
#define SHIFT_PORT  PORTB
#define SHIFT_DDR   DDRB

#define BTN1        PA0          /* left-to-right sweep     */
#define BTN2        PA1          /* right-to-left sweep     */
#define BTN3        PA2          /* blink                   */

#define LATCH_PIN   PB4          /* ST_CP – storage latch   */
#define MOSI_PIN    PB5          /* SER   – serial data     */
#define SCK_PIN     PB7          /* SH_CP – shift clock     */

/* ── Timing constants (ms) ──────────────────────────────── */
#define STEP_DELAY_MS   150U     /* inter-step pause in sweeps  */
#define BLINK_ON_MS     300U     /* blink ON  half-period        */
#define BLINK_OFF_MS    300U     /* blink OFF half-period        */
#define BLINK_COUNT       5U     /* number of blink cycles       */
#define DEBOUNCE_MS      20U     /* button debounce delay        */

/* ══════════════════════════════════════════════════════════
 * SPI / 74HC595 helpers
 * ══════════════════════════════════════════════════════════ */

/**
 * spi_init – configure ATmega32 hardware SPI as master.
 *
 * ATmega32 SPI pins (fixed by hardware):
 *   PB4 = /SS  (we repurpose as the 74HC595 LATCH – driven manually)
 *   PB5 = MOSI
 *   PB6 = MISO (unused but set as input automatically)
 *   PB7 = SCK
 *
 * We set PB4 as a general-purpose output and toggle it
 * manually for the latch pulse, which is the standard
 * approach when /SS is not wired to the slave's chip-select.
 */
static void spi_init(void)
{
    /* Set MOSI, SCK, and LATCH as outputs; MISO stays input */
    SHIFT_DDR |= (1 << MOSI_PIN) | (1 << SCK_PIN) | (1 << LATCH_PIN);

    /* Latch starts LOW (data only latched on rising edge) */
    SHIFT_PORT &= ~(1 << LATCH_PIN);

    /*
     * SPCR – SPI Control Register
     *   SPE  (bit 6) : enable SPI
     *   MSTR (bit 4) : master mode
     *   CPOL (bit 3) : clock polarity 0 (idle LOW)  ← 74HC595 compatible
     *   CPHA (bit 2) : clock phase   0 (sample on leading edge)
     *   SPR1:SPR0    : 00 → fosc/4  (2 MHz @ 8 MHz crystal – plenty fast)
     *
     * Data order: MSB first (bit 7 of the byte → Q7 of 74HC595,
     * which we treat as LED 8 / rightmost).
     * If your LED wiring is reversed, flip the byte before sending.
     */
    SPCR = (1 << SPE) | (1 << MSTR);
    /* SPSR – no double-speed needed */
}

/**
 * spi_send_byte – shift one byte into the 74HC595.
 *
 * Writing to SPDR starts the transmission; reading SPSR's SPIF
 * bit confirms completion (hardware clears SPIF on next SPDR
 * access, so we just poll it here).
 */
static void spi_send_byte(uint8_t data)
{
    SPDR = data;                             /* start transmission  */
    while (!(SPSR & (1 << SPIF)));          /* wait until done     */
}

/**
 * sr_latch – pulse the LATCH line to transfer the shift
 * register contents to the storage register (output latches).
 * The 74HC595 latches on the rising edge of ST_CP.
 */
static inline void sr_latch(void)
{
    SHIFT_PORT |=  (1 << LATCH_PIN);        /* rising edge – latch */
    SHIFT_PORT &= ~(1 << LATCH_PIN);        /* bring back LOW      */
}

/**
 * led_write – send one byte to the 74HC595 and latch it.
 *
 * Bit 7 → Q7 (LED 8), Bit 0 → Q0 (LED 1) with MSB-first SPI.
 * A '1' lights an LED (assumes active-HIGH LED wiring on Q0–Q7).
 */
static void led_write(uint8_t pattern)
{
    spi_send_byte(pattern);
    sr_latch();
}

/* ══════════════════════════════════════════════════════════
 * Button helpers
 * ══════════════════════════════════════════════════════════ */

/**
 * buttons_init – configure Port A pins as inputs.
 *
 * With external pull-down resistors the internal pull-ups are
 * not needed, so we leave PORTA bits 0-2 cleared (no pull-up).
 */
static void buttons_init(void)
{
    DDRA  &= ~((1 << BTN1) | (1 << BTN2) | (1 << BTN3));  /* inputs  */
    PORTA &= ~((1 << BTN1) | (1 << BTN2) | (1 << BTN3));  /* no pull-up */
}

/** is_pressed – return non-zero if the given PA pin is HIGH. */
static inline uint8_t is_pressed(uint8_t pin)
{
    return (PINA & (1 << pin));
}

/**
 * wait_for_release – busy-wait until all three buttons are
 * released, then add a short debounce delay.
 */
static void wait_for_release(void)
{
    while (is_pressed(BTN1) || is_pressed(BTN2) || is_pressed(BTN3));
    _delay_ms(DEBOUNCE_MS);
}

/* ══════════════════════════════════════════════════════════
 * LED animations
 * ══════════════════════════════════════════════════════════ */

/**
 * anim_left_to_right – illuminate LEDs one by one from Q0→Q7.
 *
 * Pattern progression (binary, Q7..Q0):
 *   0000 0001  →  0000 0011  →  …  →  1111 1111
 */
static void anim_left_to_right(void)
{
    uint8_t pattern = 0x00;
    for (uint8_t i = 0; i < 8; i++)
    {
        pattern = (pattern >> 1) | 0x80;   /* shift in a '1' from MSB  */
        /*
         * Alternatively, to fill from LSB (Q0 first):
         *   pattern |= (1 << i);
         * Choose whichever matches your physical LED order.
         *
         * Using MSB-shift: first byte = 1000 0000, last = 1111 1111,
         * so Q7 lights first and Q0 last.  Swap the line below if you
         * want Q0 first:
         *   pattern |= (uint8_t)(1 << i);
         */
        led_write(pattern);
        _delay_ms(STEP_DELAY_MS);
    }
}

/**
 * anim_right_to_left – illuminate LEDs one by one from Q7→Q0.
 */
static void anim_right_to_left(void)
{
    uint8_t pattern = 0x00;
    for (uint8_t i = 0; i < 8; i++)
    {
        pattern = (pattern << 1) | 0x01;   /* shift in a '1' from LSB  */
        led_write(pattern);
        _delay_ms(STEP_DELAY_MS);
    }
}

/**
 * anim_blink – flash all 8 LEDs together BLINK_COUNT times,
 * then leave them OFF.
 */
static void anim_blink(void)
{
    for (uint8_t i = 0; i < BLINK_COUNT; i++)
    {
        led_write(0xFF);                    /* all ON                   */
        _delay_ms(BLINK_ON_MS);
        led_write(0x00);                    /* all OFF                  */
        _delay_ms(BLINK_OFF_MS);
    }
}

/* ══════════════════════════════════════════════════════════
 * Main
 * ══════════════════════════════════════════════════════════ */

int main(void)
{
    /* ── Initialise peripherals ── */
    buttons_init();
    spi_init();

    /* ── Power-on state: all LEDs OFF ── */
    led_write(0x00);

    /* ── Main event loop ── */
    while (1)
    {
        /* Simple priority: BTN1 > BTN2 > BTN3 */

        if (is_pressed(BTN1))
        {
            _delay_ms(DEBOUNCE_MS);          /* debounce on press       */
            if (is_pressed(BTN1))            /* confirm still pressed   */
            {
                led_write(0x00);             /* reset before animation  */
                wait_for_release();          /* wait for finger off     */
                anim_left_to_right();
            }
        }
        else if (is_pressed(BTN2))
        {
            _delay_ms(DEBOUNCE_MS);
            if (is_pressed(BTN2))
            {
                led_write(0x00);
                wait_for_release();
                anim_right_to_left();
            }
        }
        else if (is_pressed(BTN3))
        {
            _delay_ms(DEBOUNCE_MS);
            if (is_pressed(BTN3))
            {
                wait_for_release();
                anim_blink();
            }
        }
    }

    return 0;                                /* never reached           */
}