// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <pebble.h>
#include "protocol.h"

void call_init(void (*changed)(void));
void call_deinit(void);

// CALL_STATE handler for comm.
void call_handle_state(DictionaryIterator *message);

bool call_in_progress(void);

// Asks the phone to place the call and opens the call screen.
void call_dial(const char *name, const char *number);

void call_window_push(void);

// Whether two phone numbers end with the same digits, written either way.
bool call_same_number(const char *a, const char *b);
