CC     = gcc
CFLAGS = -Wall -Wextra -Werror -O2 -fopenmp -g

all: sudoku-serial

obj:
	mkdir -p obj

obj/sudoku-serial.o: sudoku/serial/sudoku-serial.c
	mkdir -p obj
	$(CC) $(CFLAGS) -c sudoku/serial/sudoku-serial.c -o obj/sudoku-serial.o

sudoku-serial: obj/sudoku-serial.o
	$(CC) $(CFLAGS) obj/sudoku-serial.o -o sudoku-serial

test1: sudoku-serial
	./sudoku-serial tests/input_4x4.txt

test2: sudoku-serial
	./sudoku-serial tests/input_4x4_impossible.txt

test3: sudoku-serial
	./sudoku-serial tests/input_9x9.txt

test: test1 test2 test3

clean:
	rm -rf obj sudoku-serial

re: clean all

.PHONY: all test test1 test2 test3 clean re