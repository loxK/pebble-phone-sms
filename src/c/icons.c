// SPDX-License-Identifier: AGPL-3.0-or-later
#include "icons.h"

#define GRID 20

static GPoint at(GRect box, int x, int y) {
  return GPoint(box.origin.x + x * box.size.w / GRID, box.origin.y + y * box.size.h / GRID);
}

static int16_t scale(GRect box, int value) {
  return value * box.size.w / GRID;
}

static uint8_t stroke(GRect box) {
  return box.size.w >= 26 ? 3 : 2;
}

static void fill_polygon(GContext *ctx, GRect box, const GPoint *grid, uint32_t count) {
  GPoint points[12];
  for (uint32_t i = 0; i < count; i++) {
    points[i] = at(box, grid[i].x, grid[i].y);
  }
  GPathInfo info = { .num_points = count, .points = points };
  GPath *path = gpath_create(&info);
  if (path) {
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  }
}

static void polyline(GContext *ctx, GRect box, const GPoint *grid, uint32_t count) {
  for (uint32_t i = 1; i < count; i++) {
    graphics_draw_line(ctx, at(box, grid[i - 1].x, grid[i - 1].y), at(box, grid[i].x, grid[i].y));
  }
}

void icons_keypad(GContext *ctx, GRect box) {
  int16_t radius = scale(box, 2);
  for (int row = 0; row < 3; row++) {
    for (int column = 0; column < 3; column++) {
      graphics_fill_circle(ctx, at(box, 4 + column * 6, 2 + row * 5), radius);
    }
  }
  graphics_fill_circle(ctx, at(box, 10, 17), radius);
}

void icons_phone(GContext *ctx, GRect box) {
  fill_polygon(ctx, box,
               (GPoint[]){ { 2, 3 }, { 5, 1 }, { 8, 5 }, { 6, 8 }, { 12, 14 }, { 15, 12 },
                           { 19, 15 }, { 17, 18 }, { 14, 19 }, { 9, 17 }, { 4, 12 }, { 1, 7 } },
               12);
}

void icons_speaker(GContext *ctx, GRect box) {
  fill_polygon(ctx, box, (GPoint[]){ { 1, 7 }, { 5, 7 }, { 10, 2 }, { 10, 18 }, { 5, 13 }, { 1, 13 } },
               6);
  graphics_context_set_stroke_width(ctx, stroke(box));
  graphics_draw_line(ctx, at(box, 13, 7), at(box, 13, 13));
  graphics_draw_line(ctx, at(box, 17, 4), at(box, 17, 16));
}

void icons_bluetooth(GContext *ctx, GRect box) {
  graphics_context_set_stroke_width(ctx, stroke(box));
  polyline(ctx, box, (GPoint[]){ { 5, 5 }, { 15, 14 }, { 10, 19 }, { 10, 1 }, { 15, 6 }, { 5, 15 } },
           6);
}

void icons_smartphone(GContext *ctx, GRect box) {
  graphics_context_set_stroke_width(ctx, stroke(box));
  graphics_draw_round_rect(ctx, (GRect){ at(box, 5, 1), GSize(scale(box, 10), scale(box, 18)) },
                           scale(box, 2));
  graphics_draw_line(ctx, at(box, 9, 15), at(box, 11, 15));
}

static void draw_mic(GContext *ctx, GRect box) {
  graphics_fill_rect(ctx, (GRect){ at(box, 7, 1), GSize(scale(box, 6), scale(box, 11)) },
                     scale(box, 3), GCornersAll);
  graphics_context_set_stroke_width(ctx, stroke(box));
  polyline(ctx, box,
           (GPoint[]){ { 4, 8 }, { 4, 10 }, { 7, 14 }, { 13, 14 }, { 16, 10 }, { 16, 8 } }, 6);
  graphics_draw_line(ctx, at(box, 10, 14), at(box, 10, 18));
  graphics_draw_line(ctx, at(box, 6, 19), at(box, 14, 19));
}

void icons_mic(GContext *ctx, GRect box) {
  draw_mic(ctx, box);
}

void icons_mic_off(GContext *ctx, GRect box) {
  draw_mic(ctx, box);
  graphics_context_set_stroke_width(ctx, stroke(box) + 1);
  graphics_draw_line(ctx, at(box, 2, 1), at(box, 18, 19));
}

void icons_hang_up(GContext *ctx, GRect box) {
  fill_polygon(ctx, box,
               (GPoint[]){ { 0, 13 }, { 0, 9 }, { 4, 6 }, { 16, 6 }, { 20, 9 }, { 20, 13 },
                           { 15, 13 }, { 14, 10 }, { 6, 10 }, { 5, 13 } },
               10);
}

void icons_backspace(GContext *ctx, GRect box) {
  graphics_context_set_stroke_width(ctx, stroke(box));
  polyline(ctx, box,
           (GPoint[]){ { 6, 4 }, { 19, 4 }, { 19, 16 }, { 6, 16 }, { 1, 10 }, { 6, 4 } }, 6);
  graphics_draw_line(ctx, at(box, 10, 7), at(box, 15, 13));
  graphics_draw_line(ctx, at(box, 15, 7), at(box, 10, 13));
}

void icons_smiley(GContext *ctx, GRect box) {
  graphics_context_set_stroke_width(ctx, stroke(box));
  graphics_draw_circle(ctx, at(box, 10, 10), scale(box, 9) - 1);
  int16_t eye = scale(box, 3) / 2 + 1;
  graphics_fill_circle(ctx, at(box, 7, 8), eye);
  graphics_fill_circle(ctx, at(box, 13, 8), eye);
  GRect mouth = { at(box, 5, 5), GSize(scale(box, 10), scale(box, 10)) };
  graphics_draw_arc(ctx, mouth, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(110), DEG_TO_TRIGANGLE(250));
}

// Pebble system icons, from the PebbleOS iconography: 25 px drawings of black
// strokes over white paper. Each draw loads its own copy: kept, they would take
// heap the 64 KB watches cannot spare.

typedef enum {
  PEBBLE_FAVORITES,
  PEBBLE_CONTACTS,
  PEBBLE_RECENTS,
  PEBBLE_WRITE,
  PEBBLE_MESSAGE,
  PEBBLE_SEARCH,
  PEBBLE_CALL,
  PEBBLE_DICTATE,
  PEBBLE_INCOMING,
  PEBBLE_OUTGOING,
  PEBBLE_MISSED,
  PEBBLE_REJECTED,
  PEBBLE_ICON_COUNT,
} PebbleIcon;

static const uint32_t PEBBLE_RESOURCES[PEBBLE_ICON_COUNT] = {
  RESOURCE_ID_ICON_FAVORITES, RESOURCE_ID_ICON_CONTACTS, RESOURCE_ID_ICON_RECENTS,
  RESOURCE_ID_ICON_WRITE,     RESOURCE_ID_SMS_ICON,      RESOURCE_ID_ICON_SEARCH,
  RESOURCE_ID_ICON_PHONE,     RESOURCE_ID_ICON_MICROPHONE, RESOURCE_ID_ICON_INCOMING,
  RESOURCE_ID_ICON_OUTGOING,  RESOURCE_ID_ICON_MISSED,   RESOURCE_ID_ICON_REJECTED,
};

static GColor s_color;

void icons_set_color(GColor color) {
  s_color = color;
}

typedef struct {
  int16_t size;
  int16_t native;
} Scale;

// Precise points are in eighths of a pixel and sit half a pixel up and left of
// the drawing's grid: scaled around the grid origin, they stay on it.
static bool scale_command(GDrawCommand *command, uint32_t index, void *context) {
  const Scale *scale = context;
  int16_t shift = gdraw_command_get_type(command) == GDrawCommandTypePrecisePath ? 4 : 0;
  for (uint16_t i = 0; i < gdraw_command_get_num_points(command); i++) {
    GPoint point = gdraw_command_get_point(command, i);
    point.x = (point.x + shift) * scale->size / scale->native - shift;
    point.y = (point.y + shift) * scale->size / scale->native - shift;
    gdraw_command_set_point(command, i, point);
  }
  return true;
}

// The strokes take the color, the white paper becomes see-through.
static bool paint_command(GDrawCommand *command, uint32_t index, void *context) {
  GColor color = *(GColor *)context;
  if (!gcolor_equal(gdraw_command_get_stroke_color(command), GColorClear)) {
    gdraw_command_set_stroke_color(command, color);
  }
  GColor fill = gdraw_command_get_fill_color(command);
  if (gcolor_equal(fill, GColorWhite)) {
    gdraw_command_set_fill_color(command, GColorClear);
  } else if (!gcolor_equal(fill, GColorClear)) {
    gdraw_command_set_fill_color(command, color);
  }
  return true;
}

static void draw_pebble(GContext *ctx, GRect box, PebbleIcon icon) {
  GDrawCommandImage *image = gdraw_command_image_create_with_resource(PEBBLE_RESOURCES[icon]);
  if (!image) {
    return;
  }
  // The longer side fits the box.
  int16_t size = box.size.w < box.size.h ? box.size.w : box.size.h;
  GSize native = gdraw_command_image_get_bounds_size(image);
  Scale scale = { .size = size, .native = native.w > native.h ? native.w : native.h };
  GDrawCommandList *commands = gdraw_command_image_get_command_list(image);
  gdraw_command_list_iterate(commands, scale_command, &scale);
  gdraw_command_list_iterate(commands, paint_command, &s_color);
  GSize drawn = GSize(native.w * size / scale.native, native.h * size / scale.native);
  gdraw_command_image_set_bounds_size(image, drawn);
  gdraw_command_image_draw(ctx, image,
                           GPoint(box.origin.x + (box.size.w - drawn.w) / 2,
                                  box.origin.y + (box.size.h - drawn.h) / 2));
  gdraw_command_image_destroy(image);
}

void icons_favorites(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_FAVORITES);
}

void icons_contacts(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_CONTACTS);
}

void icons_recents(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_RECENTS);
}

void icons_write(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_WRITE);
}

void icons_message(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_MESSAGE);
}

void icons_search(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_SEARCH);
}

void icons_call(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_CALL);
}

void icons_dictate(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_DICTATE);
}

void icons_incoming(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_INCOMING);
}

void icons_outgoing(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_OUTGOING);
}

void icons_missed(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_MISSED);
}

void icons_rejected(GContext *ctx, GRect box) {
  draw_pebble(ctx, box, PEBBLE_REJECTED);
}
