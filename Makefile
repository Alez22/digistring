CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
CROSS ?= m68k-linux-gnu-
CROSS_CFLAGS = -mcpu=54455 -O2 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie -fomit-frame-pointer -Wall -Wextra -Werror

.PHONY: test cross-check clean
out:
	mkdir -p out
out/test_karplus: karplus.c karplus.h ks_tables.inc tests/test_karplus.c | out
	$(CC) $(CFLAGS) -I. karplus.c tests/test_karplus.c -lm -o $@
test: out/test_karplus
	./out/test_karplus
cross-check: | out
	mkdir -p out/cross
	$(CROSS)as -mcpu=54455 -o out/cross/glue.o glue.s
	$(CROSS)gcc $(CROSS_CFLAGS) -I. -c digitakt.c -o out/cross/digitakt.o
	$(CROSS)gcc $(CROSS_CFLAGS) -I. -c karplus.c -o out/cross/karplus.o
	$(CROSS)size out/cross/glue.o out/cross/digitakt.o out/cross/karplus.o
clean:
	rm -rf out
