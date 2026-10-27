// SPDX-License-Identifier: AGPL-3.0-or-later
// Vector icons designed on a 20x20 grid and scaled to the box they are drawn
// in, with the current fill and stroke colors.
#pragma once

#include <pebble.h>

void icons_keypad(GContext *ctx, GRect box);
void icons_phone(GContext *ctx, GRect box);
void icons_speaker(GContext *ctx, GRect box);
void icons_bluetooth(GContext *ctx, GRect box);
void icons_smartphone(GContext *ctx, GRect box);
void icons_mic(GContext *ctx, GRect box);
void icons_mic_off(GContext *ctx, GRect box);
void icons_hang_up(GContext *ctx, GRect box);
void icons_backspace(GContext *ctx, GRect box);
void icons_smiley(GContext *ctx, GRect box);

// Pebble system icons, painted in the color last given to icons_set_color.
void icons_set_color(GColor color);
void icons_favorites(GContext *ctx, GRect box);
void icons_contacts(GContext *ctx, GRect box);
void icons_recents(GContext *ctx, GRect box);
void icons_write(GContext *ctx, GRect box);
void icons_message(GContext *ctx, GRect box);
void icons_search(GContext *ctx, GRect box);
void icons_call(GContext *ctx, GRect box);
void icons_dictate(GContext *ctx, GRect box);
void icons_incoming(GContext *ctx, GRect box);
void icons_outgoing(GContext *ctx, GRect box);
void icons_missed(GContext *ctx, GRect box);
void icons_rejected(GContext *ctx, GRect box);
