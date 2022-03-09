#include "memsearch.h"

#include <stdint.h>

void *memsearch(const void *big, size_t big_len, const void *little,
                size_t little_len) {
  const uint8_t *start = (const uint8_t *)big;
  const uint8_t *end = start + big_len - (little_len - 1);
  size_t range = end - start;
  int first_byte = (*(int *)little) & 0xFF;
  while (start && start <= end) {
    start = memchr(start, first_byte, range);
    if (!start) {
      break;
    }

    if (!memcmp(start, little, little_len)) {
      // Match
      return (void *)start;
    }

    ++start;
    range = end - start;
  }

  return NULL;
}
