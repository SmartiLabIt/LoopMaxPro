#include "kernel/worker/worker.h"

#include "kernel/log/logger.h"

namespace lmp {

bool Worker::inlineDefault_ = false;

void Worker::begin(hal::IRtos* rtos) {
  rtos_ = rtos;
  inline_ = inlineDefault_;
  if (!rtos_ || mutex_) return;
  mutex_ = rtos_->createMutex();
  wake_ = rtos_->createSemaphore();
  left_ = rtos_->createSemaphore();
}

Worker::~Worker() {
  if (!taskUp_) return;
  // On a board the worker lives as long as the firmware. Where a system is built and taken
  // down, as in the tests, the task must not be left with an object that is gone.
  quit_ = true;
  wake_->give();
  left_->take(5000);
}

size_t Worker::waiting() const {
  hal::LockGuard guard(mutex_);
  return count_;
}

Status Worker::post(Job job) {
  if (!job) return Status::Invalid;
  {
    hal::LockGuard guard(mutex_);
    if (count_ >= LMP_WORKER_QUEUE) return Status::Busy;
    jobs_[(head_ + count_) % LMP_WORKER_QUEUE] = job;
    ++count_;
  }

  // The task is created with the first job: a system that never asks has none.
  if (!inline_ && !taskUp_ && rtos_ && rtos_->preemptive() && wake_) {
    hal::TaskConfig cfg;
    cfg.name = taskName_;
    cfg.stackBytes = stackBytes_;
    cfg.priority = 4;
    cfg.core = -1;
    taskUp_ = rtos_->createTask(cfg, &Worker::taskEntry, this) == Status::Ok;
    if (!taskUp_) LMP_LOGW(kLogSys, "worker: no task, the jobs run in the round of the system");
  }
  if (threaded()) wake_->give();
  return Status::Ok;
}

bool Worker::take(Job& out) {
  hal::LockGuard guard(mutex_);
  if (count_ == 0) return false;
  out = jobs_[head_];
  jobs_[head_].reset();
  head_ = (head_ + 1) % LMP_WORKER_QUEUE;
  --count_;
  running_ = true;
  return true;
}

void Worker::serve() {
  Job job;
  while (!quit_ && take(job)) {
    job();
    hal::LockGuard guard(mutex_);
    running_ = false;
    ++done_;
  }
}

void Worker::poll() {
  if (threaded()) return;
  Job job;
  if (!take(job)) return;
  job();
  hal::LockGuard guard(mutex_);
  running_ = false;
  ++done_;
}

void Worker::taskEntry(void* arg) {
  Worker* self = static_cast<Worker*>(arg);
  // Kept here: after the last word to the worker, the worker may be gone.
  hal::IRtos* rtos = self->rtos_;
  hal::ISemaphore* left = self->left_;
  while (!self->quit_) {
    self->wake_->take(1000);
    if (!self->inline_) self->serve();
  }
  left->give();
  // A task must never return: it rests.
  for (;;) rtos->delayMs(1000);
}

}  // namespace lmp
