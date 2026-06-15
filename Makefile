CC     = gcc
CFLAGS = -Wall -Wextra -Werror -O2 -fopenmp -g

all: sudoku-serial sudoku-omp

obj:
	mkdir -p obj

obj/sudoku-serial.o: sudoku/sudoku-serial.c
	mkdir -p obj
	$(CC) $(CFLAGS) -c sudoku/sudoku-serial.c -o obj/sudoku-serial.o

obj/sudoku-omp.o: sudoku/sudoku-omp.c
	mkdir -p obj
	$(CC) $(CFLAGS) -c sudoku/sudoku-omp.c -o obj/sudoku-omp.o

sudoku-serial: obj/sudoku-serial.o
	$(CC) $(CFLAGS) obj/sudoku-serial.o -o sudoku-serial

sudoku-omp: obj/sudoku-omp.o
	$(CC) $(CFLAGS) obj/sudoku-omp.o -o sudoku-omp

test1: sudoku-serial
	./sudoku-serial tests/9x9.txt

test1-omp: sudoku-omp
	./sudoku-omp tests/9x9.txt

test2: sudoku-serial
	./sudoku-serial tests/9x9-nosol.txt

test2-omp: sudoku-omp
	./sudoku-omp tests/9x9-nosol.txt

test3: sudoku-serial
	./sudoku-serial tests/16x16.txt

test3-omp: sudoku-omp
	./sudoku-omp tests/16x16.txt

test4: sudoku-serial
	./sudoku-serial tests/16x16-nosol.txt

test4-omp: sudoku-omp
	./sudoku-omp tests/16x16-nosol.txt

test5: sudoku-serial
	./sudoku-serial tests/16x16-zeros.txt

test5-omp: sudoku-omp
	./sudoku-omp tests/16x16-zeros.txt

test: test1 test2 test3 test4 test5

test-omp: test1-omp test2-omp test3-omp test4-omp test5-omp

clean:
	rm -rf obj sudoku-serial sudoku-omp

re: clean all

.PHONY: all obj \
		test test-omp \
	test1 test2 test3 test4 test5 \
	test1-omp test2-omp test3-omp test4-omp test5-omp \
        clean re