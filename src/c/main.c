// SPDX-License-Identifier: AGPL-3.0-or-later
#include <pebble.h>
#include "call.h"
#include "cards_window.h"
#include "comm.h"
#include "icons.h"
#include "compose.h"
#include "glance.h"
#include "keypad_window.h"
#include "l10n.h"
#include "list_window.h"
#include "photo.h"
#include "protocol.h"
#include "reply.h"
#include "ui.h"

typedef enum {
  ITEM_CALL,
  ITEM_FAVORITES,
  ITEM_CONTACTS,
  ITEM_RECENTS,
  ITEM_MESSAGES,
  ITEM_KEYPAD,
} Item;

static Window *s_window;
static MenuLayer *s_menu;
static Item s_items[6];
static uint8_t s_item_count;
// Shown under the title when the companion did not answer HELLO.
static const char *s_notice;

static void rebuild_items(void) {
  s_item_count = 0;
  if (call_in_progress()) {
    s_items[s_item_count++] = ITEM_CALL;
  }
  s_items[s_item_count++] = ITEM_FAVORITES;
  s_items[s_item_count++] = ITEM_CONTACTS;
  s_items[s_item_count++] = ITEM_RECENTS;
  s_items[s_item_count++] = ITEM_MESSAGES;
  s_items[s_item_count++] = ITEM_KEYPAD;
}

static void refresh(void) {
  rebuild_items();
  if (s_menu) {
    menu_layer_reload_data(s_menu);
  }
}

static const char *item_label(Item item) {
  switch (item) {
    case ITEM_CALL:
      return tr(S_CALL_IN_PROGRESS);
    case ITEM_FAVORITES:
      return tr(S_FAVORITES);
    case ITEM_CONTACTS:
      return tr(S_CONTACTS);
    case ITEM_RECENTS:
      return tr(S_RECENTS);
    case ITEM_MESSAGES:
      return tr(S_MESSAGES);
    case ITEM_KEYPAD:
      return tr(S_KEYPAD);
  }
  return "";
}

static IconDrawer item_icon(Item item) {
  switch (item) {
    case ITEM_CALL:
      return icons_call;
    case ITEM_FAVORITES:
      return icons_favorites;
    case ITEM_CONTACTS:
      return icons_contacts;
    case ITEM_RECENTS:
      return icons_recents;
    case ITEM_MESSAGES:
      return icons_write;
    case ITEM_KEYPAD:
      return icons_keypad;
  }
  return NULL;
}

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  return s_item_count;
}

static int16_t get_header_height(MenuLayer *menu, uint16_t section, void *context) {
  return s_notice ? ui_style()->header_height * 2 + 4 : ui_style()->header_height;
}

static void draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section, void *context) {
  GRect bounds = layer_get_bounds(cell_layer);
  int16_t inset = PBL_IF_ROUND_ELSE(bounds.size.w / 5, 5);
  graphics_context_set_text_color(ctx, UI_ACCENT);
  graphics_draw_text(ctx, s_notice ? s_notice : tr(S_APP_TITLE), ui_style()->header,
                     GRect(inset, -2, bounds.size.w - inset * 2, bounds.size.h),
                     GTextOverflowModeWordWrap,
                     PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft), NULL);
}

static int16_t get_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return ui_row_height(menu, index, false);
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *index, void *context) {
  Item item = s_items[index->row];
  ui_draw_row(ctx, cell_layer, item_label(item), NULL, item_icon(item), false);
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  Item item = s_items[index->row];
  switch (item) {
    case ITEM_CALL:
      call_window_push();
      break;
    case ITEM_FAVORITES:
      cards_window_push();
      break;
    case ITEM_CONTACTS:
      list_window_push(LIST_LETTERS, 0, tr(S_CONTACTS), NULL);
      break;
    case ITEM_RECENTS:
      list_window_push(LIST_CALLS, 0, tr(S_RECENTS), NULL);
      break;
    case ITEM_MESSAGES:
      list_window_push(LIST_THREADS, 0, tr(S_MESSAGES), NULL);
      break;
    case ITEM_KEYPAD:
      keypad_window_push();
      break;
  }
}

static void hello_reply(DictionaryIterator *reply, void *context) {
  s_notice = reply ? NULL : tr(S_NO_COMPANION);
  if (reply) {
    glance_read(reply);
    // Before the prefetch, which sizes the photos after the style.
    if (ui_set_sizes(comm_get_int(reply, MESSAGE_KEY_Size, UI_SIZE_SYSTEM),
                     comm_get_int(reply, MESSAGE_KEY_BodySize, UI_SIZE_SYSTEM))) {
      cards_reset();
    }
    reply_prefetch();
    // Favorites is the row selected at launch.
    cards_prefetch();
  }
  refresh();
}

// Quick launched during a call, the app opens on the call alone, so leaving the
// call screen once the call is over goes straight back to the watchface.
static void call_state_reply(DictionaryIterator *reply, void *context) {
  if (reply && call_in_progress() && window_stack_get_top_window() == s_window) {
    call_window_push();
    window_stack_remove(s_window, false);
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_menu = ui_menu_create(window, layer_get_bounds(root), (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .get_header_height = get_header_height,
    .draw_header = draw_header,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_click,
  }, NULL);
  ui_menu_attach(window, s_menu);
}

static void window_unload(Window *window) {
  if (s_menu) {
    menu_layer_destroy(s_menu);
    s_menu = NULL;
  }
}

static void enable_touch(void) {
#ifdef PBL_TOUCH
  if (ui_touch_supported()) {
    app_touch_navigation_enable(true);
  }
#endif
}

static void init(void) {
  l10n_init();
  ui_init();
  call_init(refresh);
  comm_init(call_handle_state);
  enable_touch();
  rebuild_items();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  comm_send(&(CommRequest){ .cmd = CMD_HELLO }, hello_reply, NULL);
  bool quick = launch_reason() == APP_LAUNCH_QUICK_LAUNCH;
  comm_send(&(CommRequest){ .cmd = CMD_CALL_STATE }, quick ? call_state_reply : NULL, NULL);
}

static void deinit(void) {
  glance_write();
  window_destroy(s_window);
  cards_deinit();
  reply_deinit();
  compose_deinit();
  comm_deinit();
  call_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
