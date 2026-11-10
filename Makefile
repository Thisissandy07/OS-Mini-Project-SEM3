CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -Iinclude -g
LDFLAGS = -pthread -lrt
SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)
BIN = campuscompute

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build $(BIN) logs/*.log

.PHONY: all clean
