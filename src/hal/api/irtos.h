// LoopMaxPro HAL - tasks and synchronization.
// Boards without an RTOS provide a cooperative implementation: same interface, single context.
// Objects are created at boot only and live for the whole run.
#pragma once
#include "hal/api/types.h"

namespace lmp::hal {

class IMutex {
 public:
  virtual ~IMutex() = default;
  virtual void lock() = 0;
  virtual void unlock() = 0;
};

class ISemaphore {
 public:
  virtual ~ISemaphore() = default;
  virtual void give() = 0;
  virtual bool take(uint32_t timeoutMs) = 0;
};

class IQueue {
 public:
  virtual ~IQueue() = default;
  virtual bool send(const void* item, uint32_t timeoutMs) = 0;
  virtual bool receive(void* item, uint32_t timeoutMs) = 0;
  virtual size_t waiting() const = 0;
};

using TaskFn = void (*)(void* arg);

struct TaskConfig {
  const char* name = "task";
  uint32_t stackBytes = 4096;
  uint8_t priority = 5;
  int8_t core = -1;  // -1 = any core
};

class IRtos {
 public:
  virtual ~IRtos() = default;

  virtual bool preemptive() const = 0;
  virtual IMutex* createMutex() = 0;
  virtual ISemaphore* createSemaphore() = 0;
  virtual IQueue* createQueue(size_t itemSize, size_t length) = 0;
  virtual Status createTask(const TaskConfig& cfg, TaskFn fn, void* arg) = 0;

  virtual uintptr_t currentTaskId() const = 0;
  virtual void delayMs(uint32_t ms) = 0;
  virtual void yield() = 0;
};

// Locks a mutex for the duration of a scope. A null mutex means no locking.
class LockGuard {
 public:
  explicit LockGuard(IMutex* m) : m_(m) {
    if (m_) m_->lock();
  }
  ~LockGuard() {
    if (m_) m_->unlock();
  }
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

 private:
  IMutex* m_;
};

}  // namespace lmp::hal
