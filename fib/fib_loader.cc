#include "fib/fib_loader.h"

#include <arpa/inet.h>
#include <rte_lpm.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace fib {

absl::Status LoadFibFile(const std::string& file_path, struct rte_lpm* lpm) {
  if (lpm == nullptr) {
    return absl::InvalidArgumentError("lpm pointer is null");
  }

  std::ifstream file(file_path);
  if (!file.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("cannot open FIB file: ", file_path));
  }

  std::string ip_line;
  std::string cidr_line;
  uint32_t count = 0;
  uint32_t line_num = 0;

  while (std::getline(file, ip_line)) {
    ++line_num;
    // Skip empty lines.
    if (ip_line.empty()) continue;

    if (!std::getline(file, cidr_line)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "FIB file line ", line_num, ": missing CIDR length after IP '",
          ip_line, "'"));
    }
    ++line_num;

    // Parse IP address.
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_line.c_str(), &addr) != 1) {
      return absl::InvalidArgumentError(absl::StrCat(
          "FIB file line ", line_num - 1, ": invalid IPv4 address '",
          ip_line, "'"));
    }

    // Parse CIDR prefix length.
    int cidr = 0;
    try {
      cidr = std::stoi(cidr_line);
    } catch (...) {
      return absl::InvalidArgumentError(absl::StrCat(
          "FIB file line ", line_num, ": invalid CIDR length '",
          cidr_line, "'"));
    }
    if (cidr < 0 || cidr > 32) {
      return absl::InvalidArgumentError(absl::StrCat(
          "FIB file line ", line_num, ": CIDR length out of range: ", cidr));
    }

    // rte_lpm_add expects the IP in host byte order.
    uint32_t ip_host = ntohl(addr.s_addr);

    // next_hop is unused for now — set to 0.
    int ret = rte_lpm_add(lpm, ip_host, static_cast<uint8_t>(cidr), 0);
    if (ret < 0) {
      return absl::InternalError(absl::StrCat(
          "rte_lpm_add failed for ", ip_line, "/", cidr, ": ret=", ret));
    }
    ++count;
  }

  std::cout << "FIB loaded: " << count << " prefixes from " << file_path
            << "\n";
  return absl::OkStatus();
}

}  // namespace fib
