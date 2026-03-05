#include "control/command_handler.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

using namespace dpdk_config;
using json = nlohmann::json;

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
  std::cout << "Running CommandHandler tag system tests...\n\n";

  // Create a CommandHandler with nullptr thread_manager and no-op shutdown.
  CommandHandler handler(nullptr, []() {});

  // --- GetAllCommands returns all 6 commands with correct tags ---
  // Requirements: 7.1, 7.2, 7.4
  {
    std::cout << "--- GetAllCommands tests ---\n";

    auto all = handler.GetAllCommands();
    TestCase("GetAllCommands returns 6 commands", all.size() == 6);

    // Build a map of name -> tag for easy lookup
    std::map<std::string, std::string> cmd_map(all.begin(), all.end());

    // Existing commands have tag "common" (Requirement 7.2)
    TestCase("shutdown has tag common", cmd_map["shutdown"] == "common");
    TestCase("status has tag common", cmd_map["status"] == "common");
    TestCase("get_threads has tag common", cmd_map["get_threads"] == "common");
    TestCase("get_stats has tag common", cmd_map["get_stats"] == "common");

    // list_commands has tag "common" (Requirement 10.1)
    TestCase("list_commands has tag common",
             cmd_map["list_commands"] == "common");

    // get_flow_table has tag "five_tuple_forwarding" (Requirement 8.1)
    TestCase("get_flow_table has tag five_tuple_forwarding",
             cmd_map["get_flow_table"] == "five_tuple_forwarding");

    std::cout << "\n";
  }

  // --- GetCommandsByTag("common") returns the 5 common commands ---
  // Requirements: 7.3
  {
    std::cout << "--- GetCommandsByTag tests ---\n";

    auto common = handler.GetCommandsByTag("common");
    TestCase("GetCommandsByTag(common) returns 5 commands",
             common.size() == 5);

    std::set<std::string> common_set(common.begin(), common.end());
    TestCase("common contains shutdown",
             common_set.count("shutdown") == 1);
    TestCase("common contains status",
             common_set.count("status") == 1);
    TestCase("common contains get_threads",
             common_set.count("get_threads") == 1);
    TestCase("common contains get_stats",
             common_set.count("get_stats") == 1);
    TestCase("common contains list_commands",
             common_set.count("list_commands") == 1);
    TestCase("common does not contain get_flow_table",
             common_set.count("get_flow_table") == 0);

    // GetCommandsByTag("five_tuple_forwarding") returns only get_flow_table
    auto ftf = handler.GetCommandsByTag("five_tuple_forwarding");
    TestCase("GetCommandsByTag(five_tuple_forwarding) returns 1 command",
             ftf.size() == 1);
    TestCase("five_tuple_forwarding contains get_flow_table",
             ftf.size() == 1 && ftf[0] == "get_flow_table");

    // Unknown tag returns empty
    auto unknown = handler.GetCommandsByTag("nonexistent_tag");
    TestCase("GetCommandsByTag(nonexistent_tag) returns empty",
             unknown.empty());

    std::cout << "\n";
  }

  // --- HandleCommand: get_flow_table returns "not_supported" without callback ---
  // Requirements: 8.3
  {
    std::cout << "--- get_flow_table not_supported tests ---\n";

    std::string request = R"({"command":"get_flow_table"})";
    auto response_opt = handler.HandleCommand(request);
    TestCase("get_flow_table returns a value", response_opt.has_value());
    json response = json::parse(*response_opt);

    TestCase("get_flow_table status is error",
             response["status"] == "error");
    TestCase("get_flow_table error is not_supported",
             response["error"] == "not_supported");

    std::cout << "\n";
  }

  // --- HandleCommand: list_commands unfiltered returns all commands ---
  // Requirements: 10.2
  {
    std::cout << "--- list_commands unfiltered tests ---\n";

    std::string request = R"({"command":"list_commands"})";
    auto response_opt = handler.HandleCommand(request);
    TestCase("list_commands returns a value", response_opt.has_value());
    json response = json::parse(*response_opt);

    TestCase("list_commands status is success",
             response["status"] == "success");

    auto commands = response["result"]["commands"];
    TestCase("list_commands returns 6 commands", commands.size() == 6);

    // Verify all command names are present
    std::set<std::string> names;
    for (const auto& cmd : commands) {
      names.insert(cmd["name"].get<std::string>());
    }
    TestCase("unfiltered contains shutdown", names.count("shutdown") == 1);
    TestCase("unfiltered contains status", names.count("status") == 1);
    TestCase("unfiltered contains get_threads",
             names.count("get_threads") == 1);
    TestCase("unfiltered contains get_stats",
             names.count("get_stats") == 1);
    TestCase("unfiltered contains list_commands",
             names.count("list_commands") == 1);
    TestCase("unfiltered contains get_flow_table",
             names.count("get_flow_table") == 1);

    std::cout << "\n";
  }

  // --- HandleCommand: list_commands filtered by tag returns correct subset ---
  // Requirements: 10.3
  {
    std::cout << "--- list_commands filtered tests ---\n";

    // Filter by "common"
    std::string request =
        R"({"command":"list_commands","params":{"tag":"common"}})";
    auto response_opt = handler.HandleCommand(request);
    TestCase("list_commands(common) returns a value", response_opt.has_value());
    json response = json::parse(*response_opt);

    TestCase("list_commands(common) status is success",
             response["status"] == "success");

    auto commands = response["result"]["commands"];
    TestCase("list_commands(common) returns 5 commands",
             commands.size() == 5);

    // All returned commands should have tag "common"
    bool all_common = true;
    for (const auto& cmd : commands) {
      if (cmd["tag"] != "common") {
        all_common = false;
        break;
      }
    }
    TestCase("all filtered commands have tag common", all_common);

    // Filter by "five_tuple_forwarding"
    std::string request2 =
        R"({"command":"list_commands","params":{"tag":"five_tuple_forwarding"}})";
    auto response_opt2 = handler.HandleCommand(request2);
    json response2 = json::parse(*response_opt2);

    auto commands2 = response2["result"]["commands"];
    TestCase("list_commands(five_tuple_forwarding) returns 1 command",
             commands2.size() == 1);
    TestCase("filtered result is get_flow_table",
             commands2.size() == 1 &&
                 commands2[0]["name"] == "get_flow_table");

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
