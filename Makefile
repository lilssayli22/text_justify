# ==============================
# Makefile officiel ENSIMAG
# ==============================

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
SRC = src/AODjustify.c
BIN = bin/AODjustify

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

run:
	./bin/AODjustify 40 tests/test1.in

valgrind:
	valgrind --tool=cachegrind ./bin/AODjustify 40 tests/test1.in

clean:
	rm -rf bin/*.o bin/AODjustify *.out

.PHONY: all run valgrind clean
