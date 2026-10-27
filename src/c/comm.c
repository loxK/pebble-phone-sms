// SPDX-License-Identifier: AGPL-3.0-or-later
#include "comm.h"
#include "demo.h"
#include "protocol.h"

#define QUEUE_SIZE 6
#define PENDING_SIZE 6
#define REPLY_TIMEOUT_MS 12000
// The phone answers SEND_SMS once the radio reports, which takes a while on a poor signal.
#define SMS_REPLY_TIMEOUT_MS 65000
#define RETRY_DELAY_MS 400
#define MAX_RETRIES 2
#define INBOX_CAP 4096
#define OUTBOX_SIZE 768
// How long the link stays responsive after the last message.
#define FAST_LINK_LINGER_MS 3000

typedef struct {
  uint8_t cmd;
  uint32_t seq;
  int32_t id;
  int32_t offset;
  int32_t flags;
  char *text;
  char *address;
} Queued;

typedef struct {
  uint32_t seq;
  CommReplyHandler handler;
  void *context;
  AppTimer *timer;
} Pending;

static Queued s_queue[QUEUE_SIZE];
static uint8_t s_queue_len;
static Pending s_pending[PENDING_SIZE];
static bool s_sending;
static uint8_t s_retries;
static AppTimer *s_retry_timer;
static uint32_t s_next_seq = 1;
static uint32_t s_budget;
static CommCallStateHandler s_call_state_handler;
static AppTimer *s_slow_timer;
static bool s_fast;

static void slow_down(void *data) {
  s_slow_timer = NULL;
  s_fast = false;
  app_comm_set_sniff_interval(SNIFF_INTERVAL_NORMAL);
}

// At rest the watch spaces its Bluetooth exchanges out to save power, and a
// 4 KB page then takes about 1.5 s. Ask for a responsive link while messages
// flow and let it relax a few seconds after the last one.
static void keep_link_fast(void) {
  if (!s_fast) {
    app_comm_set_sniff_interval(SNIFF_INTERVAL_REDUCED);
    s_fast = true;
  }
  if (s_slow_timer) {
    app_timer_reschedule(s_slow_timer, FAST_LINK_LINGER_MS);
  } else {
    s_slow_timer = app_timer_register(FAST_LINK_LINGER_MS, slow_down, NULL);
  }
}

static void send_head(void);

static char *dup_string(const char *source) {
  if (!source) {
    return NULL;
  }
  size_t length = strlen(source);
  char *copy = malloc(length + 1);
  if (copy) {
    memcpy(copy, source, length + 1);
  }
  return copy;
}

static Pending *find_pending(uint32_t seq) {
  for (int i = 0; i < PENDING_SIZE; i++) {
    if (s_pending[i].handler && s_pending[i].seq == seq) {
      return &s_pending[i];
    }
  }
  return NULL;
}

static void finish(Pending *pending, DictionaryIterator *reply) {
  CommReplyHandler handler = pending->handler;
  void *context = pending->context;
  if (pending->timer) {
    app_timer_cancel(pending->timer);
  }
  *pending = (Pending){ 0 };
  // Last, so that the handler is free to queue a new request.
  handler(reply, context);
}

static void timeout_callback(void *data) {
  Pending *pending = find_pending((uint32_t)(uintptr_t)data);
  if (pending) {
    pending->timer = NULL;
    finish(pending, NULL);
  }
}

static void pop_head(void) {
  free(s_queue[0].text);
  free(s_queue[0].address);
  s_queue_len--;
  memmove(&s_queue[0], &s_queue[1], s_queue_len * sizeof(Queued));
}

// The handler runs from a timer, never from inside comm_send, so a caller that
// stores the sequence it returns is not undercut by its own failure callback.
static void fail_head(void) {
  uint32_t seq = s_queue[0].seq;
  pop_head();
  Pending *pending = find_pending(seq);
  if (!pending) {
    return;
  }
  if (pending->timer) {
    app_timer_reschedule(pending->timer, 0);
  } else {
    pending->timer = app_timer_register(0, timeout_callback, (void *)(uintptr_t)seq);
  }
}

// A send timeout means the acknowledgement was lost, not that the phone never
// got the message: only requests that read are safe to send again.
static bool repeatable(uint8_t cmd) {
  switch (cmd) {
    case CMD_SEND_SMS:
    case CMD_DIAL:
    case CMD_HANG_UP:
    case CMD_MUTE:
    case CMD_AUDIO_ROUTE:
      return false;
    default:
      return true;
  }
}

static void retry_callback(void *data) {
  s_retry_timer = NULL;
  send_head();
}

static void send_head(void) {
  if (s_sending || s_retry_timer || s_queue_len == 0) {
    return;
  }
#ifdef DEMO_NOW
  // The demo phone answers instead of the Bluetooth link.
  while (s_queue_len) {
    Queued *queued = &s_queue[0];
    demo_request(queued->cmd, queued->seq, queued->id, queued->offset, queued->flags,
                 queued->text ? queued->text : queued->address, s_budget);
    pop_head();
  }
  return;
#endif
  Queued *head = &s_queue[0];
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "outbox_begin: %d", (int)result);
    fail_head();
    send_head();
    return;
  }
  dict_write_uint8(iter, MESSAGE_KEY_Cmd, head->cmd);
  dict_write_int32(iter, MESSAGE_KEY_Seq, (int32_t)head->seq);
  dict_write_int32(iter, MESSAGE_KEY_Id, head->id);
  dict_write_int32(iter, MESSAGE_KEY_Offset, head->offset);
  if (head->flags) {
    dict_write_int32(iter, MESSAGE_KEY_Flags, head->flags);
  }
  if (head->cmd == CMD_HELLO) {
    dict_write_int32(iter, MESSAGE_KEY_Budget, (int32_t)s_budget);
  }
  if (head->text) {
    dict_write_cstring(iter, MESSAGE_KEY_Text, head->text);
  }
  if (head->address) {
    dict_write_cstring(iter, MESSAGE_KEY_Address, head->address);
  }
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "outbox_send: %d", (int)result);
    fail_head();
    send_head();
    return;
  }
  s_sending = true;
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
  s_sending = false;
  s_retries = 0;
  pop_head();
  send_head();
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  s_sending = false;
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", (int)reason);
  bool again = reason == APP_MSG_BUSY || (reason == APP_MSG_SEND_TIMEOUT && repeatable(s_queue[0].cmd));
  if (s_retries < MAX_RETRIES && again) {
    s_retries++;
    s_retry_timer = app_timer_register(RETRY_DELAY_MS, retry_callback, NULL);
    return;
  }
  s_retries = 0;
  fail_head();
  send_head();
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  keep_link_fast();
  int32_t cmd = comm_get_int(iter, MESSAGE_KEY_Cmd, 0);
  if (cmd == CMD_CALL_STATE && s_call_state_handler) {
    s_call_state_handler(iter);
  }
  Tuple *seq = dict_find(iter, MESSAGE_KEY_Seq);
  if (seq) {
    Pending *pending = find_pending((uint32_t)comm_get_int(iter, MESSAGE_KEY_Seq, 0));
    if (pending) {
      finish(pending, iter);
    }
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", (int)reason);
}

void comm_init(CommCallStateHandler call_state_handler) {
  s_call_state_handler = call_state_handler;
  uint32_t inbox = app_message_inbox_size_maximum();
  if (inbox > INBOX_CAP) {
    inbox = INBOX_CAP;
  }
  uint32_t outbox = app_message_outbox_size_maximum();
  if (outbox > OUTBOX_SIZE) {
    outbox = OUTBOX_SIZE;
  }
  // Room left next to the blob: the dictionary header (1 byte), Cmd (8), Seq, Status,
  // Offset, Total and Flags (11 each) and the header of Items itself (7), 71 bytes.
  s_budget = inbox - 80;
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(inbox, outbox);
#ifdef DEMO_NOW
  demo_init(inbox_received);
#endif
}

void comm_deinit(void) {
  app_message_deregister_callbacks();
  if (s_slow_timer) {
    app_timer_cancel(s_slow_timer);
    slow_down(NULL);
  }
  while (s_queue_len) {
    pop_head();
  }
  for (int i = 0; i < PENDING_SIZE; i++) {
    if (s_pending[i].timer) {
      app_timer_cancel(s_pending[i].timer);
    }
  }
}

uint32_t comm_budget(void) {
  return s_budget;
}

uint32_t comm_send(const CommRequest *request, CommReplyHandler handler, void *context) {
  if (s_queue_len == QUEUE_SIZE) {
    return 0;
  }
  keep_link_fast();
  Pending *slot = NULL;
  if (handler) {
    for (int i = 0; i < PENDING_SIZE && !slot; i++) {
      if (!s_pending[i].handler) {
        slot = &s_pending[i];
      }
    }
    if (!slot) {
      return 0;
    }
  }
  uint32_t seq = s_next_seq++;
  s_queue[s_queue_len++] = (Queued){
    .cmd = request->cmd,
    .seq = seq,
    .id = request->id,
    .offset = request->offset,
    .flags = request->flags,
    .text = dup_string(request->text),
    .address = dup_string(request->address),
  };
  if (slot) {
    *slot = (Pending){
      .seq = seq,
      .handler = handler,
      .context = context,
      .timer = app_timer_register(request->cmd == CMD_SEND_SMS ? SMS_REPLY_TIMEOUT_MS : REPLY_TIMEOUT_MS,
                                  timeout_callback, (void *)(uintptr_t)seq),
    };
  }
  send_head();
  return seq;
}

void comm_cancel(void *context) {
  for (int i = 0; i < PENDING_SIZE; i++) {
    if (s_pending[i].handler && s_pending[i].context == context) {
      if (s_pending[i].timer) {
        app_timer_cancel(s_pending[i].timer);
      }
      s_pending[i] = (Pending){ 0 };
    }
  }
}

int32_t comm_get_int(DictionaryIterator *iter, uint32_t key, int32_t fallback) {
  Tuple *tuple = dict_find(iter, key);
  if (!tuple) {
    return fallback;
  }
  bool is_signed = tuple->type == TUPLE_INT;
  switch (tuple->length) {
    case 1:
      return is_signed ? tuple->value->int8 : tuple->value->uint8;
    case 2:
      return is_signed ? tuple->value->int16 : tuple->value->uint16;
    case 4:
      return is_signed ? tuple->value->int32 : (int32_t)tuple->value->uint32;
    default:
      return fallback;
  }
}

const char *comm_get_string(DictionaryIterator *iter, uint32_t key) {
  Tuple *tuple = dict_find(iter, key);
  return tuple && tuple->type == TUPLE_CSTRING ? tuple->value->cstring : NULL;
}
