// Example Cilk Fibonacci (Conceptual, based on OpenCilk tutorials)
#include <stdio.h>
#include <stdlib.h>
#include <cilk/cilk.h> // Include Cilk header

// Alternative using cilk_scope for clarity on parallel region
long p_fib(int n) {
    if (n < 2) {
        return n;
    }
    long fib_n_1, fib_n_2;
    fib_n_1 = cilk_spawn p_fib(n - 1); // Spawn one task
    fib_n_2 = p_fib(n - 2);          // Other runs in current thread
    return fib_n_1 + fib_n_2;
}

int main(int argc, char *argv[]) {
    int n = 42; // Calculate the 30th Fibonacci number (can be much larger)
    if (argc > 1) {
        n = atoi(argv[1]);
    }
    printf("fib(%d) = %ld\n", n, p_fib(n)); // Using p_fib for clearer scope
    return 0;
}

