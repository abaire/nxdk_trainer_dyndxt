#include "cmd_search.h"

#include <lib/xboxkrnl/xboxkrnl.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "command_processor_util.h"
#include "memsearch.h"
#include "vad_tree_util.h"

// Node in a linked list of results.
// Each node holds multiple addresses to amortize the overhead of the links.
typedef struct ResultBucket {
  struct ResultBucket *next;
  uint32_t num_results;
  intptr_t results[512];
} ResultBucket;

// Describes a virtual memory region and associated search results.
typedef struct SearchRegion {
  intptr_t base;
  intptr_t end;
  ResultBucket *results;
} SearchRegion;

// Describes a search and its results.
typedef struct SearchState {
  uint32_t term;
  uint32_t byte_size;

  uint32_t num_regions;
  SearchRegion regions[128];
} SearchState;

typedef struct NewSearchContext {
  // Index of the region being searched.
  uint32_t current_region;
  // Last matching address within the current region.
  void *last_search_result;
} NewSearchContext;

SearchState search_state = {0};
static const uint32_t kMaxRegions =
    sizeof(search_state.regions) / sizeof(search_state.regions[0]);
VADRegionInfoSet region_info_set = {0};

static union { NewSearchContext new_search_context; } context_store;

static HRESULT_API SendNewSearchResults(CommandContext *ctx, char *response,
                                        DWORD response_len);
static HRESULT StartNewSearch(uint32_t search_term, uint32_t byte_size,
                              char *response, DWORD response_len,
                              CommandContext *ctx);
static void FreeSearchResults(ResultBucket *head);
static void FreeSearchState(void);

HRESULT HandleSearch(const char *command, char *response, DWORD response_len,
                     CommandContext *ctx) {
  CommandParameters cp;
  int32_t result = CPParseCommandParameters(command, &cp);
  if (result < 0) {
    return CPPrintError(result, response, response_len);
  }

  BOOL new_search = FALSE;
  uint32_t search_term;
  uint32_t byte_size;
  if (CPGetUInt32("term", &search_term, &cp)) {
    if (!CPGetUInt32("bytes", &byte_size, &cp)) {
      byte_size = 4;
    } else {
      if (byte_size > 4 || byte_size < 1) {
        sprintf(response, "Invalid `bytes` param %d, must be between 1 and 4.",
                byte_size);
        return XBOX_E_FAIL;
      }
    }
    new_search = TRUE;
  }

  CPDelete(&cp);

  if (new_search) {
    return StartNewSearch(search_term, byte_size, response, response_len, ctx);
  }

  *response = 0;
  strncat(response, "TODO: Implement me", response_len);
  return XBOX_S_OK;
}

static HRESULT StartNewSearch(uint32_t search_term, uint32_t byte_size,
                              char *response, DWORD response_len,
                              CommandContext *ctx) {
  FreeSearchState();

  NTSTATUS status = VADGetWritableRegions(&region_info_set);
  if (!NT_SUCCESS(status)) {
    sprintf(response, "Failed to fetch writable regions. 0x%X", status);
    return XBOX_E_FAIL;
  }

  if (region_info_set.num_entries >= kMaxRegions) {
    sprintf(response, "Too many memory regions to search. %d",
            region_info_set.num_entries);
    return XBOX_E_FAIL;
  }

  search_state.term = search_term;
  search_state.byte_size = byte_size;

  MEMORY_BASIC_INFORMATION *info = region_info_set.entries;
  SearchRegion *region = search_state.regions;
  for (uint32_t i = 0; i < region_info_set.num_entries; ++i, ++info, ++region) {
    region->base = (intptr_t)info->BaseAddress;
    region->end = region->base + info->RegionSize;
    region->results = NULL;
  }
  search_state.num_regions = region_info_set.num_entries;

  memset(&context_store.new_search_context, 0,
         sizeof(context_store.new_search_context));
  ctx->handler = SendNewSearchResults;

  return XBOX_S_MULTILINE;
}

static HRESULT_API SendNewSearchResults(CommandContext *ctx, char *response,
                                        DWORD response_len) {
  NewSearchContext *search_ctx = &context_store.new_search_context;
  if (search_ctx->current_region >= search_state.num_regions) {
    return XBOX_S_NO_MORE_DATA;
  }

  SearchRegion *region = search_state.regions + search_ctx->current_region;
  if (!search_ctx->last_search_result) {
    search_ctx->last_search_result = (void *)region->base;
  }

  const uint8_t *start = (const uint8_t *)search_ctx->last_search_result;
  const uint8_t *end =
      (const uint8_t *)region->end - (search_state.byte_size - 1);
  size_t range = end - start;
  while (start && start <= end) {
    start = memchr(start, search_state.term, range);
    if (!start) {
      break;
    }

    if (!memcmp(start, &search_state.term, search_state.byte_size)) {
      // Match
      start += search_state.byte_size;
    }

    range = end - start;
  }
  //  const uint8_t *byte_str = (const uint8_t *)&search_state.term;
  //  for (; start <= last; ++start) {
  //    if (*start != *byte_str) {
  //      continue;
  //    }
  //
  //    if (!memcmp())
  //  }
  //  void *result = memmem(search_ctx->last_search_result, region->end -
  //  search_ctx->last_search_result, &search_state.term,
  //  search_state.byte_size)

  uint32_t current_value = (uint32_t)ctx->user_data;
  if (current_value == region_info_set.num_entries) {
    DmFreePool(region_info_set.entries);
    region_info_set.num_entries = 0;
    region_info_set.entries = NULL;
    response[0] = 0;
    strncat(response, "Memory info", response_len);
    return XBOX_S_NO_MORE_DATA;
  }

  ctx->user_data = (void *)(current_value + 1);
  char *buffer = (char *)ctx->buffer;

  MEMORY_BASIC_INFORMATION *info = region_info_set.entries + current_value;
  sprintf(buffer, "addr=0x%X sz=%d p=0x%X st=0x%X t=0x%X", info->BaseAddress,
          info->RegionSize, info->Protect, info->State, info->Type);

  return XBOX_S_OK;
}

static void FreeSearchResults(ResultBucket *head) {
  while (head) {
    ResultBucket *to_free = head;
    head = head->next;
    DmFreePool(to_free);
  }
}

static void FreeSearchState(void) {
  for (uint32_t i = 0; i < search_state.num_regions; ++i) {
    FreeSearchResults(search_state.regions[i].results);
    search_state.regions[i].results = NULL;
  }
  search_state.num_regions = 0;
}
