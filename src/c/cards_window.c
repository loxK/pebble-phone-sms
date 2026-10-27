// SPDX-License-Identifier: AGPL-3.0-or-later
#include <pebble.h>
#include "cards_window.h"
#include "call.h"
#include "comm.h"
#include "entries.h"
#include "l10n.h"
#include "list_window.h"
#include "photo.h"
#include "protocol.h"
#include "ui.h"

#define CAPACITY 60
#define REPEAT_MS 200
// Photos kept around the current card: the previous one, itself, the next one.
#define SLOTS 3
#define NO_CARD -1
// The up and down arrows: their tip, their height, and the gap the card keeps from them.
#define ARROW_TIP PBL_IF_ROUND_ELSE(8, 3)
#define ARROW_SIZE 6
#define ARROW_ROOM (ARROW_TIP + ARROW_SIZE + 4)

typedef struct {
  int32_t index;
  GBitmap *bitmap;
} PhotoSlot;

typedef struct {
  Window *window;
  Layer *canvas;
  EntryList entries;
  uint16_t index;
  bool loading;
  int32_t status;
  // A slot with an index and no bitmap is a contact without a photo.
  PhotoSlot slots[SLOTS];
  PhotoRequest *photo_request;
  int32_t request_index;
} CardsView;

// Lives for the whole session: filled ahead of time from the main menu and
// kept between visits, so the cards come back with their photos.
static CardsView *s_view;

// One size for every card, so that a photo fetched ahead fits any of them: the
// room between the arrows less a one-line name and its detail, at most the style's.
static int16_t photo_size(void) {
  const UiStyle *style = ui_style();
  int16_t room = PBL_DISPLAY_HEIGHT - ARROW_ROOM * 2 - 4 - 2 - style->title_height -
                 style->subtitle_height;
  return room < style->photo ? room : style->photo;
}

static void page_reply(DictionaryIterator *reply, void *context);

static void redraw(CardsView *view) {
  if (view->canvas) {
    layer_mark_dirty(view->canvas);
  }
}

static void request_page(CardsView *view) {
  if (view->loading) {
    return;
  }
  CommRequest request = { .cmd = CMD_FAVORITES, .offset = view->entries.count };
  view->loading = comm_send(&request, page_reply, view) != 0;
  if (!view->loading) {
    view->status = STATUS_FAILED;
  }
}

static bool is_near(const CardsView *view, int32_t index) {
  return index >= (int32_t)view->index - 1 && index <= (int32_t)view->index + 1;
}

static PhotoSlot *slot_for(CardsView *view, int32_t index) {
  for (int i = 0; i < SLOTS; i++) {
    if (view->slots[i].index == index) {
      return &view->slots[i];
    }
  }
  return NULL;
}

static void clear_slot(PhotoSlot *slot) {
  if (slot->bitmap) {
    gbitmap_destroy(slot->bitmap);
  }
  *slot = (PhotoSlot){ .index = NO_CARD };
}

// Frees the photos that left the neighbourhood of the current card.
static void prune(CardsView *view) {
  for (int i = 0; i < SLOTS; i++) {
    if (view->slots[i].index != NO_CARD && !is_near(view, view->slots[i].index)) {
      clear_slot(&view->slots[i]);
    }
  }
  if (view->photo_request && !is_near(view, view->request_index)) {
    photo_cancel(view->photo_request);
    view->photo_request = NULL;
  }
}

static void fetch_next(CardsView *view);

static void store(CardsView *view, int32_t index, GBitmap *bitmap) {
  PhotoSlot *slot = slot_for(view, NO_CARD);
  if (!slot) {
    if (bitmap) {
      gbitmap_destroy(bitmap);
    }
    return;
  }
  *slot = (PhotoSlot){ .index = index, .bitmap = bitmap };
}

static void photo_received(GBitmap *photo, void *context) {
  CardsView *view = context;
  view->photo_request = NULL;
  store(view, view->request_index, photo);
  if (view->request_index == view->index) {
    redraw(view);
  }
  fetch_next(view);
}

// One transfer at a time: the current card first, then the next, then the previous.
// Prefetched from the main menu, only the first card loads: the radio waits for
// the cards to show before fetching the neighbours.
static void fetch_next(CardsView *view) {
  if (view->photo_request) {
    return;
  }
  const int32_t order[] = { view->index, (int32_t)view->index + 1, (int32_t)view->index - 1 };
  size_t wanted = view->window ? ARRAY_LENGTH(order) : 1;
  for (size_t i = 0; i < wanted; i++) {
    int32_t index = order[i];
    if (index < 0 || index >= view->entries.count || slot_for(view, index)) {
      continue;
    }
    view->request_index = index;
    view->photo_request =
        photo_fetch(view->entries.items[index].id, NULL, photo_size(), photo_received, view);
    if (view->photo_request) {
      return;
    }
    // Nothing could be sent: remember the card as photo-less rather than retry in a loop.
    store(view, index, NULL);
  }
}

static void show_card(CardsView *view) {
  prune(view);
  // The card on screen goes before a neighbour already on its way.
  if (view->photo_request && view->request_index != view->index && !slot_for(view, view->index)) {
    photo_cancel(view->photo_request);
    view->photo_request = NULL;
  }
  if (view->index < view->entries.count) {
    fetch_next(view);
    if (view->index + 2 >= view->entries.count && entries_can_grow(&view->entries)) {
      request_page(view);
    }
  }
  redraw(view);
}

static void page_reply(DictionaryIterator *reply, void *context) {
  CardsView *view = context;
  bool first = view->entries.count == 0;
  view->loading = false;
  view->status = reply ? comm_get_int(reply, MESSAGE_KEY_Status, STATUS_OK) : STATUS_UNREACHABLE;
  if (view->status == STATUS_OK) {
    view->entries.total = comm_get_int(reply, MESSAGE_KEY_Total, 0);
    Tuple *items = dict_find(reply, MESSAGE_KEY_Items);
    if (items && !entries_append(&view->entries, items->value->data, items->length)) {
      view->status = STATUS_FAILED;
    }
  }
  if (first && view->entries.count > 0) {
    show_card(view);
  } else {
    // The next card may have just arrived.
    fetch_next(view);
    redraw(view);
  }
}

static const char *status_text(const CardsView *view) {
  if (view->loading) {
    return tr(S_LOADING);
  }
  switch (view->status) {
    case STATUS_OK:
      return tr(S_NO_FAVORITES);
    case STATUS_NO_PERMISSION:
      return tr(S_NO_PERMISSION);
    case STATUS_UNREACHABLE:
      return tr(S_UNREACHABLE);
    default:
      return tr(S_FAILED);
  }
}

static void draw_arrow(GContext *ctx, GPoint tip, bool up) {
  int16_t direction = up ? 1 : -1;
  GPoint points[] = { tip, GPoint(tip.x - ARROW_SIZE, tip.y + ARROW_SIZE * direction),
                      GPoint(tip.x + ARROW_SIZE, tip.y + ARROW_SIZE * direction) };
  GPathInfo info = { .num_points = 3, .points = points };
  GPath *path = gpath_create(&info);
  if (path) {
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  CardsView *view = *(CardsView **)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  if (view->index >= view->entries.count) {
    ui_draw_notice(ctx, bounds, status_text(view));
    return;
  }
  const UiStyle *style = ui_style();
  const Entry *entry = &view->entries.items[view->index];
  int16_t inset = PBL_IF_ROUND_ELSE(22, 8);
  int16_t width = bounds.size.w - inset * 2;

  int16_t photo = photo_size();
  GRect name_box = GRect(inset, 0, width, style->title_height * 2);
  GSize name_size = graphics_text_layout_get_content_size(
      entry->title, style->title, name_box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);
  int16_t block = photo + 4 + name_size.h + 2 + (entry->detail[0] ? style->subtitle_height : 0);
  // Centred between the arrows; a name on two lines may take some of their room.
  int16_t top = ARROW_ROOM + (bounds.size.h - ARROW_ROOM * 2 - block) / 2;
  if (top < 0) {
    top = 0;
  }
  GRect photo_box = GRect((bounds.size.w - photo) / 2, top, photo, photo);
  PhotoSlot *slot = slot_for(view, view->index);
  avatar_draw(ctx, photo_box, slot ? slot->bitmap : NULL, entry->title);

  int16_t y = top + photo + 4;
  name_box.origin.y = y;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, entry->title, style->title, name_box, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
  y += name_size.h + 2;
  if (entry->detail[0]) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, entry->detail, style->subtitle, GRect(inset, y, width, style->subtitle_height),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  graphics_context_set_fill_color(ctx, UI_ACCENT);
  int16_t arrow_x = bounds.size.w / 2;
  if (view->index > 0) {
    draw_arrow(ctx, GPoint(arrow_x, ARROW_TIP), true);
  }
  if (view->index + 1 < view->entries.count || entries_can_grow(&view->entries)) {
    draw_arrow(ctx, GPoint(arrow_x, bounds.size.h - ARROW_TIP), false);
  }
#ifndef PBL_ROUND
  char position[12];
  snprintf(position, sizeof(position), "%d/%d", view->index + 1, view->entries.total);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, position, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(bounds.size.w - 48, bounds.size.h - 20, 44, 18),
                     GTextOverflowModeFill, GTextAlignmentRight, NULL);
#endif
}

static void move(CardsView *view, int delta) {
  int32_t next = (int32_t)view->index + delta;
  if (next < 0 || next >= view->entries.count) {
    return;
  }
  view->index = next;
  show_card(view);
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  move(context, -1);
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  move(context, 1);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  CardsView *view = context;
  if (view->index < view->entries.count) {
    const Entry *entry = &view->entries.items[view->index];
    list_window_push(LIST_NUMBERS, entry->id, entry->title, NULL);
  } else if (!view->loading) {
    request_page(view);
    redraw(view);
  }
}

static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  CardsView *view = context;
  if (view->index < view->entries.count && view->entries.items[view->index].extra[0]) {
    const Entry *entry = &view->entries.items[view->index];
    call_dial(entry->title, entry->extra);
  }
}

static void click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, REPEAT_MS, up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, REPEAT_MS, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long_click, NULL);
}

static void window_load(Window *window) {
  CardsView *view = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  view->canvas = layer_create_with_data(layer_get_bounds(root), sizeof(CardsView *));
  if (!view->canvas) {
    ui_close_soon(window);
    return;
  }
  *(CardsView **)layer_get_data(view->canvas) = view;
  layer_set_update_proc(view->canvas, canvas_update);
  layer_add_child(root, view->canvas);
  if (view->entries.count == 0) {
    request_page(view);
  } else {
    show_card(view);
  }
}

// Away from the cards, covered or closed, only the photo shown on return stays: the
// neighbours are fetched again then, and the heap goes to the other screens.
static void release_neighbours(CardsView *view) {
  for (int i = 0; i < SLOTS; i++) {
    if (view->slots[i].index != (int32_t)view->index) {
      clear_slot(&view->slots[i]);
    }
  }
  if (view->photo_request && view->request_index != view->index) {
    photo_cancel(view->photo_request);
    view->photo_request = NULL;
  }
}

static void window_appear(Window *window) {
  CardsView *view = window_get_user_data(window);
  if (view->canvas && view->index < view->entries.count) {
    fetch_next(view);
  }
}

static void window_disappear(Window *window) {
  release_neighbours(window_get_user_data(window));
}

static void window_unload(Window *window) {
  CardsView *view = window_get_user_data(window);
  release_neighbours(view);
  layer_destroy(view->canvas);
  view->canvas = NULL;
  view->window = NULL;
  window_destroy(window);
}

static CardsView *view_get(void) {
  if (s_view) {
    return s_view;
  }
  s_view = calloc(1, sizeof(CardsView));
  if (!s_view) {
    return NULL;
  }
  entries_init(&s_view->entries, CAPACITY);
  for (int i = 0; i < SLOTS; i++) {
    s_view->slots[i].index = NO_CARD;
  }
  return s_view;
}

void cards_prefetch(void) {
  CardsView *view = view_get();
  if (view && view->entries.count == 0) {
    request_page(view);
  }
}

void cards_window_push(void) {
  CardsView *view = view_get();
  if (!view || view->window) {
    return;
  }
  view->window = window_create();
  if (!view->window) {
    return;
  }
  window_set_user_data(view->window, view);
  window_set_click_config_provider_with_context(view->window, click_config, view);
  window_set_window_handlers(view->window, (WindowHandlers){
    .load = window_load,
    .appear = window_appear,
    .disappear = window_disappear,
    .unload = window_unload,
  });
  window_stack_push(view->window, true);
}

// Drops the cached photos, sized for a style that no longer applies, and fetches them anew.
void cards_reset(void) {
  if (!s_view) {
    return;
  }
  photo_cancel(s_view->photo_request);
  s_view->photo_request = NULL;
  for (int i = 0; i < SLOTS; i++) {
    clear_slot(&s_view->slots[i]);
  }
  if (s_view->window) {
    show_card(s_view);
  }
}

void cards_deinit(void) {
  if (!s_view) {
    return;
  }
  comm_cancel(s_view);
  photo_cancel(s_view->photo_request);
  for (int i = 0; i < SLOTS; i++) {
    clear_slot(&s_view->slots[i]);
  }
  entries_clear(&s_view->entries);
  free(s_view);
  s_view = NULL;
}

GBitmap *cards_photo(const char *number, int16_t size) {
  if (!s_view || !number || !number[0]) {
    return NULL;
  }
  for (int i = 0; i < SLOTS; i++) {
    const PhotoSlot *slot = &s_view->slots[i];
    if (slot->bitmap && gbitmap_get_bounds(slot->bitmap).size.w == size && slot->index >= 0 &&
        slot->index < s_view->entries.count) {
      const char *extra = s_view->entries.items[slot->index].extra;
      if (extra && extra[0] && call_same_number(extra, number)) {
        return slot->bitmap;
      }
    }
  }
  return NULL;
}
