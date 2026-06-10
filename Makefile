CC      = gcc
CFLAGS  = -O3 -fopenmp -g -march=native -Wall -Wextra
LDFLAGS = -lm

TARGETS = primes_omp primes

.PHONY: all clean run run-ref help

## Build both binaries (default)
all: $(TARGETS)

## Final version (primes_omp.c) — recommended
primes_omp: primes_omp.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

## Reference version (primes.c)
primes: primes.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

## Run the final version with default range <2, 100 000 000>
run: primes_omp
	OMP_NUM_THREADS=$$(nproc) ./primes_omp 2 100000000

## Run with custom range: make run-range M=2 N=50000000
run-range: primes_omp
	OMP_NUM_THREADS=$$(nproc) ./primes_omp $(M) $(N)

## Run the reference version
run-ref: primes
	OMP_NUM_THREADS=$$(nproc) ./primes 2 100000000

## Remove build artifacts
clean:
	rm -f $(TARGETS)
	rm -rf *.dSYM

help:
	@echo "Targets:"
	@echo "  all        — build primes_omp and primes (default)"
	@echo "  primes_omp — build final version"
	@echo "  primes     — build reference version"
	@echo "  run        — build and run primes_omp (2..100M, all cores)"
	@echo "  run-range  — run with custom M/N, e.g.: make run-range M=2 N=50000000"
	@echo "  run-ref    — build and run reference version"
	@echo "  clean      — remove binaries"
