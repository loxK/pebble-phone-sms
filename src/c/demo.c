// SPDX-License-Identifier: AGPL-3.0-or-later
#include "demo.h"

#ifdef DEMO_NOW

#include "demo_photos.h"
#include "protocol.h"

#define MINUTES 60
#define HOURS (60 * MINUTES)
#define DAYS (24 * HOURS)
// Long enough for the watch to show its loading state as it does with a real phone.
#define ANSWER_DELAY_MS 40
#define QUEUE_SIZE 8

typedef struct {
  int32_t id;
  const char *name;
  const char *label;
  const char *number;
  bool favorite;
} Contact;

// Numbers in the 555-01xx range, which belongs to no one.
static const Contact CONTACTS[] = {
  { 1, "Alice Martin", "Mobile", "+1 555 0101", true },
  { 2, "Hugo Bernard", "Mobile", "+1 555 0102", true },
  { 3, "Léa Dubois", "Work", "+1 555 0103", true },
  { 4, "Mum", "Home", "+1 555 0104", true },
  { 5, "Noah Petit", "Mobile", "+1 555 0105", true },
  { 6, "Sarah Lopez", "Mobile", "+1 555 0106", true },
  { 7, "Anna Weber", "Mobile", "+1 555 0107", false },
  { 8, "Bruno Costa", "Mobile", "+1 555 0108", false },
  { 9, "Lucas Moreau", "Work", "+1 555 0109", false },
};

typedef struct {
  uint8_t type;
  int32_t ago;
  int contact;
} Call;

static const Call CALLS[] = {
  { CALL_TYPE_MISSED, 20 * MINUTES, 1 },
  { CALL_TYPE_OUTGOING, 2 * HOURS, 4 },
  { CALL_TYPE_INCOMING, 5 * HOURS, 2 },
  { CALL_TYPE_INCOMING, 1 * DAYS + 3 * HOURS, 6 },
  { CALL_TYPE_REJECTED, 2 * DAYS, 9 },
  { CALL_TYPE_OUTGOING, 3 * DAYS, 3 },
};

typedef struct {
  int32_t id;
  int32_t thread;
  int32_t ago;
  bool mine;
  const char *body;
} Message;

// Most recent first in each conversation, as the phone sends them.
static const Message MESSAGES[] = {
  { 101, 11, 5 * MINUTES, false, "Dinner at 7? I booked the table by the lagoon." },
  { 102, 11, 9 * MINUTES, true, "Sounds great, I'm leaving the office now." },
  { 103, 11, 40 * MINUTES, false, "Are you free tonight?" },
  { 104, 12, 50 * MINUTES, false, "The boat leaves at 9, don't be late!" },
  { 105, 12, 2 * HOURS, true, "Count me in for tomorrow." },
  { 106, 13, 3 * HOURS, false, "Call me when you land." },
  { 107, 14, 1 * DAYS, false, "Thanks for yesterday :)" },
  { 108, 15, 2 * DAYS, true, "The photos are on the drive." },
};

typedef struct {
  int32_t id;
  int contact;
  bool unread;
} Thread;

static const Thread THREADS[] = {
  { 11, 1, true },
  { 12, 2, false },
  { 13, 4, false },
  { 14, 3, false },
  { 15, 6, false },
};

static const char *const REPLIES[] = {
  "On my way", "I'll call you back", "Busy right now", "OK", "Thanks!", "Running late",
};

typedef struct {
  uint8_t cmd;
  uint32_t seq;
  int32_t id;
  int32_t offset;
  int32_t flags;
  char text[48];
  uint32_t budget;
} Request;

static DemoDelivery s_deliver;
static Request s_queue[QUEUE_SIZE];
static uint8_t s_count;
static AppTimer *s_timer;
// Taken from the heap for each answer. The 64 KB platforms have less of it,
// hence smaller pages and photo chunks there.
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
#define ITEMS_SIZE 4096
#define PHOTO_CHUNK 4096
#else
#define ITEMS_SIZE 1280
#define PHOTO_CHUNK 384
#endif
// Room for the tuples around the records or the pixels.
#define BUFFER_SIZE (ITEMS_SIZE + 304)
#define PHOTO_BUFFER_SIZE (PHOTO_CHUNK + 96)
static uint8_t *s_buffer;
static size_t s_buffer_size;
static uint8_t *s_items;
static struct {
  uint8_t state;
  int contact;
  int32_t flags;
} s_call;

static const Contact *contact(int id) {
  for (size_t i = 0; i < ARRAY_LENGTH(CONTACTS); i++) {
    if (CONTACTS[i].id == id) {
      return &CONTACTS[i];
    }
  }
  return NULL;
}

static const Contact *by_number(const char *number) {
  for (size_t i = 0; i < ARRAY_LENGTH(CONTACTS); i++) {
    if (strcmp(CONTACTS[i].number, number) == 0) {
      return &CONTACTS[i];
    }
  }
  return NULL;
}

// A page under construction: records from offset on, as many as the budget holds.
typedef struct {
  int32_t offset;
  uint32_t budget;
  int32_t index;
  size_t length;
  int32_t total;
  // Set by the first record that did not fit: the ones after it wait for the next page.
  bool full;
} Page;

static void write_string(Page *page, const char *text) {
  size_t length = strlen(text) + 1;
  memcpy(s_items + page->length, text, length);
  page->length += length;
}

static void add(Page *page, uint8_t flags, int32_t id, uint32_t time, const char *title,
                const char *detail, const char *extra) {
  int32_t index = page->index++;
  page->total++;
  if (index < page->offset || page->full) {
    return;
  }
  size_t size = 9 + strlen(title) + strlen(detail) + strlen(extra) + 3;
  if ((page->length > 0 && page->length + size > page->budget) || page->length + size > ITEMS_SIZE) {
    page->full = true;
    return;
  }
  s_items[page->length++] = flags;
  memcpy(s_items + page->length, &id, 4);
  memcpy(s_items + page->length + 4, &time, 4);
  page->length += 8;
  write_string(page, title);
  write_string(page, detail);
  write_string(page, extra);
}

// Case-blind on ASCII, which is all the demo names and queries use.
static bool starts_with(const char *name, const char *prefix) {
  for (; *prefix; name++, prefix++) {
    char a = *name >= 'a' && *name <= 'z' ? *name - 32 : *name;
    char b = *prefix >= 'a' && *prefix <= 'z' ? *prefix - 32 : *prefix;
    if (a != b) {
      return false;
    }
  }
  return true;
}

static uint32_t ago(int32_t seconds) {
  return (uint32_t)(DEMO_NOW - seconds);
}

static void list(const Request *request, Page *page) {
  switch (request->cmd) {
    case CMD_FAVORITES:
      for (size_t i = 0; i < ARRAY_LENGTH(CONTACTS); i++) {
        if (CONTACTS[i].favorite) {
          add(page, 0, CONTACTS[i].id, 0, CONTACTS[i].name, CONTACTS[i].label, CONTACTS[i].number);
        }
      }
      break;
    case CMD_LETTERS: {
      static const struct {
        const char *letter;
        const char *names;
        int32_t count;
      } LETTERS[] = {
        { "A", "Alice, Anna", 2 }, { "B", "Bruno", 1 }, { "H", "Hugo", 1 }, { "L", "Léa, Lucas", 2 },
        { "M", "Mum", 1 }, { "N", "Noah", 1 }, { "S", "Sarah", 1 },
      };
      for (size_t i = 0; i < ARRAY_LENGTH(LETTERS); i++) {
        add(page, 0, LETTERS[i].count, 0, LETTERS[i].letter, LETTERS[i].names, "");
      }
      break;
    }
    case CMD_CONTACTS:
    case CMD_SEARCH:
      for (size_t i = 0; i < ARRAY_LENGTH(CONTACTS); i++) {
        bool match = starts_with(CONTACTS[i].name, request->text);
        if (match) {
          add(page, 0, CONTACTS[i].id, 0, CONTACTS[i].name, "", "");
        }
      }
      break;
    case CMD_NUMBERS: {
      const Contact *found = contact(request->id);
      if (found) {
        add(page, 0, 0, 0, found->label, found->number, "");
      }
      break;
    }
    case CMD_CALLS:
      for (size_t i = 0; i < ARRAY_LENGTH(CALLS); i++) {
        const Contact *caller = contact(CALLS[i].contact);
        add(page, CALLS[i].type, 0, ago(CALLS[i].ago), caller->name, caller->number, "");
      }
      break;
    case CMD_THREADS:
      for (size_t i = 0; i < ARRAY_LENGTH(THREADS); i++) {
        const Contact *with = contact(THREADS[i].contact);
        for (size_t m = 0; m < ARRAY_LENGTH(MESSAGES); m++) {
          if (MESSAGES[m].thread == THREADS[i].id) {
            add(page, THREADS[i].unread ? THREAD_FLAG_UNREAD : 0, THREADS[i].id,
                ago(MESSAGES[m].ago), with->name, MESSAGES[m].body, with->number);
            break;
          }
        }
      }
      break;
    case CMD_MESSAGES:
      for (size_t m = 0; m < ARRAY_LENGTH(MESSAGES); m++) {
        if (MESSAGES[m].thread == request->id) {
          add(page, MESSAGES[m].mine ? MESSAGE_FLAG_SENT : 0, MESSAGES[m].id, ago(MESSAGES[m].ago),
              "", MESSAGES[m].body, "");
        }
      }
      break;
    case CMD_REPLIES:
      for (size_t i = 0; i < ARRAY_LENGTH(REPLIES); i++) {
        add(page, 0, 0, 0, "", REPLIES[i], "");
      }
      break;
  }
}

// Bytes per row of a photo in the format the phone sends (see PROTOCOL.md).
static int row_bytes(int size) {
  return PBL_IF_COLOR_ELSE(size, (size + 7) / 8);
}

#define DEMO_ROW_BYTES PBL_IF_COLOR_ELSE(DEMO_PHOTO_SIZE, (DEMO_PHOTO_SIZE + 7) / 8)

// Copies length bytes from offset of a portrait shrunk to size, by nearest
// neighbour: a real phone dithers for the size asked, the demo only has the one
// it was built with. The portraits stay in flash, read a row at a time.
static void read_portrait(int portrait, int size, size_t offset, uint8_t *out, size_t length) {
  ResHandle handle = resource_get_handle(RESOURCE_ID_DEMO_PORTRAITS);
  size_t base = (size_t)portrait * DEMO_ROW_BYTES * DEMO_PHOTO_SIZE;
  uint8_t row[DEMO_ROW_BYTES];
  int loaded = -1;
  for (size_t i = 0; i < length; i++) {
    size_t index = offset + i;
    int source_y = index / row_bytes(size) * DEMO_PHOTO_SIZE / size;
    if (source_y != loaded) {
      resource_load_byte_range(handle, base + source_y * DEMO_ROW_BYTES, row, DEMO_ROW_BYTES);
      loaded = source_y;
    }
#ifdef PBL_COLOR
    out[i] = row[index % size * DEMO_PHOTO_SIZE / size];
#else
    int first = index % row_bytes(size) * 8;
    out[i] = 0;
    for (int bit = 0; bit < 8 && first + bit < size; bit++) {
      int source_x = (first + bit) * DEMO_PHOTO_SIZE / size;
      out[i] |= ((row[source_x / 8] >> (source_x % 8)) & 1) << bit;
    }
#endif
  }
}

static void begin(DictionaryIterator **iter, uint8_t cmd) {
  dict_write_begin(*iter, s_buffer, s_buffer_size);
  dict_write_uint8(*iter, MESSAGE_KEY_Cmd, cmd);
}

static void deliver(DictionaryIterator *iter) {
  uint32_t size = dict_write_end(iter);
  DictionaryIterator reader;
  dict_read_begin_from_buffer(&reader, s_buffer, size);
  s_deliver(&reader, NULL);
}

static void push_call_state(int32_t seq) {
  DictionaryIterator storage;
  DictionaryIterator *iter = &storage;
  begin(&iter, CMD_CALL_STATE);
  if (seq) {
    dict_write_int32(iter, MESSAGE_KEY_Seq, seq);
  }
  const Contact *with = contact(s_call.contact);
  dict_write_uint8(iter, MESSAGE_KEY_State, s_call.state);
  dict_write_cstring(iter, MESSAGE_KEY_Name, with ? with->name : "");
  dict_write_cstring(iter, MESSAGE_KEY_Address, with ? with->number : "");
  dict_write_int32(iter, MESSAGE_KEY_Elapsed, 0);
  dict_write_int32(iter, MESSAGE_KEY_Flags, s_call.flags);
  deliver(iter);
}

static void answer(const Request *request) {
  if (request->cmd == CMD_CALL_STATE) {
    push_call_state(request->seq);
    return;
  }
  DictionaryIterator storage;
  DictionaryIterator *iter = &storage;
  begin(&iter, request->cmd);
  dict_write_int32(iter, MESSAGE_KEY_Seq, request->seq);
  int32_t status = STATUS_OK;

  switch (request->cmd) {
    case CMD_HELLO:
      dict_write_int32(iter, MESSAGE_KEY_Flags, CAP_CONTACTS | CAP_CALL_LOG | CAP_DIAL |
                                                    CAP_READ_SMS | CAP_SEND_SMS | CAP_CALL_CONTROL);
      dict_write_int32(iter, MESSAGE_KEY_Size, 0);
      dict_write_int32(iter, MESSAGE_KEY_BodySize, 0);
      break;
    case CMD_PHOTO: {
      // The first contacts have a portrait, the others show their initials.
      const Contact *owner = request->id ? contact(request->id) : by_number(request->text);
      int portrait = -1;
      for (size_t i = 0; owner && i < ARRAY_LENGTH(DEMO_PHOTO_CONTACTS); i++) {
        if (DEMO_PHOTO_CONTACTS[i] == owner->id) {
          portrait = i;
        }
      }
      int size = request->flags & 0xfff;
      if (portrait < 0 || size <= 0 || size > DEMO_PHOTO_SIZE) {
        status = STATUS_NOT_FOUND;
        break;
      }
      size_t length = (size_t)row_bytes(size) * size;
      size_t offset = request->offset < (int32_t)length ? (size_t)request->offset : length;
      size_t chunk = length - offset;
      chunk = chunk < request->budget - 48 ? chunk : request->budget - 48;
      chunk = chunk < PHOTO_CHUNK ? chunk : PHOTO_CHUNK;
      dict_write_int32(iter, MESSAGE_KEY_Offset, offset);
      dict_write_int32(iter, MESSAGE_KEY_Total, length);
      uint8_t *pixels = malloc(chunk);
      if (!pixels) {
        status = STATUS_FAILED;
        break;
      }
      read_portrait(portrait, size, offset, pixels, chunk);
      dict_write_data(iter, MESSAGE_KEY_Items, pixels, chunk);
      free(pixels);
      break;
    }
    case CMD_MESSAGE_BODY:
      for (size_t m = 0; m < ARRAY_LENGTH(MESSAGES); m++) {
        if (MESSAGES[m].id == request->id) {
          dict_write_cstring(iter, MESSAGE_KEY_Text, MESSAGES[m].body);
        }
      }
      break;
    case CMD_DIAL: {
      const Contact *callee = by_number(request->text);
      s_call.contact = callee ? callee->id : 0;
      s_call.state = CALL_DIALING;
      s_call.flags = ROUTE_EARPIECE << 4;
      dict_write_int32(iter, MESSAGE_KEY_Flags, DIAL_FOLLOWED);
      break;
    }
    case CMD_SEND_SMS:
      break;
    default: {
      Page page = { .offset = request->offset, .budget = request->budget };
      list(request, &page);
      dict_write_int32(iter, MESSAGE_KEY_Offset, request->offset);
      dict_write_int32(iter, MESSAGE_KEY_Total, page.total);
      dict_write_data(iter, MESSAGE_KEY_Items, s_items, page.length);
      if (request->cmd == CMD_SEARCH) {
        dict_write_cstring(iter, MESSAGE_KEY_Text, request->text);
      }
      break;
    }
  }
  dict_write_int32(iter, MESSAGE_KEY_Status, status);
  deliver(iter);

  // What the phone pushes on its own after a call command.
  switch (request->cmd) {
    case CMD_DIAL:
      push_call_state(0);
      break;
    case CMD_HANG_UP:
      s_call.state = CALL_NONE;
      push_call_state(0);
      break;
    case CMD_MUTE:
      s_call.flags ^= CALL_FLAG_MUTED;
      push_call_state(0);
      break;
    case CMD_AUDIO_ROUTE:
      s_call.flags = (s_call.flags & ~0xf0) |
                     ((CALL_FLAG_ROUTE(s_call.flags) == ROUTE_SPEAKER ? ROUTE_EARPIECE : ROUTE_SPEAKER) << 4);
      push_call_state(0);
      break;
  }
}

static void answer_next(void *data) {
  s_timer = NULL;
  if (s_count == 0) {
    return;
  }
  Request request = s_queue[0];
  s_count--;
  memmove(&s_queue[0], &s_queue[1], s_count * sizeof(Request));
  if (s_count > 0) {
    s_timer = app_timer_register(ANSWER_DELAY_MS, answer_next, NULL);
  }
  // A photo chunk comes straight from the portraits, without the list buffer.
  s_buffer_size = request.cmd == CMD_PHOTO ? PHOTO_BUFFER_SIZE : BUFFER_SIZE;
  s_buffer = malloc(s_buffer_size);
  s_items = request.cmd == CMD_PHOTO ? NULL : malloc(ITEMS_SIZE);
  if (s_buffer && (s_items || request.cmd == CMD_PHOTO)) {
    answer(&request);
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "demo: no memory for cmd %d, %d bytes free", (int)request.cmd,
            (int)heap_bytes_free());
  }
  free(s_buffer);
  free(s_items);
  s_buffer = NULL;
  s_items = NULL;
}

void demo_init(DemoDelivery deliver) {
  s_deliver = deliver;
}

void demo_request(uint8_t cmd, uint32_t seq, int32_t id, int32_t offset, int32_t flags,
                  const char *text, uint32_t budget) {
  if (s_count == QUEUE_SIZE) {
    return;
  }
  Request *request = &s_queue[s_count++];
  *request = (Request){
    .cmd = cmd, .seq = seq, .id = id, .offset = offset, .flags = flags,
    .budget = budget < ITEMS_SIZE ? budget : ITEMS_SIZE,
  };
  if (text) {
    strncpy(request->text, text, sizeof(request->text) - 1);
  }
  if (!s_timer) {
    s_timer = app_timer_register(ANSWER_DELAY_MS, answer_next, NULL);
  }
}

#endif
