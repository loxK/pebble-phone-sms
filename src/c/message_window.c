// SPDX-License-Identifier: AGPL-3.0-or-later
#include "message_window.h"
#include "comm.h"
#include "l10n.h"
#include "photo.h"
#include "protocol.h"
#include "reply.h"
#include "ui.h"

// Card layout of the system notifications (notification_layout.h in the firmware),
// colored like the Pebble app colors Google Messages notifications.
#define BANNER_COLOR PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorLightGray)
#define ICON_SIZE 25
#define CARD_MARGIN PBL_IF_ROUND_ELSE(12, 10)
#define CARD_BOTTOM_PADDING 18
#define FOOTER_PADDING PBL_IF_ROUND_ELSE(6, 3)
#define BODY_PADDING 2
#define TEXT_FLOW_INSET 8
#define ARROW_HEIGHT PBL_IF_ROUND_ELSE(16, 19)
#ifdef PBL_ROUND
// A disc whose bottom edge closes the 60 px banner, status bar included.
#define BANNER_HEIGHT (60 - STATUS_BAR_LAYER_HEIGHT)
#define BANNER_RADIUS 140
#define ICON_TOP ((BANNER_HEIGHT - ICON_SIZE) / 2 - 2)
#elif defined(PBL_PLATFORM_EMERY)
#define BANNER_HEIGHT 29
#define ICON_TOP ((BANNER_HEIGHT - ICON_SIZE) / 2 - 2)
#else
#define BANNER_HEIGHT 36
#define ICON_TOP ((BANNER_HEIGHT - ICON_SIZE) / 2 - 1)
#endif

// Scrolling and swapping of the system notification window (swap_layer.c).
#define SCROLL_MS 200
#define SWAP_MS PBL_IF_ROUND_ELSE(150, 200)
#define SCROLL_PX PBL_IF_ROUND_ELSE(page_height(view), 48)
#define REPEATING_SCROLL_PX 24
#define INITIAL_SCROLL_PX PBL_IF_ROUND_ELSE(page_height(view), BANNER_HEIGHT)
// How much of the next message shows under the end of the current one.
#define PEEK_PX PBL_IF_ROUND_ELSE(0, BANNER_HEIGHT)
#define FUDGE_PX PEEK_PX
#define SCROLL_REPEAT_MS 200
// Held buttons pause this many repeats at the edge of a message before moving on.
#define SWAP_DELAY 3
#define DRAG_THRESHOLD_PX 10
#define SWAP_OVERPULL_PX 40

// Pictures in the watch's own pixel format: one byte a pixel in color, one bit in black and
// white. Only the card on screen holds one, sized for what the heap of each platform spares.
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
#define PICTURE_MAX_WIDTH 180
#define PICTURE_MAX_PIXELS (180 * 180)
#elif defined(PBL_COLOR)
#define PICTURE_MAX_WIDTH 124
#define PICTURE_MAX_PIXELS (96 * 96)
#else
#define PICTURE_MAX_WIDTH 124
#define PICTURE_MAX_PIXELS (124 * 160)
#endif
#define PICTURE_MAX_HEIGHT (PICTURE_MAX_WIDTH * 2)
#define PICTURE_MIN_SIDE 8
// Heap left over once a picture is in, for the rows and messages still to load.
#define PICTURE_HEAP_SPARE 8192
// The sender line measures to its baseline: the gap keeps its descenders off the picture.
#define PICTURE_MARGIN 8

typedef struct MessageView MessageView;
typedef struct Card Card;

// Every move shifts a few cards by the same distance. Their targets are kept so that a move
// cut short, or stopped before its first frame, when the firmware calls no handler at all,
// still lands where it was going.
typedef struct {
  Card *card;
  int16_t to_y;
} Move;

struct Card {
  MessageView *view;
  Layer *layer;
  int16_t index;
  TextLayer *app_name;
  TextLayer *sender;
  TextLayer *body;
  TextLayer *time;
  // Reserved at the picture's final size from the start, so the card never changes height.
  Layer *picture_layer;
  GBitmap *picture;
  PhotoRequest *picture_request;
  int32_t message_id;
  // Its full text came in during a move: it is laid out again once the move lands.
  bool needs_layout;
  char sender_text[48];
  char time_text[16];
  char *text;
};

struct MessageView {
  Window *window;
  StatusBarLayer *status_bar;
  // Holds the cards, which move inside it like the layouts of a swap layer.
  Layer *viewport;
  Layer *arrow;
  GBitmap *arrow_bitmap;
  Layer *action_button;
  GDrawCommandImage *icon;
  Card *previous;
  Card *current;
  Card *next;
  Animation *animation;
  Move moves[3];
  uint8_t move_count;
  AnimationStoppedHandler move_done;
  int8_t swap_delay;
  int16_t first_index;
  MessageSource source;
  int32_t thread;
  char thread_name[48];
  char address[32];
#ifdef PBL_TOUCH
  GPoint touch_start;
  int16_t touch_base;
  bool dragging;
  // The tap that opened this window ends here too: the firmware hands its liftoff to the
  // window that subscribed meanwhile. Only gestures that started here count.
  bool touching;
#endif
};

static int16_t viewport_height(const MessageView *view) {
  return layer_get_frame(view->viewport).size.h;
}

#ifdef PBL_ROUND
static int16_t page_height(const MessageView *view) {
  return viewport_height(view) - ARROW_HEIGHT;
}
#endif

static GRect card_frame(const Card *card) {
  return layer_get_frame(card->layer);
}

static void set_card_y(Card *card, int16_t y) {
  GRect frame = card_frame(card);
  frame.origin.y = y;
  layer_set_frame(card->layer, frame);
}

// Previous and next sit right above and below the current message.
static void restack(MessageView *view) {
  if (!view->current) {
    return;
  }
  GRect current = card_frame(view->current);
  if (view->next) {
    set_card_y(view->next, current.origin.y + current.size.h);
  }
  if (view->previous) {
    set_card_y(view->previous, current.origin.y - card_frame(view->previous).size.h);
  }
}

// ---------------------------------------------------------------------------------------------
// Cards

static void card_draw(Layer *layer, GContext *ctx) {
  Card *card = *(Card **)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, BANNER_COLOR);
#ifdef PBL_ROUND
  graphics_fill_circle(ctx, GPoint(bounds.size.w / 2, BANNER_HEIGHT - 1 - BANNER_RADIUS),
                       BANNER_RADIUS);
#else
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, BANNER_HEIGHT), 0, GCornerNone);
#endif
  if (card->view->icon) {
    gdraw_command_image_draw(ctx, card->view->icon,
                             GPoint((bounds.size.w - ICON_SIZE) / 2, ICON_TOP));
  }
}

// Reads the "<w>x<h>" a picture record carries.
static bool parse_size(const char *text, int32_t *width, int32_t *height) {
  int32_t values[2] = { 0, 0 };
  const char *cursor = text;
  for (int i = 0; i < 2; i++) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    while (*cursor >= '0' && *cursor <= '9') {
      values[i] = values[i] * 10 + (*cursor++ - '0');
      if (values[i] > 100000) {
        return false;
      }
    }
    if (i == 0 && *cursor++ != 'x') {
      return false;
    }
  }
  *width = values[0];
  *height = values[1];
  return *width > 0 && *height > 0;
}

// The picture keeps its proportions, as wide as the text at most and never enlarged.
static GSize picture_size(const char *dimensions, int16_t text_width) {
  int32_t source_w;
  int32_t source_h;
  if (!parse_size(dimensions, &source_w, &source_h)) {
    return GSizeZero;
  }
  int32_t w = text_width < PICTURE_MAX_WIDTH ? text_width : PICTURE_MAX_WIDTH;
  if (w > source_w) {
    w = source_w;
  }
  int32_t h = w * source_h / source_w;
  while (w > PICTURE_MIN_SIDE && (w * h > PICTURE_MAX_PIXELS || h > PICTURE_MAX_HEIGHT)) {
    w -= 2;
    h = w * source_h / source_w;
  }
  // Past the narrowest width, an extreme strip gets squeezed into the caps.
  if (h > PICTURE_MAX_HEIGHT) {
    h = PICTURE_MAX_HEIGHT;
  }
  if (h < PICTURE_MIN_SIDE) {
    h = PICTURE_MIN_SIDE;
  }
  return w < PICTURE_MIN_SIDE ? GSizeZero : GSize(w, h);
}

static void picture_draw(Layer *layer, GContext *ctx) {
  Card *card = *(Card **)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  if (card->picture) {
    graphics_draw_bitmap_in_rect(ctx, card->picture, bounds);
    return;
  }
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#else
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_rect(ctx, bounds);
#endif
}

static void picture_loaded(GBitmap *picture, void *context) {
  Card *card = context;
  card->picture_request = NULL;
  card->picture = picture;
  layer_mark_dirty(card->picture_layer);
}

static void drop_picture(Card *card) {
  if (!card) {
    return;
  }
  photo_cancel(card->picture_request);
  card->picture_request = NULL;
  if (card->picture) {
    gbitmap_destroy(card->picture);
    card->picture = NULL;
    layer_mark_dirty(card->picture_layer);
  }
}

static size_t picture_bytes(GSize size) {
  return PBL_IF_COLOR_ELSE(size.w, (size.w + 7) / 8) * size.h;
}

// The cards around the current one keep the picture they loaded while the heap has room
// for the current one besides, so stepping back does not fetch it again. A neighbour
// still loading is stopped: the card on screen goes first.
static void focus_pictures(MessageView *view) {
  Card *card = view->current;
  Card *neighbours[] = { view->previous, view->next };
  for (size_t i = 0; i < ARRAY_LENGTH(neighbours); i++) {
    if (neighbours[i] && neighbours[i]->picture_request) {
      drop_picture(neighbours[i]);
    }
  }
  if (!card || !card->picture_layer || card->picture || card->picture_request) {
    return;
  }
  GSize size = layer_get_frame(card->picture_layer).size;
  if (heap_bytes_free() < picture_bytes(size) + PICTURE_HEAP_SPARE) {
    drop_picture(view->previous);
    drop_picture(view->next);
  }
  card->picture_request = photo_fetch_picture(card->message_id, size, picture_loaded, card);
}

static int16_t place(TextLayer *layer, const char *text, GFont font, int16_t y, int16_t width,
                     int16_t padding) {
  GSize size = graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, width, 10000), GTextOverflowModeWordWrap,
      PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  layer_set_frame(text_layer_get_layer(layer), GRect(CARD_MARGIN, y + padding, width, size.h + 6));
  return y + padding + size.h + padding;
}

// A card fills at least the screen, as system notifications do.
static void card_layout(Card *card) {
  const UiStyle *style = ui_style();
  GRect frame = card_frame(card);
  int16_t width = frame.size.w - CARD_MARGIN * 2;
  text_layer_set_text(card->body, card->text);
  int16_t y = place(card->app_name, tr(S_MESSAGES), style->body_header, BANNER_HEIGHT, width,
                    style->body_padding);
  y = place(card->sender, card->sender_text, style->body_header, y, width, style->body_padding);
  if (card->picture_layer) {
    GRect picture = layer_get_frame(card->picture_layer);
    picture.origin = GPoint((frame.size.w - picture.size.w) / 2, y + PICTURE_MARGIN);
    layer_set_frame(card->picture_layer, picture);
    y = picture.origin.y + picture.size.h + PICTURE_MARGIN / 2;
  }
  y = place(card->body, card->text, style->body, y, width, BODY_PADDING);
  y = place(card->time, card->time_text, style->body_footer, y, width, FOOTER_PADDING);
  y += CARD_BOTTOM_PADDING;
  int16_t minimum = viewport_height(card->view);
  frame.size.h = y > minimum ? y : minimum;
  layer_set_frame(card->layer, frame);
}

static bool set_text(Card *card, const char *text) {
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (!copy) {
    return false;
  }
  memcpy(copy, text, length + 1);
  free(card->text);
  card->text = copy;
  return true;
}

static void update_chrome(MessageView *view);

static void finish_animation(MessageView *view);

static void body_reply(DictionaryIterator *reply, void *context) {
  Card *card = context;
  const char *text = reply ? comm_get_string(reply, MESSAGE_KEY_Text) : NULL;
  if (text && set_text(card, text)) {
    // A move keeps the heights it started with, and ending it here could free this very
    // card: the card grows once the move lands.
    if (card->view->animation) {
      card->needs_layout = true;
      return;
    }
    card_layout(card);
    restack(card->view);
    update_chrome(card->view);
  }
}

static TextLayer *add_text(Card *card, const char *text, GFont font) {
  TextLayer *layer = text_layer_create(GRectZero);
  if (!layer) {
    return NULL;
  }
  text_layer_set_font(layer, font);
  text_layer_set_text(layer, text);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_alignment(layer, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  layer_add_child(card->layer, text_layer_get_layer(layer));
  return layer;
}

static void card_destroy(Card *card) {
  if (!card) {
    return;
  }
  comm_cancel(card);
  drop_picture(card);
  if (card->picture_layer) {
    layer_destroy(card->picture_layer);
  }
  text_layer_destroy(card->app_name);
  text_layer_destroy(card->sender);
  text_layer_destroy(card->body);
  text_layer_destroy(card->time);
  layer_destroy(card->layer);
  free(card->text);
  free(card);
}

// The message at index as a card in the viewport, under the arrow; NULL when not loaded.
static Card *card_create(MessageView *view, int16_t index) {
  const Entry *message = index >= 0 ? view->source.entry_at(view->source.context, index) : NULL;
  Card *card = message ? calloc(1, sizeof(Card)) : NULL;
  if (!card) {
    return NULL;
  }
  card->view = view;
  card->index = index;
  if (!set_text(card, message->detail)) {
    free(card);
    return NULL;
  }
  ui_format_time(message->time, card->time_text, sizeof(card->time_text));
  const char *sender = (message->flags & MESSAGE_FLAG_SENT) ? tr(S_ME)
                       : message->title[0]                  ? message->title
                                                            : view->thread_name;
  snprintf(card->sender_text, sizeof(card->sender_text), "%s", sender);

  GRect bounds = layer_get_bounds(view->viewport);
  card->layer = layer_create_with_data(GRect(0, 0, bounds.size.w, bounds.size.h), sizeof(Card *));
  if (!card->layer) {
    free(card->text);
    free(card);
    return NULL;
  }
  *(Card **)layer_get_data(card->layer) = card;
  layer_set_update_proc(card->layer, card_draw);
  layer_insert_below_sibling(card->layer, view->arrow);

  const UiStyle *style = ui_style();
  card->app_name = add_text(card, tr(S_MESSAGES), style->body_header);
  card->sender = add_text(card, card->sender_text, style->body_header);
  card->body = add_text(card, card->text, style->body);
  card->time = add_text(card, card->time_text, style->body_footer);
  // Short of memory, the message is left unshown rather than half built.
  if (!card->app_name || !card->sender || !card->body || !card->time) {
    card_destroy(card);
    return NULL;
  }
  card->message_id = message->id;
  if (message->flags & MESSAGE_FLAG_PICTURE) {
    GSize size = picture_size(message->extra, bounds.size.w - CARD_MARGIN * 2);
    if (size.w) {
      card->picture_layer = layer_create_with_data(GRect(0, 0, size.w, size.h), sizeof(Card *));
      if (card->picture_layer) {
        *(Card **)layer_get_data(card->picture_layer) = card;
        layer_set_update_proc(card->picture_layer, picture_draw);
        layer_add_child(card->layer, card->picture_layer);
      }
    }
  }
  card_layout(card);
#ifdef PBL_ROUND
  text_layer_enable_screen_text_flow_and_paging(card->app_name, TEXT_FLOW_INSET);
  text_layer_enable_screen_text_flow_and_paging(card->sender, TEXT_FLOW_INSET);
  text_layer_enable_screen_text_flow_and_paging(card->body, TEXT_FLOW_INSET);
  text_layer_enable_screen_text_flow_and_paging(card->time, TEXT_FLOW_INSET);
#endif

  if (message->flags & MESSAGE_FLAG_CUT) {
    comm_send(&(CommRequest){ .cmd = CMD_MESSAGE_BODY, .id = message->id }, body_reply, card);
  }
  return card;
}

// ---------------------------------------------------------------------------------------------
// Status bar and arrow

static void update_chrome(MessageView *view) {
  if (!view->current) {
    return;
  }
  GRect current = card_frame(view->current);
  bool entire = current.size.h == viewport_height(view);
#ifdef PBL_ROUND
  bool filled = current.origin.y >= -96 && current.origin.y <= 66;
  bool at_bottom = current.origin.y < viewport_height(view) - current.size.h;
  bool arrow = !entire && !at_bottom;
#else
  bool filled = current.origin.y >= -(3 * BANNER_HEIGHT / 2) - 1 &&
                current.origin.y <= BANNER_HEIGHT / 2 - 1;
  bool arrow = current.origin.y == 0 && (!entire || view->next);
#endif
  GColor color = filled ? BANNER_COLOR : GColorWhite;
  status_bar_layer_set_colors(view->status_bar, color, gcolor_legible_over(color));
  layer_set_hidden(view->arrow, !arrow);
}

static void arrow_draw(Layer *layer, GContext *ctx) {
  MessageView *view = *(MessageView **)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
#ifndef PBL_ROUND
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#endif
  GRect arrow = gbitmap_get_bounds(view->arrow_bitmap);
  arrow.origin.x = (bounds.size.w - arrow.size.w) / 2;
  arrow.origin.y = PBL_IF_ROUND_ELSE(bounds.size.h - arrow.size.h - 8, 7);
  graphics_context_set_compositing_mode(ctx, PBL_IF_COLOR_ELSE(GCompOpSet, GCompOpAssign));
  graphics_draw_bitmap_in_rect(ctx, view->arrow_bitmap, arrow);
}

// ---------------------------------------------------------------------------------------------
// Scrolling and swapping

static int16_t current_offset(const MessageView *view) {
  return -card_frame(view->current).origin.y;
}

// How far the current message scrolls before the next one takes over.
static int16_t max_scroll(const MessageView *view) {
  int16_t max_dy = card_frame(view->current).size.h - viewport_height(view);
  if (view->next) {
    max_dy += PEEK_PX;
  }
#ifdef PBL_ROUND
  else {
    int16_t page = page_height(view);
    max_dy = (max_dy + page - 1) / page * page;
  }
#endif
  return max_dy > 0 ? max_dy : 0;
}

// Cards whose full text arrived during the move take their new height.
static void lay_out_waiting(MessageView *view) {
  bool changed = false;
  Card *cards[] = { view->previous, view->current, view->next };
  for (size_t i = 0; i < ARRAY_LENGTH(cards); i++) {
    if (cards[i] && cards[i]->needs_layout) {
      cards[i]->needs_layout = false;
      card_layout(cards[i]);
      changed = true;
    }
  }
  if (changed) {
    restack(view);
    update_chrome(view);
  }
}

// Lands every card of the move, whether it ran to its end, was cut short, or never started.
static void move_landed(Animation *animation, bool finished, void *context) {
  MessageView *view = context;
  view->animation = NULL;
  for (uint8_t i = 0; i < view->move_count; i++) {
    set_card_y(view->moves[i].card, view->moves[i].to_y);
  }
  view->move_count = 0;
  AnimationStoppedHandler done = view->move_done;
  view->move_done = NULL;
  if (done) {
    done(animation, finished, view);
  }
  lay_out_waiting(view);
}

static void finish_animation(MessageView *view) {
  Animation *animation = view->animation;
  if (!animation) {
    return;
  }
  animation_unschedule(animation);
  // Stopped before its first frame, an animation calls no handler.
  if (view->animation == animation) {
    move_landed(animation, false, view);
  }
}

static Animation *move_card(MessageView *view, Card *card, int16_t dy, uint32_t duration,
                            AnimationCurve curve) {
  if (!card || view->move_count == ARRAY_LENGTH(view->moves)) {
    return NULL;
  }
  GRect to = card_frame(card);
  to.origin.y += dy;
  view->moves[view->move_count++] = (Move){ .card = card, .to_y = to.origin.y };
  PropertyAnimation *move = property_animation_create_layer_frame(card->layer, NULL, &to);
  if (!move) {
    return NULL;
  }
  Animation *animation = property_animation_get_animation(move);
  animation_set_duration(animation, duration);
  animation_set_curve(animation, curve);
  return animation;
}

// Plays the moves together. A part missing for want of memory is skipped: its card
// still lands, with the others.
static Animation *together(Animation *a, Animation *b, Animation *c) {
  Animation *parts[3];
  uint32_t count = 0;
  Animation *given[] = { a, b, c };
  for (size_t i = 0; i < ARRAY_LENGTH(given); i++) {
    if (given[i]) {
      parts[count++] = given[i];
    }
  }
  if (count <= 1) {
    return count ? parts[0] : NULL;
  }
  Animation *spawn = animation_spawn_create_from_array(parts, count);
  if (!spawn) {
    for (uint32_t i = 0; i < count; i++) {
      animation_destroy(parts[i]);
    }
  }
  return spawn;
}

// Without an animation, for want of memory, the cards land at once.
static void run(MessageView *view, Animation *animation, AnimationStoppedHandler done) {
  view->move_done = done;
  if (!animation) {
    move_landed(NULL, true, view);
    return;
  }
  animation_set_handlers(animation, (AnimationHandlers){ .stopped = move_landed }, view);
  view->animation = animation;
  animation_schedule(animation);
}

static void scroll_stopped(Animation *animation, bool finished, void *context) {
  update_chrome(context);
}

static void scroll(MessageView *view, int16_t dy, AnimationCurve curve) {
  finish_animation(view);
  if (dy == 0) {
    return;
  }
  Animation *current = move_card(view, view->current, dy, SCROLL_MS, curve);
  Animation *next = move_card(view, view->next, dy, SCROLL_MS, curve);
  run(view, together(current, next, NULL), scroll_stopped);
}

static void scroll_to_top(MessageView *view) {
  scroll(view, current_offset(view), AnimationCurveEaseOut);
}

static void scroll_to_bottom(MessageView *view) {
  GRect current = card_frame(view->current);
  int16_t height = viewport_height(view);
  if (current.size.h < height) {
    scroll(view, height - current.size.h - current.origin.y, AnimationCurveEaseOut);
  }
}

static void swap_down_stopped(Animation *animation, bool finished, void *context) {
  MessageView *view = context;
  card_destroy(view->previous);
  view->previous = NULL;
  view->source.moved(view->source.context, view->current->index);
  view->next = card_create(view, view->current->index + 1);
  restack(view);
  update_chrome(view);
  focus_pictures(view);
}

static void swap_up_stopped(Animation *animation, bool finished, void *context) {
  MessageView *view = context;
  card_destroy(view->next);
  view->next = view->current;
  view->current = view->previous;
  view->previous = NULL;
  view->source.moved(view->source.context, view->current->index);
  restack(view);
  update_chrome(view);
  focus_pictures(view);
}

// Up lands on the end of the previous message, or on its top for a full swap.
static void attempt_swap(MessageView *view, bool down, bool full) {
  finish_animation(view);
  if (down) {
    if (!view->next) {
      // The list may have loaded it since this message opened.
      view->next = card_create(view, view->current->index + 1);
      restack(view);
    }
    if (!view->next) {
      scroll_to_bottom(view);
      return;
    }
    view->previous = view->current;
    view->current = view->next;
    view->next = NULL;
    GRect previous = card_frame(view->previous);
    int16_t dy = -(previous.origin.y + previous.size.h);
    Animation *previous_move = move_card(view, view->previous, dy, SWAP_MS, AnimationCurveEaseOut);
    Animation *current_move = move_card(view, view->current, dy, SWAP_MS, AnimationCurveEaseOut);
    run(view, together(previous_move, current_move, NULL), swap_down_stopped);
    return;
  }

  view->previous = card_create(view, view->current->index - 1);
  if (!view->previous) {
    scroll_to_top(view);
    return;
  }
  restack(view);
  GRect previous = card_frame(view->previous);
  GRect current = card_frame(view->current);
  int16_t dy = full ? previous.size.h - current.origin.y : viewport_height(view) - PEEK_PX;
  Animation *previous_move = move_card(view, view->previous, dy, SWAP_MS, AnimationCurveEaseInOut);
  Animation *current_move = move_card(view, view->current, dy, SWAP_MS, AnimationCurveEaseInOut);
  Animation *next_move = move_card(view, view->next, dy, SWAP_MS, AnimationCurveEaseInOut);
  run(view, together(previous_move, current_move, next_move), swap_up_stopped);
}

static void handle_swap_attempt(MessageView *view, bool down, bool repeating) {
  if (!repeating || view->swap_delay <= 0) {
    attempt_swap(view, down, false);
    view->swap_delay = SWAP_DELAY;
  } else {
    view->swap_delay--;
  }
}

static void attempt_scroll(MessageView *view, bool down, bool repeating) {
  if (!view->current) {
    return;
  }
  finish_animation(view);
#ifdef PBL_ROUND
  repeating = false;
#endif
  int16_t offset = current_offset(view);
  int16_t max_dy = max_scroll(view);
  int16_t dy;

  if (!down) {
    if (offset == 0) {
      handle_swap_attempt(view, false, repeating);
      return;
    }
    dy = (offset - FUDGE_PX) < SCROLL_PX ? offset : repeating ? REPEATING_SCROLL_PX : SCROLL_PX;
  } else {
    if (offset >= max_dy) {
      handle_swap_attempt(view, true, repeating);
      return;
    }
    // Pause at the top of a message.
    if (offset == 0 && repeating && view->swap_delay > 0) {
      view->swap_delay--;
      return;
    }
    view->swap_delay = SWAP_DELAY;
    if ((max_dy - offset - FUDGE_PX) < SCROLL_PX) {
      dy = -(max_dy - offset);
    } else if (offset == 0) {
      // The first press only scrolls the banner away.
      dy = -INITIAL_SCROLL_PX;
    } else {
      dy = repeating ? -REPEATING_SCROLL_PX : -SCROLL_PX;
    }
  }
  scroll(view, dy, repeating ? AnimationCurveLinear : AnimationCurveEaseOut);
}

// ---------------------------------------------------------------------------------------------
// Buttons, as the notification window takes them

static bool is_down(ClickRecognizerRef recognizer) {
  return click_recognizer_get_button_id(recognizer) == BUTTON_ID_DOWN;
}

static void raw_click(ClickRecognizerRef recognizer, void *context) {
  // Only the press that starts a click pattern scrolls, so a double press does not wait.
  if (click_number_of_clicks_counted(recognizer) < 1) {
    attempt_scroll(context, is_down(recognizer), false);
  }
}

static void repeating_click(ClickRecognizerRef recognizer, void *context) {
  if (click_recognizer_is_repeating(recognizer)) {
    attempt_scroll(context, is_down(recognizer), true);
  }
}

static void up_double_click(ClickRecognizerRef recognizer, void *context) {
  MessageView *view = context;
  if (!view->current) {
    return;
  }
  finish_animation(view);
  // When the first press already swapped, go to the top of that message instead.
  GRect current = card_frame(view->current);
  if (current.size.h - viewport_height(view) - current_offset(view) ==
      -PBL_IF_ROUND_ELSE(BANNER_HEIGHT + STATUS_BAR_LAYER_HEIGHT, BANNER_HEIGHT)) {
    scroll_to_top(view);
  } else {
    attempt_swap(view, false, true);
  }
}

static void down_double_click(ClickRecognizerRef recognizer, void *context) {
  MessageView *view = context;
  if (!view->current) {
    return;
  }
  finish_animation(view);
  if (current_offset(view) != 0) {
    attempt_swap(view, true, true);
  }
}

// Select answers the conversation, as it opens the actions of a system notification.
static void reply(MessageView *view) {
  reply_open(view->thread, view->thread_name, view->address);
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  reply(context);
}

static void click_config(void *context) {
  const ButtonId buttons[] = { BUTTON_ID_UP, BUTTON_ID_DOWN };
  for (size_t i = 0; i < ARRAY_LENGTH(buttons); i++) {
    window_raw_click_subscribe(buttons[i], raw_click, NULL, context);
    window_single_repeating_click_subscribe(buttons[i], SCROLL_REPEAT_MS, repeating_click);
    window_multi_click_subscribe(buttons[i], 2, 2, 100, false,
                                 buttons[i] == BUTTON_ID_UP ? up_double_click : down_double_click);
    window_set_click_context(buttons[i], context);
  }
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_set_click_context(BUTTON_ID_SELECT, context);
}

// ---------------------------------------------------------------------------------------------
// Touch: the messages follow the finger, an over-pull past an end changes message

#ifdef PBL_TOUCH
static void touch_handler(const TouchEvent *event, void *context) {
  MessageView *view = context;
  if (event->non_navigational || !view->current ||
      (event->type != TouchEvent_Touchdown && !view->touching)) {
    return;
  }
  int16_t dx = event->x - view->touch_start.x;
  int16_t dy = event->y - view->touch_start.y;
  switch (event->type) {
    case TouchEvent_Touchdown:
      finish_animation(view);
      view->touching = true;
      view->touch_start = GPoint(event->x, event->y);
      view->touch_base = current_offset(view);
      view->dragging = false;
      break;
    case TouchEvent_PositionUpdate:
      if (!view->dragging && abs(dy) >= DRAG_THRESHOLD_PX && abs(dy) >= abs(dx)) {
        view->dragging = true;
      }
      if (view->dragging) {
        int16_t offset = view->touch_base - dy;
        int16_t max_dy = max_scroll(view);
        set_card_y(view->current, -(offset < 0 ? 0 : offset > max_dy ? max_dy : offset));
        restack(view);
        update_chrome(view);
      }
      break;
    case TouchEvent_Liftoff:
      view->touching = false;
      if (view->dragging) {
        view->dragging = false;
        int16_t requested = view->touch_base - dy;
        int16_t max_dy = max_scroll(view);
        if (requested < -SWAP_OVERPULL_PX) {
          handle_swap_attempt(view, false, false);
        } else if (requested > max_dy + SWAP_OVERPULL_PX) {
          handle_swap_attempt(view, true, false);
        } else {
          int16_t target = requested < 0 ? 0 : requested > max_dy ? max_dy : requested;
#ifdef PBL_ROUND
          // Round text flow only lines up on page boundaries.
          int16_t page = page_height(view);
          int16_t last_page = max_dy / page * page;
          target = (target + page / 2) / page * page;
          target = target < last_page ? target : last_page;
#endif
          scroll(view, current_offset(view) - target, AnimationCurveEaseOut);
        }
      } else if (abs(dx) > 2 * DRAG_THRESHOLD_PX && abs(dx) > abs(dy)) {
        // As on notifications: right goes back, left opens the reply.
        if (dx > 0) {
          window_stack_pop(true);
        } else {
          reply(view);
        }
      }
      break;
  }
}
#endif

static void window_appear(Window *window) {
#ifdef PBL_TOUCH
  if (ui_touch_supported()) {
    // A gesture cut short by another window never sent its liftoff here.
    ((MessageView *)window_get_user_data(window))->touching = false;
    touch_service_subscribe(touch_handler, window_get_user_data(window));
  }
#endif
}

static void window_disappear(Window *window) {
#ifdef PBL_TOUCH
  if (ui_touch_supported()) {
    touch_service_unsubscribe();
  }
#endif
}

// ---------------------------------------------------------------------------------------------
// Window

// The half disc a system notification shows on its right edge when Select opens
// actions, sized as the firmware's action button.
static void action_button_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int16_t radius = PBL_IF_ROUND_ELSE(12, 13);
  int16_t offset = ui_style()->large ? 4 : PBL_IF_ROUND_ELSE(1, 8);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(bounds.size.w + offset, bounds.size.h / 2), radius);
}

static void window_load(Window *window) {
  MessageView *view = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  view->viewport = layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w,
                                      bounds.size.h - STATUS_BAR_LAYER_HEIGHT));
  view->arrow_bitmap = gbitmap_create_with_resource(RESOURCE_ID_ARROW_DOWN);
  view->arrow = layer_create_with_data(
      GRect(0, bounds.size.h - STATUS_BAR_LAYER_HEIGHT - ARROW_HEIGHT, bounds.size.w, ARROW_HEIGHT),
      sizeof(MessageView *));
  view->action_button = layer_create(bounds);
  view->status_bar = status_bar_layer_create();
  if (!view->viewport || !view->arrow_bitmap || !view->arrow || !view->action_button ||
      !view->status_bar) {
    ui_close_soon(window);
    return;
  }

  layer_add_child(root, view->viewport);
  *(MessageView **)layer_get_data(view->arrow) = view;
  layer_set_update_proc(view->arrow, arrow_draw);
  layer_add_child(view->viewport, view->arrow);

  layer_set_update_proc(view->action_button, action_button_draw);
  layer_add_child(root, view->action_button);
  layer_set_hidden(view->action_button, !view->address[0]);

  status_bar_layer_set_separator_mode(view->status_bar, StatusBarLayerSeparatorModeNone);
  layer_add_child(root, status_bar_layer_get_layer(view->status_bar));

  view->icon = gdraw_command_image_create_with_resource(RESOURCE_ID_SMS_ICON);
  view->current = card_create(view, view->first_index);
  if (!view->current) {
    ui_close_soon(window);
    return;
  }
  view->next = card_create(view, view->first_index + 1);
  restack(view);
  update_chrome(view);
  focus_pictures(view);
}

static void window_unload(Window *window) {
  MessageView *view = window_get_user_data(window);
  finish_animation(view);
  card_destroy(view->previous);
  card_destroy(view->current);
  card_destroy(view->next);
  layer_destroy(view->arrow);
  layer_destroy(view->action_button);
  gbitmap_destroy(view->arrow_bitmap);
  if (view->icon) {
    gdraw_command_image_destroy(view->icon);
  }
  status_bar_layer_destroy(view->status_bar);
  layer_destroy(view->viewport);
  free(view);
  window_destroy(window);
}

void message_window_push(const MessageSource *source, int16_t index, int32_t thread,
                         const char *thread_name, const char *address) {
  if (!source->entry_at(source->context, index)) {
    return;
  }
  MessageView *view = calloc(1, sizeof(MessageView));
  if (!view) {
    return;
  }
  view->source = *source;
  view->first_index = index;
  view->swap_delay = SWAP_DELAY;
  strncpy(view->thread_name, thread_name, sizeof(view->thread_name) - 1);
  view->thread = thread;
  strncpy(view->address, address ? address : "", sizeof(view->address) - 1);

  view->window = window_create();
  if (!view->window) {
    free(view);
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
