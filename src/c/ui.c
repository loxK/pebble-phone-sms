// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui.h"
#include "icons.h"

static UiStyle s_style;

#define PERSIST_SIZE 1
#define PERSIST_BODY_SIZE 2

static PreferredContentSize content_size(int32_t size) {
  switch (size) {
    case UI_SIZE_SMALL:
      return PreferredContentSizeSmall;
    case UI_SIZE_MEDIUM:
      return PreferredContentSizeMedium;
    case UI_SIZE_LARGE:
      return PreferredContentSizeLarge;
    case UI_SIZE_EXTRA_LARGE:
      return PreferredContentSizeExtraLarge;
    default:
      return preferred_content_size();
  }
}

// Fonts and cell heights of the system menus for each size, from the
// firmware system theme, so the app matches the launcher.
static void apply(int32_t size) {
  switch (content_size(size)) {
    case PreferredContentSizeSmall:
      s_style = (UiStyle){
        .title = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
        .subtitle = fonts_get_system_font(FONT_KEY_GOTHIC_18),
        .header = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
        .title_height = 22,
        .subtitle_height = 22,
        .header_height = 18,
        .row_single = 42,
        .row_double = 50,
        .icon = 18,
        .photo = 56,
      };
      break;
    case PreferredContentSizeMedium:
      s_style = (UiStyle){
        .title = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
        .subtitle = fonts_get_system_font(FONT_KEY_GOTHIC_18),
        .header = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
        .title_height = 28,
        .subtitle_height = 22,
        .header_height = 18,
        .row_single = 44,
        .row_double = 54,
        .icon = 22,
        .photo = 60,
      };
      break;
    case PreferredContentSizeLarge:
      s_style = (UiStyle){
        .title = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
        .subtitle = fonts_get_system_font(FONT_KEY_GOTHIC_24),
        .header = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
        .title_height = 28,
        .subtitle_height = 28,
        .header_height = 22,
        .row_single = 50,
        .row_double = 60,
        .icon = 24,
        .photo = 84,
        .large = true,
      };
      break;
    default:
      s_style = (UiStyle){
        .title = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
        .subtitle = fonts_get_system_font(FONT_KEY_GOTHIC_28),
        .header = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
        .title_height = 34,
        .subtitle_height = 34,
        .header_height = 22,
        .row_single = 64,
        .row_double = 72,
        .icon = 28,
        .photo = 84,
        .large = true,
      };
      break;
  }
}

// Notifications have a text size of their own, which apps cannot read. Its default
// is the size each platform was designed for.
static PreferredContentSize notification_size(int32_t size) {
  if (size != UI_SIZE_SYSTEM) {
    return content_size(size);
  }
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  return PreferredContentSizeLarge;
#else
  return PreferredContentSizeMedium;
#endif
}

// Header, body and footer fonts of system notifications. Gothic 28 bold stands
// in for the Gothic 36 body of the largest size, which apps cannot load.
static void apply_body(int32_t size) {
  switch (notification_size(size)) {
    case PreferredContentSizeSmall:
      s_style.body_header = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      s_style.body = fonts_get_system_font(FONT_KEY_GOTHIC_18);
      s_style.body_footer = fonts_get_system_font(FONT_KEY_GOTHIC_14);
      s_style.body_padding = 3;
      break;
    case PreferredContentSizeMedium:
      s_style.body_header = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      s_style.body = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
      s_style.body_footer = fonts_get_system_font(PBL_IF_ROUND_ELSE(FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_18));
      s_style.body_padding = 3;
      break;
    case PreferredContentSizeLarge:
      s_style.body_header = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
      s_style.body = fonts_get_system_font(FONT_KEY_GOTHIC_28);
      s_style.body_footer = fonts_get_system_font(FONT_KEY_GOTHIC_18);
      s_style.body_padding = 0;
      break;
    default:
      s_style.body_header = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
      s_style.body = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
      s_style.body_footer = fonts_get_system_font(FONT_KEY_GOTHIC_24);
      s_style.body_padding = 0;
      break;
  }
}

static int32_t stored(uint32_t key) {
  return persist_exists(key) ? persist_read_int(key) : UI_SIZE_SYSTEM;
}

// The menu style replaces the whole struct, so the body comes after it.
void ui_init(void) {
  apply(stored(PERSIST_SIZE));
  apply_body(stored(PERSIST_BODY_SIZE));
}

bool ui_set_sizes(int32_t menu, int32_t body) {
  if (menu == stored(PERSIST_SIZE) && body == stored(PERSIST_BODY_SIZE)) {
    return false;
  }
  persist_write_int(PERSIST_SIZE, menu);
  persist_write_int(PERSIST_BODY_SIZE, body);
  ui_init();
  return true;
}

// Touch faults third-party apps on firmware older than 4.33.2.
bool ui_touch_supported(void) {
#ifdef PBL_TOUCH
  WatchInfoVersion version = watch_info_get_firmware_version();
  bool fixed = version.major > 4 || (version.major == 4 && version.minor > 33) ||
               (version.major == 4 && version.minor == 33 && version.patch >= 2);
  return fixed && touch_service_is_enabled();
#else
  return false;
#endif
}

const UiStyle *ui_style(void) {
  return &s_style;
}

MenuLayer *ui_menu_create(Window *window, GRect frame, MenuLayerCallbacks callbacks, void *context) {
  MenuLayer *menu = menu_layer_create(frame);
  if (!menu) {
    return NULL;
  }
  menu_layer_set_callbacks(menu, context, callbacks);
  menu_layer_set_highlight_colors(menu, UI_ACCENT, GColorWhite);
  menu_layer_set_click_config_onto_window(menu, window);
  return menu;
}

static void remove_window(void *window) {
  // Gone already if back was pressed in the meantime.
  if (window_stack_contains_window(window)) {
    window_stack_remove(window, false);
  }
}

void ui_close_soon(Window *window) {
  // A window cannot leave the stack while it loads.
  app_timer_register(0, remove_window, window);
}

bool ui_menu_attach(Window *window, MenuLayer *menu) {
  if (!menu) {
    ui_close_soon(window);
    return false;
  }
  layer_add_child(window_get_root_layer(window), menu_layer_get_layer(menu));
  return true;
}

int16_t ui_row_height(MenuLayer *menu, MenuIndex *index, bool two_lines) {
#ifdef PBL_ROUND
  if (menu_layer_is_index_selected(menu, index)) {
    return two_lines ? s_style.row_double + 12 : s_style.row_single + 12;
  }
  return s_style.title_height + 6;
#else
  return two_lines ? s_style.row_double : s_style.row_single;
#endif
}

void ui_draw_header(GContext *ctx, const Layer *cell_layer, const char *text) {
  GRect bounds = layer_get_bounds(cell_layer);
  int16_t inset = PBL_IF_ROUND_ELSE(bounds.size.w / 5, 5);
  graphics_context_set_text_color(ctx, UI_ACCENT);
  graphics_draw_text(ctx, text, s_style.header, GRect(inset, -2, bounds.size.w - inset * 2, bounds.size.h),
                     GTextOverflowModeTrailingEllipsis,
                     PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft), NULL);
}

static void draw_row(GContext *ctx, const Layer *cell_layer, const char *title, const char *subtitle,
                     IconDrawer icon, GColor tint, bool marker) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  bool show_subtitle = subtitle && subtitle[0];
#ifdef PBL_ROUND
  GTextAlignment alignment = GTextAlignmentCenter;
  int16_t inset = 14;
  show_subtitle = show_subtitle && highlighted;
#else
  GTextAlignment alignment = GTextAlignmentLeft;
  int16_t inset = 6;
#endif
  GColor ink = highlighted ? GColorWhite : UI_ACCENT;
  // Room the icon or the marker takes before the title.
  int16_t lead = icon ? s_style.icon + 8 : (marker ? 14 : 0);
  int16_t left = inset + lead;
  int16_t width = bounds.size.w - left - inset;
  GRect subtitle_box = GRect(left, 0, width, s_style.subtitle_height);
#ifdef PBL_ROUND
  subtitle_box = GRect(inset, 0, bounds.size.w - inset * 2, s_style.subtitle_height);
  // As the system cells do on round, the icon and the text form one centred
  // block: an icon pinned to the edge would fall outside the circle.
  if (lead) {
    int16_t used = graphics_text_layout_get_content_size(title, s_style.title,
                                                         GRect(0, 0, width, s_style.title_height),
                                                         GTextOverflowModeTrailingEllipsis,
                                                         GTextAlignmentLeft).w;
    if (show_subtitle) {
      int16_t below = graphics_text_layout_get_content_size(
          subtitle, s_style.subtitle, GRect(0, 0, width, s_style.subtitle_height),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).w;
      used = below > used ? below : used;
    }
    left = (bounds.size.w - lead - used) / 2 + lead;
    width = bounds.size.w - left - inset;
    subtitle_box = GRect(left, 0, width, s_style.subtitle_height);
    alignment = GTextAlignmentLeft;
  }
#endif
  int16_t block = s_style.title_height + (show_subtitle ? s_style.subtitle_height - 4 : 0);
  // System fonts sit a few pixels below the top of their box.
  int16_t y = (bounds.size.h - block) / 2 - (s_style.title_height > 28 ? 4 : 3);
  graphics_context_set_fill_color(ctx, ink);
  graphics_context_set_stroke_color(ctx, ink);
  if (icon) {
    int16_t size = s_style.icon;
    icons_set_color(highlighted || gcolor_equal(tint, GColorClear) ? ink : PBL_IF_COLOR_ELSE(tint, ink));
    icon(ctx, GRect(left - lead, (bounds.size.h - size) / 2, size, size));
  } else if (marker) {
    // On the title line: a centred subtitle can run under it.
    graphics_fill_circle(ctx, GPoint(left - lead + 4, y + s_style.title_height / 2 + 3), 4);
  }
  graphics_draw_text(ctx, title, s_style.title, GRect(left, y, width, s_style.title_height),
                     GTextOverflowModeTrailingEllipsis, alignment, NULL);
  if (show_subtitle) {
    subtitle_box.origin.y = y + s_style.title_height - 4;
    graphics_draw_text(ctx, subtitle, s_style.subtitle, subtitle_box, GTextOverflowModeTrailingEllipsis,
                       alignment, NULL);
  }
}

void ui_draw_row(GContext *ctx, const Layer *cell_layer, const char *title, const char *subtitle,
                 IconDrawer icon, bool marker) {
  draw_row(ctx, cell_layer, title, subtitle, icon, GColorClear, marker);
}

void ui_draw_tinted_row(GContext *ctx, const Layer *cell_layer, const char *title,
                        const char *subtitle, IconDrawer icon, GColor tint) {
  draw_row(ctx, cell_layer, title, subtitle, icon, tint, false);
}

void ui_draw_notice(GContext *ctx, GRect bounds, const char *text) {
  int16_t inset = PBL_IF_ROUND_ELSE(24, 8);
  GRect box = GRect(inset, 0, bounds.size.w - inset * 2, bounds.size.h);
  GSize size = graphics_text_layout_get_content_size(text, s_style.title, box,
                                                     GTextOverflowModeWordWrap, GTextAlignmentCenter);
  box.origin.y = (bounds.size.h - size.h) / 2 - 4;
  box.size.h = size.h + 8;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, text, s_style.title, box, GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

void ui_format_time(uint32_t timestamp, char *buffer, size_t size) {
  if (timestamp == 0) {
    buffer[0] = '\0';
    return;
  }
  time_t then = (time_t)timestamp;
  time_t now = time(NULL);
  struct tm then_tm = *localtime(&then);
  struct tm *now_tm = localtime(&now);
  char clock[12];
  if (clock_is_24h_style()) {
    strftime(clock, sizeof(clock), "%H:%M", &then_tm);
  } else {
    // "2:27 PM", as the system clock writes it.
    char half[4];
    strftime(half, sizeof(half), "%p", &then_tm);
    int hour = then_tm.tm_hour % 12;
    snprintf(clock, sizeof(clock), "%d:%02d %s", hour ? hour : 12, then_tm.tm_min, half);
  }
  if (then_tm.tm_year == now_tm->tm_year && then_tm.tm_yday == now_tm->tm_yday) {
    snprintf(buffer, size, "%s", clock);
  } else if (now - then < 6 * SECONDS_PER_DAY) {
    char day[8];
    strftime(day, sizeof(day), "%a", &then_tm);
    snprintf(buffer, size, "%s %s", day, clock);
  } else {
    strftime(buffer, size, "%d/%m", &then_tm);
  }
}

void ui_format_duration(int32_t seconds, char *buffer, size_t size) {
  if (seconds < 0) {
    seconds = 0;
  }
  int hours = seconds / 3600;
  int minutes = (seconds / 60) % 60;
  if (hours > 0) {
    snprintf(buffer, size, "%d:%02d:%02d", hours, minutes, (int)(seconds % 60));
  } else {
    snprintf(buffer, size, "%d:%02d", minutes, (int)(seconds % 60));
  }
}

// Copies the UTF-8 character at source, uppercased for ASCII and Latin-1 letters.
size_t ui_utf8_length(const char *text) {
  unsigned char lead = (unsigned char)text[0];
  size_t wanted = lead < 0x80 ? 1 : lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : 2;
  size_t length = 1;
  // A sequence cut short ends at the first byte that does not continue it, the NUL included.
  while (length < wanted && ((unsigned char)text[length] & 0xc0) == 0x80) {
    length++;
  }
  return length;
}

void ui_copy_text(char *buffer, const char *text, size_t size) {
  size_t written = 0;
  const char *cursor = text ? text : "";
  while (*cursor) {
    size_t length = ui_utf8_length(cursor);
    if (written + length >= size) {
      break;
    }
    memcpy(buffer + written, cursor, length);
    written += length;
    cursor += length;
  }
  buffer[written] = '\0';
}

static size_t copy_upper(const char *source, char *out) {
  unsigned char lead = (unsigned char)source[0];
  if (lead < 0x80) {
    out[0] = (lead >= 'a' && lead <= 'z') ? lead - 32 : lead;
    return 1;
  }
  size_t length = ui_utf8_length(source);
  memcpy(out, source, length);
  // é -> É and the rest of the Latin-1 lowercase block, except ÷.
  if (lead == 0xc3 && (unsigned char)source[1] >= 0xa0 && (unsigned char)source[1] != 0xb7) {
    out[1] = source[1] - 0x20;
  }
  return length;
}

void ui_initials(const char *name, char *buffer, size_t size) {
  size_t written = 0;
  int taken = 0;
  bool word_start = true;
  for (const char *cursor = name; *cursor && taken < 2 && written + 4 < size; cursor++) {
    if (*cursor == ' ' || *cursor == '-') {
      word_start = true;
      continue;
    }
    if ((*cursor & 0xc0) == 0x80) {
      continue;
    }
    if (word_start) {
      written += copy_upper(cursor, buffer + written);
      taken++;
    }
    word_start = false;
  }
  buffer[written] = '\0';
}
