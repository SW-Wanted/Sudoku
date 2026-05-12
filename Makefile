CC     = gcc
MPICC  = mpicc
MPIRUN = mpirun --use-hwthread-cpus -np 4
CFLAGS = -Wall -Wextra -Werror -O2 -fopenmp -g

all: sudoku-serial sudoku-omp sudoku-mpi

obj:
	mkdir -p obj

obj/sudoku-serial.o: sudoku/serial/sudoku-serial.c
	mkdir -p obj
	$(CC) $(CFLAGS) -c sudoku/serial/sudoku-serial.c -o obj/sudoku-serial.o

obj/sudoku-omp.o: sudoku/paralela/sudoku-omp.c
	mkdir -p obj
	$(CC) $(CFLAGS) -c sudoku/paralela/sudoku-omp.c -o obj/sudoku-omp.o

obj/sudoku-mpi.o: sudoku/paralela/sudoku-mpi.c
	mkdir -p obj
	$(MPICC) -Wall -Wextra -O2 -g -c sudoku/paralela/sudoku-mpi.c -o obj/sudoku-mpi.o

sudoku-serial: obj/sudoku-serial.o
	$(CC) $(CFLAGS) obj/sudoku-serial.o -o sudoku-serial

sudoku-omp: obj/sudoku-omp.o
	$(CC) $(CFLAGS) obj/sudoku-omp.o -o sudoku-omp

sudoku-mpi: obj/sudoku-mpi.o
	$(MPICC) -Wall -Wextra -O2 -g obj/sudoku-mpi.o -o sudoku-mpi

test1: sudoku-serial
	./sudoku-serial tests/input_4x4.txt

test1-omp: sudoku-omp
	./sudoku-omp tests/input_4x4.txt

test1-mpi: sudoku-mpi
	$(MPIRUN) ./sudoku-mpi tests/input_4x4.txt

test2: sudoku-serial
	./sudoku-serial tests/input_4x4_impossible.txt

test2-omp: sudoku-omp
	./sudoku-omp tests/input_4x4_impossible.txt

test2-mpi: sudoku-mpi
	$(MPIRUN) ./sudoku-mpi tests/input_4x4_impossible.txt

test3: sudoku-serial
	./sudoku-serial tests/input_9x9.txt

test3-omp: sudoku-omp
	./sudoku-omp tests/input_9x9.txt

test3-mpi: sudoku-mpi
	$(MPIRUN) ./sudoku-mpi tests/input_9x9.txt

test4: sudoku-serial
	./sudoku-serial tests/9x9.txt

test4-omp: sudoku-omp
	./sudoku-omp tests/9x9.txt

test4-mpi: sudoku-mpi
	$(MPIRUN) ./sudoku-mpi tests/9x9.txt

test5: sudoku-serial
	./sudoku-serial tests/9x9-nosol.txt

test5-omp: sudoku-omp
	./sudoku-omp tests/9x9-nosol.txt

test5-mpi: sudoku-mpi
	$(MPIRUN) ./sudoku-mpi tests/9x9-nosol.txt

test6: sudoku-serial
	./sudoku-serial tests/16x16.txt

test6-omp: sudoku-omp
	./sudoku-omp tests/16x16.txt

test6-mpi: sudoku-mpi
	$(MPIRUN) ./sudoku-mpi tests/16x16.txt

test7: sudoku-serial
	./sudoku-serial tests/16x16-nosol.txt

test7-omp: sudoku-omp
	./sudoku-omp tests/16x16-nosol.txt

test7-mpi: sudoku-mpi
	$(MPIRUN) ./sudoku-mpi tests/16x16-nosol.txt

test8: sudoku-serial
	./sudoku-serial tests/16x16-zeros.txt

test8-omp: sudoku-omp
	./sudoku-omp tests/16x16-zeros.txt

test8-mpi: sudoku-mpi
	$(MPIRUN) ./sudoku-mpi tests/16x16-zeros.txt

test: test1 test2 test3 test4 test5 test6 test7 test8

test-omp: test1-omp test2-omp test3-omp test4-omp test5-omp test6-omp test7-omp test8-omp

test-mpi: test1-mpi test2-mpi test3-mpi test4-mpi test5-mpi test6-mpi test7-mpi test8-mpi

clean:
	rm -rf obj sudoku-serial sudoku-omp sudoku-mpi

re: clean all

.PHONY: all obj \
        test test-omp test-mpi \
        test1 test2 test3 test4 test5 test6 test7 test8 \
        test1-omp test2-omp test3-omp test4-omp test5-omp test6-omp test7-omp test8-omp \
        test1-mpi test2-mpi test3-mpi test4-mpi test5-mpi test6-mpi test7-mpi test8-mpi \
        clean re