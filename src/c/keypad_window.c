// SPDX-License-Identifier: AGPL-3.0-or-later
#include <pebble.h>
#include "keypad_window.h"
#include "call.h"
#include "icons.h"
#include "l10n.h"
#include "ui.h"

#define MAX_DIGITS 20
#define REPEAT_MS 150
#define COLUMNS 3
#define ROWS 5
#define GAP 3

// Twelve dial keys, then the bottom row.
static const char KEYS[] = "123456789*0#+";
#define KEY_PLUS 12
#define KEY_CALL 13
#define KEY_DELETE 14
#define KEY_COUNT 15

#define COLOR_KEY PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define COLOR_CALL PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorBlack)
#define COLOR_CALL_FOCUS PBL_IF_COLOR_ELSE(GColorDarkGreen, GColorBlack)

static Window *s_window;
static Layer *s_canvas;
static char s_number[MAX_DIGITS + 1];
static uint8_t s_cursor;
// Key under a finger that is still down, -1 when none.
static int8_t s_pressed = -1;

typedef struct {
  GRect display;
  GRect grid;
  GSize cell;
} Layout;

static Layout layout(GRect bounds) {
  const UiStyle *style = ui_style();
#ifdef PBL_ROUND
  int16_t side = bounds.size.w * 64 / 100;
  int16_t display_height = style->large ? 40 : 30;
  int16_t top = bounds.size.h * 9 / 100;
  GRect display = GRect((bounds.size.w - side) / 2, top, side, display_height);
  int16_t grid_top = top + display_height + 2;
  GRect grid = GRect((bounds.size.w - side) / 2, grid_top, side, bounds.size.h * 90 / 100 - grid_top);
#else
  int16_t display_height = style->large ? 42 : 32;
  GRect display = GRect(4, 0, bounds.size.w - 8, display_height);
  GRect grid = GRect(4, display_height + 2, bounds.size.w - 8, bounds.size.h - display_height - 6);
#endif
  return (Layout){
    .display = display,
    .grid = grid,
    .cell = GSize((grid.size.w - GAP * (COLUMNS - 1)) / COLUMNS,
                  (grid.size.h - GAP * (ROWS - 1)) / ROWS),
  };
}

static GRect cell_rect(const Layout *l, int key) {
  int column = key % COLUMNS;
  int row = key / COLUMNS;
  return GRect(l->grid.origin.x + column * (l->cell.w + GAP), l->grid.origin.y + row * (l->cell.h + GAP),
               l->cell.w, l->cell.h);
}

static void draw_key(GContext *ctx, const Layout *l, int key, GFont font, int16_t font_height) {
  GRect rect = cell_rect(l, key);
  bool focused = key == s_cursor || key == s_pressed;
  int16_t corner = rect.size.h / 4;
  GColor ink;
  if (key == KEY_CALL) {
    graphics_context_set_fill_color(ctx, focused ? COLOR_CALL_FOCUS : COLOR_CALL);
    ink = GColorWhite;
  } else {
    graphics_context_set_fill_color(ctx, focused ? UI_ACCENT : COLOR_KEY);
    ink = focused ? GColorWhite : GColorBlack;
  }
  graphics_fill_rect(ctx, rect, corner, GCornersAll);
#ifndef PBL_COLOR
  if (!focused || key == KEY_CALL) {
    graphics_context_set_stroke_color(ctx, focused ? GColorWhite : GColorBlack);
    graphics_context_set_stroke_width(ctx, focused ? 3 : 1);
    graphics_draw_round_rect(ctx, grect_inset(rect, GEdgeInsets(focused ? 2 : 0)), corner);
  }
#endif

  graphics_context_set_fill_color(ctx, ink);
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_text_color(ctx, ink);
  int16_t icon = rect.size.h * 6 / 10;
  GRect icon_box = GRect(rect.origin.x + (rect.size.w - icon) / 2, rect.origin.y + (rect.size.h - icon) / 2,
                         icon, icon);
  if (key == KEY_CALL) {
    icons_phone(ctx, icon_box);
  } else if (key == KEY_DELETE) {
    icons_backspace(ctx, icon_box);
  } else {
    char label[2] = { KEYS[key], '\0' };
    graphics_draw_text(ctx, label, font,
                       GRect(rect.origin.x, rect.origin.y + (rect.size.h - font_height) / 2 - 3,
                             rect.size.w, font_height),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  Layout l = layout(layer_get_bounds(layer));

  bool long_number = strlen(s_number) > 11;
  GFont number_font = fonts_get_system_font(long_number ? FONT_KEY_GOTHIC_24_BOLD
                                                        : FONT_KEY_GOTHIC_28_BOLD);
  graphics_context_set_text_color(ctx, s_number[0] ? GColorBlack : UI_ACCENT);
  GRect text = l.display;
  text.origin.y += (text.size.h - 34) / 2 - 2;
  text.size.h = 34;
  graphics_draw_text(ctx, s_number[0] ? s_number : tr(S_KEYPAD), number_font, text,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  bool big = l.cell.h >= 36;
  GFont font = fonts_get_system_font(big ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD);
  int16_t font_height = big ? 34 : 28;
  for (int key = 0; key < KEY_COUNT; key++) {
    draw_key(ctx, &l, key, font, font_height);
  }
}

static void dial(void) {
  if (s_number[0]) {
    call_dial(NULL, s_number);
  }
}

static void press(int key) {
  size_t length = strlen(s_number);
  if (key == KEY_CALL) {
    dial();
  } else if (key == KEY_DELETE) {
    if (length > 0) {
      s_number[length - 1] = '\0';
    }
  } else if (length < MAX_DIGITS) {
    s_number[length] = KEYS[key];
    s_number[length + 1] = '\0';
  }
  layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  s_cursor = (s_cursor + KEY_COUNT - 1) % KEY_COUNT;
  layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  s_cursor = (s_cursor + 1) % KEY_COUNT;
  layer_mark_dirty(s_canvas);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  press(s_cursor);
}

static void select_long_click(ClickRecognizerRef recognizer, void *context) {
  dial();
}

// Back erases a digit, and leaves once there is nothing left to erase.
static void back_click(ClickRecognizerRef recognizer, void *context) {
  if (s_number[0]) {
    press(KEY_DELETE);
  } else {
    window_stack_pop(true);
  }
}

static void click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, REPEAT_MS, up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, REPEAT_MS, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

#ifdef PBL_TOUCH
static int hit_test(int16_t x, int16_t y) {
  Layout l = layout(layer_get_bounds(s_canvas));
  GPoint point = GPoint(x, y);
  for (int key = 0; key < KEY_COUNT; key++) {
    GRect rect = cell_rect(&l, key);
    if (grect_contains_point(&rect, &point)) {
      return key;
    }
  }
  return -1;
}

// A key counts when the finger lifts on the key it went down on.
static void touch_handler(const TouchEvent *event, void *context) {
  int key = hit_test(event->x, event->y);
  switch (event->type) {
    case TouchEvent_Touchdown:
      s_pressed = key;
      if (key >= 0) {
        s_cursor = key;
      }
      break;
    case TouchEvent_PositionUpdate:
      if (key != s_pressed) {
        s_pressed = -1;
      }
      break;
    case TouchEvent_Liftoff:
      if (key >= 0 && key == s_pressed) {
        s_pressed = -1;
        press(key);
        return;
      }
      s_pressed = -1;
      break;
  }
  layer_mark_dirty(s_canvas);
}
#endif

static void window_appear(Window *window) {
#ifdef PBL_TOUCH
  if (ui_touch_supported()) {
    touch_service_subscribe(touch_handler, NULL);
  }
#endif
}

static void window_disappear(Window *window) {
#ifdef PBL_TOUCH
  if (ui_touch_supported()) {
    touch_service_unsubscribe();
  }
#endif
  s_pressed = -1;
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  window_destroy(s_window);
  s_window = NULL;
}

void keypad_window_push(void) {
  s_number[0] = '\0';
  s_cursor = 0;
  s_pressed = -1;
  s_window = window_create();
  if (!s_window) {
    return;
  }
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .appear = window_appear,
    .disappear = window_disappear,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}
