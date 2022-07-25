#!/bin/sh

echo "Test Alive"
./main_test_alive

echo "main(read default)"
./main

echo "Set Base"
./main_setbase

echo "Read DP"
./main_read_dp

echo "Write DP"
./main_write_dp

echo "RAM Download"
./main_ram_download

echo "Flash Download"
./main_write_flash
