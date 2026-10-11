// LoopMaxPro - event bus.
// publish() can be called from any task and only queues the event.
// Handlers always run on the application task, from dispatch().
#pragma once
#include <stddef.h>

#include "base/delegate.h"
#include "base/ring_buffer.h"
#include "hal/api/irtos.h"
#include "kernel/event/event.h"
#include "lmp_config.h"

namespace lmp {

using EventHandler = Delegate<void(const Event&)>;

class EventBus {
 public:
  explicit EventBus(hal::IMutex* mutex) : mutex_(mutex) {}

  // Returns false when the table is full.
  bool subscribe(EventHandler handler);

  // Returns false when the queue is full: the event is lost and counted.
  bool publish(const Event& event);

  // Delivers the queued events to every subscriber. Returns how many were delivered.
  // Events published by the handlers are delivered by the next call.
  size_t dispatch();

  size_t pending() const;
  uint32_t lost() const { return lost_; }
  size_t subscribers() const { return count_; }

 private:
  hal::IMutex* mutex_;
  EventHandler handlers_[LMP_MAX_SUBSCRIBERS];
  size_t count_ = 0;
  Event queue_[LMP_EVENT_QUEUE];
  size_t head_ = 0;
  size_t size_ = 0;
  uint32_t lost_ = 0;
};

}  // namespace lmp
