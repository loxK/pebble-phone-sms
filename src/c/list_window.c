// SPDX-License-Identifier: AGPL-3.0-or-later
#include "list_window.h"
#include "actions_window.h"
#include "comm.h"
#include "compose.h"
#include "entries.h"
#include "icons.h"
#include "l10n.h"
#include "message_window.h"
#include "protocol.h"
#include "reply.h"
#include "ui.h"

#define CAPACITY 150
#define MESSAGE_CAPACITY 50

// Pages are fetched once the selection gets this close to the last loaded row.
#define PREFETCH_ROWS 3

typedef struct {
  Window *window;
  MenuLayer *menu;
  ListKind kind;
  int32_t id;
  char title[48];
  char arg[48];
  EntryList entries;
  bool loading;
  int32_t status;
  // compose_sent_count() when the entries were loaded.
  uint16_t sent;
  // Conversations and messages reload behind the rows on screen when the list
  // shows again: the first page lands here, then replaces entries at once.
  EntryList fresh;
  bool refreshing;
  // A text went out meanwhile: the reload lands on the newest row.
  bool refresh_to_top;
  bool shown;
  // Fingerprint of the first page on screen, as the phone computed it.
  int32_t stamp;
} ListView;

static const uint8_t COMMANDS[] = {
  [LIST_LETTERS] = CMD_LETTERS,
  [LIST_CONTACTS] = CMD_CONTACTS,
  [LIST_SEARCH] = CMD_SEARCH,
  [LIST_NUMBERS] = CMD_NUMBERS,
  [LIST_CALLS] = CMD_CALLS,
  [LIST_THREADS] = CMD_THREADS,
  [LIST_MESSAGES] = CMD_MESSAGES,
};

static void page_reply(DictionaryIterator *reply, void *context);

// The alphabet starts with a voice search row, a conversation with a Reply row.
static uint16_t prefix_rows(const ListView *view) {
  if (view->kind == LIST_LETTERS) {
    return compose_available() ? 1 : 0;
  }
  return view->kind == LIST_MESSAGES && view->arg[0] ? 1 : 0;
}

static bool has_status_row(const ListView *view) {
  return view->entries.count == 0 || view->loading || entries_can_grow(&view->entries) ||
         view->status != STATUS_OK;
}

static void request_page(ListView *view) {
  // Further pages wait for the reload: their offsets count rows it replaces.
  if (view->loading || view->refreshing) {
    return;
  }
  CommRequest request = {
    .cmd = COMMANDS[view->kind],
    .id = view->id,
    .offset = view->entries.count,
    .text = view->kind == LIST_CONTACTS || view->kind == LIST_SEARCH ? view->arg : NULL,
  };
  view->loading = comm_send(&request, page_reply, view) != 0;
  view->status = view->loading ? STATUS_OK : STATUS_FAILED;
}

static void page_reply(DictionaryIterator *reply, void *context) {
  ListView *view = context;
  view->loading = false;
  if (!reply) {
    view->status = STATUS_UNREACHABLE;
  } else {
    view->status = comm_get_int(reply, MESSAGE_KEY_Status, STATUS_OK);
    if (view->status == STATUS_OK) {
      if (comm_get_int(reply, MESSAGE_KEY_Offset, -1) == 0) {
        view->stamp = comm_get_int(reply, MESSAGE_KEY_Flags, 0);
      }
      view->entries.total = comm_get_int(reply, MESSAGE_KEY_Total, 0);
      Tuple *items = dict_find(reply, MESSAGE_KEY_Items);
      uint16_t before = view->entries.count;
      if (items && !entries_append(&view->entries, items->value->data, items->length)) {
        view->status = STATUS_FAILED;
      }
      // Conversations and messages carry unique ids; other lists may repeat theirs.
      if (view->kind == LIST_THREADS || view->kind == LIST_MESSAGES) {
        entries_drop_repeats(&view->entries, before);
      }
    }
    // The phone sends the query back in Latin letters the watch font can show.
    const char *query = comm_get_string(reply, MESSAGE_KEY_Text);
    if (view->kind == LIST_SEARCH && query && query[0]) {
      strncpy(view->title, query, sizeof(view->title) - 1);
    }
  }

  // A contact with a single number goes straight to its actions.
  if (view->kind == LIST_NUMBERS && view->status == STATUS_OK && view->entries.count == 1 &&
      view->entries.total == 1) {
    actions_window_push(view->title, view->entries.items[0].detail);
    window_stack_remove(view->window, false);
    return;
  }
  if (view->menu) {
    menu_layer_reload_data(view->menu);
  }
}

static const char *status_text(const ListView *view) {
  if (view->loading) {
    return tr(S_LOADING);
  }
  switch (view->status) {
    case STATUS_OK:
      if (view->entries.count > 0) {
        return tr(S_MORE);
      }
      return view->kind == LIST_SEARCH ? tr(S_NO_RESULT) : tr(S_EMPTY);
    case STATUS_NO_PERMISSION:
      return tr(S_NO_PERMISSION);
    case STATUS_UNREACHABLE:
      return tr(S_UNREACHABLE);
    default:
      return tr(S_FAILED);
  }
}

static const char *call_type_label(uint8_t type) {
  switch (type) {
    case CALL_TYPE_INCOMING:
      return tr(S_INCOMING);
    case CALL_TYPE_OUTGOING:
      return tr(S_OUTGOING);
    case CALL_TYPE_MISSED:
      return tr(S_MISSED);
    case CALL_TYPE_REJECTED:
      return tr(S_REJECTED);
    default:
      return "";
  }
}

// Icon and color of a call, as call logs show them: blue in, green out, red missed.
static IconDrawer call_type_icon(uint8_t type, GColor *tint) {
  switch (type) {
    case CALL_TYPE_OUTGOING:
      *tint = GColorIslamicGreen;
      return icons_outgoing;
    case CALL_TYPE_MISSED:
      *tint = GColorRed;
      return icons_missed;
    case CALL_TYPE_REJECTED:
      *tint = GColorDarkGray;
      return icons_rejected;
    default:
      *tint = GColorCobaltBlue;
      return icons_incoming;
  }
}

static bool is_two_lines(ListKind kind) {
  return kind == LIST_LETTERS || kind == LIST_NUMBERS || kind == LIST_CALLS ||
         kind == LIST_THREADS || kind == LIST_MESSAGES;
}

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  ListView *view = context;
  return prefix_rows(view) + view->entries.count + (has_status_row(view) ? 1 : 0);
}

static int16_t get_header_height(MenuLayer *menu, uint16_t section, void *context) {
  return ui_style()->header_height;
}

static void draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section, void *context) {
  ListView *view = context;
  ui_draw_header(ctx, cell_layer, view->title);
}

static const Entry *entry_at(const ListView *view, uint16_t row) {
  uint16_t prefix = prefix_rows(view);
  if (row < prefix || row - prefix >= view->entries.count) {
    return NULL;
  }
  return &view->entries.items[row - prefix];
}

static int16_t get_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  ListView *view = context;
  const Entry *entry = entry_at(view, index->row);
  bool two_lines = entry && is_two_lines(view->kind) && entry->detail[0];
  return ui_row_height(menu, index, two_lines);
}

static void draw_entry(GContext *ctx, const Layer *cell_layer, const ListView *view,
                       const Entry *entry) {
  char title[80];
  // Time, separator and the longest detail a record carries.
  char subtitle[144];
  char when[16];
  switch (view->kind) {
    case LIST_CALLS: {
      GColor tint;
      IconDrawer icon = call_type_icon(entry->flags, &tint);
      ui_format_time(entry->time, when, sizeof(when));
      snprintf(subtitle, sizeof(subtitle), "%s · %s", call_type_label(entry->flags), when);
      ui_draw_tinted_row(ctx, cell_layer, entry->title, subtitle, icon, tint);
      break;
    }
    case LIST_THREADS:
      ui_format_time(entry->time, when, sizeof(when));
      snprintf(subtitle, sizeof(subtitle), "%s · %s", when, entry->detail);
      ui_draw_row(ctx, cell_layer, entry->title, subtitle, NULL, entry->flags & THREAD_FLAG_UNREAD);
      break;
    case LIST_MESSAGES:
      ui_format_time(entry->time, when, sizeof(when));
      snprintf(title, sizeof(title), "%s · %s",
               (entry->flags & MESSAGE_FLAG_SENT) ? tr(S_ME) : view->title, when);
      if (entry->flags & MESSAGE_FLAG_PICTURE) {
        snprintf(subtitle, sizeof(subtitle), entry->detail[0] ? "%s %s" : "%s", tr(S_PICTURE),
                 entry->detail);
      } else {
        snprintf(subtitle, sizeof(subtitle), "%s", entry->detail);
      }
      ui_draw_row(ctx, cell_layer, title, subtitle, NULL, false);
      break;
    default:
      ui_draw_row(ctx, cell_layer, entry->title, entry->detail, NULL, false);
      break;
  }
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *index, void *context) {
  ListView *view = context;
  if (index->row < prefix_rows(view)) {
    if (view->kind == LIST_LETTERS) {
      ui_draw_row(ctx, cell_layer, tr(S_SEARCH), NULL, icons_search, false);
    } else {
      bool reply = compose_can_reply(view->arg);
      ui_draw_row(ctx, cell_layer, tr(reply ? S_REPLY : S_OPEN_ON_PHONE), NULL,
                  reply ? icons_message : icons_smartphone, false);
    }
    return;
  }
  const Entry *entry = entry_at(view, index->row);
  if (entry) {
    draw_entry(ctx, cell_layer, view, entry);
  } else {
    ui_draw_row(ctx, cell_layer, status_text(view), NULL, NULL, false);
  }
}

static void selection_changed(MenuLayer *menu, MenuIndex new_index, MenuIndex old_index,
                              void *context) {
  ListView *view = context;
  uint16_t loaded_end = prefix_rows(view) + view->entries.count;
  if (new_index.row + PREFETCH_ROWS >= loaded_end && entries_can_grow(&view->entries)) {
    request_page(view);
    layer_mark_dirty(menu_layer_get_layer(menu));
  }
}

static const Entry *message_at(void *context, int16_t index) {
  ListView *view = context;
  return index >= 0 ? entry_at(view, prefix_rows(view) + index) : NULL;
}

// The reader moved: the selection follows, which also loads the next page in time.
static void message_moved(void *context, int16_t index) {
  ListView *view = context;
  if (!view->menu) {
    return;
  }
  MenuIndex row = { .section = 0, .row = prefix_rows(view) + index };
  menu_layer_set_selected_index(view->menu, row, MenuRowAlignCenter, false);
  selection_changed(view->menu, row, row, view);
}

static void search_dictated(const char *text, void *context) {
  list_window_push(LIST_SEARCH, 0, text, text);
}

static void open_entry(ListView *view, const Entry *entry) {
  switch (view->kind) {
    case LIST_CONTACTS:
    case LIST_SEARCH:
      list_window_push(LIST_NUMBERS, entry->id, entry->title, NULL);
      break;
    case LIST_LETTERS:
      list_window_push(LIST_CONTACTS, 0, entry->title, entry->title);
      break;
    case LIST_NUMBERS:
      actions_window_push(view->title, entry->detail);
      break;
    case LIST_CALLS:
      // Private numbers come without one.
      if (entry->detail[0]) {
        actions_window_push(strcmp(entry->title, entry->detail) ? entry->title : NULL,
                            entry->detail);
      }
      break;
    case LIST_THREADS:
      list_window_push(LIST_MESSAGES, entry->id, entry->title, entry->extra);
      break;
    case LIST_MESSAGES: {
      MessageSource source = { .entry_at = message_at, .moved = message_moved, .context = view };
      message_window_push(&source, entry - view->entries.items, view->id, view->title, view->arg);
      break;
    }
  }
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  ListView *view = context;
  if (index->row < prefix_rows(view)) {
    if (view->kind == LIST_LETTERS) {
      compose_dictate(false, search_dictated, NULL);
    } else {
      reply_open(view->id, view->title, view->arg);
    }
    return;
  }
  const Entry *entry = entry_at(view, index->row);
  if (entry) {
    open_entry(view, entry);
  } else if (!view->loading) {
    request_page(view);
    layer_mark_dirty(menu_layer_get_layer(menu));
  }
}

static void window_load(Window *window) {
  ListView *view = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  view->menu = ui_menu_create(window, layer_get_bounds(root), (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .get_header_height = get_header_height,
    .draw_header = draw_header,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_click,
    .selection_changed = selection_changed,
  }, view);
  if (ui_menu_attach(window, view->menu)) {
    request_page(view);
  }
}

static int16_t index_of(const EntryList *list, int32_t id) {
  for (uint16_t i = 0; i < list->count; i++) {
    if (list->items[i].id == id) {
      return i;
    }
  }
  return -1;
}

// The selection stays on the same conversation or message, wherever it moved.
static void refresh_reply(DictionaryIterator *reply, void *context) {
  ListView *view = context;
  view->refreshing = false;
  bool ok = reply && comm_get_int(reply, MESSAGE_KEY_Status, STATUS_FAILED) == STATUS_OK;
  Tuple *items = ok ? dict_find(reply, MESSAGE_KEY_Items) : NULL;
  int32_t stamp = ok ? comm_get_int(reply, MESSAGE_KEY_Flags, 0) : 0;
  // Nothing changed on the phone: the rows on screen stay, every loaded page with them.
  if (ok && !items && stamp && stamp == view->stamp) {
    if (view->refresh_to_top && view->menu) {
      menu_layer_set_selected_index(view->menu, (MenuIndex){ .section = 0, .row = prefix_rows(view) },
                                    MenuRowAlignCenter, false);
    }
    view->refresh_to_top = false;
    return;
  }
  if (items && !entries_append(&view->fresh, items->value->data, items->length)) {
    ok = false;
  }
  // Under another window, a swap would move the rows a message reader is walking.
  if (!ok || !view->menu || window_stack_get_top_window() != view->window) {
    entries_clear(&view->fresh);
    return;
  }

  uint16_t prefix = prefix_rows(view);
  MenuIndex selected = menu_layer_get_selected_index(view->menu);
  const Entry *current = entry_at(view, selected.row);
  int32_t selected_id = current ? current->id : 0;
  bool had_entry = current != NULL;

  entries_clear(&view->entries);
  view->fresh.total = comm_get_int(reply, MESSAGE_KEY_Total, 0);
  view->entries = view->fresh;
  entries_init(&view->fresh, view->entries.capacity);
  view->status = STATUS_OK;
  view->stamp = stamp;
  menu_layer_reload_data(view->menu);

  int16_t index = had_entry && !view->refresh_to_top ? index_of(&view->entries, selected_id) : -1;
  uint16_t row = index >= 0                  ? prefix + index
                 : view->refresh_to_top      ? prefix
                 : selected.row < prefix     ? selected.row
                                             : prefix + view->entries.count;
  uint16_t last = get_num_rows(view->menu, 0, view) - 1;
  view->refresh_to_top = false;
  MenuIndex target = { .section = 0, .row = row < last ? row : last };
  menu_layer_set_selected_index(view->menu, target, MenuRowAlignCenter, false);
  selection_changed(view->menu, target, target, view);
}

// Conversations and messages reload when the list shows again, without the
// loading row of the first visit.
static void window_appear(Window *window) {
  ListView *view = window_get_user_data(window);
  bool messages = view->kind == LIST_THREADS || view->kind == LIST_MESSAGES;
  if (!messages || !view->menu) {
    return;
  }
  if (!view->shown) {
    view->shown = true;
    return;
  }
  if (view->sent != compose_sent_count()) {
    view->sent = compose_sent_count();
    view->refresh_to_top = true;
  }
  // A page still on its way is as fresh, and the rows it fills show "Loading" already.
  if (view->refreshing || view->loading) {
    return;
  }
  CommRequest request = {
    .cmd = COMMANDS[view->kind], .id = view->id, .offset = 0, .flags = view->stamp,
  };
  view->refreshing = comm_send(&request, refresh_reply, view) != 0;
}

static void window_unload(Window *window) {
  ListView *view = window_get_user_data(window);
  comm_cancel(view);
  if (view->menu) {
    menu_layer_destroy(view->menu);
  }
  entries_clear(&view->entries);
  entries_clear(&view->fresh);
  free(view);
  window_destroy(window);
}

void list_window_push(ListKind kind, int32_t id, const char *title, const char *arg) {
  ListView *view = calloc(1, sizeof(ListView));
  if (!view) {
    return;
  }
  view->kind = kind;
  view->id = id;
  ui_copy_text(view->title, title, sizeof(view->title));
  ui_copy_text(view->arg, arg, sizeof(view->arg));
  entries_init(&view->entries, kind == LIST_MESSAGES ? MESSAGE_CAPACITY : CAPACITY);
  entries_init(&view->fresh, view->entries.capacity);
  view->sent = compose_sent_count();
  view->window = window_create();
  if (!view->window) {
    free(view);
    return;
  }
  window_set_user_data(view->window, view);
  window_set_window_handlers(view->window, (WindowHandlers){
    .load = window_load,
    .appear = window_appear,
    .unload = window_unload,
  });
  window_stack_push(view->window, true);
}
