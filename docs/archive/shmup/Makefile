CC ?= gcc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -I.
all: arcade1_skydescent_libretro.so
arcade1_skydescent_libretro.so: libretro.c shmup.c shmup.h tapcad.h tables.inc
	$(CC) $(CFLAGS) -shared -fPIC -o $@ libretro.c shmup.c
test: shmup_test.c shmup.c
	$(CC) $(CFLAGS) -o /tmp/shmup_test shmup_test.c shmup.c && /tmp/shmup_test
clean:
	rm -f arcade1_skydescent_libretro.so
