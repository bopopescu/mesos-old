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

#ifndef __ALLOCATOR_HPP__
#define __ALLOCATOR_HPP__

#include <stout/hashmap.hpp>
#include <stout/option.hpp>

#include "common/resources.hpp"

#include "master/master.hpp"


namespace mesos {
namespace internal {
namespace master {

// Basic model of an allocator: resources are allocated to a framework
// in the form of offers. A framework can refuse some resources in
// offers and run tasks in others. Resources can be recovered from a
// framework when tasks finish/fail (or are lost due to a slave
// failure) or when an offer is rescinded.

class Allocator : public process::Process<Allocator> {
public:
  virtual ~Allocator() {}

  virtual void initialize(const process::PID<Master>& master) = 0;

  virtual void frameworkAdded(const FrameworkID& frameworkId,
                              const FrameworkInfo& frameworkInfo,
                              const Resources& used) = 0;

  virtual void frameworkDeactivated(const FrameworkID& frameworkId) = 0;

  virtual void frameworkRemoved(const FrameworkID& frameworkId) = 0;

  virtual void slaveAdded(const SlaveID& slaveId,
                          const SlaveInfo& slaveInfo,
                          const hashmap<FrameworkID, Resources>& used) = 0;

  virtual void slaveRemoved(const SlaveID& slaveId) = 0;

  virtual void updateWhitelist(
      const Option<hashset<std::string> >& whitelist) = 0;

  virtual void resourcesRequested(
      const FrameworkID& frameworkId,
      const std::vector<Request>& requests) = 0;

  // Whenever resources offered to a framework go unused (e.g.,
  // refused) the master invokes this callback.
  virtual void resourcesUnused(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const ResourceHints& offerResources,
      const Option<Filters>& filters) = 0;

  // Whenever resources are "recovered" in the cluster (e.g., a task
  // finishes, an offer is removed because a framework has failed or
  // is failing over) the master invokes this callback.
  virtual void resourcesRecovered(
      const FrameworkID& frameworkId,
      const SlaveID& slaveId,
      const ResourceHints& offerResources) = 0;

  // For allocators that want to track actual resource usage to determine
  // what offers to make. taskRemoved() is called _in addition_ to
  // resourcesRecovered
  // TODO(charles): do we need these?
  virtual void taskAdded(const FrameworkID& frameworkID, const TaskInfo& task) {}
  virtual void taskRemoved(const FrameworkID& frameworkId, const TaskInfo& task) {}
  // Because executors have usage, too
  virtual void executorAdded(const FrameworkID& frameworkId,
                             const SlaveID& slaveId,
                             const ExecutorInfo& executorInfo) {}
  virtual void executorRemoved(const FrameworkID& frameworkId,
                               const SlaveID& slaveId,
                               const ExecutorInfo& executorInfo) {}

  // Whenever a framework that has filtered resources wants to revive
  // offers for those resources the master invokes this callback.
  virtual void offersRevived(const FrameworkID& frameworkId) = 0;

  virtual void timerTick() {}

  virtual void gotUsage(const UsageMessage& usage) {}

  virtual void sanityCheck() {}
};

} // namespace master {
} // namespace internal {
} // namespace mesos {

#endif // __ALLOCATOR_HPP__
