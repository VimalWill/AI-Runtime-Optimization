// ================= main.cpp =================
#include "runtime.h"
#include "fib.h"

int main(int argc, char** argv) {
  int numThreads = argc > 1 ? atoi(argv[1]) : 8;
  Runtime<FibArgs, Worker<FibArgs, FuncType>> rt(numThreads);
  rt.run();
  return 0;
}
