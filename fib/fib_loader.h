#ifndef FIB_FIB_LOADER_H_
#define FIB_FIB_LOADER_H_

#include <string>
#include "absl/status/status.h"

struct rte_lpm;

namespace fib {

// Load FIB entries from file into the LPM table.
// Current implementation is a stub that returns OkStatus without
// inserting any prefixes. Future implementation will parse the file
// and call rte_lpm_add for each prefix entry.
absl::Status LoadFibFile(const std::string& file_path, struct rte_lpm* lpm);

}  // namespace fib

#endif  // FIB_FIB_LOADER_H_
