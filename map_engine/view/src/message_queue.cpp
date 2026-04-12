#include "map_engine/message_queue.h"

void MessageQueue::push(WindowMessage m) {
  messages_.push_back(m);
}

bool MessageQueue::pop(WindowMessage* out) {
  if (messages_.empty() || !out) {
    return false;
  }
  *out = messages_.front();
  messages_.pop_front();
  return true;
}

bool MessageQueue::empty() const {
  return messages_.empty();
}

void MessageQueue::clear() {
  messages_.clear();
}
