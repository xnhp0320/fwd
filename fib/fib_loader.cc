#include "fib/fib_loader.h"

#include "absl/status/status.h"

namespace fib {

absl::Status LoadFibFile(const std::string& /*file_path*/,
                         struct rte_lpm* /*lpm*/) {
  return absl::OkStatus();
}

}  // namespace fib
