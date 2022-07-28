#!/bin/sh

echo "============== Test Alive =============="
./main_test_alive
echo ""

echo "============== main(read default) =============="
./main
echo ""

echo "============== Set Base =============="
./main_setbase
echo ""

echo "============== Read DP =============="
./main_read_dp
echo ""

echo "============== Write DP =============="
./main_write_dp
echo ""

echo "============== RAM Download =============="
./main_ram_download
echo ""

echo "============== Flash Download =============="
./main_write_flash
echo ""

echo "============== Download program to flash =============="
./main_program_flash
echo ""

