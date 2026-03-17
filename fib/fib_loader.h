#ifndef FIB_FIB_LOADER_H_
#define FIB_FIB_LOADER_H_

#include <string>
#include "absl/status/status.h"

struct rte_lpm;

namespace fib {

// Load FIB entries from file into the LPM table.
// If rules_loaded is non-null, stores the number of prefixes inserted.
absl::Status LoadFibFile(const std::string& file_path, struct rte_lpm* lpm,
                         uint32_t* rules_loaded = nullptr);

}  // namespace fib

#endif  // FIB_FIB_LOADER_H_
