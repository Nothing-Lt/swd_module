
BINS = main_read_flash main_read_ram
CC ?= gcc

.PHONY: all
all: ${BINS}

%: %.c
	${CC} -Wall $^ -o $@

.PHONY: clean
clean:
	rm -rf ${BINS} ram.txt
