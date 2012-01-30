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

#ifndef __LOCK_HPP__
#define __LOCK_HPP__

#include <pthread.h>

namespace mesos {
namespace internal {

// RAII class for locking pthread_mutexes.
class Lock
{
public:
  Lock(pthread_mutex_t* _mutex);
  ~Lock();

  void lock();
  void unlock();

private:
  pthread_mutex_t* mutex;
  bool locked;
};

class ReadLock
{
public:
  ReadLock(pthread_rwlock_t* _lock);
  ~ReadLock();

private:
  pthread_rwlock_t* lock;
};

class WriteLock
{
public:
  WriteLock(pthread_rwlock_t* _lock);
  ~WriteLock();

private:
  pthread_rwlock_t* lock;
};

} // namespace internal {
} // namespace mesos {

#endif // __LOCK_HPP__
