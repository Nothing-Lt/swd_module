# swd_module

## Introduction
A kernel module to program Cortex-M3 MCU (*bluepill* is tried) by emulating [ARM Serial Wire Debug (SWD) protocol](https://developer.arm.com/documentation/ihi0031/a/The-Serial-Wire-Debug-Port--SW-DP-/Introduction-to-the-ARM-Serial-Wire-Debug--SWD--protocol).
- Some SoCs like raspberry-pi which has powerful application core running Linux kernel but no real-time processing unit. 
- Some MPUs like AM33x used by Beaglebone-Black has a PRU and STM32MP1x has a cortex-m core. 
- This kernel module aims to make the SoC which has no sub-core have an external extended cortex-m sub-core and control it by SoC via emulated SWD protocol.

*So far I targeted on Cortex-M3 chips that I have is "bluepill" with a stm32f103cbt6 MCU.*

## Usage
- Set the _swclk_pin and _swdio_pin. 
- compile
- use (Please refere to the test cases)

## Verified function and SBC boards

|     | Reset line | Test Alive | Read IDCODE | Read DP REG | Write DP REG | Write to RAM | Read from RAM | Write to Flash | Read from Flash | Erase entire Flash |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| RPI3-B+ | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| RPI4 | OK  | OK  | OK  | OK  | OK  | OK  | OK  | OK  | OK  | OK  |

