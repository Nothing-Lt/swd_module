#!/bin/sh

echo "============== Test Alive =============="
./main_test_alive
echo ""

echo "============== RAM Download =============="
./main_ram_write
echo ""

echo "============== RAM read(read default) =============="
./main_ram_read > ram.txt
echo "Dumped data in ram to file ram.txt\n"

echo "============== Flash Download =============="
./main_flash_write
echo ""

echo "============== Flash read(read default) =============="
./main_flash_read > flash.txt
echo "Dumped data in flash to file flash.txt\n"

echo "============== Download program to flash =============="
./main_flash_program ../blink_${1}.bin
echo ""

