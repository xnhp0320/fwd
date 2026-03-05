#include "processor/five_tuple_forwarding_processor.h"

#include <iostream>
#include <string>

#include "processor/processor_registry.h"

using namespace dpdk_config;
using namespace processor;

// Simple test helper
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
  std::cout << "Running FiveTupleForwardingProcessor tests...\n\n";

  // --- Registration tests (Requirements 1.1, 1.2) ---
  {
    std::cout << "--- Registration tests ---\n";

    auto result = ProcessorRegistry::Instance().Lookup("five_tuple_forwarding");
    TestCase("Lookup returns OK", result.ok());

    if (result.ok()) {
      const ProcessorEntry* entry = result.value();
      TestCase("Launcher is non-null", entry->launcher != nullptr);
      TestCase("Checker is non-null", entry->checker != nullptr);
      TestCase("ParamChecker is non-null", entry->param_checker != nullptr);
    } else {
      // Force failures if lookup failed
      TestCase("Launcher is non-null", false);
      TestCase("Checker is non-null", false);
      TestCase("ParamChecker is non-null", false);
    }

    std::cout << "\n";
  }

  // --- check_impl edge case: empty tx_queues (Requirement 3.1) ---
  {
    std::cout << "--- check_impl tests ---\n";

    PmdThreadConfig config;
    config.lcore_id = 1;
    FiveTupleForwardingProcessor proc(config);

    std::vector<QueueAssignment> rx_queues;
    std::vector<QueueAssignment> tx_queues_empty;

    auto status = proc.check_impl(rx_queues, tx_queues_empty);
    TestCase("Empty tx_queues returns error", !status.ok());
    TestCase("Empty tx_queues returns InvalidArgument",
             status.code() == absl::StatusCode::kInvalidArgument);

    // Non-empty tx_queues should return OK
    std::vector<QueueAssignment> tx_queues_one = {{0, 0}};
    auto ok_status = proc.check_impl(rx_queues, tx_queues_one);
    TestCase("Non-empty tx_queues returns OK", ok_status.ok());

    std::cout << "\n";
  }

  // --- Default capacity (Requirement 6.2) ---
  {
    std::cout << "--- Default capacity tests ---\n";

    PmdThreadConfig config;
    config.lcore_id = 1;
    // No "capacity" in processor_params
    FiveTupleForwardingProcessor proc(config);

    TestCase("Default capacity is 65536", proc.table_capacity() == 65536);

    // With explicit capacity
    PmdThreadConfig config2;
    config2.lcore_id = 1;
    config2.processor_params["capacity"] = "1024";
    FiveTupleForwardingProcessor proc2(config2);

    TestCase("Explicit capacity 1024", proc2.table_capacity() == 1024);

    std::cout << "\n";
  }

  // --- CheckParams tests (Requirements 5.2, 5.3, 5.5, 6.3) ---
  {
    std::cout << "--- CheckParams tests ---\n";

    // Empty map is always OK
    absl::flat_hash_map<std::string, std::string> empty_params;
    TestCase("CheckParams: empty map returns OK",
             FiveTupleForwardingProcessor::CheckParams(empty_params).ok());

    // Valid capacity
    absl::flat_hash_map<std::string, std::string> valid_cap = {
        {"capacity", "4096"}};
    TestCase("CheckParams: valid capacity returns OK",
             FiveTupleForwardingProcessor::CheckParams(valid_cap).ok());

    // Invalid capacity: zero
    absl::flat_hash_map<std::string, std::string> zero_cap = {
        {"capacity", "0"}};
    auto zero_status = FiveTupleForwardingProcessor::CheckParams(zero_cap);
    TestCase("CheckParams: zero capacity returns error", !zero_status.ok());
    TestCase("CheckParams: zero capacity is InvalidArgument",
             zero_status.code() == absl::StatusCode::kInvalidArgument);

    // Invalid capacity: negative
    absl::flat_hash_map<std::string, std::string> neg_cap = {
        {"capacity", "-1"}};
    auto neg_status = FiveTupleForwardingProcessor::CheckParams(neg_cap);
    TestCase("CheckParams: negative capacity returns error",
             !neg_status.ok());

    // Invalid capacity: non-integer
    absl::flat_hash_map<std::string, std::string> str_cap = {
        {"capacity", "abc"}};
    auto str_status = FiveTupleForwardingProcessor::CheckParams(str_cap);
    TestCase("CheckParams: non-integer capacity returns error",
             !str_status.ok());

    // Invalid capacity: empty string
    absl::flat_hash_map<std::string, std::string> empty_cap = {
        {"capacity", ""}};
    auto empty_status =
        FiveTupleForwardingProcessor::CheckParams(empty_cap);
    TestCase("CheckParams: empty capacity string returns error",
             !empty_status.ok());

    // Unrecognized key
    absl::flat_hash_map<std::string, std::string> bad_key = {
        {"unknown_key", "42"}};
    auto bad_status = FiveTupleForwardingProcessor::CheckParams(bad_key);
    TestCase("CheckParams: unrecognized key returns error",
             !bad_status.ok());
    TestCase("CheckParams: unrecognized key is InvalidArgument",
             bad_status.code() == absl::StatusCode::kInvalidArgument);

    std::cout << "\n";
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
