#include "cmd_search.h"

#include <lib/xboxkrnl/xboxkrnl.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "command_processor_util.h"
#include "memsearch.h"
#include "vad_tree_util.h"

static const uint32_t kTag = 0x74726E72;  // 'trnr'

#define kMaxResultsPerBucket 512

// Node in a linked list of results.
// Each node holds multiple addresses to amortize the overhead of the links.
typedef struct ResultBucket {
  struct ResultBucket *next;
  uint32_t num_results;
  intptr_t results[kMaxResultsPerBucket];
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

typedef struct SearchResultsContext {
  // Index of the region whose contents are being returned.
  uint32_t current_region;
  const ResultBucket *current_bucket;
} SearchResultsContext;

typedef BOOL (*Comparator)(uint32_t, uint32_t);
typedef uint32_t (*FilterFunc)(ResultBucket *bucket, Comparator comparator);

SearchState search_state = {0};
static const uint32_t kMaxRegions =
    sizeof(search_state.regions) / sizeof(search_state.regions[0]);

static union { SearchResultsContext results_context; } context_store;

static HRESULT StartNewSearch(uint32_t search_term, uint32_t byte_size,
                              char *response, DWORD response_len,
                              CommandContext *ctx);
static HRESULT FilterToValue(uint32_t new_term, char *response,
                             DWORD response_len, CommandContext *ctx);
static HRESULT FilterOp(Comparator comparator, char *response,
                        DWORD response_len, CommandContext *ctx);
static HRESULT HandleSendSearchResults(char *response, DWORD response_len,
                                       CommandContext *ctx);
static HRESULT_API SendSearchResults(CommandContext *ctx, char *response,
                                     DWORD response_len);

static void FreeSearchResults(ResultBucket *head);
static void FreeSearchState(void);

static BOOL CmpGT(uint32_t a, uint32_t b) { return a > b; }
static BOOL CmpGTE(uint32_t a, uint32_t b) { return a >= b; }
static BOOL CmpLTE(uint32_t a, uint32_t b) { return a <= b; }
static BOOL CmpLT(uint32_t a, uint32_t b) { return a < b; }
static BOOL CmpEq(uint32_t a, uint32_t b) { return a == b; }
static BOOL CmpNe(uint32_t a, uint32_t b) { return a != b; }

HRESULT HandleSearch(const char *command, char *response, DWORD response_len,
                     CommandContext *ctx) {
  CommandParameters cp;
  int32_t result = CPParseCommandParameters(command, &cp);
  if (result < 0) {
    return CPPrintError(result, response, response_len);
  }

  uint32_t search_term;
  uint32_t byte_size;
  bool term_found = CPGetUInt32("term", &search_term, &cp);
  bool byte_size_found = CPGetUInt32("bytes", &byte_size, &cp);

  Comparator comparator = NULL;
  if (CPHasKey("gt", &cp)) {
    comparator = CmpGT;
  } else if (CPHasKey("lt", &cp)) {
    comparator = CmpLT;
  } else if (CPHasKey("eq", &cp)) {
    comparator = CmpEq;
  } else if (CPHasKey("ne", &cp)) {
    comparator = CmpNe;
  } else if (CPHasKey("gte", &cp)) {
    comparator = CmpGTE;
  } else if (CPHasKey("lte", &cp)) {
    comparator = CmpLTE;
  }

  uint32_t new_term;
  bool new_value_found = CPGetUInt32("new", &new_term, &cp);

  bool fetch = CPHasKey("fetch", &cp);
  CPDelete(&cp);

  if (term_found) {
    if (!byte_size_found) {
      byte_size = 4;
    } else {
      if (!(byte_size == 1 || byte_size == 2 || byte_size == 4)) {
        sprintf(response, "Invalid `bytes` param %d, must be 1, 2, or 4.",
                byte_size);
        return XBOX_E_FAIL;
      }
    }
    return StartNewSearch(search_term, byte_size, response, response_len, ctx);
  }

  if (fetch) {
    return HandleSendSearchResults(response, response_len, ctx);
  }

  if (new_value_found) {
    return FilterToValue(new_term, response, response_len, ctx);
  }

  if (comparator) {
    return FilterOp(comparator, response, response_len, ctx);
  }

  *response = 0;
  strncat(response,
          "Missing required operation.\n"
          "  term=<value> [byte_size=<1,2,4>] - Start a new search.\n"
          "  new=<value> - Filter results to the given value.\n"
          "  gte|gt|lt|lte|eq|ne - Filter using the given operation.\n"
          "  fetch - Return the current list of results\n",
          response_len);
  strcat(response, command);
  return XBOX_E_FAIL;
}

static BOOL PopulateRegion(SearchRegion *region, uint32_t *result_count) {
  *result_count = 0;
  ResultBucket *results = NULL;
  ResultBucket *last_bucket = NULL;

  const uint8_t *start = (const uint8_t *)region->base;
  const uint8_t *end = (const uint8_t *)region->end;
  while (start) {
    start = memsearch(start, end - start, &search_state.term,
                      search_state.byte_size);
    if (!start) {
      return TRUE;
    }

    // Allocate a new bucket if necessary.
    if (!results) {
      results = (ResultBucket *)DmAllocatePoolWithTag(sizeof(*results), kTag);
      if (!results) {
        return FALSE;
      }

      memset(results, 0, sizeof(*results));

      if (last_bucket) {
        last_bucket->next = results;
      } else {
        region->results = results;
      }
    }

    ++(*result_count);
    results->results[results->num_results++] = (intptr_t)start;
    if (results->num_results == kMaxResultsPerBucket) {
      last_bucket = results;
      results = NULL;
    }

    start += search_state.byte_size;
  }

  return TRUE;
}

static BOOL InitialSearch(uint32_t *result_count) {
  *result_count = 0;

  SearchRegion *region = search_state.regions;
  for (uint32_t i = 0; i < search_state.num_regions; ++i, ++region) {
    FreeSearchResults(region->results);
    region->results = NULL;
    uint32_t region_results = 0;
    if (!PopulateRegion(region, &region_results)) {
      return FALSE;
    }
    *result_count += region_results;
  }

  return TRUE;
}

static HRESULT FilterToValue(uint32_t new_term, char *response,
                             DWORD response_len, CommandContext *ctx) {
  search_state.term = new_term;
  return FilterOp(CmpEq, response, response_len, ctx);
}

static uint32_t FilterBucket8(ResultBucket *bucket, Comparator comparator) {
  uint32_t target = search_state.term & 0xFF;
  uint32_t next_valid = 0;
  for (uint32_t i = 0; i < bucket->num_results; ++i) {
    intptr_t addr = bucket->results[i];
    uint8_t value = *(uint8_t *)addr;
    if (comparator((uint32_t)value, target)) {
      bucket->results[next_valid++] = addr;
    }
  }
  bucket->num_results = next_valid;
  return next_valid;
}

static uint32_t FilterBucket16(ResultBucket *bucket, Comparator comparator) {
  uint32_t target = search_state.term & 0xFFFF;
  uint32_t next_valid = 0;
  for (uint32_t i = 0; i < bucket->num_results; ++i) {
    intptr_t addr = bucket->results[i];
    uint16_t value = *(uint16_t *)addr;
    if (comparator((uint32_t)value, target)) {
      bucket->results[next_valid++] = addr;
    }
  }
  bucket->num_results = next_valid;
  return next_valid;
}

static uint32_t FilterBucket32(ResultBucket *bucket, Comparator comparator) {
  uint32_t next_valid = 0;
  for (uint32_t i = 0; i < bucket->num_results; ++i) {
    intptr_t addr = bucket->results[i];
    uint32_t value = *(uint32_t *)addr;
    if (comparator(value, search_state.term)) {
      bucket->results[next_valid++] = addr;
    }
  }
  bucket->num_results = next_valid;
  return next_valid;
}

static HRESULT FilterOp(Comparator comparator, char *response,
                        DWORD response_len, CommandContext *ctx) {
  SearchRegion *region = search_state.regions;

  FilterFunc filter_func;
  switch (search_state.byte_size) {
    case 1:
      filter_func = FilterBucket8;
      break;
    case 2:
      filter_func = FilterBucket16;
      break;
    case 4:
      filter_func = FilterBucket32;
      break;
    default:
      sprintf(response, "Bad state, byte_size = %d", search_state.byte_size);
      return XBOX_E_FAIL;
  }

  uint32_t total_results = 0;
  for (uint32_t i = 0; i < search_state.num_regions; ++i, ++region) {
    ResultBucket *bucket = region->results;
    while (bucket) {
      total_results += filter_func(bucket, comparator);
      bucket = bucket->next;
    }
  }

  sprintf(response, "Filtered: %d results", total_results);
  return XBOX_S_OK;
}

static HRESULT StartNewSearch(uint32_t search_term, uint32_t byte_size,
                              char *response, DWORD response_len,
                              CommandContext *ctx) {
  FreeSearchState();

  VADRegionInfoSet region_info_set;
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
  VADFreeRegionInfoSet(&region_info_set);

  uint32_t total_results;
  if (!InitialSearch(&total_results)) {
    FreeSearchState();
    *response = 0;
    strncat(response, "Out of memory while performing search.", response_len);
    return XBOX_E_ACCESS_DENIED;
  }

  sprintf(response, "result_count=%d", total_results);
  return XBOX_S_OK;
}

static HRESULT HandleSendSearchResults(char *response, DWORD response_len,
                                       CommandContext *ctx) {
  SearchResultsContext *results_ctx = &context_store.results_context;
  memset(results_ctx, 0, sizeof(*results_ctx));

  ctx->buffer_size = 12 * kMaxResultsPerBucket + 1;
  ctx->buffer = DmAllocatePoolWithTag(ctx->buffer_size, kTag);
  if (!ctx->buffer) {
    sprintf(response, "Out of memory");
    return XBOX_E_ACCESS_DENIED;
  }
  ctx->handler = SendSearchResults;

  return XBOX_S_MULTILINE;
}

static BOOL NextPopulatedBucket(SearchResultsContext *results_ctx) {
  SearchRegion *region = search_state.regions + results_ctx->current_region;
  const ResultBucket *bucket = results_ctx->current_bucket;

  while (!bucket || !bucket->num_results) {
    // Skip over empty buckets.
    if (bucket) {
      bucket = bucket->next;
      if (bucket) {
        continue;
      }

      // Skip to the next region if there are no more buckets in this one.
      ++results_ctx->current_region;
      ++region;
    }

    // Find the next region with a (potentially empty) bucket.
    while (results_ctx->current_region < search_state.num_regions &&
           !region->results) {
      DbgPrint("Seeking populated region: %d\n", results_ctx->current_region);
      ++results_ctx->current_region;
      ++region;
    }
    if (results_ctx->current_region == search_state.num_regions) {
      return FALSE;
    }
    bucket = region->results;
  }
  results_ctx->current_bucket = bucket;

  return TRUE;
}

static HRESULT_API SendSearchResults(CommandContext *ctx, char *response,
                                     DWORD response_len) {
  SearchResultsContext *results_ctx = &context_store.results_context;
  if (results_ctx->current_region >= search_state.num_regions ||
      !NextPopulatedBucket(results_ctx)) {
    DmFreePool(ctx->buffer);
    return XBOX_S_NO_MORE_DATA;
  }

  const ResultBucket *bucket = results_ctx->current_bucket;

  char *buffer = (char *)ctx->buffer;
  for (uint32_t i = 0; i < bucket->num_results; ++i) {
    int len = sprintf(buffer, "0x%08X\n", bucket->results[i]);
    buffer += len;
  }

  // Move to the next result bucket within this region.
  results_ctx->current_bucket = bucket->next;
  // Move to the next region if there are no more buckets.
  if (!results_ctx->current_bucket) {
    ++results_ctx->current_region;
  }

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
