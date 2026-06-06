# AD9854 to STM32F407 Wiring

This firmware drives the AD9854 module in parallel mode and outputs a
35 MHz sine wave at power-on.

## Power

| AD9854 module | STM32F407 board |
| --- | --- |
| VDD / VCC | 3V3 |
| GND / VSS | GND |

The AD9854 module and STM32F407 must share GND.

## Control Pins

| AD9854 pin | STM32F407 pin |
| --- | --- |
| RESET / RST | PA0 |
| UDCLK / UCLK | PA1 |
| WR | PA2 |
| RD | PA3 |
| OSK | PA4 |
| FDATA / FSK | PA5 |

## Parallel Data And Address Bus

| AD9854 pin | STM32F407 pin |
| --- | --- |
| A0 | PC0 |
| A1 | PC1 |
| A2 | PC2 |
| A3 | PC3 |
| A4 | PC4 |
| A5 | PC5 |
| D0 | PC6 |
| D1 | PC7 |
| D2 | PC8 |
| D3 | PC9 |
| D4 | PC10 |
| D5 | PC11 |
| D6 | PC12 |
| D7 | PC13 |

## Buttons Kept

| Board button | STM32F407 pin | Firmware action |
| --- | --- | --- |
| K0 | PE4 | Re-apply 35 MHz sine |
| K1 | PE3 | Toggle output amplitude full-scale / zero |

## Status LED

| Signal | STM32F407 pin |
| --- | --- |
| STATUS_LED | PA7 (active low) |

## Output

Measure the waveform on the AD9854 module sine output. PA1 is only the AD9854
update-clock control pin, not the 35 MHz output.
