// SPDX-License-Identifier: AGPL-3.0-or-later
#include "entries.h"

void entries_init(EntryList *list, uint16_t capacity) {
  *list = (EntryList){ .capacity = capacity };
}

void entries_clear(EntryList *list) {
  for (uint16_t i = 0; i < list->count; i++) {
    free(list->items[i].title);
  }
  free(list->items);
  list->items = NULL;
  list->allocated = 0;
  list->count = 0;
  list->total = 0;
}

#define GROWTH 8

static bool make_room(EntryList *list) {
  if (list->count < list->allocated) {
    return true;
  }
  uint16_t allocated = list->allocated + GROWTH;
  if (allocated > list->capacity) {
    allocated = list->capacity;
  }
  Entry *items = realloc(list->items, allocated * sizeof(Entry));
  if (!items) {
    return false;
  }
  list->items = items;
  list->allocated = allocated;
  return true;
}

static uint32_t read_u32(const uint8_t *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

// Length of the NUL terminated string at data, or -1 when it overruns end.
static int string_length(const uint8_t *data, const uint8_t *end) {
  const uint8_t *cursor = data;
  while (cursor < end && *cursor) {
    cursor++;
  }
  return cursor < end ? cursor - data : -1;
}

bool entries_append(EntryList *list, const uint8_t *data, uint16_t length) {
  const uint8_t *cursor = data;
  const uint8_t *end = data + length;
  while (cursor < end && list->count < list->capacity) {
    if (end - cursor < 9) {
      return false;
    }
    uint8_t flags = cursor[0];
    int32_t id = (int32_t)read_u32(cursor + 1);
    uint32_t time = read_u32(cursor + 5);
    cursor += 9;

    const uint8_t *strings = cursor;
    int lengths[3];
    for (int i = 0; i < 3; i++) {
      lengths[i] = string_length(cursor, end);
      if (lengths[i] < 0) {
        return false;
      }
      cursor += lengths[i] + 1;
    }

    if (!make_room(list)) {
      return false;
    }
    size_t size = cursor - strings;
    char *block = malloc(size);
    if (!block) {
      return false;
    }
    memcpy(block, strings, size);
    list->items[list->count++] = (Entry){
      .flags = flags,
      .id = id,
      .time = time,
      .title = block,
      .detail = block + lengths[0] + 1,
      .extra = block + lengths[0] + 1 + lengths[1] + 1,
    };
  }
  return true;
}

void entries_drop_repeats(EntryList *list, uint16_t from) {
  uint16_t kept = from;
  for (uint16_t i = from; i < list->count; i++) {
    bool repeat = false;
    for (uint16_t j = 0; j < from && !repeat; j++) {
      repeat = list->items[j].id == list->items[i].id;
    }
    if (repeat) {
      free(list->items[i].title);
    } else {
      list->items[kept++] = list->items[i];
    }
  }
  list->count = kept;
}

bool entries_can_grow(const EntryList *list) {
  return list->count < list->total && list->count < list->capacity;
}
