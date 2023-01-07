#!/bin/sh

echo "============== core name =============="
cat /sys/class/swd/rpu/core_name
echo ""

echo "============== core mem info =============="
cat /sys/class/swd/rpu/core_mem
echo ""

echo "============== core status =============="
cat /sys/class/swd/rpu/status
echo ""

echo "============== halting core =============="
echo 0 > /sys/class/swd/rpu/control
echo ""

core=`cat /sys/class/swd/rpu/core_name`
echo "============== write to ram =============="
cat ../blink_$core.bin > /sys/class/swd/rpu/ram
echo ""

echo "============== main ram =============="
./main_read_ram
echo ""

echo "============== Programming flash =============="
cat ../blink_$core.bin > /sys/class/swd/rpu/flash
echo ""

echo "============== unhalting core =============="
echo 1 > /sys/class/swd/rpu/control
echo ""
