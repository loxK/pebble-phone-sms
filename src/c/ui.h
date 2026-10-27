// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <pebble.h>

#define UI_ACCENT PBL_IF_COLOR_ELSE(GColorMidnightGreen, GColorBlack)

// Draws an icon inside a square box with the current fill and stroke colors.
typedef void (*IconDrawer)(GContext *ctx, GRect box);

// Fonts and metrics following the "Text size" setting of the watch.
typedef struct {
  GFont title;
  GFont subtitle;
  GFont header;
  int16_t title_height;
  int16_t subtitle_height;
  int16_t header_height;
  int16_t row_single;
  int16_t row_double;
  int16_t icon;
  // Contact photos and avatars, in pixels.
  int16_t photo;
  bool large;
  // Message reading screen, laid out like a system notification and sized on its own.
  GFont body;
  GFont body_header;
  GFont body_footer;
  // Space the system puts above the app name and the sender at this size.
  int16_t body_padding;
} UiStyle;

// Sizes chosen on the phone. UI_SIZE_SYSTEM follows the "Text size" setting of the
// watch for menus, and the default notification text size for messages.
enum {
  UI_SIZE_SYSTEM = 0,
  UI_SIZE_SMALL = 1,
  UI_SIZE_MEDIUM = 2,
  UI_SIZE_LARGE = 3,
  UI_SIZE_EXTRA_LARGE = 4,
};

// Sets up the style with the last sizes the phone asked for.
void ui_init(void);

// Stores and applies the menu and message sizes from the phone. True when the style changed.
bool ui_set_sizes(int32_t menu, int32_t body);

// True on a touch watch whose firmware lets third-party apps use touch safely.
bool ui_touch_supported(void);

const UiStyle *ui_style(void);

// MenuLayer with the app colors, bound to the window click config, or NULL when
// the heap is short.
MenuLayer *ui_menu_create(Window *window, GRect frame, MenuLayerCallbacks callbacks, void *context);

// Adds the menu to the window. Without one, the window closes itself once
// loaded and this returns false.
bool ui_menu_attach(Window *window, MenuLayer *menu);

// Takes a window that could not build its layers, for want of memory, off the stack
// once it has loaded.
void ui_close_soon(Window *window);

int16_t ui_row_height(MenuLayer *menu, MenuIndex *index, bool two_lines);

void ui_draw_header(GContext *ctx, const Layer *cell_layer, const char *text);

// Title in bold with an optional subtitle and icon; marker draws a dot before the title.
void ui_draw_row(GContext *ctx, const Layer *cell_layer, const char *title, const char *subtitle,
                 IconDrawer icon, bool marker);

// The same, with the icon in tint on color screens while the row is not highlighted.
void ui_draw_tinted_row(GContext *ctx, const Layer *cell_layer, const char *title,
                        const char *subtitle, IconDrawer icon, GColor tint);

// Centered message for empty, loading and error states.
void ui_draw_notice(GContext *ctx, GRect bounds, const char *text);

// Bytes of the character at text, never past its end even when the sequence is cut short.
size_t ui_utf8_length(const char *text);

// Copies the whole characters of text that fit in size bytes, terminator included.
void ui_copy_text(char *buffer, const char *text, size_t size);

// "14:32" for today, "Mon 14:32" within a week, "21/09" before that.
void ui_format_time(uint32_t timestamp, char *buffer, size_t size);

// Seconds as "4:07" or "1:04:07".
void ui_format_duration(int32_t seconds, char *buffer, size_t size);

// Up to two uppercase initials of a name, UTF-8 aware.
void ui_initials(const char *name, char *buffer, size_t size);
