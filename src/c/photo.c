// SPDX-License-Identifier: AGPL-3.0-or-later
#include "photo.h"
#include "comm.h"
#include "icons.h"
#include "protocol.h"
#include "ui.h"

#ifdef PBL_COLOR
#define FORMAT PHOTO_FORMAT_COLOR
#define BITMAP_FORMAT GBitmapFormat8Bit
#define ROW_BYTES(width) (width)
#else
#define FORMAT PHOTO_FORMAT_MONO
#define BITMAP_FORMAT GBitmapFormat1Bit
#define ROW_BYTES(width) (((width) + 7) / 8)
#endif

struct PhotoRequest {
  int32_t id;
  char number[32];
  GSize size;
  int32_t flags;
  GBitmap *bitmap;
  int32_t received;
  PhotoHandler handler;
  void *context;
};

static void chunk_reply(DictionaryIterator *reply, void *context);

static bool request_chunk(PhotoRequest *request) {
  CommRequest message = {
    .cmd = CMD_PHOTO,
    .id = request->id,
    .offset = request->received,
    .flags = request->flags,
    .address = request->id ? NULL : request->number,
  };
  return comm_send(&message, chunk_reply, request) != 0;
}

static void finish(PhotoRequest *request, bool success) {
  GBitmap *bitmap = success ? request->bitmap : NULL;
  if (!success && request->bitmap) {
    gbitmap_destroy(request->bitmap);
  }
  PhotoHandler handler = request->handler;
  void *context = request->context;
  free(request);
  handler(bitmap, context);
}

// Writes a run of the phone's tightly packed rows into the bitmap's padded rows.
static void copy_pixels(PhotoRequest *request, const uint8_t *data, int32_t offset, int32_t length) {
  int32_t row_bytes = ROW_BYTES(request->size.w);
  uint16_t stride = gbitmap_get_bytes_per_row(request->bitmap);
  uint8_t *pixels = gbitmap_get_data(request->bitmap);
  while (length > 0) {
    int32_t row = offset / row_bytes;
    int32_t column = offset % row_bytes;
    int32_t run = row_bytes - column < length ? row_bytes - column : length;
    memcpy(pixels + row * stride + column, data, run);
    data += run;
    offset += run;
    length -= run;
  }
}

static void chunk_reply(DictionaryIterator *reply, void *context) {
  PhotoRequest *request = context;
  if (!reply || comm_get_int(reply, MESSAGE_KEY_Status, STATUS_FAILED) != STATUS_OK) {
    finish(request, false);
    return;
  }
  int32_t expected = ROW_BYTES(request->size.w) * request->size.h;
  int32_t total = comm_get_int(reply, MESSAGE_KEY_Total, 0);
  int32_t offset = comm_get_int(reply, MESSAGE_KEY_Offset, -1);
  Tuple *items = dict_find(reply, MESSAGE_KEY_Items);
  // An empty chunk would ask for the same offset again, forever.
  if (total != expected || offset != request->received || !items || items->length == 0 ||
      offset + items->length > expected) {
    finish(request, false);
    return;
  }
  if (!request->bitmap) {
    request->bitmap = gbitmap_create_blank(request->size, BITMAP_FORMAT);
    if (!request->bitmap) {
      finish(request, false);
      return;
    }
  }
  copy_pixels(request, items->value->data, offset, items->length);
  request->received += items->length;
  if (request->received >= expected) {
    finish(request, true);
  } else if (!request_chunk(request)) {
    finish(request, false);
  }
}

static PhotoRequest *start(int32_t id, const char *number, GSize size, int32_t flags,
                           PhotoHandler handler, void *context) {
  PhotoRequest *request = calloc(1, sizeof(PhotoRequest));
  if (!request) {
    return NULL;
  }
  request->id = id;
  if (number) {
    strncpy(request->number, number, sizeof(request->number) - 1);
  }
  request->size = size;
  request->flags = flags;
  request->handler = handler;
  request->context = context;
  if (!request_chunk(request)) {
    free(request);
    return NULL;
  }
  return request;
}

PhotoRequest *photo_fetch(int32_t contact_id, const char *number, int16_t size,
                          PhotoHandler handler, void *context) {
  if (!contact_id && !(number && number[0])) {
    return NULL;
  }
  return start(contact_id, number, GSize(size, size), PHOTO_FLAGS(size, FORMAT), handler,
               context);
}

PhotoRequest *photo_fetch_picture(int32_t message_id, GSize size, PhotoHandler handler,
                                  void *context) {
  return start(message_id, NULL, size, PICTURE_FLAGS(size, FORMAT), handler, context);
}

void photo_cancel(PhotoRequest *request) {
  if (!request) {
    return;
  }
  comm_cancel(request);
  if (request->bitmap) {
    gbitmap_destroy(request->bitmap);
  }
  free(request);
}


#ifdef PBL_COLOR
static const uint8_t AVATAR_COLORS[] = {
  GColorCobaltBlueARGB8, GColorFollyARGB8,   GColorIslamicGreenARGB8,
  GColorPurpleARGB8,     GColorWindsorTanARGB8, GColorBlueMoonARGB8,
};
#endif

static GColor avatar_color(const char *name) {
#ifdef PBL_COLOR
  uint32_t hash = 0;
  for (const char *cursor = name; *cursor; cursor++) {
    hash = hash * 31 + (uint8_t)*cursor;
  }
  return (GColor){ .argb = AVATAR_COLORS[hash % ARRAY_LENGTH(AVATAR_COLORS)] };
#else
  return GColorBlack;
#endif
}

void avatar_draw(GContext *ctx, GRect box, GBitmap *photo, const char *name) {
  GPoint center = grect_center_point(&box);
  int16_t radius = box.size.w / 2;
  if (photo) {
    graphics_context_set_compositing_mode(ctx, PBL_IF_COLOR_ELSE(GCompOpSet, GCompOpAssign));
    graphics_draw_bitmap_in_rect(ctx, photo, box);
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
#ifndef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, center, radius);
#endif
    return;
  }

  graphics_context_set_fill_color(ctx, avatar_color(name ? name : ""));
  graphics_fill_circle(ctx, center, radius);
  char initials[12];
  bool is_number = !name || !name[0] || name[0] == '+' || (name[0] >= '0' && name[0] <= '9');
  if (is_number) {
    int16_t icon = box.size.w / 2;
    graphics_context_set_fill_color(ctx, GColorWhite);
    icons_phone(ctx, GRect(center.x - icon / 2, center.y - icon / 2, icon, icon));
    return;
  }
  ui_initials(name, initials, sizeof(initials));
  GFont font = fonts_get_system_font(box.size.w >= 70 ? FONT_KEY_GOTHIC_28_BOLD
                                                      : FONT_KEY_GOTHIC_24_BOLD);
  int16_t height = box.size.w >= 70 ? 34 : 28;
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, initials, font,
                     GRect(box.origin.x, center.y - height / 2 - 5, box.size.w, height),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}
