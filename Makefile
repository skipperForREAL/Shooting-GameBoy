CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=gnu11
LDLIBS  = -lX11

SRC = src/main.c src/cpu.c src/memory.c src/gpu.c src/timer.c \
      src/interrupts.c src/keys.c src/platform_x11.c
OBJ = $(SRC:.c=.o)

gbemu: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/*.h
	$(CC) $(CFLAGS) -c -o $@ $<

roms/shooter.gb: tools/shooter.py tools/gbasm.py
	python3 tools/shooter.py $@

run: gbemu roms/shooter.gb
	./gbemu roms/shooter.gb

clean:
	rm -f gbemu src/*.o

.PHONY: run clean
