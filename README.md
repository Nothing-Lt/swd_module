# RPU (Real-time Processing Unit)

## Introduction
A real-time processing unit. This is a solution for SoCs like raspberry-pi which has powerful application core running Linux kernel but no real-time processing unit. Some MPUs like AM33x used by Beaglebone-Black has a PRU and STM32MP1x has a cortex-m core. This repository is a solution for it. 

*So far I targeted on Cortex-M3 chips that I have the "bluepill" called by people with a stm32f103cbt6 MCU.*

## Usage
- Set the _swclk_pin and _swdio_pin. 
- compile
- use (Please refere to the test cases)

## Verified function and SBC boards

|     | Reset line | Test Alive | Read IDCODE | Read DP REG | Write DP REG | Write to RAM | Read from RAM | Write to Flash | Read from Flash | Erase entire Flash |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| RPI3-B+ | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| RPI4 |
| BeagleBone-Black | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK |

TODO:
<pre>
Implement a sysfs in /sys/devices/rpu with the following structure
/sys/devices/rpu/
|- control /* 1: start to run. 0: stop the core */
|- ram /* Read from or write to RAM */
|- flash /* Read from or write to Flash */
</pre>