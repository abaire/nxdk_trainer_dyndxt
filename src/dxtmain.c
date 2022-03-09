#include <stdio.h>
#include <string.h>
#include <windows.h>
//#include <xboxkrnl/xboxkrnl.h>

#include "cmd_search.h"
#include "nxdk_dxt_dll_main.h"
#include "xbdm.h"

static const char kHandlerName[] = "trainer";
static const uint32_t kTag = 0x74726E72;  // 'trnr'

typedef struct CommandTableEntry {
  const char *command;
  HRESULT (*processor)(const char *, char *, DWORD, CommandContext *);
} CommandTableEntry;

static HRESULT_API ProcessCommand(const char *command, char *response,
                                  DWORD response_len, CommandContext *ctx);

// Enumerates the command table.
static HRESULT HandleHello(const char *command, char *response,
                           DWORD response_len, CommandContext *ctx);
static HRESULT_API SendHelloData(CommandContext *ctx, char *response,
                                 DWORD response_len);

static const CommandTableEntry kCommandTable[] = {
    {"hello", HandleHello},
    {"search", HandleSearch},
};
static const uint32_t kCommandTableNumEntries =
    sizeof(kCommandTable) / sizeof(kCommandTable[0]);

HRESULT DXTMain(void) {
  return DmRegisterCommandProcessor(kHandlerName, ProcessCommand);
}

static HRESULT_API ProcessCommand(const char *command, char *response,
                                  DWORD response_len, CommandContext *ctx) {
  const char *subcommand = command + sizeof(kHandlerName);

  const CommandTableEntry *entry = kCommandTable;
  for (uint32_t i = 0; i < kCommandTableNumEntries; ++i, ++entry) {
    uint32_t len = strlen(entry->command);
    if (!strncmp(subcommand, entry->command, len)) {
      return entry->processor(subcommand + len, response, response_len, ctx);
    }
  }

  return XBOX_E_UNKNOWN_COMMAND;
}

static HRESULT HandleHello(const char *command, char *response,
                           DWORD response_len, CommandContext *ctx) {
  ctx->user_data = 0;
  ctx->handler = SendHelloData;
  *response = 0;
  strncat(response, "Available commands:", response_len);
  return XBOX_S_MULTILINE;
}

static HRESULT_API SendHelloData(CommandContext *ctx, char *response,
                                 DWORD response_len) {
  uint32_t current_index = (uint32_t)ctx->user_data++;

  if (current_index >= kCommandTableNumEntries) {
    return XBOX_S_NO_MORE_DATA;
  }

  const CommandTableEntry *entry = &kCommandTable[current_index];
  uint32_t command_len = strlen(entry->command) + 1;
  if (command_len > ctx->buffer_size) {
    response[0] = 0;
    strncat(response, "Response buffer is too small", response_len);
    return XBOX_E_ACCESS_DENIED;
  }

  memcpy(ctx->buffer, entry->command, command_len);
  return XBOX_S_OK;
}
