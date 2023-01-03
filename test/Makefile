
BINS = main_flash_write main_flash_program main_flash_erase_all main_ram_write main_test_alive main_ram_read main_flash_read
CC ?= gcc

.PHONY: all
all: ${BINS}

%: %.c
	${CC} -Wall $^ -o $@

.PHONY: clean
clean:
	rm -rf ${BINS}
