/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "lock.hpp"

namespace mesos {
namespace internal {

Lock::Lock(pthread_mutex_t* _mutex)
  : mutex(_mutex), locked(false)
{
  lock();
}


void Lock::lock()
{
  if (!locked) {
    pthread_mutex_lock(mutex);
    locked = true;
  }
}


void Lock::unlock()
{
  if (locked) {
    pthread_mutex_unlock(mutex);
    locked = false;
  }
}


Lock::~Lock()
{
  unlock();
}

ReadLock::ReadLock(pthread_rwlock_t* _lock) : lock(_lock)
{
  pthread_rwlock_rdlock(lock);
}

ReadLock::~ReadLock() {
  pthread_rwlock_unlock(lock);
}

WriteLock::WriteLock(pthread_rwlock_t* _lock) : lock(_lock)
{
  pthread_rwlock_wrlock(lock);
}

WriteLock::~WriteLock() {
  pthread_rwlock_unlock(lock);
}

} // namespace internal {
} // namespace mesos {
