// ================= main.cpp =================
#include "runtime.h"
#include "fib.h"

int main() {
  Runtime<FibArgs, Worker<FibArgs, FuncType>> rt(4);
  rt.run();
  return 0;
}
