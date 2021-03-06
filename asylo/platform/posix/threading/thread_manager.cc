/*
 *
 * Copyright 2017 Asylo authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "asylo/platform/posix/threading/thread_manager.h"

#include <pthread.h>

#include <algorithm>
#include <cstdlib>
#include <memory>

#include "asylo/platform/posix/pthread_impl.h"
#include "asylo/platform/primitives/trusted_primitives.h"

namespace asylo {
namespace {

// Returns when |predicate| returns true. |mutex| must be locked.
void WaitFor(const std::function<bool()> &predicate, pthread_cond_t *cond,
             pthread_mutex_t *mutex) {
  while (!predicate()) {
    int ret = pthread_cond_wait(cond, mutex);
    CHECK_EQ(ret, 0);
  }
}

}  // namespace

using pthread_impl::PthreadMutexLock;

bool ReturnFalse() { return false; }

ThreadManager::Thread::Thread(const ThreadOptions &options,
                              std::function<void *()> start_routine)
    : start_routine_(std::move(start_routine)), detached_(options.detached) {}

void ThreadManager::Thread::Run() {
  // Unblock anyone waiting for thread to start.
  UpdateThreadState(ThreadState::RUNNING);

  // Run the thread and store the start function's return value.
  ret_ = start_routine_();

  // Run cleanup routines, if any.
  RunCleanupRoutines();

  // Unblock anyone waiting for this to finish.
  UpdateThreadState(ThreadState::DONE);
}

void *ThreadManager::Thread::GetReturnValue() const { return ret_; }

bool ThreadManager::Thread::detached() const { return detached_; }

void ThreadManager::Thread::UpdateThreadId(const pthread_t thread_id) {
  PthreadMutexLock lock(&lock_);
  thread_id_ = thread_id;
}

pthread_t ThreadManager::Thread::GetThreadId() {
  PthreadMutexLock lock(&lock_);
  return thread_id_;
}

void ThreadManager::Thread::UpdateThreadState(const ThreadState &new_state) {
  PthreadMutexLock lock(&lock_);
  this->state_ = new_state;
  int ret = pthread_cond_broadcast(&state_change_cond_);
  CHECK_EQ(ret, 0);
}

void ThreadManager::Thread::WaitForThreadToEnterState(
    const ThreadState &desired_state,
    const std::function<bool()> &alternative_predicate) {
  PthreadMutexLock lock(&lock_);
  WaitFor(
      [this, desired_state, alternative_predicate]() {
        return state_ == desired_state || alternative_predicate();
      },
      &state_change_cond_, &lock_);
}

void ThreadManager::Thread::WaitForThreadToExitState(
    const ThreadState &undesired_state) {
  PthreadMutexLock lock(&lock_);
  WaitFor([this, undesired_state]() { return state_ != undesired_state; },
          &state_change_cond_, &lock_);
}

void ThreadManager::Thread::Detach() {
  PthreadMutexLock lock(&lock_);
  int ret = pthread_cond_broadcast(&state_change_cond_);
  CHECK_EQ(ret, 0);
  detached_ = true;
}

void ThreadManager::Thread::PushCleanupRoutine(
    const std::function<void()> &func) {
  cleanup_functions_.push(func);
}

void ThreadManager::Thread::PopCleanupRoutine(bool execute) {
  // pthread_cleanup_push and pthread_cleanup_pop are guaranteed by the compiler
  // (at compile-time!) to always occur in pairs. An attempt to pop an empty
  // stack means something is wrong internally.
  CHECK(!cleanup_functions_.empty());

  std::function<void()> func = cleanup_functions_.top();
  cleanup_functions_.pop();

  if (execute) {
    func();
  }
}

void ThreadManager::Thread::RunCleanupRoutines() {
  while (!cleanup_functions_.empty()) {
    PopCleanupRoutine(/*execute=*/true);
  }
}

ThreadManager *ThreadManager::GetInstance() {
  static ThreadManager *instance = new ThreadManager();
  return instance;
}

std::shared_ptr<ThreadManager::Thread> ThreadManager::EnqueueThread(
    const ThreadOptions &options,
    const std::function<void *()> &start_routine) {
  PthreadMutexLock lock(&threads_lock_);

  queued_threads_.emplace(std::make_shared<Thread>(options, start_routine));
  std::shared_ptr<Thread> thread = queued_threads_.back();

  // If a Thread object cannot be allocated, abort.
  CHECK(thread != nullptr);

  pthread_cond_broadcast(&threads_cond_);
  return thread;
}

std::shared_ptr<ThreadManager::Thread> ThreadManager::DequeueThread() {
  PthreadMutexLock lock(&threads_lock_);
  // There should be a one-to-one mapping of threads donated to the enclave
  // and threads created from above at the pthread API layer waiting to run.
  // If a thread gets donated and there's no thread waiting to run, something
  // has gone very wrong.
  CHECK(!queued_threads_.empty());

  std::shared_ptr<Thread> thread = queued_threads_.front();
  queued_threads_.pop();

  // Bind the Thread we just took off the queue to the thread id of the donated
  // enclave thread we're running under.
  const pthread_t thread_id = pthread_self();
  thread->UpdateThreadId(thread_id);

  threads_[thread_id] = thread;

  pthread_cond_broadcast(&threads_cond_);
  return thread;
}

int ThreadManager::CreateThread(const std::function<void *()> &start_routine,
                                const ThreadOptions &options,
                                pthread_t *const thread_id_out) {
  std::shared_ptr<Thread> thread = EnqueueThread(options, start_routine);

  // Exit and create a thread to enter with EnclaveCall DonateThread.
  if (asylo::primitives::TrustedPrimitives::CreateThread()) {
    return ECHILD;
  }

  // Wait until a thread enters and executes the job.
  thread->WaitForThreadToExitState(Thread::ThreadState::QUEUED);

  if (thread_id_out != nullptr) {
    *thread_id_out = thread->GetThreadId();
  }

  return 0;
}

// StartThread is called from trusted_application.cc as the start routine when
// a new thread is donated to the Enclave.
int ThreadManager::StartThread() {
  std::shared_ptr<Thread> thread = DequeueThread();

  // Run the start_routine.
  thread->Run();

  // Wait for the caller to join before releasing the thread if the thread is
  // joinable.
  thread->WaitForThreadToEnterState(Thread::ThreadState::JOINED,
                                    std::bind(&Thread::detached, thread));

  PthreadMutexLock threads_lock(&threads_lock_);
  threads_.erase(pthread_self());
  pthread_cond_broadcast(&threads_cond_);

  return 0;
}

int ThreadManager::JoinThread(const pthread_t thread_id,
                              void **return_value_out) {
  std::shared_ptr<Thread> thread = GetThread(thread_id);
  if (thread == nullptr) {
    return ESRCH;
  }

  // Wait until the job is finished executing.
  thread->WaitForThreadToEnterState(Thread::ThreadState::DONE,
                                    std::bind(&Thread::detached, thread));

  if (thread->detached()) {
    return EINVAL;
  }

  if (return_value_out != nullptr) {
    *return_value_out = thread->GetReturnValue();
  }

  thread->UpdateThreadState(Thread::ThreadState::JOINED);

  return 0;
}

int ThreadManager::DetachThread(const pthread_t thread_id) {
  std::shared_ptr<Thread> thread = GetThread(thread_id);
  if (thread == nullptr) {
    return ESRCH;
  }

  if (thread->detached()) {
    return EINVAL;
  }

  thread->Detach();

  return 0;
}

std::shared_ptr<ThreadManager::Thread> ThreadManager::GetThread(
    pthread_t thread_id) {
  PthreadMutexLock lock(&threads_lock_);
  auto it = threads_.find(thread_id);
  if (it == threads_.end()) {
    return nullptr;
  }
  return it->second;
}

void ThreadManager::PushCleanupRoutine(const std::function<void()> &func) {
  std::shared_ptr<Thread> thread = GetThread(pthread_self());

  // This is only ever called on pthread_self, so the thread should always be
  // found.
  CHECK(thread != nullptr);

  // No lock needed since this is a per-thread data structure.
  thread->PushCleanupRoutine(func);
}

void ThreadManager::PopCleanupRoutine(bool execute) {
  std::shared_ptr<Thread> thread = GetThread(pthread_self());

  // This is only ever called on pthread_self, so the thread should always be
  // found.
  CHECK(thread != nullptr);

  // No lock needed since this is a per-thread data structure.
  thread->PopCleanupRoutine(execute);
}

void ThreadManager::Finalize() {
  // Wait for any expected threads to be donated and all threads to return from
  // start_routine.
  PthreadMutexLock lock(&threads_lock_);
  WaitFor([this]() { return queued_threads_.empty() && threads_.empty(); },
          &threads_cond_, &threads_lock_);
}

}  // namespace asylo
