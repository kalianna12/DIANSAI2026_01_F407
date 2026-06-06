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
| RESET / RST | PA6 |
| UDCLK / UCLK | PA4 |
| WR | PA5 |
| RD | PA8 |
| OSK | PA2 |
| FDATA / FSK | PB10 |

## Parallel Data And Address Bus

| AD9854 pin | STM32F407 pin |
| --- | --- |
| D0 | PC0 |
| D1 | PC1 |
| D2 | PC2 |
| D3 | PC3 |
| D4 | PC4 |
| D5 | PC5 |
| D6 | PC6 |
| D7 | PC7 |
| A0 | PC8 |
| A1 | PC9 |
| A2 | PC10 |
| A3 | PC11 |
| A4 | PC12 |
| A5 | PC13 |

## Buttons Kept

| Board button | STM32F407 pin | Firmware action |
| --- | --- | --- |
| K0 | PE4 | Re-apply 35 MHz sine |
| K1 | PE3 | Toggle output amplitude full-scale / zero |

## Output

Measure the waveform on the AD9854 module sine output. PA4 is only the AD9854
update-clock control pin, not the 35 MHz output.
