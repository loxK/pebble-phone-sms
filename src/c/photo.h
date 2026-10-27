// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <pebble.h>

// Receives the photo, owned by the handler from then on, or NULL when the
// contact has none or the transfer failed.
typedef void (*PhotoHandler)(GBitmap *photo, void *context);

typedef struct PhotoRequest PhotoRequest;

// Fetches a contact photo by contact id, or by phone number when id is 0, size
// pixels wide and high: the phone dithers it for that size.
// Returns NULL when the request could not be sent; the handler is then never called.
PhotoRequest *photo_fetch(int32_t contact_id, const char *number, int16_t size,
                          PhotoHandler handler, void *context);

// Fetches the picture of a message, scaled by the phone to size. Same contract
// as photo_fetch.
PhotoRequest *photo_fetch_picture(int32_t message_id, GSize size, PhotoHandler handler,
                                  void *context);

// Stops a request whose handler has not run yet.
void photo_cancel(PhotoRequest *request);

// The photo when there is one, otherwise a colored disc with the initials of
// name, or a handset when name is a bare number.
void avatar_draw(GContext *ctx, GRect box, GBitmap *photo, const char *name);
