// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <pebble.h>

// Fetches the quick replies set on the phone, once per launch.
void reply_prefetch(void);

// Reply menu in the style of the system one: dictation when there is a
// microphone, the quick replies, a grid of emoji, and opening the conversation
// on the phone. For a sender that takes no reply, the conversation opens on the
// phone at once, and the menu only shows when that fails, to tell why.
// thread is the conversation answered, 0 for a new message.
void reply_open(int32_t thread, const char *name, const char *address);

void reply_deinit(void);
