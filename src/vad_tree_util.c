#include "vad_tree_util.h"

#include "xbdm.h"

typedef struct ApplyRegionContext {
  VADProcessRegion proc;
  void *user_data;
} ApplyRegionContext;

typedef struct GetWritableRegionsContext {
  VADRegionInfoSet *info_set;
  DWORD next_index;
} GetWritableRegionsContext;

static const DWORD kTag = 0x74726E72;  // 'trnr'

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
  PMMGLOBALDATA mm_global_data = (PMMGLOBALDATA)&MmGlobalData;
  PMMADDRESS_NODE node = *mm_global_data->VadRoot;

  RtlEnterCriticalSection(mm_global_data->AddressSpaceLock);
  NTSTATUS status = ApplyAllocation(node, proc, user_data);
  RtlLeaveCriticalSection(mm_global_data->AddressSpaceLock);

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

static NTSTATUS WritableRegionCountProc(MEMORY_BASIC_INFORMATION *info,
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
  NTSTATUS status = VADApplyRegions(WritableRegionCountProc, &ret);
  if (!NT_SUCCESS(status)) {
    return 0xFFFFFFFF;
  }

  return ret;
}

static NTSTATUS WritableRegionInfoProc(MEMORY_BASIC_INFORMATION *info,
                                       void *user_data) {
  GetWritableRegionsContext *ctx = (GetWritableRegionsContext *)user_data;
  if ((info->Protect & PAGE_EXECUTE_READWRITE) ||
      (info->Protect & PAGE_READWRITE)) {
    ctx->info_set->entries[ctx->next_index++] = *info;
  }
  return STATUS_SUCCESS;
}

NTSTATUS VADGetWritableRegions(VADRegionInfoSet *ret) {
  ret->num_entries = VADCountWritableRegions();
  if (ret->num_entries == 0xFFFFFFFF) {
    return STATUS_UNSUCCESSFUL;
  }

  ret->entries =
      DmAllocatePoolWithTag(sizeof(ret->entries[0]) * ret->num_entries, kTag);
  if (!ret->entries) {
    return STATUS_NO_MEMORY;
  }

  GetWritableRegionsContext ctx = {ret, 0};
  return VADApplyRegions(WritableRegionInfoProc, &ctx);
}

void VADFreeRegionInfoSet(VADRegionInfoSet *tofree) {
  if (!tofree->entries) {
    return;
  }

  DmFreePool(tofree->entries);
  tofree->entries = NULL;
  tofree->num_entries = 0;
}
