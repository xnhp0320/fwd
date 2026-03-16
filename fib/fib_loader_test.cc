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

  // Stub returns OkStatus for any input, including nullptr.
  {
    auto status = fib::LoadFibFile("any_path", nullptr);
    TestCase("LoadFibFile with nullptr returns OkStatus", status.ok());
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
