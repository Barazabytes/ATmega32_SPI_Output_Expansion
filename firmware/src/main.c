/*
 * ============================================================
 *  ATmega32 – 74HC595 LED Control via Hardware SPI
 * ============================================================
 *
 *  File organisation
 *  -----------------
 *  1. Includes & defines
 *  2. [SPI INIT]      spi_master_init()
 *  3. [GPIO INIT]     gpio_init()
 *  4. [DATA TX]       spi_transmit()
 *  5. [CLK GEN]       — handled internally by SPCR/SPSR hardware
 *  6. [LATCH CTRL]    latch_pulse()
 *  7. [PATTERN SEL]   pattern_select()
 *  8. [TIMING CTRL]   timing_delay_ms()  +  animation helpers
 *  9. main()
 *
 *  Pin Mapping
 *  -----------
 *   PA0 – Button 1  →  sweep LEFT  to RIGHT
 *   PA1 – Button 2  →  sweep RIGHT to LEFT
 *   PA2 – Button 3  →  blink all LEDs
 *   PB4 – LATCH (STC / ST_CP)   → 74HC595 pin 12
 *   PB5 – MOSI  (DS  / SER)     → 74HC595 pin 14
 *   PB7 – SCK   (SHC / SH_CP)   → 74HC595 pin 11
 *
 *  74HC595 static wiring (fix in hardware)
 *   MR  → VCC  (disable master-reset; must be HIGH)
 *   OE  → GND  (permanently enable outputs; must be LOW)
 *
 *  Button wiring: active-HIGH with external pull-down resistors.
 *   Each button connects its PA pin to +5 V when pressed;
 *   a dedicated 10 kΩ resistor per pin pulls it to GND at rest.
 * ============================================================
 */

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
 *
 * The ATmega32 SPI peripheral has fixed pin assignments on PB:
 *   PB4 = /SS  → repurposed here as manual LATCH output
 *   PB5 = MOSI → serial data to 74HC595 DS pin
 *   PB6 = MISO → unused (left as input)
 *   PB7 = SCK  → shift clock to 74HC595 SH_CP pin
 *
 * SPCR settings used:
 *   SPE  = 1  : enable the SPI module
 *   MSTR = 1  : master mode (ATmega32 drives SCK)
 *   CPOL = 0  : clock idle LOW  (Mode 0 – 74HC595 compatible)
 *   CPHA = 0  : data sampled on leading (rising) edge
 *   DORD = 0  : MSB transmitted first
 *   SPR1:SPR0 = 00 : fosc/4 → 2 MHz @ 8 MHz crystal
 *
 * The LATCH pin (PB4) is driven manually as a GPIO because the
 * 74HC595 ST_CP is edge-triggered, not a SPI chip-select.
 * ============================================================ */
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
 *
 * Port A bits 0-2 are button inputs.
 * External 10 kΩ pull-down resistors are on each pin, so the
 * internal pull-ups (PORTA register) must remain cleared.
 *
 * The LATCH pin direction is already set inside spi_master_init
 * because it shares Port B's DDR with the SPI output pins.
 * ============================================================ */
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
 *
 * Writing a byte to SPDR triggers hardware transmission.
 * The SPI peripheral shifts the byte out MSB-first on MOSI,
 * generating SCK pulses automatically (see section 5).
 * Polling SPIF in SPSR confirms the 8-clock transfer is done.
 *
 *  Bit 7 of `data` → first bit on MOSI → enters 74HC595 SER
 *                    → after 8 clocks sits at Q7 output.
 *  Bit 0 of `data` → last  bit on MOSI → sits at Q0 output.
 * ============================================================ */
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
 *
 * No explicit function is needed: the ATmega32 SPI module
 * drives PB7 (SCK) automatically when SPE=1 and MSTR=1.
 *
 * SCK characteristics (from SPCR/SPSR settings above):
 *   Polarity : idle LOW  (CPOL = 0)
 *   Phase    : data valid on rising edge (CPHA = 0) – SPI Mode 0
 *   Frequency: fosc / 4 = 2 MHz  (SPI2X = 0, SPR1:0 = 00)
 *
 * The 74HC595 SH_CP input latches DS on the rising SCK edge,
 * which matches Mode 0 exactly.
 * ============================================================ */


/* ============================================================
 * 6. LATCH CONTROL — Toggle latch after SPI transfer
 * ============================================================
 *
 * After spi_transmit() completes, the 8 bits sit in the
 * 74HC595 shift register but have NOT appeared on Q0-Q7 yet.
 * A LOW→HIGH→LOW pulse on ST_CP (LATCH_PIN / PB4) copies the
 * shift register into the storage register, updating outputs.
 *
 * The pulse must be at least 20 ns wide (74HC595 datasheet,
 * Vcc = 5 V).  Two consecutive PORT writes at 8 MHz give
 * ~125 ns each, far exceeding the minimum requirement.
 * ============================================================ */
static void latch_pulse(void)
{
    SHIFT_PORT |=  (1 << LATCH_PIN);   /* Rising  edge → latch   */
    SHIFT_PORT &= ~(1 << LATCH_PIN);   /* Return LOW for next TX */
}


/* ============================================================
 * 7. PATTERN SELECTION — Select LED pattern from button input
 * ============================================================
 *
 * scan_buttons() samples PA0-PA2 and returns a pattern_t.
 * It applies a two-stage debounce:
 *   Stage 1 – detect initial press.
 *   Stage 2 – wait DEBOUNCE_MS, then confirm pin is still HIGH.
 * Returns PATTERN_NONE if no button is pressed or passes
 * debounce. Priority order: BTN1 > BTN2 > BTN3.
 * ============================================================ */
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

/*
 * led_update – the single point that combines sections 4, 5, 6:
 *   transmit data  (SCK generated by hardware during transmit)
 *   pulse latch    (outputs update after this call returns)
 */
static void led_update(uint8_t pattern)
{
    spi_transmit(pattern);   /* [DATA TX]   shift bits via MOSI  */
                             /* [CLK GEN]   SCK auto-generated   */
    latch_pulse();           /* [LATCH CTRL] commit to outputs   */
}


/* ============================================================
 * 8. TIMING CONTROL — Delays between LED pattern updates
 * ============================================================
 *
 * timing_delay_ms() wraps _delay_ms so the delay magnitude is
 * a runtime variable rather than a compile-time constant.
 * Used by all three animation routines below.
 * ============================================================ */
static void timing_delay_ms(uint16_t ms)
{
    while (ms--) {
        _delay_ms(1);
    }
}

/* --- Animation: sweep left → right (Q7 first, Q0 last) ---
 *
 * Pattern progression each SWEEP_STEP_MS:
 *   Step 1: 1000 0000  (Q7 ON)
 *   Step 2: 1100 0000  (Q7, Q6 ON)
 *   ...
 *   Step 8: 1111 1111  (all ON)
 *
 * The shift fills from the MSB because the SPI sends MSB first;
 * the first bit shifted out lands at Q7 after 8 clock pulses.
 * Swap to `pattern |= (1 << i)` if your board wires Q0 as LED1.
 */
static void anim_sweep_ltr(void)
{
    uint8_t pattern = 0x00;
    for (uint8_t i = 0; i < 8; i++) {
        pattern = (uint8_t)((pattern >> 1) | 0x80); /* fill MSB→LSB */
        led_update(pattern);
        timing_delay_ms(SWEEP_STEP_MS);
    }
}

/* --- Animation: sweep right → left (Q0 first, Q7 last) ---
 *
 * Pattern progression each SWEEP_STEP_MS:
 *   Step 1: 0000 0001  (Q0 ON)
 *   Step 2: 0000 0011  (Q0, Q1 ON)
 *   ...
 *   Step 8: 1111 1111  (all ON)
 */
static void anim_sweep_rtl(void)
{
    uint8_t pattern = 0x00;
    for (uint8_t i = 0; i < 8; i++) {
        pattern = (uint8_t)((pattern << 1) | 0x01); /* fill LSB→MSB */
        led_update(pattern);
        timing_delay_ms(SWEEP_STEP_MS);
    }
}

/* --- Animation: blink all LEDs BLINK_CYCLES times ---
 *
 * Writes 0xFF (all ON) then 0x00 (all OFF) with separate
 * configurable on/off delays; leaves all LEDs OFF at end.
 */
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
    /* [SPI INIT]  Configure ATmega32 as SPI master ----------- */
    spi_master_init();

    /* [GPIO INIT] Configure buttons and latch pin ------------ */
    gpio_init();

    /* Power-on safe state: all LEDs OFF ---------------------- */
    led_update(0x00);

    /* Main event loop ---------------------------------------- */
    while (1)
    {
        /* [PATTERN SEL] read buttons with debounce */
        pattern_t selected = scan_buttons();

        if (selected == PATTERN_NONE) {
            continue;                    /* nothing pressed, keep polling */
        }

        /* Clear LEDs before every animation */
        led_update(0x00);

        /* Release button before animation starts */
        wait_release();

        /* [TIMING CTRL] dispatch animation; delays are inside each */
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