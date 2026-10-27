// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "entries.h"

// The messages of a conversation, as loaded by the list that opened the reader.
typedef struct {
  // The message at index, or NULL past the loaded ones.
  const Entry *(*entry_at)(void *context, int16_t index);
  // The reader moved to index: the list follows and loads ahead.
  void (*moved)(void *context, int16_t index);
  void *context;
} MessageSource;

// Shows one SMS; Down past its end and Up above its top move to the next and
// previous ones, Select answers the conversation thread at address. A body the
// phone cut short is fetched in full.
void message_window_push(const MessageSource *source, int16_t index, int32_t thread,
                         const char *thread_name, const char *address);
