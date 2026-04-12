#ifndef VM_LOCATION_VM_LOCATION_TABLE_H_
#define VM_LOCATION_VM_LOCATION_TABLE_H_

#include "indirect_table/indirect_table.h"
#include "vm_location/tunnel_info.h"
#include "vm_location/vm_location_key.h"

namespace vm_location {

using VmLocationTable = indirect_table::IndirectTable<
    VmLocationKey, TunnelInfo,
    VmLocationKeyHash, VmLocationKeyEqual,
    TunnelInfoHash, TunnelInfoEqual>;

}  // namespace vm_location

#endif  // VM_LOCATION_VM_LOCATION_TABLE_H_
