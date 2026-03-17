#include "processor/tbm_forwarding_processor.h"

#include <iostream>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "processor/processor_context.h"
#include "processor/processor_registry.h"

extern "C" {
#include "tbm/tbmlib.h"
}

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
  std::cout << "Running TbmForwardingProcessor tests...\n\n";

  // --- Registration tests (Requirement 5.1) ---
  {
    std::cout << "--- Registration tests ---\n";

    auto result = ProcessorRegistry::Instance().Lookup("tbm_forwarding");
    TestCase("Lookup returns OK", result.ok());

    if (result.ok()) {
      const ProcessorEntry* entry = result.value();
      TestCase("Launcher is non-null", entry->launcher != nullptr);
      TestCase("Checker is non-null", entry->checker != nullptr);
      TestCase("ParamChecker is non-null", entry->param_checker != nullptr);
    } else {
      TestCase("Launcher is non-null", false);
      TestCase("Checker is non-null", false);
      TestCase("ParamChecker is non-null", false);
    }

    std::cout << "\n";
  }

  // --- Coexistence with LPM (Requirement 14.1) ---
  {
    std::cout << "--- Coexistence tests ---\n";

    auto tbm_result = ProcessorRegistry::Instance().Lookup("tbm_forwarding");
    auto lpm_result = ProcessorRegistry::Instance().Lookup("lpm_forwarding");

    TestCase("tbm_forwarding is registered", tbm_result.ok());
    TestCase("lpm_forwarding is registered", lpm_result.ok());

    // Both should be distinct entries
    if (tbm_result.ok() && lpm_result.ok()) {
      TestCase("TBM and LPM are distinct entries",
               tbm_result.value() != lpm_result.value());
    } else {
      TestCase("TBM and LPM are distinct entries", false);
    }

    std::cout << "\n";
  }

  // --- check_impl tests (Requirements 6.1, 6.2) ---
  {
    std::cout << "--- check_impl tests ---\n";

    PmdThreadConfig config;
    config.lcore_id = 1;
    TbmForwardingProcessor proc(config);

    std::vector<QueueAssignment> rx_queues;
    std::vector<QueueAssignment> tx_queues_empty;

    auto status = proc.check_impl(rx_queues, tx_queues_empty);
    TestCase("Empty tx_queues returns error", !status.ok());
    TestCase("Empty tx_queues returns InvalidArgument",
             status.code() == absl::StatusCode::kInvalidArgument);

    std::vector<QueueAssignment> tx_queues_one = {{0, 0}};
    auto ok_status = proc.check_impl(rx_queues, tx_queues_one);
    TestCase("Non-empty tx_queues returns OK", ok_status.ok());

    std::cout << "\n";
  }

  // --- CheckParams tests (Requirements 11.1, 11.2) ---
  {
    std::cout << "--- CheckParams tests ---\n";

    absl::flat_hash_map<std::string, std::string> empty_params;
    TestCase("CheckParams: empty map returns OK",
             TbmForwardingProcessor::CheckParams(empty_params).ok());

    absl::flat_hash_map<std::string, std::string> one_param = {
        {"unknown_key", "42"}};
    auto bad_status = TbmForwardingProcessor::CheckParams(one_param);
    TestCase("CheckParams: non-empty map returns error", !bad_status.ok());
    TestCase("CheckParams: non-empty map returns InvalidArgument",
             bad_status.code() == absl::StatusCode::kInvalidArgument);

    std::cout << "\n";
  }

  // --- ExportProcessorData tests (Requirements 1.2, 12.2) ---
  {
    std::cout << "--- ExportProcessorData tests ---\n";

    PmdThreadConfig config;
    config.lcore_id = 1;
    TbmForwardingProcessor proc(config);

    // Set up a ProcessorContext with known sentinel values.
    ProcessorContext ctx{};
    void* sentinel_session = reinterpret_cast<void*>(0xDEAD);
    void* sentinel_data = reinterpret_cast<void*>(0xBEEF);
    void* sentinel_lpm = reinterpret_cast<void*>(0xCAFE);
    ctx.session_table = sentinel_session;
    ctx.processor_data = sentinel_data;
    ctx.lpm_table = sentinel_lpm;

    // Provide a TBM table pointer.
    FibTbm tbm{};
    ctx.tbm_table = &tbm;

    proc.ExportProcessorData(ctx);

    TestCase("ExportProcessorData: session_table unchanged",
             ctx.session_table == sentinel_session);
    TestCase("ExportProcessorData: processor_data unchanged",
             ctx.processor_data == sentinel_data);
    TestCase("ExportProcessorData: lpm_table unchanged",
             ctx.lpm_table == sentinel_lpm);

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
