// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <pebble.h>

typedef struct {
  uint8_t cmd;
  int32_t id;
  int32_t offset;
  int32_t flags;
  const char *text;
  const char *address;
} CommRequest;

// Called once per request: with the reply, or with NULL when the phone did not
// answer in time or the message could not leave the watch.
typedef void (*CommReplyHandler)(DictionaryIterator *reply, void *context);

// Called for CALL_STATE messages, pushed by the phone or answering a request.
typedef void (*CommCallStateHandler)(DictionaryIterator *message);

void comm_init(CommCallStateHandler call_state_handler);
void comm_deinit(void);

// Largest page of records the phone may send back.
uint32_t comm_budget(void);

// Queues a request. Returns its sequence number, or 0 when the queue is full.
// A NULL handler means no reply is expected.
uint32_t comm_send(const CommRequest *request, CommReplyHandler handler, void *context);

// Drops every pending reply handler registered with this context.
void comm_cancel(void *context);

int32_t comm_get_int(DictionaryIterator *iter, uint32_t key, int32_t fallback);
const char *comm_get_string(DictionaryIterator *iter, uint32_t key);
