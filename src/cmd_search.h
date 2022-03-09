#ifndef TRAINER_DYNDXT_SRC_CMD_SEARCH_H_
#define TRAINER_DYNDXT_SRC_CMD_SEARCH_H_

#include "xbdm.h"

HRESULT HandleSearch(const char *command, char *response, DWORD response_len,
                     CommandContext *ctx);

#endif  // TRAINER_DYNDXT_SRC_CMD_SEARCH_H_
