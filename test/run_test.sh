#!/bin/sh

echo "============== Test Alive =============="
./main_test_alive
echo ""

echo "============== RAM Download =============="
./main_ram_download
echo ""

echo "============== RAM read(read default) =============="
./main_ram_read
echo ""

echo "============== Flash Download =============="
./main_flash_write
echo ""

echo "============== Flash read(read default) =============="
./main_flash_read
echo ""

echo "============== Download program to flash =============="
./main_flash_program blink_${1}.bin
echo ""

