CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -Iinclude -g -MMD -MP
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

test: all
	sh tests/run_tests.sh

clean:
	rm -rf build $(BIN) logs/*.log

-include $(OBJ:.o=.d)

.PHONY: all test clean
