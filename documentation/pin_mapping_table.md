# ATmega32 Pin Mapping

This document describes the hardware interfacing between the ATmega32 microcontroller and the 74HC595 shift register, along with input buttons and system-level connections.

---

## ATmega32 Pin Mapping

| ATmega32 Pin | Pin Name | Direction | Connected To | Function |
|--------------|----------|----------|--------------|----------|
| PB4 | GPIO | Output | 74HC595 (STC) | Latch Control |
| PB5 | MOSI | Output | 74HC595 (DS) | Serial Data Out |
| PB6 | MISO | Input | Not used | Not used |
| PB7 | SCK | Output | 74HC595 (SHC) | Serial Clock |
| PA0 | GPIO | Input | Button 1 (S1) | Pattern Select 1 |
| PA1 | GPIO | Input | Button 2 (S2) | Pattern Select 2 |
| PA2 | GPIO | Input | Button 3 (S3) | Pattern Select 3 |
| RESET | Reset | Input | Reset button + 10kΩ pull-up | System Reset |
| XTAL1 | Crystal In | Input | 16 MHz crystal | Clock Input |
| XTAL2 | Crystal Out | Output | 16 MHz crystal | Clock Output |
| VCC | Power | Input | +5V supply | Power |
| GND | Ground | Input | 0V reference | Ground |

---

## 74HC595 Shift Register Mapping

| 74HC595 Pin | Pin Name | Type | Connected To | Configuration | Purpose |
|-------------|----------|------|--------------|---------------|---------|
| Pin 14 | SER (Serial Data) | Input | ATmega32 PB5 (MOSI) | - | Receives serial LED data |
| Pin 11 | SRCLK (Shift Clock) | Input | ATmega32 PB7 (SCK) | Rising edge triggered | Clocks data into shift register |
| Pin 12 | RCLK (Latch Clock) | Input | ATmega32 PB4 (GPIO) | Rising edge triggered | Transfers shift register to output register |
| Pin 13 | OE (Output Enable) | Input | GND | Active LOW | Enables outputs |
| Pin 10 | SRCLR (Master Reset) | Input | VCC | Active LOW | Keeps register active (no reset) |
| Pins 15, 1–7 | Q0–Q7 | Output | LEDs via 220Ω resistors | - | Parallel LED outputs |
| Pin 16 | VCC | Power | +5V | - | Power supply |
| Pin 8 | GND | Ground | 0V | - | Ground reference |

---
