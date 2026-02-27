#include "config/config_parser.h"

#include <iostream>
#include <string>

using namespace dpdk_config;

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
  std::cout << "Running ConfigParser PMD thread tests...\n\n";

  // Test parsing valid PMD thread configuration with lcore_id only
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "lcore_id": 1
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Parse PMD thread with lcore_id only", 
             result.ok() && 
             result.value().pmd_threads.size() == 1 &&
             result.value().pmd_threads[0].lcore_id == 1 &&
             result.value().pmd_threads[0].rx_queues.empty() &&
             result.value().pmd_threads[0].tx_queues.empty());
  }

  // Test parsing PMD thread with rx_queues
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "lcore_id": 2,
          "rx_queues": [
            {"port_id": 0, "queue_id": 0},
            {"port_id": 0, "queue_id": 1}
          ]
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Parse PMD thread with rx_queues", 
             result.ok() && 
             result.value().pmd_threads.size() == 1 &&
             result.value().pmd_threads[0].lcore_id == 2 &&
             result.value().pmd_threads[0].rx_queues.size() == 2 &&
             result.value().pmd_threads[0].rx_queues[0].port_id == 0 &&
             result.value().pmd_threads[0].rx_queues[0].queue_id == 0 &&
             result.value().pmd_threads[0].rx_queues[1].port_id == 0 &&
             result.value().pmd_threads[0].rx_queues[1].queue_id == 1);
  }

  // Test parsing PMD thread with tx_queues
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "lcore_id": 3,
          "tx_queues": [
            {"port_id": 1, "queue_id": 0}
          ]
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Parse PMD thread with tx_queues", 
             result.ok() && 
             result.value().pmd_threads.size() == 1 &&
             result.value().pmd_threads[0].lcore_id == 3 &&
             result.value().pmd_threads[0].tx_queues.size() == 1 &&
             result.value().pmd_threads[0].tx_queues[0].port_id == 1 &&
             result.value().pmd_threads[0].tx_queues[0].queue_id == 0);
  }

  // Test parsing PMD thread with both rx_queues and tx_queues
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "lcore_id": 4,
          "rx_queues": [
            {"port_id": 0, "queue_id": 0}
          ],
          "tx_queues": [
            {"port_id": 0, "queue_id": 0}
          ]
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Parse PMD thread with both rx_queues and tx_queues", 
             result.ok() && 
             result.value().pmd_threads.size() == 1 &&
             result.value().pmd_threads[0].lcore_id == 4 &&
             result.value().pmd_threads[0].rx_queues.size() == 1 &&
             result.value().pmd_threads[0].tx_queues.size() == 1);
  }

  // Test error when lcore_id is missing
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "rx_queues": []
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Error when lcore_id is missing", 
             !result.ok() && 
             result.status().message().find("missing required field: lcore_id") != std::string::npos);
  }

  // Test error when lcore_id is not an unsigned integer
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "lcore_id": "invalid"
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Error when lcore_id is invalid type", 
             !result.ok() && 
             result.status().message().find("must be an unsigned integer") != std::string::npos);
  }

  // Test error when rx_queues is not an array
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "lcore_id": 1,
          "rx_queues": "invalid"
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Error when rx_queues is not an array", 
             !result.ok() && 
             result.status().message().find("must be an array") != std::string::npos);
  }

  // Test error when tx_queues is not an array
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "lcore_id": 1,
          "tx_queues": {}
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Error when tx_queues is not an array", 
             !result.ok() && 
             result.status().message().find("must be an array") != std::string::npos);
  }

  // Test error when queue assignment is missing port_id
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "lcore_id": 1,
          "rx_queues": [
            {"queue_id": 0}
          ]
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Error when queue assignment is missing port_id", 
             !result.ok() && 
             result.status().message().find("missing required field: port_id") != std::string::npos);
  }

  // Test error when queue assignment is missing queue_id
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "lcore_id": 1,
          "rx_queues": [
            {"port_id": 0}
          ]
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Error when queue assignment is missing queue_id", 
             !result.ok() && 
             result.status().message().find("missing required field: queue_id") != std::string::npos);
  }

  // Test parsing multiple PMD threads
  {
    std::string json_content = R"({
      "pmd_threads": [
        {
          "lcore_id": 1,
          "rx_queues": [{"port_id": 0, "queue_id": 0}]
        },
        {
          "lcore_id": 2,
          "tx_queues": [{"port_id": 1, "queue_id": 0}]
        }
      ]
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Parse multiple PMD threads", 
             result.ok() && 
             result.value().pmd_threads.size() == 2 &&
             result.value().pmd_threads[0].lcore_id == 1 &&
             result.value().pmd_threads[1].lcore_id == 2);
  }

  // Test parsing empty pmd_threads array
  {
    std::string json_content = R"({
      "pmd_threads": []
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Parse empty pmd_threads array", 
             result.ok() && 
             result.value().pmd_threads.empty());
  }

  // Test parsing config without pmd_threads field
  {
    std::string json_content = R"({
      "core_mask": "0xff"
    })";

    auto result = ConfigParser::ParseString(json_content);
    TestCase("Parse config without pmd_threads field", 
             result.ok() && 
             result.value().pmd_threads.empty());
  }

  // Test that pmd_threads is not added to additional_params
  {
    std::string json_content = R"({
      "core_mask": "0xff",
      "pmd_threads": [
        {
          "lcore_id": 1,
          "rx_queues": [{"port_id": 0, "queue_id": 0}]
        }
      ],
      "custom_field": "custom_value"
    })";

    auto result = ConfigParser::ParseString(json_content);
    bool pmd_threads_not_in_additional = true;
    if (result.ok()) {
      for (const auto& param : result.value().additional_params) {
        if (param.first == "pmd_threads") {
          pmd_threads_not_in_additional = false;
          break;
        }
      }
    }
    TestCase("pmd_threads not added to additional_params", 
             result.ok() && 
             pmd_threads_not_in_additional &&
             result.value().pmd_threads.size() == 1 &&
             result.value().additional_params.size() == 1 &&
             result.value().additional_params[0].first == "custom_field");
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
