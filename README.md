# swd_module

## Introduction
A kernel module to program Cortex-M MCU by emulating [ARM Serial Wire Debug (SWD) protocol](https://developer.arm.com/documentation/ihi0031/a/The-Serial-Wire-Debug-Port--SW-DP-/Introduction-to-the-ARM-Serial-Wire-Debug--SWD--protocol).
- Some SoCs like raspberry-pi which has powerful application core running Linux kernel but no real-time processing unit. 
- Some MPUs like AM33x used by Beaglebone-Black has a PRU and STM32MP1x has a cortex-m core. 
- This kernel module aims to make the SoC which has no sub-core have an external extended cortex-m sub-core and control it by SoC via emulated SWD protocol.

## Usage
### swd
- Set the _swclk_pin and _swdio_pin.
- Set the core
- compile
- use (Please refere to the test cases)

### rpu_sysfs
Structure of rpu_sysfs "/sys/class/swd/rpu"
<pre>
/sys/class/swd/rpu
├── core_name  // core name
├── core_mem // mem info/layout of core
├── control // control the core to be halt or unhalt
├── flash   // read/write on flash
├── ram     // read/write on ram
└── status  // check the core is halt or unhalt

</pre>

- halt core by "$ echo 0 > /sys/class/swd/rpu/control"
- do read/write on ram/flash. i.e. "$ cat blink_$corename.bin > /sys/class/swd/rpu/flash"
- unhalt core by "$ echo 1 > /sys/class/swd/rpu/control"

### stm32f103c8t6([bluepill](https://stm32-base.org/boards/STM32F103C8T6-Blue-Pill.html))

#### Verified function and SBC boards of swd
|     | Reset line | Test Alive | Read IDCODE | Write to RAM | Read from RAM | Write to Flash | Read from Flash | Erase entire Flash |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| RPI3-B+ | | | | | | | | |
| RPI4 |  |  |  |  |  |  |  |  |
#### Verified function and SBC boards of rpu_sysfs
|     | Core halt | Core unhalt | Write to RAM | Read from RAM | Write to Flash | Read from Flash |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| RPI3-B+ |  |  |  |  |  |  |
| RPI4 |  |  |  |  |  |  |

### stm32f411ceu6([blackpill](https://shop.pimoroni.com/products/stm32f411-blackpill-development-board?variant=39274213343315))

#### Verified function and SBC boards of swd
|     | Reset line | Test Alive | Read IDCODE | Write to RAM | Read from RAM | Write to Flash | Read from Flash | Erase entire Flash |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| RPI3-B+ | | | | | | | | |
| RPI4 |  |   |   |  |  |  |  |  |
#### Verified function and SBC boards of rpu_sysfs
|     | Core halt | Core unhalt | Write to RAM | Read from RAM | Write to Flash | Read from Flash |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| RPI3-B+ |  |  |  |  |  |  |
| RPI4 |  |  |  |  |  |  |


## Build Instruction

### Prepare Environment

- rpi kernel header
```
$ sudo apt update
$ sudo apt install raspberrypi-kernel-headers
```

- gcc, make, git

### Clone Code

```
$ git clone https://github.com/Nothing-Lt/swd_module.git --recursive
```

### Build swd_module

```
$ cd swd_module/src
$ make
```

### Build test program

Test program for rpu and swd are preapred

#### swd
```
$ cd swd_module/test/swd
$ make
$ run_test.sh stm32f103c8t6 #or stm32f411ceu6
```

#### rpu
```
$ cd swd_module/test/rpu
$ make
$ run_test.sh
```