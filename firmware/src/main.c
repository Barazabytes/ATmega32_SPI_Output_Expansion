/* ============================================================
 * 1. Includes & Defines
 * ============================================================ */

#define F_CPU 8000000UL          /* Match your crystal frequency  */

#include <avr/io.h>
#include <util/delay.h>

/* --- SPI / Shift-register pins (Port B) --- */
#define SHIFT_DDR   DDRB
#define SHIFT_PORT  PORTB

#define LATCH_PIN   PB4          /* ST_CP – storage/latch clock   */
#define MOSI_PIN    PB5          /* DS    – serial data           */
/* PB6 = MISO, left as input (not connected to 74HC595)          */
#define SCK_PIN     PB7          /* SH_CP – shift clock           */

/* --- Button input pins (Port A) --- */
#define BTN_DDR     DDRA
#define BTN_PORT    PORTA
#define BTN_PIN     PINA

#define BTN1        PA0          /* Sweep left  → right           */
#define BTN2        PA1          /* Sweep right → left            */
#define BTN3        PA2          /* Blink all                     */

/* --- Timing constants (all in milliseconds) --- */
#define SWEEP_STEP_MS   150U     /* Pause between each sweep step */
#define BLINK_ON_MS     300U     /* LED-on  duration per blink    */
#define BLINK_OFF_MS    300U     /* LED-off duration per blink    */
#define BLINK_CYCLES      5U     /* Total on/off blink cycles     */
#define DEBOUNCE_MS      20U     /* Debounce settling time        */

/* --- Pattern type tag --- */
typedef enum {
    PATTERN_NONE  = 0,
    PATTERN_LTR,                 /* left-to-right sweep           */
    PATTERN_RTL,                 /* right-to-left sweep           */
    PATTERN_BLINK                /* full blink                    */
} pattern_t;


/* ============================================================
 * 2. SPI INIT — Configure ATmega32 as SPI master
 * ============================================================
 */
static void spi_master_init(void)
{
    /*
     * Data-direction: MOSI, SCK, and LATCH → outputs.
     * MISO stays as input (ATmega32 hardware requirement).
     */
    SHIFT_DDR |= (1 << MOSI_PIN) | (1 << SCK_PIN) | (1 << LATCH_PIN);

    /* Pre-assert LATCH LOW so a clean rising edge latches later. */
    SHIFT_PORT &= ~(1 << LATCH_PIN);

    /*
     * Write SPCR: SPE | MSTR
     * CPOL=0, CPHA=0, DORD=0, SPR1:0=00 are all default zeros.
     */
    SPCR = (1 << SPE) | (1 << MSTR);

    /*
     * SPSR: SPI2X=0 → no double-speed.
     * Effective SCK = 8 MHz / 4 = 2 MHz, well within the
     * 74HC595's 25 MHz maximum shift frequency.
     */
}


/* ============================================================
 * 3. GPIO INIT — Configure buttons and latch pin
 * ============================================================
 */
static void gpio_init(void)
{
    /* Bits 0-2 as inputs (clear DDR bits). */
    BTN_DDR &= ~((1 << BTN1) | (1 << BTN2) | (1 << BTN3));

    /* Disable internal pull-ups (external pull-downs are used). */
    BTN_PORT &= ~((1 << BTN1) | (1 << BTN2) | (1 << BTN3));
}


/* ============================================================
 * 4. DATA TRANSMISSION — Send 8-bit LED pattern via MOSI
 * ============================================================
 */
static void spi_transmit(uint8_t data)
{
    SPDR = data;                        /* Load byte → start TX   */
    while (!(SPSR & (1 << SPIF)));     /* Wait for SPIF flag     */
    /*
     * Reading SPDR (or a subsequent write to it) clears SPIF.
     * Here we only write, so SPIF stays set but that is harmless
     * because the next spi_transmit() will overwrite SPDR first.
     */
}


/* ============================================================
 * 5. CLOCK GENERATION — SCK from the SPI peripheral
 * ============================================================
 */


/* ============================================================
 * 6. LATCH CONTROL — Toggle latch after SPI transfer
 * ============================================================
 */
static void latch_pulse(void)
{
    SHIFT_PORT |=  (1 << LATCH_PIN);   /* Rising  edge → latch   */
    SHIFT_PORT &= ~(1 << LATCH_PIN);   /* Return LOW for next TX */
}


/* ============================================================
 * 7. PATTERN SELECTION — Select LED pattern from button input
 * ============================================================
 */
static inline uint8_t btn_raw(uint8_t pin)
{
    return (BTN_PIN & (1 << pin)) ? 1 : 0;
}

static pattern_t scan_buttons(void)
{
    if (btn_raw(BTN1)) {
        _delay_ms(DEBOUNCE_MS);
        if (btn_raw(BTN1)) return PATTERN_LTR;
    }
    else if (btn_raw(BTN2)) {
        _delay_ms(DEBOUNCE_MS);
        if (btn_raw(BTN2)) return PATTERN_RTL;
    }
    else if (btn_raw(BTN3)) {
        _delay_ms(DEBOUNCE_MS);
        if (btn_raw(BTN3)) return PATTERN_BLINK;
    }
    return PATTERN_NONE;
}

/* Wait until all buttons are released, then debounce-settle. */
static void wait_release(void)
{
    while (btn_raw(BTN1) || btn_raw(BTN2) || btn_raw(BTN3));
    _delay_ms(DEBOUNCE_MS);
}

static void led_update(uint8_t pattern)
{
    spi_transmit(pattern);  
                             
    latch_pulse();           
}


/* ============================================================
 * 8. TIMING CONTROL — Delays between LED pattern updates
 * ============================================================
 */
static void timing_delay_ms(uint16_t ms)
{
    while (ms--) {
        _delay_ms(1);
    }
}

static void anim_sweep_ltr(void)
{
    uint8_t pattern = 0x00;
    for (uint8_t i = 0; i < 8; i++) {
        pattern = (uint8_t)((pattern >> 1) | 0x80); /* fill MSB→LSB */
        led_update(pattern);
        timing_delay_ms(SWEEP_STEP_MS);
    }
}

static void anim_sweep_rtl(void)
{
    uint8_t pattern = 0x00;
    for (uint8_t i = 0; i < 8; i++) {
        pattern = (uint8_t)((pattern << 1) | 0x01); /* fill LSB→MSB */
        led_update(pattern);
        timing_delay_ms(SWEEP_STEP_MS);
    }
}

static void anim_blink(void)
{
    for (uint8_t i = 0; i < BLINK_CYCLES; i++) {
        led_update(0xFF);
        timing_delay_ms(BLINK_ON_MS);
        led_update(0x00);
        timing_delay_ms(BLINK_OFF_MS);
    }
}


/* ============================================================
 * 9. main()
 * ============================================================ */
int main(void)
{

    spi_master_init();

    gpio_init();

    led_update(0x00);

    while (1)
    {
        pattern_t selected = scan_buttons();

        if (selected == PATTERN_NONE) {
            continue;
        }

        led_update(0x00);

        wait_release();

        switch (selected) {
            case PATTERN_LTR:
                anim_sweep_ltr();
                break;
            case PATTERN_RTL:
                anim_sweep_rtl();
                break;
            case PATTERN_BLINK:
                anim_blink();
                break;
            default:
                break;
        }
    }

    return 0;  /* never reached */
}
