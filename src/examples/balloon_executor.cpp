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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include <mesos/executor.hpp>

#include <stout/numify.hpp>

using namespace mesos;


// The amount of memory in MB each balloon step consumes.
const static size_t BALLOON_STEP_MB = 64;


// This function will increase the memory footprint gradually. The parameter
// limit specifies the upper limit (in MB) of the memory footprint. The
// parameter step specifies the step size (in MB).
static void balloon(size_t limit)
{
  size_t chunk = BALLOON_STEP_MB * 1024 * 1024;
  for (size_t i = 0; i < limit / BALLOON_STEP_MB; i++) {
    std::cout << "Increasing memory footprint by "
              << BALLOON_STEP_MB << " MB" << std::endl;

    // Allocate virtual memory.
    char* buffer = (char *)malloc(chunk);

    // We use memset here so that the memory actually gets paged in. However,
    // the memory may get paged out again depending on the OS page replacement
    // algorithm. Therefore, to ensure X MB of memory is actually used, we need
    // to pass Y (Y > X) to this function.
    ::memset(buffer, 1, chunk);

    // Try not to increase the memory footprint too fast.
    ::sleep(1);
  }
}


class BalloonExecutor : public Executor
{
public:
  virtual ~BalloonExecutor() {}

  virtual void registered(ExecutorDriver* driver,
                          const ExecutorInfo& executorInfo,
                          const FrameworkInfo& frameworkInfo,
                          const SlaveInfo& slaveInfo)
  {
    std::cout << "Registered" << std::endl;
  }

  virtual void reregistered(ExecutorDriver* driver,
                            const SlaveInfo& slaveInfo)
  {
    std::cout << "Reregistered" << std::endl;
  }

  virtual void disconnected(ExecutorDriver* driver)
  {
    std::cout << "Disconnected" << std::endl;
  }

  virtual void launchTask(ExecutorDriver* driver, const TaskInfo& task)
  {
    std::cout << "Starting task " << task.task_id().value() << std::endl;

    TaskStatus status;
    status.mutable_task_id()->MergeFrom(task.task_id());
    status.set_state(TASK_RUNNING);

    driver->sendStatusUpdate(status);

    std::istringstream istr(task.data());
    int balloonSize = 0, childBalloonSize = 0;
    istr >> balloonSize >> childBalloonSize;
    if (istr) {
      std::cerr << "Could not parse " << task.data();
    }

    pid_t child = 0;

    if (childBalloonSize > 0) {
      child = ::fork();
      if (child == -1) {
        ::setpriority(PRIO_PROCESS, getpid(), 10);
        balloon(childBalloonSize);
        ::_exit(0);
      }
    }

    // Simulate a memory leak situation.
    balloon(balloonSize);

    if (child) {
      waitpid(child, NULL, 0);
    }

    std::cout << "Finishing task " << task.task_id().value() << std::endl;

    status.mutable_task_id()->MergeFrom(task.task_id());
    status.set_state(TASK_FINISHED);

    driver->sendStatusUpdate(status);
  }

  virtual void killTask(ExecutorDriver* driver, const TaskID& taskId)
  {
    std::cout << "Kill task " << taskId.value() << std::endl;
  }

  virtual void frameworkMessage(ExecutorDriver* driver, const std::string& data)
  {
    std::cout << "Framework message: " << data << std::endl;
  }

  virtual void shutdown(ExecutorDriver* driver)
  {
    std::cout << "Shutdown" << std::endl;
  }

  virtual void error(ExecutorDriver* driver, const std::string& message)
  {
    std::cout << "Error message: " << message << std::endl;
  }
};


int main(int argc, char** argv)
{
  BalloonExecutor executor;
  MesosExecutorDriver driver(&executor);
  return driver.run() == DRIVER_STOPPED ? 0 : 1;
}
