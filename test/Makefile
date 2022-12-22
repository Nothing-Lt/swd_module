
BINS = main_write_flash main_program_flash main_erase_flash_all main_ram_download main_test_alive main
CC ?= gcc

.PHONY: all
all: ${BINS}

%: %.c
	${CC} -Wall $^ -o $@

.PHONY: clean
clean:
	rm -rf ${BINS}
