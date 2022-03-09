#ifndef TRAINER_DYNDXT_VAD_TREE_UTIL_H
#define TRAINER_DYNDXT_VAD_TREE_UTIL_H

#include <xboxkrnl/xboxkrnl.h>

typedef NTSTATUS (*VADProcessNode)(PMMADDRESS_NODE node, void *user_data);
typedef NTSTATUS (*VADProcessRegion)(MEMORY_BASIC_INFORMATION *info,
                                     void *user_data);

// Recursively apply the given function to each node in the VAD tree.
//
// This method locks the virtual allocation directory, so it is important that
// no allocations are done within the callback methods.
NTSTATUS VADApplyAllocations(VADProcessNode proc, void *user_data);

// Recursively apply the given function to each region within each allocation in
// the VAD tree.
//
// This method locks the virtual allocation directory, so it is important that
// no allocations are done within the callback methods.
NTSTATUS VADApplyRegions(VADProcessRegion proc, void *user_data);

DWORD VADCountWritableRegions();

#endif  // TRAINER_DYNDXT_VAD_TREE_UTIL_H
