# SPI Configuration — ATmega32 (Master Mode) with 74HC595

## 1. Overview

This document describes the SPI configuration used in the ATmega32 microcontroller to interface with a 74HC595 shift register. The SPI peripheral is configured in Master mode to transmit 8-bit data streams that control 8 output LEDs through the shift register.

The design prioritizes simplicity and deterministic timing for reliable LED state updates.

---

## 2. SPI Mode Selection

The ATmega32 SPI is configured as:

- Mode: Master
- Data Order: MSB first (default configuration used)
- Clock Polarity (CPOL): 0
- Clock Phase (CPHA): 0
- SPI Mode: Mode 0 (CPOL = 0, CPHA = 0)

This mode ensures data is sampled on the rising edge of the clock, which is compatible with the 74HC595 shift register timing requirements.

---

## 3. Pin Configuration

### SPI Signals (ATmega32 → 74HC595)

| Signal | ATmega32 Pin | Port | 74HC595 Pin | Function |
|--------|--------------|------|-------------|----------|
| MOSI   | PB5          | PORTB | DS          | Serial Data Input |
| SCK    | PB7          | PORTB | SHCP        | Shift Clock |
| LATCH  | PB4          | PORTB | STCP        | Storage Register Clock |

---

## 4. Register Configuration

### SPI Control Register (SPCR)

The SPI is initialized using the following configuration:

- SPE = 1 → Enable SPI
- MSTR = 1 → Master mode enabled
- SPR0 = 1 → Clock prescaler set to Fosc/16

This provides a balanced SPI clock speed suitable for LED visualization and reliable shift register operation.

---

### Initialization Register Setup

SPCR configuration:

- SPI Enabled
- Master Mode Selected
- Clock Speed: Fosc / 16
- Interrupts: Disabled (polling mode used)

SPSR register:

- SPI2X = 0 (no double speed)

---

## 5. Data Transmission Flow

1. Load 8-bit data into SPDR register
2. Wait until transmission complete (SPIF flag set)
3. Toggle LATCH pin (STCP) to transfer data from shift register buffer to output register
4. LEDs update simultaneously

---

## 6. Latch Control Behavior

The LATCH pin (PB4) is manually controlled in software:

- LOW → prepare for data capture
- HIGH → transfer shifted data to output register

This ensures glitch-free LED updates and prevents intermediate states from appearing on outputs.

---

## 7. Timing Considerations

- SPI clock is derived from system clock (Fosc / 16)
- Latch pulse is software-controlled and must occur after SPI transmission completion
- No additional delay is required between bytes for single 74HC595 operation

---

## 8. Design Notes

- Hardware SPI is preferred over bit-banging for stability and timing accuracy
- 74HC595 requires proper latch sequencing to avoid output glitches
- SPI Mode 0 is compatible with standard shift register timing
- System is scalable to multiple chained 74HC595 ICs by extending byte transmissions

---

## 9. Summary

The SPI configuration provides a stable and deterministic communication channel between ATmega32 and the 74HC595 shift register. The setup is optimized for LED control applications and can be extended for larger output expansion systems without modification to the core SPI settings.
