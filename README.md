# swd_module

## Introduction
A kernel module to program Cortex-M3/4 MCU by emulating [ARM Serial Wire Debug (SWD) protocol](https://developer.arm.com/documentation/ihi0031/a/The-Serial-Wire-Debug-Port--SW-DP-/Introduction-to-the-ARM-Serial-Wire-Debug--SWD--protocol).
- Some SoCs like raspberry-pi which has powerful application core running Linux kernel but no real-time processing unit. 
- Some MPUs like AM33x used by Beaglebone-Black has a PRU and STM32MP1x has a cortex-m core. 
- This kernel module aims to make the SoC which has no sub-core have an external extended cortex-m sub-core and control it by SoC via emulated SWD protocol.

## Usage
- Set the _swclk_pin and _swdio_pin.
- Select the core in _init function
- Compile
- use (Please refere to the test cases)

## Verified function and SBC boards

### stm32f103c8t6([bluepill](https://stm32-base.org/boards/STM32F103C8T6-Blue-Pill.html))
|     | Reset line | Test Alive | Read IDCODE | Write to RAM | Read from RAM | Write to Flash | Read from Flash | Erase entire Flash |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| RPI3-B+ | | | | | | | | |
| RPI4 |   |    |    |   |   |   |   |   |

### stm32f411ceu6([blackpill](https://shop.pimoroni.com/products/stm32f411-blackpill-development-board?variant=39274213343315))
|     | Reset line | Test Alive | Read IDCODE | Write to RAM | Read from RAM | Write to Flash | Read from Flash | Erase entire Flash |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| RPI3-B+ | | | | | | | | |
| RPI4 |   |    |    |   |   |   |   |   |


