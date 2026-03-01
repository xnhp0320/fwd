#include "processor/processor_registry.h"

#include <algorithm>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace processor {

ProcessorRegistry& ProcessorRegistry::Instance() {
  static ProcessorRegistry instance;
  return instance;
}

void ProcessorRegistry::Register(const std::string& name,
                                 ProcessorEntry entry) {
  entries_[name] = std::move(entry);
}

absl::StatusOr<const ProcessorEntry*> ProcessorRegistry::Lookup(
    const std::string& name) const {
  auto it = entries_.find(name);
  if (it == entries_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Processor '", name, "' not found"));
  }
  return &it->second;
}

std::vector<std::string> ProcessorRegistry::RegisteredNames() const {
  std::vector<std::string> names;
  names.reserve(entries_.size());
  for (const auto& [name, _] : entries_) {
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace processor
