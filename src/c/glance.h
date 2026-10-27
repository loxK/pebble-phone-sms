// SPDX-License-Identifier: AGPL-3.0-or-later
// The line under the app name in the launcher: the latest missed call or text,
// as the phone picked it in its HELLO reply.
#pragma once

#include <pebble.h>

// Reads the event of a HELLO reply, if any, and builds its line right away so
// that writing it on exit needs no memory.
void glance_read(DictionaryIterator *hello_reply);

// Writes the line to the launcher. Leaves the previous one alone when the phone
// never answered.
void glance_write(void);
