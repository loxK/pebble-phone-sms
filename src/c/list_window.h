// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <pebble.h>

typedef enum {
  LIST_LETTERS,
  LIST_CONTACTS,
  LIST_SEARCH,
  LIST_NUMBERS,
  LIST_CALLS,
  LIST_THREADS,
  LIST_MESSAGES,
} ListKind;

// A list filled page by page from the phone. id and arg narrow the query: the
// contact of LIST_NUMBERS, the thread and its number for LIST_MESSAGES, the
// letter for LIST_CONTACTS, the spoken query for LIST_SEARCH.
void list_window_push(ListKind kind, int32_t id, const char *title, const char *arg);
