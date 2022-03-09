#include "vad_tree_util.h"

typedef struct ApplyRegionContext {
  VADProcessRegion proc;
  void *user_data;
} ApplyRegionContext;

static NTSTATUS ApplyAllocation(PMMADDRESS_NODE node, VADProcessNode proc,
                                void *user_data) {
  NTSTATUS status;

  if (node->LeftChild) {
    status = ApplyAllocation(node->LeftChild, proc, user_data);
    if (!NT_SUCCESS(status)) {
      return status;
    }
  }

  status = proc(node, user_data);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  if (node->RightChild) {
    status = ApplyAllocation(node->RightChild, proc, user_data);
    if (!NT_SUCCESS(status)) {
      return status;
    }
  }

  return STATUS_SUCCESS;
}

NTSTATUS VADApplyAllocations(VADProcessNode proc, void *user_data) {
  PMMADDRESS_NODE node = *(MmGlobalData.VadRoot);

  RtlEnterCriticalSection(MmGlobalData.AddressSpaceLock);
  NTSTATUS status = ApplyAllocation(node, proc, user_data);
  RtlLeaveCriticalSection(MmGlobalData.AddressSpaceLock);

  return status;
}

static NTSTATUS ApplyRegion(PMMADDRESS_NODE n, void *user_data) {
  ApplyRegionContext *ctx = (ApplyRegionContext *)user_data;

  DWORD base_address = n->StartingVpn << 12;
  DWORD end_address = n->EndingVpn << 12;
  DWORD region_size = end_address - base_address;

  DWORD total_size = 0;
  MEMORY_BASIC_INFORMATION info;

  DWORD vm_base = base_address;
  while (total_size < region_size) {
    NTSTATUS status = NtQueryVirtualMemory((PVOID)vm_base, &info);
    if (!NT_SUCCESS(status)) {
      return status;
    }

    ctx->proc(&info, ctx->user_data);

    total_size += info.RegionSize;
    vm_base += info.RegionSize;
  }

  return STATUS_SUCCESS;
}

NTSTATUS VADApplyRegions(VADProcessRegion proc, void *user_data) {
  ApplyRegionContext context = {proc, user_data};
  return VADApplyAllocations(ApplyRegion, &context);
}

static NTSTATUS WritableRegionProc(MEMORY_BASIC_INFORMATION *info,
                                   void *user_data) {
  DWORD *count = (DWORD *)user_data;
  if ((info->Protect & PAGE_EXECUTE_READWRITE) ||
      (info->Protect & PAGE_READWRITE) || (info->Protect & PAGE_READWRITE)) {
    ++(*count);
  }
  return STATUS_SUCCESS;
}

DWORD VADCountWritableRegions() {
  DWORD ret = 0;
  NTSTATUS status = VADApplyRegions(WritableRegionProc, &ret);
  if (!NT_SUCCESS(status)) {
    return 0xFFFFFFFF;
  }

  return ret;
}