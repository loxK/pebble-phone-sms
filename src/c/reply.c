// SPDX-License-Identifier: AGPL-3.0-or-later
#include <pebble.h>
#include "reply.h"
#include "comm.h"
#include "compose.h"
#include "entries.h"
#include "icons.h"
#include "l10n.h"
#include "protocol.h"
#include "ui.h"

#define MAX_REPLIES 10
#define REPEAT_MS 150
#define GRID_GAP 4

// Colour screens draw each emoji pixel as a 2x2 block: the emoji fonts stop at 28 px.
#ifdef PBL_COLOR
#define GRID_COLUMNS 3
#define EMOJI_SCALE 2
#define GLYPH_BOX 36
#else
#define GRID_COLUMNS 4
#define EMOJI_SCALE 1
#endif

// The emoji the system reply menu offers, which the watch fonts carry.
static const char *const EMOJI[] = {
  "👍", "👎", "😃",
  "😂", "😉", "😍",
  "🎉", "\xe2\x9d\xa4", "😘",
  "😇", "😎", "😛",
  "😟", "😩", "😭",
  "😴", "😐", "😯",
  "👌", "💩", "🍺",
};
#define EMOJI_COUNT ((int)ARRAY_LENGTH(EMOJI))

typedef enum {
  ROW_DICTATE,
  ROW_REPLY,
  ROW_EMOJI,
  ROW_OPEN,
} RowKind;

// The quick replies form a second section, under a rule.
#define SECTION_ACTIONS 0
#define SECTION_REPLIES 1
#define SEPARATOR_HEIGHT 9

static EntryList s_replies;
static bool s_loading;

static char s_name[48];
static char s_address[32];
static int32_t s_thread;

static Window *s_window;
static MenuLayer *s_menu;
static char s_header[64];
// Under "Open on phone": NULL, or why the phone did not open the conversation.
static const char *s_open_status;
static bool s_opening;
// A sender that takes no reply: the menu only opens the conversation on the phone.
static bool s_open_only;

static Window *s_emoji_window;
static Layer *s_grid;
static int s_cursor;
// Pixels the grid is scrolled by, from the top of its first row.
static int s_scroll;
static int s_pressed = -1;
#ifdef PBL_TOUCH
// Where the finger went down, and the scroll at that moment.
static int16_t s_touch_y;
static int s_touch_scroll;
static bool s_dragging;
#endif

static void replies_reply(DictionaryIterator *reply, void *context) {
  s_loading = false;
  if (!reply || comm_get_int(reply, MESSAGE_KEY_Status, STATUS_FAILED) != STATUS_OK) {
    return;
  }
  Tuple *items = dict_find(reply, MESSAGE_KEY_Items);
  entries_clear(&s_replies);
  s_replies.total = comm_get_int(reply, MESSAGE_KEY_Total, 0);
  if (items) {
    entries_append(&s_replies, items->value->data, items->length);
  }
  // The menu reads the list row by row on each draw, so it can grow under it.
  if (s_menu) {
    menu_layer_reload_data(s_menu);
  }
}

void reply_prefetch(void) {
  if (s_loading || s_replies.count > 0) {
    return;
  }
  if (!s_replies.capacity) {
    entries_init(&s_replies, MAX_REPLIES);
  }
  s_loading = comm_send(&(CommRequest){ .cmd = CMD_REPLIES }, replies_reply, NULL) != 0;
}

// Takes the reply windows off the stack and sends text.
static void send(const char *text) {
  if (s_emoji_window) {
    window_stack_remove(s_emoji_window, false);
  }
  if (s_window) {
    window_stack_remove(s_window, false);
  }
  compose_send(s_thread, s_address, text);
}

// Emoji grid ///////////////////////////////////////////////////////////////

#define ROWS ((EMOJI_COUNT + GRID_COLUMNS - 1) / GRID_COLUMNS)
// Finger travel before a touch turns into a scroll.
#define DRAG_THRESHOLD 8

typedef struct {
  GRect bounds;
  GRect grid;
  GSize cell;
  int pitch;
  int visible;
  int max_scroll;
} GridLayout;

static GridLayout grid_layout(void) {
  GRect bounds = layer_get_bounds(s_grid);
  GRect grid = grect_inset(bounds, GEdgeInsets(PBL_IF_ROUND_ELSE(bounds.size.h / 8, 6),
                                               PBL_IF_ROUND_ELSE(bounds.size.w / 7, 6)));
  int16_t width = (grid.size.w - GRID_GAP * (GRID_COLUMNS - 1)) / GRID_COLUMNS;
  int16_t wanted = EMOJI_SCALE > 1 ? width : (ui_style()->large ? 48 : 38);
  // As many whole rows as fit, stretched to fill the height exactly, so the
  // grid never shows a strip of empty space at either end.
  int visible = (grid.size.h + GRID_GAP) / (wanted + GRID_GAP);
  if (visible < 1) {
    visible = 1;
  }
  int16_t height = (grid.size.h - GRID_GAP * (visible - 1)) / visible;
  int pitch = height + GRID_GAP;
  int max_scroll = (ROWS - visible) * pitch;
  return (GridLayout){
    .bounds = bounds,
    .grid = grid,
    .cell = GSize(width, height),
    .pitch = pitch,
    .visible = visible,
    .max_scroll = max_scroll > 0 ? max_scroll : 0,
  };
}

static int clamp_scroll(const GridLayout *l, int scroll) {
  return scroll < 0 ? 0 : scroll > l->max_scroll ? l->max_scroll : scroll;
}

static GRect cell_rect(const GridLayout *l, int index) {
  int row = index / GRID_COLUMNS;
  int column = index % GRID_COLUMNS;
  return GRect(l->grid.origin.x + column * (l->cell.w + GRID_GAP),
               l->grid.origin.y + row * l->pitch - s_scroll, l->cell.w, l->cell.h);
}

// Only whole cells are drawn: a doubled emoji is read back from the screen,
// which a cell hanging off an edge cannot provide.
static bool is_shown(const GridLayout *l, GRect rect) {
  return rect.origin.y >= 0 && rect.origin.y + rect.size.h <= l->bounds.size.h;
}

// Scrolls just enough to keep the cursor's row on screen.
static void follow_cursor(void) {
  GridLayout l = grid_layout();
  int row = s_cursor / GRID_COLUMNS;
  int first = s_scroll / l.pitch;
  if (row < first) {
    first = row;
  } else if (row >= first + l.visible) {
    first = row - l.visible + 1;
  }
  s_scroll = clamp_scroll(&l, first * l.pitch);
}

static GFont emoji_font(int16_t *height) {
#ifdef PBL_COLOR
  // Doubled, so the source glyph must fit half a cell.
  bool big = ui_style()->large;
  *height = big ? 34 : 22;
  return fonts_get_system_font(big ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_18_BOLD);
#else
  *height = ui_style()->large ? 34 : 28;
  return fonts_get_system_font(ui_style()->large ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD);
#endif
}

#ifdef PBL_COLOR
// Draws the emoji small on the white screen, reads its pixels back from the frame
// buffer, then paints them again twice as large, centered in rect.
static void draw_scaled(GContext *ctx, const char *emoji, GFont font, int16_t font_height, GRect rect) {
  static uint8_t pixels[GLYPH_BOX * GLYPH_BOX];
  GRect source = GRect(rect.origin.x + (rect.size.w - GLYPH_BOX) / 2,
                       rect.origin.y + (rect.size.h - GLYPH_BOX) / 2, GLYPH_BOX, GLYPH_BOX);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, emoji, font,
                     GRect(source.origin.x, source.origin.y + (GLYPH_BOX - font_height) / 2 - 2,
                           GLYPH_BOX, font_height),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  GBitmap *frame = graphics_capture_frame_buffer(ctx);
  if (!frame) {
    return;
  }
  int min_x = GLYPH_BOX, min_y = GLYPH_BOX, max_x = -1, max_y = -1;
  for (int y = 0; y < GLYPH_BOX; y++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(frame, source.origin.y + y);
    for (int x = 0; x < GLYPH_BOX; x++) {
      int sx = source.origin.x + x;
      uint8_t pixel = (sx >= row.min_x && sx <= row.max_x) ? row.data[sx] : GColorWhiteARGB8;
      pixels[y * GLYPH_BOX + x] = pixel;
      if (pixel != GColorWhiteARGB8) {
        min_x = x < min_x ? x : min_x;
        min_y = y < min_y ? y : min_y;
        max_x = x > max_x ? x : max_x;
        max_y = y > max_y ? y : max_y;
      }
    }
  }
  graphics_release_frame_buffer(ctx, frame);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, source, 0, GCornerNone);
  if (max_x < min_x) {
    return;
  }
  GPoint origin = GPoint(rect.origin.x + (rect.size.w - EMOJI_SCALE * (max_x - min_x + 1)) / 2,
                         rect.origin.y + (rect.size.h - EMOJI_SCALE * (max_y - min_y + 1)) / 2);
  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x;) {
      uint8_t color = pixels[y * GLYPH_BOX + x];
      int start = x++;
      while (x <= max_x && pixels[y * GLYPH_BOX + x] == color) {
        x++;
      }
      if (color != GColorWhiteARGB8) {
        graphics_context_set_fill_color(ctx, (GColor){ .argb = color });
        graphics_fill_rect(ctx,
                           GRect(origin.x + EMOJI_SCALE * (start - min_x), origin.y + EMOJI_SCALE * (y - min_y),
                                 EMOJI_SCALE * (x - start), EMOJI_SCALE),
                           0, GCornerNone);
      }
    }
  }
}
#endif

static void grid_update(Layer *layer, GContext *ctx) {
  GridLayout l = grid_layout();
  int16_t font_height;
  GFont font = emoji_font(&font_height);
  for (int i = 0; i < EMOJI_COUNT; i++) {
    GRect rect = cell_rect(&l, i);
    if (!is_shown(&l, rect)) {
      continue;
    }
#ifdef PBL_COLOR
    draw_scaled(ctx, EMOJI[i], font, font_height, rect);
#else
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, EMOJI[i], font,
                       GRect(rect.origin.x, rect.origin.y + (rect.size.h - font_height) / 2 - 2,
                             rect.size.w, font_height),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
#endif
    // A ring rather than a fill keeps the emoji's dark outlines readable.
    if (i == s_cursor || i == s_pressed) {
      graphics_context_set_stroke_color(ctx, UI_ACCENT);
      graphics_context_set_stroke_width(ctx, 3);
      graphics_draw_round_rect(ctx, grect_inset(rect, GEdgeInsets(1)), rect.size.h / 4);
    }
  }
}

static void move_cursor(int delta) {
  s_cursor = (s_cursor + delta + EMOJI_COUNT) % EMOJI_COUNT;
  follow_cursor();
  layer_mark_dirty(s_grid);
}

static void emoji_up(ClickRecognizerRef recognizer, void *context) {
  move_cursor(-1);
}

static void emoji_down(ClickRecognizerRef recognizer, void *context) {
  move_cursor(1);
}

static void emoji_select(ClickRecognizerRef recognizer, void *context) {
  send(EMOJI[s_cursor]);
}

static void emoji_click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, REPEAT_MS, emoji_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, REPEAT_MS, emoji_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, emoji_select);
}

#ifdef PBL_TOUCH
static int hit_test(int16_t x, int16_t y) {
  GridLayout l = grid_layout();
  GPoint point = GPoint(x, y);
  for (int i = 0; i < EMOJI_COUNT; i++) {
    GRect rect = cell_rect(&l, i);
    if (is_shown(&l, rect) && grect_contains_point(&rect, &point)) {
      return i;
    }
  }
  return -1;
}

// Settles on a whole row and brings the cursor back on screen.
static void end_drag(void) {
  GridLayout l = grid_layout();
  s_scroll = clamp_scroll(&l, (s_scroll + l.pitch / 2) / l.pitch * l.pitch);
  int first = s_scroll / l.pitch;
  int last = first + l.visible - 1;
  int row = s_cursor / GRID_COLUMNS;
  if (row < first || row > last) {
    int target = row < first ? first : last;
    s_cursor = target * GRID_COLUMNS + s_cursor % GRID_COLUMNS;
    if (s_cursor >= EMOJI_COUNT) {
      s_cursor = EMOJI_COUNT - 1;
    }
  }
}

// A short tap sends the emoji under the finger; a drag scrolls the grid.
static void touch_handler(const TouchEvent *event, void *context) {
  switch (event->type) {
    case TouchEvent_Touchdown:
      s_touch_y = event->y;
      s_touch_scroll = s_scroll;
      s_dragging = false;
      s_pressed = hit_test(event->x, event->y);
      break;
    case TouchEvent_PositionUpdate: {
      int16_t dy = event->y - s_touch_y;
      if (!s_dragging && (dy > DRAG_THRESHOLD || dy < -DRAG_THRESHOLD)) {
        s_dragging = true;
        s_pressed = -1;
      }
      if (s_dragging) {
        GridLayout l = grid_layout();
        s_scroll = clamp_scroll(&l, s_touch_scroll - dy);
      } else if (hit_test(event->x, event->y) != s_pressed) {
        s_pressed = -1;
      }
      break;
    }
    case TouchEvent_Liftoff:
      if (s_dragging) {
        end_drag();
      } else if (s_pressed >= 0 && hit_test(event->x, event->y) == s_pressed) {
        int index = s_pressed;
        s_pressed = -1;
        send(EMOJI[index]);
        return;
      }
      s_pressed = -1;
      s_dragging = false;
      break;
  }
  layer_mark_dirty(s_grid);
}
#endif

static void emoji_appear(Window *window) {
#ifdef PBL_TOUCH
  if (ui_touch_supported()) {
    touch_service_subscribe(touch_handler, NULL);
  }
#endif
}

static void emoji_disappear(Window *window) {
#ifdef PBL_TOUCH
  if (ui_touch_supported()) {
    touch_service_unsubscribe();
  }
#endif
  s_pressed = -1;
}

static void emoji_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_grid = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_grid, grid_update);
  layer_add_child(root, s_grid);
}

static void emoji_unload(Window *window) {
  layer_destroy(s_grid);
  s_grid = NULL;
  window_destroy(s_emoji_window);
  s_emoji_window = NULL;
}

static void open_emoji(void) {
  s_cursor = 0;
  s_scroll = 0;
  s_pressed = -1;
#ifdef PBL_TOUCH
  s_dragging = false;
#endif
  s_emoji_window = window_create();
  if (!s_emoji_window) {
    return;
  }
  window_set_click_config_provider(s_emoji_window, emoji_click_config);
  window_set_window_handlers(s_emoji_window, (WindowHandlers){
    .load = emoji_load,
    .appear = emoji_appear,
    .disappear = emoji_disappear,
    .unload = emoji_unload,
  });
  window_stack_push(s_emoji_window, true);
}

// Reply list ///////////////////////////////////////////////////////////////

static uint16_t dictate_rows(void) {
  return compose_available() ? 1 : 0;
}

// Dictate, emoji and opening the conversation on the phone, then the quick replies.
static RowKind row_kind(const MenuIndex *index) {
  if (s_open_only) {
    return ROW_OPEN;
  }
  if (index->section == SECTION_REPLIES) {
    return ROW_REPLY;
  }
  if (index->row < dictate_rows()) {
    return ROW_DICTATE;
  }
  return index->row == dictate_rows() ? ROW_EMOJI : ROW_OPEN;
}

static const char *reply_at(const MenuIndex *index) {
  return s_replies.items[index->row].detail;
}

static uint16_t get_num_sections(MenuLayer *menu, void *context) {
  return s_replies.count && !s_open_only ? 2 : 1;
}

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  if (s_open_only) {
    return 1;
  }
  return section == SECTION_REPLIES ? s_replies.count : dictate_rows() + 2;
}

static int16_t get_header_height(MenuLayer *menu, uint16_t section, void *context) {
  return section == SECTION_REPLIES ? SEPARATOR_HEIGHT : ui_style()->header_height;
}

static void draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section, void *context) {
  if (section != SECTION_REPLIES) {
    ui_draw_header(ctx, cell_layer, s_header);
    return;
  }
  GRect bounds = layer_get_bounds(cell_layer);
  int16_t inset = PBL_IF_ROUND_ELSE(bounds.size.w / 4, 8);
  int16_t y = bounds.size.h / 2;
  graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack));
  graphics_draw_line(ctx, GPoint(inset, y), GPoint(bounds.size.w - inset - 1, y));
}

static void push_menu(void);

static void open_reply(DictionaryIterator *reply, void *context) {
  s_opening = false;
  int32_t status = reply ? comm_get_int(reply, MESSAGE_KEY_Status, STATUS_FAILED) : STATUS_UNREACHABLE;
  if (status == STATUS_OK) {
    vibes_short_pulse();
    if (s_window) {
      window_stack_remove(s_window, true);
    }
    return;
  }
  s_open_status = status == STATUS_NO_PERMISSION ? tr(S_NO_PERMISSION)
                  : status == STATUS_UNREACHABLE ? tr(S_UNREACHABLE)
                                                 : tr(S_FAILED);
  if (s_menu) {
    menu_layer_reload_data(s_menu);
  } else {
    // Opened straight from a sender that takes no reply: the menu only shows to say why not.
    push_menu();
  }
}

static void open_on_phone(void) {
  if (s_opening) {
    return;
  }
  s_open_status = NULL;
  s_opening = comm_send(&(CommRequest){ .cmd = CMD_OPEN_THREAD, .id = s_thread, .address = s_address },
                        open_reply, &s_opening) != 0;
  if (!s_opening) {
    s_open_status = tr(S_FAILED);
  }
  if (s_menu) {
    menu_layer_reload_data(s_menu);
  } else if (!s_opening) {
    push_menu();
  }
}

static int16_t get_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return ui_row_height(menu, index, false);
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *index, void *context) {
  switch (row_kind(index)) {
    case ROW_DICTATE:
      ui_draw_row(ctx, cell_layer, tr(S_DICTATE), NULL, icons_dictate, false);
      break;
    case ROW_REPLY:
      ui_draw_row(ctx, cell_layer, reply_at(index), NULL, icons_message, false);
      break;
    case ROW_EMOJI:
      ui_draw_row(ctx, cell_layer, tr(S_EMOJI), NULL, icons_smiley, false);
      break;
    case ROW_OPEN:
      ui_draw_row(ctx, cell_layer, tr(S_OPEN_ON_PHONE), s_opening ? tr(S_LOADING) : s_open_status,
                  icons_smartphone, false);
      break;
  }
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  switch (row_kind(index)) {
    case ROW_DICTATE:
      compose_sms(s_thread, s_address);
      break;
    case ROW_REPLY:
      send(reply_at(index));
      break;
    case ROW_EMOJI:
      open_emoji();
      break;
    case ROW_OPEN:
      open_on_phone();
      break;
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_menu = ui_menu_create(window, layer_get_bounds(root), (MenuLayerCallbacks){
    .get_num_sections = get_num_sections,
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
  comm_cancel(&s_opening);
  s_opening = false;
  if (s_menu) {
    menu_layer_destroy(s_menu);
    s_menu = NULL;
  }
  window_destroy(s_window);
  s_window = NULL;
}

void reply_open(int32_t thread, const char *name, const char *address) {
  if (s_window || s_opening || !address || !address[0]) {
    return;
  }
  s_open_only = !compose_can_reply(address);
  s_thread = thread;
  strncpy(s_name, name ? name : "", sizeof(s_name) - 1);
  s_name[sizeof(s_name) - 1] = '\0';
  strncpy(s_address, address, sizeof(s_address) - 1);
  s_address[sizeof(s_address) - 1] = '\0';
  snprintf(s_header, sizeof(s_header), "%s", s_name[0] ? s_name : s_address);
  s_open_status = NULL;

  // Opening is all there is to do: no menu, a vibration once the phone shows it.
  if (s_open_only) {
    open_on_phone();
    return;
  }
  // The phone may have been unreachable at launch.
  reply_prefetch();
  push_menu();
}

static void push_menu(void) {
  s_window = window_create();
  if (!s_window) {
    return;
  }
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

void reply_deinit(void) {
  entries_clear(&s_replies);
}
