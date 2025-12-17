// ================= main.cpp =================
#include "runtime.h"
#include "fib.h"

int main() {
  Runtime<FibArgs, Worker<FibArgs, FuncType>> rt(18);
  rt.run();
  return 0;
}
