// SPDX-License-Identifier: AGPL-3.0-or-later
// A phone that lives in the watchapp, for screenshots: the demo build answers
// every request with fixed, made-up data dated from DEMO_NOW. Only built with
// tools/shoot_screenshots.sh, which defines DEMO_NOW.
#pragma once

#include <pebble.h>

#ifdef DEMO_NOW

// Receives the answers, as inbox_received receives the phone's messages.
typedef void (*DemoDelivery)(DictionaryIterator *message, void *context);

void demo_init(DemoDelivery deliver);

// Takes a request leaving the watch: text is its Text, or its Address when it has no
// text. budget is the largest page the watch reads.
void demo_request(uint8_t cmd, uint32_t seq, int32_t id, int32_t offset, int32_t flags,
                  const char *text, uint32_t budget);

#endif
