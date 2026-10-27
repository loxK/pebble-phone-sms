// SPDX-License-Identifier: AGPL-3.0-or-later
#include "compose.h"
#include "comm.h"
#include "l10n.h"
#include "protocol.h"
#include "ui.h"

#define DICTATION_BUFFER 512

// Geometry and timings of the system progress window, so a text leaves the
// watch the way a notification reply does.
#define BAR_WIDTH 80
#define BAR_HEIGHT PBL_IF_COLOR_ELSE(6, 7)
#define BAR_RADIUS PBL_IF_COLOR_ELSE(2, 3)
#define FAKE_STEP_MS 200
#define FAKE_STEP 2
#define FAKE_MAX 80
#define BAR_TO_TRANS_MS 160
#define TRANS_TO_DOT_MS 90
#define DOT_TRANSITION_RADIUS 13
#define DOT_RADIUS 7
#define DOT_OFFSET 25
#define SHRINK_MS 250
#define FRAME_MS 33
#define SENT_HOLD_MS 1000
#define FAILED_HOLD_MS 2500
#define ICON_TEXT_GAP PBL_IF_ROUND_ELSE(2, 4)

typedef enum {
  PHASE_PROGRESS,
  PHASE_TO_DOT,
  PHASE_SENT,
  PHASE_SHRINK,
  PHASE_FAILED,
} Phase;

static char s_address[32];
static int32_t s_thread;
static uint16_t s_sent_count;

static Window *s_window;
static Layer *s_canvas;
static AppTimer *s_timer;
static Phase s_phase;
static uint32_t s_phase_start;
static int16_t s_percent;
static const char *s_message;
static GDrawCommandSequence *s_sent;
static GDrawCommandImage *s_warning;

#ifdef PBL_MICROPHONE
static DictationSession *s_session;
static DictationHandler s_handler;
static void *s_handler_context;
#endif

static uint32_t now_ms(void) {
  time_t seconds;
  uint16_t millis;
  time_ms(&seconds, &millis);
  return (uint32_t)seconds * 1000 + millis;
}

static uint32_t elapsed(void) {
  return now_ms() - s_phase_start;
}

static void set_phase(Phase phase) {
  s_phase = phase;
  s_phase_start = now_ms();
}

static void schedule(uint32_t delay_ms);

static int16_t lerp(int16_t from, int16_t to, int32_t num, int32_t den) {
  return from + (to - from) * num / den;
}

static GRect lerp_rect(GRect from, GRect to, int32_t num, int32_t den) {
  return GRect(lerp(from.origin.x, to.origin.x, num, den), lerp(from.origin.y, to.origin.y, num, den),
               lerp(from.size.w, to.size.w, num, den), lerp(from.size.h, to.size.h, num, den));
}

static void fill_round(GContext *ctx, GRect rect, uint16_t radius) {
  uint16_t half = (rect.size.w < rect.size.h ? rect.size.w : rect.size.h) / 2;
  graphics_fill_rect(ctx, rect, radius < half ? radius : half, GCornersAll);
}

static GRect bar_frame(GRect bounds) {
  GPoint center = grect_center_point(&bounds);
  return GRect(center.x - BAR_WIDTH / 2, center.y - BAR_HEIGHT / 2, BAR_WIDTH, BAR_HEIGHT);
}

static void draw_bar(GContext *ctx, GRect bar, int16_t width) {
  GRect frame = GRect(bar.origin.x, bar.origin.y, width, bar.size.h);
  GRect fill = GRect(bar.origin.x, bar.origin.y, BAR_WIDTH * s_percent / 100, bar.size.h);
  if (fill.size.w > width) {
    fill.size.w = width;
  }
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite));
  fill_round(ctx, frame, BAR_RADIUS);
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  fill_round(ctx, fill, BAR_RADIUS);
#ifndef PBL_COLOR
  if (width > 0) {
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_round_rect(ctx, frame, BAR_RADIUS);
  }
#endif
}

// The bar swells into a dot, then the dot shrinks: ease-in, then linear.
static void draw_dot(GContext *ctx, GRect bar) {
  GRect beg = GRect(0, 0, BAR_WIDTH, BAR_HEIGHT);
  GRect mid = GRect(DOT_OFFSET - DOT_TRANSITION_RADIUS + 2, BAR_HEIGHT - DOT_TRANSITION_RADIUS + 1,
                    DOT_TRANSITION_RADIUS * 2, DOT_TRANSITION_RADIUS * 2);
  GRect end = GRect(DOT_OFFSET - DOT_RADIUS - 1, BAR_HEIGHT - DOT_RADIUS - 2, DOT_RADIUS * 2,
                    DOT_RADIUS * 2);
  int32_t t = elapsed();
  GRect rect;
  if (t < BAR_TO_TRANS_MS) {
    rect = lerp_rect(beg, mid, t * t, BAR_TO_TRANS_MS * BAR_TO_TRANS_MS);
  } else {
    t -= BAR_TO_TRANS_MS;
    rect = lerp_rect(mid, end, t < TRANS_TO_DOT_MS ? t : TRANS_TO_DOT_MS, TRANS_TO_DOT_MS);
  }
  rect.origin.x += bar.origin.x;
  rect.origin.y += bar.origin.y;
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  fill_round(ctx, rect, DOT_TRANSITION_RADIUS);
}

// Icon above a centered caption, the block centered on screen like a system dialog.
static void draw_result(GContext *ctx, GRect bounds, GSize icon_size, const char *text) {
  GFont font = ui_style()->title;
  int16_t inset = PBL_IF_ROUND_ELSE(24, 6);
  GRect text_box = GRect(inset, 0, bounds.size.w - inset * 2, bounds.size.h);
  GSize text_size = graphics_text_layout_get_content_size(text, font, text_box,
                                                          GTextOverflowModeWordWrap,
                                                          GTextAlignmentCenter);
  int16_t top = (bounds.size.h - icon_size.h - ICON_TEXT_GAP - text_size.h) / 2;
  GPoint origin = GPoint((bounds.size.w - icon_size.w) / 2, top);

  if (s_phase == PHASE_SENT && s_sent) {
    uint32_t t = elapsed();
    uint32_t total = gdraw_command_sequence_get_total_duration(s_sent);
    GDrawCommandFrame *frame = gdraw_command_sequence_get_frame_by_elapsed(s_sent, t < total ? t : total);
    if (frame) {
      gdraw_command_frame_draw(ctx, s_sent, frame, origin);
    }
  } else if (s_phase == PHASE_FAILED && s_warning) {
    gdraw_command_image_draw(ctx, s_warning, origin);
  }

  text_box.origin.y = top + icon_size.h + ICON_TEXT_GAP;
  text_box.size.h = text_size.h + 4;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, text, font, text_box, GTextOverflowModeWordWrap, GTextAlignmentCenter,
                     NULL);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GRect bar = bar_frame(bounds);
  switch (s_phase) {
    case PHASE_PROGRESS:
      draw_bar(ctx, bar, BAR_WIDTH);
      break;
    case PHASE_TO_DOT:
      draw_dot(ctx, bar);
      break;
    case PHASE_SHRINK: {
      int32_t t = elapsed();
      int32_t left = SHRINK_MS - (t < SHRINK_MS ? t : SHRINK_MS);
      draw_bar(ctx, bar, BAR_WIDTH * left * left / (SHRINK_MS * SHRINK_MS));
      break;
    }
    case PHASE_SENT:
      draw_result(ctx, bounds,
                  s_sent ? gdraw_command_sequence_get_bounds_size(s_sent) : GSizeZero, tr(S_SENT));
      break;
    case PHASE_FAILED:
      draw_result(ctx, bounds, s_warning ? gdraw_command_image_get_bounds_size(s_warning) : GSizeZero,
                  s_message);
      break;
  }
}

static void tick(void *data) {
  s_timer = NULL;
  uint32_t t = elapsed();
  switch (s_phase) {
    case PHASE_PROGRESS:
      s_percent += FAKE_STEP;
      if (s_percent < FAKE_MAX) {
        schedule(FAKE_STEP_MS);
      }
      break;
    case PHASE_TO_DOT:
      if (t < BAR_TO_TRANS_MS + TRANS_TO_DOT_MS) {
        schedule(FRAME_MS);
        break;
      }
      s_sent = gdraw_command_sequence_create_with_resource(RESOURCE_ID_SENT_ANIMATION);
      set_phase(PHASE_SENT);
      schedule(FRAME_MS);
      break;
    case PHASE_SENT: {
      uint32_t total = s_sent ? gdraw_command_sequence_get_total_duration(s_sent) : 0;
      if (t < total) {
        schedule(FRAME_MS);
      } else if (t < total + SENT_HOLD_MS) {
        schedule(total + SENT_HOLD_MS - t);
      } else {
        window_stack_remove(s_window, true);
        return;
      }
      break;
    }
    case PHASE_SHRINK:
      if (t < SHRINK_MS) {
        schedule(FRAME_MS);
        break;
      }
      s_warning = gdraw_command_image_create_with_resource(RESOURCE_ID_WARNING_ICON);
      set_phase(PHASE_FAILED);
      schedule(FAILED_HOLD_MS);
      break;
    case PHASE_FAILED:
      window_stack_remove(s_window, true);
      return;
  }
  layer_mark_dirty(s_canvas);
}

static void schedule(uint32_t delay_ms) {
  if (s_timer) {
    app_timer_cancel(s_timer);
  }
  s_timer = app_timer_register(delay_ms, tick, NULL);
}

static void set_recipient(int32_t thread, const char *address) {
  s_thread = thread;
  strncpy(s_address, address, sizeof(s_address) - 1);
  s_address[sizeof(s_address) - 1] = '\0';
}

// Back waits for the outcome: leaving earlier would hide whether the text went out.
static void back_click(ClickRecognizerRef recognizer, void *context) {
  if (s_phase == PHASE_SENT || s_phase == PHASE_FAILED) {
    window_stack_remove(s_window, true);
  }
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  window_set_background_color(window, PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite));
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
  s_percent = 0;
  set_phase(PHASE_PROGRESS);
  schedule(FAKE_STEP_MS);
}

static void window_unload(Window *window) {
  comm_cancel(window);
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  if (s_sent) {
    gdraw_command_sequence_destroy(s_sent);
    s_sent = NULL;
  }
  if (s_warning) {
    gdraw_command_image_destroy(s_warning);
    s_warning = NULL;
  }
  layer_destroy(s_canvas);
  window_destroy(s_window);
  s_window = NULL;
}

static void sms_reply(DictionaryIterator *reply, void *context) {
  int32_t status = reply ? comm_get_int(reply, MESSAGE_KEY_Status, STATUS_FAILED)
                         : STATUS_UNREACHABLE;
  // A pending text lands in the conversation all the same.
  if (status == STATUS_OK || status == STATUS_PENDING) {
    s_sent_count++;
  }
  if (!s_window) {
    return;
  }
  if (status == STATUS_OK) {
    s_percent = 100;
    set_phase(PHASE_TO_DOT);
    vibes_short_pulse();
  } else {
    s_message = status == STATUS_NO_PERMISSION ? tr(S_NO_PERMISSION)
                : status == STATUS_UNREACHABLE ? tr(S_UNREACHABLE)
                : status == STATUS_PENDING     ? tr(S_SEND_PENDING)
                                               : tr(S_SEND_FAILED);
    set_phase(PHASE_SHRINK);
    // Not a failure: no second pulse that would invite a second send.
    if (status == STATUS_PENDING) {
      vibes_short_pulse();
    } else {
      vibes_double_pulse();
    }
  }
  schedule(FRAME_MS);
  layer_mark_dirty(s_canvas);
}

static void send_to_recipient(const char *text) {
  if (s_window) {
    return;
  }
  s_window = window_create();
  // Without the window that tells how it went, the text does not leave.
  if (!s_window) {
    return;
  }
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  CommRequest request = { .cmd = CMD_SEND_SMS, .id = s_thread, .text = text, .address = s_address };
  if (!comm_send(&request, sms_reply, s_window)) {
    sms_reply(NULL, s_window);
  }
}
void compose_send(int32_t thread, const char *address, const char *text) {
  set_recipient(thread, address);
  send_to_recipient(text);
}

#ifdef PBL_MICROPHONE
static void dictation_callback(DictationSession *session, DictationSessionStatus status,
                               char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess && transcription && transcription[0] && s_handler) {
    s_handler(transcription, s_handler_context);
  }
}

static void dictated_sms(const char *text, void *context) {
  send_to_recipient(text);
}
#endif

uint16_t compose_sent_count(void) {
  return s_sent_count;
}

bool compose_can_reply(const char *address) {
  if (!address || !address[0]) {
    return false;
  }
  for (const char *cursor = address; *cursor; cursor++) {
    char c = *cursor;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (uint8_t)c >= 0x80) {
      return false;
    }
  }
  return true;
}

bool compose_available(void) {
  return PBL_IF_MICROPHONE_ELSE(true, false);
}

void compose_dictate(bool confirm, DictationHandler handler, void *context) {
#ifdef PBL_MICROPHONE
  if (!s_session) {
    s_session = dictation_session_create(DICTATION_BUFFER, dictation_callback, NULL);
    if (!s_session) {
      return;
    }
  }
  s_handler = handler;
  s_handler_context = context;
  dictation_session_enable_confirmation(s_session, confirm);
  dictation_session_start(s_session);
#endif
}

void compose_sms(int32_t thread, const char *address) {
#ifdef PBL_MICROPHONE
  if (s_window) {
    return;
  }
  set_recipient(thread, address);
  compose_dictate(true, dictated_sms, NULL);
#endif
}

void compose_deinit(void) {
#ifdef PBL_MICROPHONE
  if (s_session) {
    dictation_session_destroy(s_session);
  }
#endif
}
