// LoopMaxPro - worker: the place for what takes long.
// A request to a server or a download lasts seconds: it runs here, in a task of its own, and
// the application task keeps going. On the boards without tasks a job runs inside the round
// of the system, which waits for it.
//
// A job never touches the state of services and modules: it works on data of its own and
// leaves the result where the application task picks it up.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "base/delegate.h"
#include "base/status.h"
#include "hal/api/irtos.h"
#include "lmp_config.h"

namespace lmp {

class Worker {
 public:
  using Job = Delegate<void()>;

  Worker() = default;
  // Waits for the job that is running and lets the task go.
  ~Worker();
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  // Call once, before the first job. rtos can be null.
  void begin(hal::IRtos* rtos);
  // Runs the jobs inside poll() also where tasks exist: every step happens when the caller
  // asks for it. For the tests.
  void setInline(bool enabled) { inline_ = enabled; }
  // What begin() chooses for the workers created from now on.
  static void setInlineDefault(bool enabled) { inlineDefault_ = enabled; }
  // The name and the stack of its task, before the first job: the worker of a service can
  // need less than the one of the system.
  void setTask(const char* name, uint32_t stackBytes) {
    taskName_ = name;
    stackBytes_ = stackBytes;
  }

  // Queues a job. Busy when the queue is full.
  Status post(Job job);
  // Application task, at every round: runs one queued job when there is no task for them.
  void poll();

  size_t waiting() const;
  // True while a job is running.
  bool busy() const { return running_; }
  // True when the jobs run in a task of their own.
  bool threaded() const { return taskUp_ && !inline_; }
  uint32_t done() const { return done_; }

 private:
  bool take(Job& out);
  void serve();
  static void taskEntry(void* arg);

  hal::IRtos* rtos_ = nullptr;
  hal::IMutex* mutex_ = nullptr;
  hal::ISemaphore* wake_ = nullptr;
  hal::ISemaphore* left_ = nullptr;   // given by the task when it lets go
  Job jobs_[LMP_WORKER_QUEUE];
  size_t head_ = 0;
  size_t count_ = 0;
  uint32_t done_ = 0;
  bool inline_ = false;
  bool taskUp_ = false;
  const char* taskName_ = "lmp_worker";
  uint32_t stackBytes_ = LMP_WORKER_STACK;
  volatile bool quit_ = false;
  volatile bool running_ = false;

  static bool inlineDefault_;
};

}  // namespace lmp
