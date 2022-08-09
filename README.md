# swd_module

## Introduction
A kernel module for you to program your *bluepill* by emulating [ARM Serial Wire Debug (SWD) protocol](https://developer.arm.com/documentation/ihi0031/a/The-Serial-Wire-Debug-Port--SW-DP-/Introduction-to-the-ARM-Serial-Wire-Debug--SWD--protocol).
- Some SoCs like raspberry-pi which has powerful application core running Linux kernel but no real-time processing unit. 
- Some MPUs like AM33x used by Beaglebone-Black has a PRU and STM32MP1x has a cortex-m core. 
- This kernel module aims to make the SoC which has no sub-core have an external extended cortex-m sub-core and control it by SoC via emulated SWD protocol.

*So far I targeted on Cortex-M3 chips that I have is "bluepill" with a stm32f103cbt6 MCU.*

## Usage
### swd
- Set the _swclk_pin and _swdio_pin. 
- compile
- use (Please refere to the test cases)

### rpu_sysfs
Structure of rpu_sysfs "/sys/class/swd/rpu"
<pre>
/sys/class/swd/rpu
├── control // control the core to be halt or unhalt
├── flash   // read/write on flash
├── ram     // read/write on ram
└── status  // check the core is halt or unhalt
</pre>

- halt core by "$ echo 0 > /sys/class/swd/rpu/control"
- do read/write on ram/flash. i.e. "$ cat blink.bin > /sys/class/swd/rpu/flash"
- unhalt core by "$ echo 1 > /sys/class/swd/rpu/control"


## Verified function and SBC boards of swd

|     | Reset line | Test Alive | Read IDCODE | Read DP REG | Write DP REG | Write to RAM | Read from RAM | Write to Flash | Read from Flash | Erase entire Flash |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| RPI3-B+ | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| RPI4 | OK | OK | OK | OK | OK | NG | OK | OK | OK | OK |
| BeagleBone-Black | OK | OK | OK | OK | OK | OK | OK | OK | OK | OK |


## Verified function and SBC boards of rpu_sysfs
|     | Core halt | Core unhalt | Write to RAM | Read from RAM | Write to Flash | Read from Flash |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| RPI3-B+ | 
| RPI4 | 
| BeagleBone-Black | 