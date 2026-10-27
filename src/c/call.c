// SPDX-License-Identifier: AGPL-3.0-or-later
#include "call.h"
#include "cards_window.h"
#include "comm.h"
#include "icons.h"
#include "l10n.h"
#include "photo.h"
#include "ui.h"

#define CLOSE_DELAY_MS 2000

// Background colors of the system call screen.
#define COLOR_WAITING PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define COLOR_CONNECTED PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorWhite)
#define COLOR_ENDED PBL_IF_COLOR_ELSE(GColorRed, GColorWhite)

static struct {
  CallState state;
  char name[48];
  char number[32];
  int32_t flags;
  time_t connected_at;
  // Set when the phone refused or never answered the dial request.
  const char *error;
  // The phone placed the call but cannot follow it: the screen closes on its own.
  bool handed_off;
} s_call;

static void (*s_changed)(void);
static Window *s_window;
static Layer *s_canvas;
static StatusBarLayer *s_status_bar;
static AppTimer *s_close_timer;
static GBitmap *s_photo;
static PhotoRequest *s_photo_request;
static char s_photo_number[32];

// Digits compared from the end of two phone numbers: as many as a New Caledonian
// number has, so "+687 76.12.34" and "761234" match like Android's loose compare.
#define SAME_NUMBER_DIGITS 6

bool call_same_number(const char *a, const char *b) {
  const char *x = a + strlen(a);
  const char *y = b + strlen(b);
  int matched = 0;
  while (matched < SAME_NUMBER_DIGITS) {
    while (x > a && (x[-1] < '0' || x[-1] > '9')) {
      x--;
    }
    while (y > b && (y[-1] < '0' || y[-1] > '9')) {
      y--;
    }
    if (x == a || y == b) {
      // One ran out of digits: equal only if both did.
      return x == a && y == b && matched > 0;
    }
    if (*--x != *--y) {
      return false;
    }
    matched++;
  }
  return true;
}

static bool is_live(CallState state) {
  return state == CALL_DIALING || state == CALL_RINGING || state == CALL_ACTIVE ||
         state == CALL_HOLDING;
}

static bool shows_controls(void) {
  return is_live(s_call.state) && !s_call.error;
}

bool call_in_progress(void) {
  return is_live(s_call.state);
}

static const char *state_label(void) {
  if (s_call.error) {
    return s_call.error;
  }
  switch (s_call.state) {
    case CALL_DIALING:
      return tr(S_DIALING);
    case CALL_RINGING:
      return tr(S_RINGING);
    case CALL_ACTIVE:
      return tr(S_ACTIVE);
    case CALL_HOLDING:
      return tr(S_ON_HOLD);
    default:
      return tr(S_ENDED);
  }
}

static const char *route_label(int route) {
  switch (route) {
    case ROUTE_SPEAKER:
      return tr(S_SPEAKER);
    case ROUTE_BLUETOOTH:
      return tr(S_BLUETOOTH);
    case ROUTE_EARPIECE:
    case ROUTE_WIRED_HEADSET:
      return tr(S_EARPIECE);
    default:
      return NULL;
  }
}

static GColor background(void) {
  if (s_call.error || !is_live(s_call.state)) {
    return COLOR_ENDED;
  }
  return s_call.state == CALL_ACTIVE ? COLOR_CONNECTED : COLOR_WAITING;
}

// White on the green and red backgrounds, black on the grey one.
static GColor ink(void) {
  return gcolor_equal(background(), COLOR_WAITING) ? GColorBlack : PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
}

static void redraw(void);

static void close_callback(void *data) {
  s_close_timer = NULL;
  if (is_live(s_call.state) && !s_call.error && !s_call.handed_off) {
    return;
  }
  if (s_call.handed_off) {
    s_call.state = CALL_NONE;
    s_call.handed_off = false;
    redraw();
  }
  if (s_window && window_stack_get_top_window() == s_window) {
    window_stack_pop(true);
  }
}

static void schedule_close(void) {
  if (s_close_timer) {
    app_timer_reschedule(s_close_timer, CLOSE_DELAY_MS);
  } else {
    s_close_timer = app_timer_register(CLOSE_DELAY_MS, close_callback, NULL);
  }
}

static void drop_photo(void) {
  photo_cancel(s_photo_request);
  s_photo_request = NULL;
  if (s_photo) {
    gbitmap_destroy(s_photo);
    s_photo = NULL;
  }
  s_photo_number[0] = '\0';
}

static void photo_received(GBitmap *photo, void *context) {
  s_photo_request = NULL;
  s_photo = photo;
  if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

#ifdef PBL_ROUND
#define CONTENT_TOP (STATUS_BAR_LAYER_HEIGHT + 4)
// Text flush right against the bar stops where its right edge comes within
// 25 px of the circle: lower on Round than on Round 2, where it sits further
// from the centre.
#ifdef PBL_PLATFORM_CHALK
#define CONTENT_BOTTOM 20
#else
#define CONTENT_BOTTOM 44
#endif
#else
#define CONTENT_TOP (STATUS_BAR_LAYER_HEIGHT + 2)
#define CONTENT_BOTTOM 0
#endif

static int16_t status_height(const UiStyle *style) {
  return style->large ? 30 : 24;
}

static int16_t route_height(const UiStyle *style) {
  return PBL_IF_ROUND_ELSE(style->subtitle_height, status_height(style));
}

// One size for the whole call, fetched and drawn as is: room for a one-line
// name, the number, the state and the audio route.
static int16_t photo_size(void) {
  const UiStyle *style = ui_style();
  int16_t text = style->title_height + style->subtitle_height + status_height(style) +
                 route_height(style);
  int16_t room = PBL_DISPLAY_HEIGHT - CONTENT_TOP - CONTENT_BOTTOM - 4 - text;
  int16_t size = PBL_IF_ROUND_ELSE(PBL_DISPLAY_HEIGHT / 4, style->photo);
  size = size < style->photo ? size : style->photo;
  return room < size ? room : size;
}

// Under this size the layout has no room left for a photo.
static bool photo_shown(void) {
  return photo_size() > 24;
}

static void fetch_photo(void) {
  if (!s_window || !s_call.number[0]) {
    return;
  }
  if (!photo_shown()) {
    drop_photo();
    return;
  }
  // A favorite's card may hold the photo already: the 64 KB watches have no room
  // for a second copy.
  if (cards_photo(s_call.number, photo_size())) {
    drop_photo();
    return;
  }
  if (call_same_number(s_photo_number, s_call.number)) {
    return;
  }
  drop_photo();
  strncpy(s_photo_number, s_call.number, sizeof(s_photo_number) - 1);
  s_photo_request = photo_fetch(0, s_call.number, photo_size(), photo_received, NULL);
}

// The clock centers over what the action bar leaves, or over the whole width once it is gone.
static void place_status_bar(void) {
#ifndef PBL_ROUND
  GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
  int16_t bar = shows_controls() ? ACTION_BAR_WIDTH : 0;
  layer_set_frame(status_bar_layer_get_layer(s_status_bar),
                  GRect(0, 0, bounds.size.w - bar, STATUS_BAR_LAYER_HEIGHT));
#endif
}

static void redraw(void) {
  if (s_canvas) {
    fetch_photo();
    status_bar_layer_set_colors(s_status_bar, GColorClear, ink());
    place_status_bar();
    layer_mark_dirty(s_canvas);
  }
  if (s_changed) {
    s_changed();
  }
}

#ifdef PBL_ROUND
// The geometry of the system action bar on round: icons of at most 18 px, the
// up and down ones this far from the top and bottom edges.
#define ROUND_ICON 18
#ifdef PBL_PLATFORM_GABBRO
#define ROUND_ICON_MARGIN 80
#else
#define ROUND_ICON_MARGIN 53
#endif
#endif

// Centers of the up, select and down controls, in button order.
static void control_centers(GRect bounds, GPoint centers[3]) {
#ifdef PBL_ROUND
  int16_t x = bounds.size.w - ACTION_BAR_WIDTH / 2 - 1;
  centers[0] = GPoint(x, ROUND_ICON_MARGIN + ROUND_ICON / 2);
  centers[1] = GPoint(x, bounds.size.h / 2);
  centers[2] = GPoint(x, bounds.size.h - ROUND_ICON_MARGIN - ROUND_ICON / 2);
#else
  int16_t x = bounds.size.w - ACTION_BAR_WIDTH / 2;
  centers[0] = GPoint(x, bounds.size.h * 22 / 100);
  centers[1] = GPoint(x, bounds.size.h / 2);
  centers[2] = GPoint(x, bounds.size.h * 78 / 100);
#endif
}

static void draw_controls(GContext *ctx, GRect bounds) {
  bool full = !(s_call.flags & CALL_FLAG_HANGUP_ONLY);
  int16_t icon = PBL_IF_ROUND_ELSE(ROUND_ICON, ui_style()->icon);
  GPoint centers[3];
  control_centers(bounds, centers);
  GPoint up = centers[0];
  GPoint select = centers[1];
  GPoint down = centers[2];
  graphics_context_set_fill_color(ctx, GColorBlack);
#ifdef PBL_ROUND
  // The bar is the edge of a disc far larger than the screen, as the system draws it.
  int16_t radius = bounds.size.h * 19 / 18;
  graphics_fill_circle(ctx, GPoint(bounds.size.w - ACTION_BAR_WIDTH + radius, bounds.size.h / 2), radius);
#else
  graphics_fill_rect(ctx, GRect(bounds.size.w - ACTION_BAR_WIDTH, 0, ACTION_BAR_WIDTH, bounds.size.h),
                     0, GCornerNone);
#endif
#define ICON_BOX(point) GRect((point).x - icon / 2, (point).y - icon / 2, icon, icon)

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  if (full) {
    switch (CALL_FLAG_ROUTE(s_call.flags)) {
      case ROUTE_SPEAKER:
        icons_speaker(ctx, ICON_BOX(up));
        break;
      case ROUTE_BLUETOOTH:
        icons_bluetooth(ctx, ICON_BOX(up));
        break;
      default:
        icons_smartphone(ctx, ICON_BOX(up));
        break;
    }
    if (s_call.flags & CALL_FLAG_MUTED) {
      icons_mic_off(ctx, ICON_BOX(select));
    } else {
      icons_mic(ctx, ICON_BOX(select));
    }
  }
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
  icons_hang_up(ctx, ICON_BOX(down));
#undef ICON_BOX
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const UiStyle *style = ui_style();
  graphics_context_set_fill_color(ctx, background());
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  bool controls = shows_controls();
  if (controls) {
    draw_controls(ctx, bounds);
  }

#ifdef PBL_ROUND
  // As the system call screen: flush right against the action bar, centred once it is gone.
  GTextAlignment align = controls ? GTextAlignmentRight : GTextAlignmentCenter;
  int16_t inset = 10;
  int16_t width = bounds.size.w - inset * 2 - (controls ? ACTION_BAR_WIDTH + 18 - inset : 0);
#else
  GTextAlignment align = GTextAlignmentCenter;
  int16_t inset = 4;
  int16_t width = bounds.size.w - (controls ? ACTION_BAR_WIDTH : 0) - inset * 2;
#endif
  GFont status_font = fonts_get_system_font(style->large ? FONT_KEY_GOTHIC_24_BOLD
                                                          : FONT_KEY_GOTHIC_18_BOLD);
  int16_t status = status_height(style);

  const char *name = s_call.name[0] ? s_call.name : s_call.number;
  bool show_number = s_call.name[0] && s_call.number[0];
  GRect name_box = GRect(inset, 0, width, style->title_height * 2);
  GSize name_size = graphics_text_layout_get_content_size(
      name, style->title, name_box, GTextOverflowModeTrailingEllipsis, align);

  int16_t top = CONTENT_TOP;
  int16_t height = bounds.size.h - CONTENT_TOP - CONTENT_BOTTOM;
  // On rectangles the audio route row keeps its room once the call is over, so
  // nothing moves; round screens use it for the photo.
  bool route_row = PBL_IF_ROUND_ELSE(controls, true);
  int16_t text_block = name_size.h + (show_number ? style->subtitle_height : 0) + status +
                       (route_row ? route_height(style) : 0);
  int16_t photo = photo_size();
  // A name on two lines pushes the text down rather than shrink the photo,
  // which would cut it: a bitmap is drawn at its own size.
  int16_t spare = height - photo - 4 - text_block;
  int16_t y = top + (spare > 0 ? spare / 2 : 0);
  if (photo_shown()) {
    int16_t photo_x = align == GTextAlignmentRight ? inset + width - photo : inset + (width - photo) / 2;
    GBitmap *shared = cards_photo(s_call.number, photo);
    avatar_draw(ctx, GRect(photo_x, y, photo, photo), shared ? shared : s_photo, s_call.name);
    y += photo + 4;
  }

  graphics_context_set_text_color(ctx, ink());
  name_box.origin.y = y - 4;
  graphics_draw_text(ctx, name, style->title, name_box, GTextOverflowModeTrailingEllipsis,
                     align, NULL);
  y += name_size.h;
  if (show_number) {
    graphics_draw_text(ctx, s_call.number, style->subtitle,
                       GRect(inset, y - 4, width, style->subtitle_height),
                       GTextOverflowModeTrailingEllipsis, align, NULL);
    y += style->subtitle_height;
  }

  char line[48];
  if (s_call.state == CALL_ACTIVE && !s_call.error) {
    ui_format_duration(time(NULL) - s_call.connected_at, line, sizeof(line));
  } else {
    snprintf(line, sizeof(line), "%s", state_label());
  }
  graphics_draw_text(ctx, line, status_font, GRect(inset, y - 2, width, status),
                     GTextOverflowModeTrailingEllipsis, align, NULL);
  y += status;

  if (controls) {
    const char *route = route_label(CALL_FLAG_ROUTE(s_call.flags));
    bool muted = s_call.flags & CALL_FLAG_MUTED;
    snprintf(line, sizeof(line), "%s%s%s", route ? route : "", route && muted ? " · " : "",
             muted ? tr(S_MUTED) : "");
    graphics_draw_text(ctx, line, style->subtitle, GRect(inset, y - 4, width, style->subtitle_height),
                       GTextOverflowModeTrailingEllipsis, align, NULL);
  }
}

static void send_simple(uint8_t cmd) {
  comm_send(&(CommRequest){ .cmd = cmd }, NULL, NULL);
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  if (shows_controls() && !(s_call.flags & CALL_FLAG_HANGUP_ONLY)) {
    send_simple(CMD_AUDIO_ROUTE);
  }
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  if (shows_controls() && !(s_call.flags & CALL_FLAG_HANGUP_ONLY)) {
    send_simple(CMD_MUTE);
  }
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  if (shows_controls()) {
    send_simple(CMD_HANG_UP);
  }
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
}

#ifdef PBL_TOUCH
static int s_touched = -1;

// Index of the control under the finger, -1 elsewhere.
static int control_at(int16_t x, int16_t y) {
  if (!shows_controls()) {
    return -1;
  }
  GRect bounds = layer_get_bounds(s_canvas);
  GPoint centers[3];
  control_centers(bounds, centers);
  int first = (s_call.flags & CALL_FLAG_HANGUP_ONLY) ? 2 : 0;
  // The round bar bulges past its width at mid-height.
  if (x < bounds.size.w - ACTION_BAR_WIDTH - PBL_IF_ROUND_ELSE(8, 0)) {
    return -1;
  }
  // Each third of the height goes to the nearest control.
  int zone = 0;
  for (int i = 1; i < 3; i++) {
    if (abs(y - centers[i].y) < abs(y - centers[zone].y)) {
      zone = i;
    }
  }
  return zone >= first ? zone : -1;
}

// Only the controls answer a tap: the rest of the screen stays free for the
// double tap that lights the screen.
static void touch_handler(const TouchEvent *event, void *context) {
  int control = control_at(event->x, event->y);
  switch (event->type) {
    case TouchEvent_Touchdown:
      s_touched = control;
      break;
    case TouchEvent_PositionUpdate:
      if (control != s_touched) {
        s_touched = -1;
      }
      break;
    case TouchEvent_Liftoff:
      if (control >= 0 && control == s_touched) {
        ClickHandler handlers[] = { up_click, select_click, down_click };
        handlers[control](NULL, NULL);
      }
      s_touched = -1;
      break;
  }
}
#endif

// Subscribing to raw touch also stops the system from turning any tap into Select.
static void window_appear(Window *window) {
  // The cards may have let go of the photo while another window was on top.
  fetch_photo();
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
  s_touched = -1;
#endif
}

static void tick(struct tm *tick_time, TimeUnits changed) {
  if (s_call.state == CALL_ACTIVE && s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);

  s_status_bar = status_bar_layer_create();
  status_bar_layer_set_colors(s_status_bar, GColorClear, ink());
  place_status_bar();
  layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

  tick_timer_service_subscribe(SECOND_UNIT, tick);
  fetch_photo();
}

static void window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  status_bar_layer_destroy(s_status_bar);
  layer_destroy(s_canvas);
  s_canvas = NULL;
  drop_photo();
  window_destroy(s_window);
  s_window = NULL;
  if (s_close_timer) {
    app_timer_cancel(s_close_timer);
    s_close_timer = NULL;
  }
}

void call_window_push(void) {
  if (s_window) {
    return;
  }
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

void call_handle_state(DictionaryIterator *message) {
  CallState previous = s_call.state;
  s_call.state = (CallState)comm_get_int(message, MESSAGE_KEY_State, CALL_NONE);
  s_call.flags = comm_get_int(message, MESSAGE_KEY_Flags, 0);
  const char *name = comm_get_string(message, MESSAGE_KEY_Name);
  const char *number = comm_get_string(message, MESSAGE_KEY_Address);
  if (is_live(s_call.state)) {
    s_call.error = NULL;
    s_call.handed_off = false;
    if (!is_live(previous)) {
      // A new call: nothing of the last one carries over.
      s_call.name[0] = '\0';
      s_call.number[0] = '\0';
      if (s_close_timer) {
        app_timer_cancel(s_close_timer);
        s_close_timer = NULL;
      }
    }
    if (name && name[0]) {
      strncpy(s_call.name, name, sizeof(s_call.name) - 1);
    }
    // Keep the contact's own spelling of the number when Telecom reports the same one.
    if (number && number[0] && !call_same_number(number, s_call.number)) {
      strncpy(s_call.number, number, sizeof(s_call.number) - 1);
    }
  }
  if (s_call.state == CALL_ACTIVE) {
    s_call.connected_at = time(NULL) - comm_get_int(message, MESSAGE_KEY_Elapsed, 0);
    if (previous != CALL_ACTIVE && s_window) {
      vibes_short_pulse();
    }
  }
  if (!is_live(s_call.state) && is_live(previous)) {
    s_call.state = CALL_ENDED;
    schedule_close();
  }
  redraw();
}

// Number of the latest dial: a reply that comes after the next dial began is not its own.
static uint32_t s_dial;

static void dial_reply(DictionaryIterator *reply, void *context) {
  if ((uintptr_t)context != s_dial) {
    return;
  }
  int32_t status = reply ? comm_get_int(reply, MESSAGE_KEY_Status, STATUS_FAILED)
                         : STATUS_UNREACHABLE;
  if (status == STATUS_OK) {
    if (!(comm_get_int(reply, MESSAGE_KEY_Flags, 0) & DIAL_FOLLOWED)) {
      s_call.handed_off = true;
      schedule_close();
    }
    return;
  }
  s_call.state = CALL_ENDED;
  s_call.error = status == STATUS_NO_PERMISSION ? tr(S_NO_PERMISSION)
                 : status == STATUS_UNREACHABLE ? tr(S_UNREACHABLE)
                                                : tr(S_FAILED);
  schedule_close();
  redraw();
}

void call_dial(const char *name, const char *number) {
  memset(&s_call, 0, sizeof(s_call));
  strncpy(s_call.name, name ? name : "", sizeof(s_call.name) - 1);
  strncpy(s_call.number, number, sizeof(s_call.number) - 1);
  s_call.state = CALL_DIALING;
  s_dial++;
  if (!comm_send(&(CommRequest){ .cmd = CMD_DIAL, .address = number }, dial_reply,
                 (void *)(uintptr_t)s_dial)) {
    s_call.state = CALL_ENDED;
    s_call.error = tr(S_FAILED);
  }
  call_window_push();
  if (s_call.error) {
    schedule_close();
  }
  redraw();
}

void call_init(void (*changed)(void)) {
  s_changed = changed;
}

void call_deinit(void) {
}
