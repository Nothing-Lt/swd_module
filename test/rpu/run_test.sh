#!/bin/sh

echo "============== halting core =============="
echo 0 > /sys/class/swd/rpu/control
echo ""

echo "============== write to ram =============="
cat ../blink.bin > /sys/class/swd/rpu/ram
echo ""


echo "============== main ram =============="
./main_read_ram
echo ""

echo "============== Programming flash =============="
cat ../blink.bin > /sys/class/swd/rpu/flash
echo ""

echo "============== unhalting core =============="
echo 1 > /sys/class/swd/rpu/control
echo ""