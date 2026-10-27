// SPDX-License-Identifier: AGPL-3.0-or-later
// Constants shared with the Android companion, see PROTOCOL.md.
#pragma once

#include <pebble.h>

enum {
  CMD_HELLO = 1,
  CMD_FAVORITES = 2,
  CMD_LETTERS = 3,
  CMD_CONTACTS = 4,
  CMD_NUMBERS = 5,
  CMD_CALLS = 6,
  CMD_THREADS = 7,
  CMD_MESSAGES = 8,
  CMD_MESSAGE_BODY = 9,
  CMD_DIAL = 10,
  CMD_SEND_SMS = 11,
  CMD_HANG_UP = 12,
  CMD_MUTE = 13,
  CMD_AUDIO_ROUTE = 14,
  CMD_CALL_STATE = 15,
  CMD_PHOTO = 16,
  CMD_SEARCH = 17,
  CMD_REPLIES = 18,
  CMD_OPEN_THREAD = 19,
};

enum {
  STATUS_OK = 0,
  STATUS_NO_PERMISSION = 1,
  STATUS_NOT_FOUND = 2,
  STATUS_FAILED = 3,
  // SEND_SMS only: the radio has not reported yet, the text may still leave.
  STATUS_PENDING = 4,
  // Watch side only: the request never got an answer.
  STATUS_UNREACHABLE = 100,
};

enum {
  CAP_CONTACTS = 1 << 0,
  CAP_CALL_LOG = 1 << 1,
  CAP_DIAL = 1 << 2,
  CAP_READ_SMS = 1 << 3,
  CAP_SEND_SMS = 1 << 4,
  CAP_CALL_CONTROL = 1 << 5,
};

typedef enum {
  CALL_NONE = 0,
  CALL_DIALING = 1,
  CALL_RINGING = 2,
  CALL_ACTIVE = 3,
  CALL_HOLDING = 4,
  CALL_ENDED = 5,
} CallState;

// android.telecom.CallAudioState routes, carried in bits 4-7 of the call flags.
enum {
  ROUTE_EARPIECE = 1,
  ROUTE_BLUETOOTH = 2,
  ROUTE_WIRED_HEADSET = 4,
  ROUTE_SPEAKER = 8,
};

#define CALL_FLAG_MUTED (1 << 0)
#define CALL_FLAG_ROUTE(flags) (((flags) >> 4) & 0x0f)
#define CALL_FLAG_HANGUP_ONLY (1 << 8)

// Flags of the DIAL reply.
#define DIAL_FOLLOWED (1 << 0)

enum {
  CALL_TYPE_INCOMING = 1,
  CALL_TYPE_OUTGOING = 2,
  CALL_TYPE_MISSED = 3,
  CALL_TYPE_REJECTED = 5,
};

// Flags of the event record of the HELLO reply.
enum {
  GLANCE_MISSED_CALL = 1,
  GLANCE_SMS = 2,
};

#define THREAD_FLAG_UNREAD (1 << 0)
#define MESSAGE_FLAG_SENT (1 << 0)
#define MESSAGE_FLAG_CUT (1 << 1)
// The message holds a picture, whose size the record carries in extra as "<w>x<h>".
#define MESSAGE_FLAG_PICTURE (1 << 2)

// Photo request flags: edge length in the low 12 bits, then the pixel format.
#define PHOTO_FLAGS(size, format) ((size) | ((format) << 12))
// A message picture adds its height in bits 16-27.
#define PICTURE_FLAGS(size, format) \
  ((size).w | ((format) << 12) | ((int32_t)(size).h << 16) | (1 << 28))
enum {
  // One byte per pixel, 0bAARRGGBB, transparent outside the inscribed circle.
  PHOTO_FORMAT_COLOR = 0,
  // One bit per pixel, rows padded to whole bytes, least significant bit first.
  PHOTO_FORMAT_MONO = 1,
};
