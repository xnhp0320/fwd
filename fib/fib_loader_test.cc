#include "fib/fib_loader.h"

#include <iostream>
#include <string>

int failed_tests = 0;

void TestCase(const std::string& name, bool condition) {
  if (condition) {
    std::cout << "[PASS] " << name << "\n";
  } else {
    std::cout << "[FAIL] " << name << "\n";
    failed_tests++;
  }
}

int main() {
  std::cout << "Running FibLoader tests...\n\n";

  // nullptr lpm pointer should return InvalidArgumentError.
  {
    auto status = fib::LoadFibFile("any_path", nullptr);
    TestCase("LoadFibFile with nullptr returns error", !status.ok());
  }

  // Non-existent file should return NotFoundError.
  {
    // Pass a non-null but invalid pointer — LoadFibFile will fail on
    // file open before touching the lpm pointer.
    struct rte_lpm* fake = reinterpret_cast<struct rte_lpm*>(0x1);
    auto status = fib::LoadFibFile("/nonexistent/path.txt", fake);
    TestCase("LoadFibFile with bad path returns error", !status.ok());
  }

  std::cout << "\n";
  if (failed_tests == 0) {
    std::cout << "All tests passed!\n";
    return 0;
  } else {
    std::cout << failed_tests << " test(s) failed.\n";
    return 1;
  }
}
