// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <pebble.h>

typedef struct {
  uint8_t flags;
  int32_t id;
  uint32_t time;
  // The three strings share one allocation owned by title.
  char *title;
  char *detail;
  char *extra;
} Entry;

typedef struct {
  Entry *items;
  uint16_t count;
  // Slots allocated in items, grown as pages arrive, up to capacity.
  uint16_t allocated;
  uint16_t capacity;
  // Size of the whole list on the phone, which may exceed capacity.
  uint16_t total;
} EntryList;

void entries_init(EntryList *list, uint16_t capacity);
void entries_clear(EntryList *list);

// Appends the records of a page blob. Returns false on a malformed blob.
bool entries_append(EntryList *list, const uint8_t *data, uint16_t length);

// Drops the items from index from on whose id an earlier item already has. A list
// that shifted on the phone between two pages sends a row of the previous page again.
void entries_drop_repeats(EntryList *list, uint16_t from);

// True when the phone holds more items than are loaded and there is room.
bool entries_can_grow(const EntryList *list);
