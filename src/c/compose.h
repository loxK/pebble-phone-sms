// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <pebble.h>

typedef void (*DictationHandler)(const char *text, void *context);

// False on watches without a microphone.
bool compose_available(void);

// Runs a dictation session and hands over the transcription, if any.
void compose_dictate(bool confirm, DictationHandler handler, void *context);

// Sends text to address and shows how it went. thread is the conversation it
// answers, 0 for a new one: the phone answers from the SIM that thread uses.
void compose_send(int32_t thread, const char *address, const char *text);

// Dictates a message, lets the user confirm the transcription, then sends it.
void compose_sms(int32_t thread, const char *address);

// False for an empty address and for senders that write their name instead of a
// number ("AUTHMSG"): nothing can answer them. Short codes such as 1228 take replies.
bool compose_can_reply(const char *address);

// Texts sent since launch: a list that shows messages reloads when it changed.
uint16_t compose_sent_count(void);

void compose_deinit(void);
