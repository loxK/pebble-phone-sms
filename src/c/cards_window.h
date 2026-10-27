// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <pebble.h>

// Favorites, one card per contact with its photo. Select opens the contact,
// a long press calls its main number.
void cards_window_push(void);

// Loads the first favorites and their photos before the window is opened.
void cards_prefetch(void);

// Reloads the photos after a style change.
void cards_reset(void);

void cards_deinit(void);

// The photo the cards hold for the favorite whose number this is, if it is size
// pixels wide, or NULL. It stays theirs and may go at any change of card: use
// it at once, never keep it.
GBitmap *cards_photo(const char *number, int16_t size);
