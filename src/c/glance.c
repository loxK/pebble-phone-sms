// SPDX-License-Identifier: AGPL-3.0-or-later
#include "glance.h"
#include "entries.h"
#include "l10n.h"
#include "protocol.h"
#include "ui.h"

// ATTRIBUTE_APP_GLANCE_SUBTITLE_MAX_LEN of the firmware, which the SDK does not export.
#define LINE_SIZE (150 + 1)
// The launcher drops the event a day after it happened.
#define LIFETIME_S (24 * 60 * 60)
#define SEPARATOR " \xc2\xb7 "

static char s_line[LINE_SIZE];
static uint32_t s_icon;
static time_t s_expiration;
// Set once the phone has told what to show, even if that is nothing.
static bool s_known;

typedef struct {
  char *cursor;
  char *end;
} Writer;

// Copies whole characters of text while they fit. Phone data goes through
// escaped, since a brace would open a template expression. With whole set,
// text is written entirely or not at all.
static void put(Writer *writer, const char *text, bool escape, bool whole) {
  char *start = writer->cursor;
  const char *source = text;
  while (*source) {
    size_t length = ui_utf8_length(source);
    bool special = escape && (*source == '{' || *source == '\\');
    if (writer->cursor + length + special >= writer->end) {
      if (whole) {
        writer->cursor = start;
      }
      // Nothing written after a cut: the line would skip a part.
      writer->end = writer->cursor;
      break;
    }
    if (special) {
      *writer->cursor++ = '\\';
    }
    memcpy(writer->cursor, source, length);
    writer->cursor += length;
    source += length;
  }
  *writer->cursor = '\0';
}

static void build(const Entry *event) {
  Writer writer = { .cursor = s_line, .end = s_line + LINE_SIZE };
  s_line[0] = '\0';
  s_expiration = event->time + LIFETIME_S;
  if (s_expiration <= time(NULL)) {
    return;
  }

  // The firmware renders the age and keeps it current in the launcher.
  char age[96];
  snprintf(age, sizeof(age), "{time_since(%lu)|format(%s)}", (unsigned long)event->time,
           tr(S_GLANCE_AGE));

  if (event->flags == GLANCE_MISSED_CALL) {
    s_icon = PUBLISHED_ID_GLANCE_MISSED;
    put(&writer, tr(S_GLANCE_MISSED), false, true);
    put(&writer, event->title, true, false);
    put(&writer, SEPARATOR, false, true);
    put(&writer, age, false, true);
  } else {
    s_icon = PUBLISHED_ID_GLANCE_SMS;
    put(&writer, event->title, true, false);
    put(&writer, SEPARATOR, false, true);
    put(&writer, age, false, true);
    put(&writer, SEPARATOR, false, true);
    put(&writer, event->detail, true, false);
  }
}

void glance_read(DictionaryIterator *hello_reply) {
  s_known = true;
  s_line[0] = '\0';
  Tuple *items = dict_find(hello_reply, MESSAGE_KEY_Items);
  if (!items || items->type != TUPLE_BYTE_ARRAY) {
    return;
  }
  EntryList list;
  entries_init(&list, 1);
  if (entries_append(&list, items->value->data, items->length) && list.count == 1) {
    build(&list.items[0]);
  }
  entries_clear(&list);
}

static void add_slice(AppGlanceReloadSession *session, size_t limit, void *context) {
  AppGlanceResult result = app_glance_add_slice(session, (AppGlanceSlice){
    .layout = {
      .icon = s_icon,
      .subtitle_template_string = s_line,
    },
    .expiration_time = s_expiration,
  });
  if (result != APP_GLANCE_RESULT_SUCCESS) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "glance slice refused: %d", (int)result);
  }
}

void glance_write(void) {
  if (!s_known) {
    return;
  }
  bool current = s_line[0] && s_expiration > time(NULL);
  app_glance_reload(current ? add_slice : NULL, NULL);
}
