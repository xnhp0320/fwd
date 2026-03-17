#include "fib/fib_loader.h"

#include <arpa/inet.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <utility>

extern "C" {
#include "tbm/tbmlib.h"
}

int failed_tests = 0;

void TestCase(const std::string& name, bool condition) {
  if (condition) {
    std::cout << "[PASS] " << name << "\n";
  } else {
    std::cout << "[FAIL] " << name << "\n";
    failed_tests++;
  }
}

// Helper: write content to a temp file and return its path.
static std::string WriteTempFib(const std::string& content,
                                const std::string& suffix) {
  std::string path = "/tmp/fib_loader_test_" + suffix + ".txt";
  std::ofstream out(path);
  out << content;
  out.close();
  return path;
}

// Callback for tbm_iterate — collects (ip, cidr) pairs into a set.
struct IterateCtx {
  std::set<std::pair<uint32_t, uint32_t>> entries;
};

void CollectEntry(FibCidr* cidr, uint32_t* /*value*/, void* aux) {
  auto* ctx = static_cast<IterateCtx*>(aux);
  ctx->entries.insert({cidr->ip, cidr->cidr});
}

int main() {
  std::cout << "Running FibLoader tests...\n\n";

  // ---- LPM tests ----

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

  // ---- TBM tests ----

  std::cout << "\n--- LoadFibFileToTbm tests ---\n\n";

  // Test: null tbm pointer returns InvalidArgumentError.
  {
    auto status = fib::LoadFibFileToTbm("any_path", nullptr);
    TestCase("LoadFibFileToTbm with nullptr returns InvalidArgumentError",
             !status.ok() &&
                 status.code() == absl::StatusCode::kInvalidArgument);
  }

  // Test: non-existent file returns NotFoundError.
  {
    FibTbm tbm = {};
    tbm_init(&tbm, 1024);
    auto status = fib::LoadFibFileToTbm("/nonexistent/path.txt", &tbm);
    TestCase("LoadFibFileToTbm with bad path returns NotFoundError",
             !status.ok() && status.code() == absl::StatusCode::kNotFound);
    tbm_free(&tbm);
  }

  // Test: valid FIB file loads entries correctly.
  {
    FibTbm tbm = {};
    tbm_init(&tbm, 1048576);
    uint32_t rules_loaded = 0;
    auto status = fib::LoadFibFileToTbm("fib/ipv4_test_fib.txt", &tbm,
                                        &rules_loaded);
    TestCase("LoadFibFileToTbm with valid file returns OK", status.ok());
    TestCase("LoadFibFileToTbm rules_loaded == 10", rules_loaded == 10);

    // Iterate and verify all 10 entries are present.
    IterateCtx ctx;
    tbm_iterate(&tbm, CollectEntry, &ctx);
    TestCase("LoadFibFileToTbm iterate yields 10 entries",
             ctx.entries.size() == 10);

    // Spot-check a few known entries (IP in host byte order).
    // 1.0.0.0/24 → host order 0x01000000
    TestCase("LoadFibFileToTbm contains 1.0.0.0/24",
             ctx.entries.count({0x01000000, 24}) == 1);
    // 1.0.64.0/18 → host order 0x01004000
    TestCase("LoadFibFileToTbm contains 1.0.64.0/18",
             ctx.entries.count({0x01004000, 18}) == 1);
    // 1.0.128.0/17 → host order 0x01008000
    TestCase("LoadFibFileToTbm contains 1.0.128.0/17",
             ctx.entries.count({0x01008000, 17}) == 1);

    tbm_free(&tbm);
  }

  // Test: invalid IPv4 address returns InvalidArgumentError.
  {
    std::string path = WriteTempFib("not_an_ip\n24\n", "bad_ip");
    FibTbm tbm = {};
    tbm_init(&tbm, 1024);
    auto status = fib::LoadFibFileToTbm(path, &tbm);
    TestCase("LoadFibFileToTbm with invalid IP returns InvalidArgumentError",
             !status.ok() &&
                 status.code() == absl::StatusCode::kInvalidArgument);
    tbm_free(&tbm);
  }

  // Test: CIDR outside 0-32 returns InvalidArgumentError.
  {
    std::string path = WriteTempFib("10.0.0.0\n33\n", "bad_cidr");
    FibTbm tbm = {};
    tbm_init(&tbm, 1024);
    auto status = fib::LoadFibFileToTbm(path, &tbm);
    TestCase("LoadFibFileToTbm with CIDR>32 returns InvalidArgumentError",
             !status.ok() &&
                 status.code() == absl::StatusCode::kInvalidArgument);
    tbm_free(&tbm);
  }

  // Test: negative CIDR returns InvalidArgumentError.
  {
    std::string path = WriteTempFib("10.0.0.0\n-1\n", "neg_cidr");
    FibTbm tbm = {};
    tbm_init(&tbm, 1024);
    auto status = fib::LoadFibFileToTbm(path, &tbm);
    TestCase("LoadFibFileToTbm with CIDR<0 returns InvalidArgumentError",
             !status.ok() &&
                 status.code() == absl::StatusCode::kInvalidArgument);
    tbm_free(&tbm);
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
