#include <stdio.h>
#include <string.h>
#include <windows.h>
//#include <xboxkrnl/xboxkrnl.h>

#include "command_processor_util.h"
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

// Send multiline text response to the client.
static HRESULT HandleSearch(const char *command, char *response,
                            DWORD response_len, CommandContext *ctx);
static HRESULT_API SendSearchData(CommandContext *ctx, char *response,
                                  DWORD response_len);

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

// Send multiline text response to the client.
static HRESULT HandleSearch(const char *command, char *response,
                            DWORD response_len, CommandContext *ctx) {
  PMMADDRESS_NODE node = *MmGlobalData.VadRoot;
  ctx->user_data = node;
  ctx->handler = SendSearchData;

  *response = 0;
  strncat(response, "Memory info", response_len);
  return XBOX_S_MULTILINE;
}

static HRESULT_API SendSearchData(CommandContext *ctx, char *response,
                                  DWORD response_len) {
  uint32_t current_value = (uint32_t)ctx->user_data;
  --current_value;

  if (!current_value) {
    response[0] = 0;
    strncat(response, "Memory info", response_len);
    return XBOX_S_NO_MORE_DATA;
  }

  ctx->user_data = (void *)current_value;

  char *ret = (char *)ctx->buffer;
  //  PMMGLOBALDATA p = &MmGlobalData;
  PMMADDRESS_NODE node = *MmGlobalData.VadRoot;

  EnterCriticalSection(MmGlobalData.AddressSpaceLock);

  //  sprintf(ret,
  //          "0x%X 0x%X RPR: 0x%X SPR: 0x%X AP: %d APbU: 0x%X ADL: 0x%X VadRoot
  //          0x%x VadHint 0x%x VadFreeHint 0x%x", &MmGlobalData, p,
  //          p->RetailPfnRegion,
  //          p->SystemPteRange,
  //          p->AvailablePages,
  //          p->AllocatedPagesByUsage,
  //          p->AddressSpaceLock,
  //          p->VadRoot,
  //          p->VadHint,
  //          p->VadFreeHint
  //          );
  sprintf(ret, "0x%X - 0x%X  L: 0x%X R: 0x%X", node->StartingVpn << 12,
          node->EndingVpn << 12, node->LeftChild, node->RightChild);

  LeaveCriticalSection(MmGlobalData.AddressSpaceLock);

  /*
> walkmem
Base Address: 0x00010000 size: 4096 protection: 0x4
Base Address: 0x00011000 size: 880640 protection: 0x2
Base Address: 0x000e8000 size: 843776 protection: 0x4
Base Address: 0x001b6000 size: 86016 protection: 0x2
Base Address: 0x001d0000 size: 24576 protection: 0x4
Base Address: 0x001d6000 size: 1028096 protection: 0x20004
Base Address: 0x002f0000 size: 462848 protection: 0x20004
Base Address: 0x003f0000 size: 4096 protection: 0x20004
Base Address: 0x004d2000 size: 4096 protection: 0x20004
Base Address: 0x80000000 size: 4096 protection: 0x20004
Base Address: 0x8000a000 size: 24576 protection: 0x20004
Base Address: 0x80010000 size: 221184 protection: 0x2
Base Address: 0x80046000 size: 110592 protection: 0x4
Base Address: 0x83a8c000 size: 2162688 protection: 0x20404
Base Address: 0x83c9c000 size: 4096 protection: 0x20004
Base Address: 0x83c9e000 size: 8192 protection: 0x20404
Base Address: 0x83ca0000 size: 77824 protection: 0x20004
Base Address: 0x83cb3000 size: 4096 protection: 0x20404
Base Address: 0x83cb4000 size: 8192 protection: 0x20004
Base Address: 0x83cb6000 size: 8192 protection: 0x20404
Base Address: 0x83cb8000 size: 53248 protection: 0x20004
Base Address: 0x83cc5000 size: 12288 protection: 0x20404
Base Address: 0x83cc8000 size: 4096 protection: 0x20004
Base Address: 0x83cc9000 size: 12288 protection: 0x20404
Base Address: 0x83ccc000 size: 4096 protection: 0x20004
Base Address: 0x83ccd000 size: 12288 protection: 0x20404
Base Address: 0x83cd0000 size: 8192 protection: 0x20004
Base Address: 0x83cd2000 size: 8192 protection: 0x20404
Base Address: 0x83cd4000 size: 8192 protection: 0x20004
Base Address: 0x83cd6000 size: 8192 protection: 0x20404
Base Address: 0x83cd8000 size: 86016 protection: 0x20004
Base Address: 0x83ced000 size: 4096 protection: 0x20404
Base Address: 0x83cee000 size: 90112 protection: 0x20004
Base Address: 0x83d04000 size: 2457600 protection: 0x20404
Base Address: 0x83f5c000 size: 4096 protection: 0x20004
Base Address: 0x83f5d000 size: 524288 protection: 0x20404
Base Address: 0x83fdd000 size: 12288 protection: 0x20004
Base Address: 0x83feb000 size: 20480 protection: 0x20204
Base Address: 0x83ff0000 size: 131072 protection: 0x20004
Base Address: 0x87feb000 size: 20480 protection: 0x20204
Base Address: 0xb0000000 size: 69632 protection: 0x20004
Base Address: 0xb0011000 size: 315392 protection: 0x2
Base Address: 0xb005e000 size: 57344 protection: 0x4
Base Address: 0xb006c000 size: 24576 protection: 0x2
Base Address: 0xb0072000 size: 4096 protection: 0x20002
Base Address: 0xb0073000 size: 139264 protection: 0x20004
Base Address: 0xb0096000 size: 8192 protection: 0x20004
Base Address: 0xb0098000 size: 16384 protection: 0x20002
Base Address: 0xb009c000 size: 61440 protection: 0x20004
Base Address: 0xb00ac000 size: 12288 protection: 0x20004
Base Address: 0xb00b0000 size: 8192 protection: 0x20004
Base Address: 0xb00b2000 size: 98304 protection: 0x4
Base Address: 0xb00ca000 size: 12288 protection: 0x20004
Base Address: 0xb00ce000 size: 12288 protection: 0x20004
Base Address: 0xb00d2000 size: 8192 protection: 0x20004
Base Address: 0xb00d5000 size: 122880 protection: 0x20004
Base Address: 0xb0105000 size: 90112 protection: 0x20004
Base Address: 0xc0000000 size: 8192 protection: 0x20004
Base Address: 0xd0008000 size: 4096 protection: 0x20004
Base Address: 0xd0009000 size: 65536 protection: 0x20002
Base Address: 0xd0019000 size: 12288 protection: 0x20004
Base Address: 0xd001e000 size: 24576 protection: 0x20004
Base Address: 0xd0026000 size: 57344 protection: 0x20004
Base Address: 0xd0035000 size: 65536 protection: 0x20004
Base Address: 0xd004c000 size: 53248 protection: 0x20004
Base Address: 0xd0067000 size: 4096 protection: 0x20004
Base Address: 0xd0070000 size: 4096 protection: 0x20004
Base Address: 0xd0071000 size: 2031616 protection: 0x20002
Base Address: 0xd0262000 size: 65536 protection: 0x20004
   */

  return XBOX_S_OK;
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
