
BINS = main_erase_flash_all main_ram_download main_read_dp main_setbase main_test_alive main_write_dp main
CC ?= gcc

.PHONY: all
all: ${BINS}

%: %.c
	${CC} -Wall $^ -o $@

.PHONY: clean
clean:
	rm -rf ${BINS}