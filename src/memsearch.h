#ifndef SCRATCHPAD_MEMMEM_MEMMEM_H_
#define SCRATCHPAD_MEMMEM_MEMMEM_H_

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Portable implementation of memmem.
void *memsearch(const void *big, size_t big_len, const void *little,
                size_t little_len);

#ifdef __cplusplus
};  // extern "C"
#endif

#endif  // SCRATCHPAD_MEMMEM_MEMMEM_H_
