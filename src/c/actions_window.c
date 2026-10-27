// SPDX-License-Identifier: AGPL-3.0-or-later
#include <pebble.h>
#include "actions_window.h"
#include "call.h"
#include "icons.h"
#include "l10n.h"
#include "reply.h"
#include "ui.h"

typedef enum {
  ACTION_CALL,
  ACTION_SMS,
} Action;

typedef struct {
  Window *window;
  MenuLayer *menu;
  char name[48];
  char number[32];
  Action actions[2];
  uint8_t count;
} ActionsView;

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  ActionsView *view = context;
  return view->count;
}

static int16_t get_header_height(MenuLayer *menu, uint16_t section, void *context) {
  ActionsView *view = context;
  const UiStyle *style = ui_style();
  return style->title_height + (view->name[0] ? style->subtitle_height : 0) + PBL_IF_ROUND_ELSE(14, 4);
}

static void draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section, void *context) {
  ActionsView *view = context;
  const UiStyle *style = ui_style();
  GRect bounds = layer_get_bounds(cell_layer);
  GTextAlignment alignment = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  int16_t inset = PBL_IF_ROUND_ELSE(bounds.size.w / 6, 6);
  int16_t width = bounds.size.w - inset * 2;
  int16_t y = PBL_IF_ROUND_ELSE(8, -2);
  const char *title = view->name[0] ? view->name : view->number;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, title, style->title, GRect(inset, y, width, style->title_height),
                     GTextOverflowModeTrailingEllipsis, alignment, NULL);
  if (view->name[0]) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, view->number, style->subtitle,
                       GRect(inset, y + style->title_height - 4, width, style->subtitle_height),
                       GTextOverflowModeTrailingEllipsis, alignment, NULL);
  }
}

static int16_t get_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return ui_row_height(menu, index, false);
}

static void draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *index, void *context) {
  ActionsView *view = context;
  if (view->actions[index->row] == ACTION_CALL) {
    ui_draw_row(ctx, cell_layer, tr(S_CALL), NULL, icons_call, false);
  } else {
    ui_draw_row(ctx, cell_layer, tr(S_SEND_SMS), NULL, icons_write, false);
  }
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  ActionsView *view = context;
  if (view->actions[index->row] == ACTION_CALL) {
    call_dial(view->name, view->number);
  } else {
    reply_open(0, view->name, view->number);
  }
}

static void window_load(Window *window) {
  ActionsView *view = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  view->menu = ui_menu_create(window, layer_get_bounds(root), (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .get_header_height = get_header_height,
    .draw_header = draw_header,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_click,
  }, view);
  ui_menu_attach(window, view->menu);
}

static void window_unload(Window *window) {
  ActionsView *view = window_get_user_data(window);
  if (view->menu) {
    menu_layer_destroy(view->menu);
  }
  free(view);
  window_destroy(window);
}

void actions_window_push(const char *name, const char *number) {
  ActionsView *view = calloc(1, sizeof(ActionsView));
  if (!view) {
    return;
  }
  strncpy(view->name, name ? name : "", sizeof(view->name) - 1);
  strncpy(view->number, number, sizeof(view->number) - 1);
  view->actions[view->count++] = ACTION_CALL;
  view->actions[view->count++] = ACTION_SMS;
  view->window = window_create();
  if (!view->window) {
    free(view);
    return;
  }
  window_set_user_data(view->window, view);
  window_set_window_handlers(view->window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(view->window, true);
}
