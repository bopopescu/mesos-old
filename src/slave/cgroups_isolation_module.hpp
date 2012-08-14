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

#ifndef __CGROUPS_ISOLATION_MODULE_HPP__
#define __CGROUPS_ISOLATION_MODULE_HPP__

#include <string>

#include <process/future.hpp>
#include <process/pid.hpp>

#include <stout/hashmap.hpp>
#include <stout/hashset.hpp>
#include <stout/lambda.hpp>

#include "launcher/launcher.hpp"

#include "slave/flags.hpp"
#include "slave/isolation_module.hpp"
#include "slave/reaper.hpp"
#include "slave/slave.hpp"
#include "slave/statistics.hpp"

namespace mesos {
namespace internal {
namespace slave {

enum CgroupOOMPolicy {
  OOM_KILL,
  OOM_KILL_PRIORITY
};

class CgroupsIsolationModule
  : public IsolationModule,
    public ProcessExitedListener,
    public ResourceStatisticsCollector
{
public:
  CgroupsIsolationModule();

  virtual ~CgroupsIsolationModule();

  virtual void initialize(const Flags& flags,
                          bool local,
                          const process::PID<Slave>& slave);

  virtual void launchExecutor(const FrameworkID& frameworkId,
                              const FrameworkInfo& frameworkInfo,
                              const ExecutorInfo& executorInfo,
                              const std::string& directory,
                              const ResourceHints& resources);

  virtual void killExecutor(const FrameworkID& frameworkId,
                            const ExecutorID& executorId);

  virtual void resourcesChanged(const FrameworkID& frameworkId,
                                const ExecutorID& executorId,
                                const ResourceHints& resources);

  virtual Option<ResourceStatistics> collectResourceStatistics(
      const FrameworkID& frameworkId,
      const ExecutorID& executorId);

protected:
  // Main method executed after a fork() to create a Launcher for launching an
  // executor's process. The Launcher will chdir() to the child's working
  // directory, fetch the executor, set environment varibles, switch user, etc,
  // and finally exec() the executor process.
  virtual launcher::ExecutorLauncher* createExecutorLauncher(
      const FrameworkID& frameworkId,
      const FrameworkInfo& frameworkInfo,
      const ExecutorInfo& executorInfo,
      const std::string& directory);


  virtual void processExited(pid_t pid, int status);

private:
  // No copying, no assigning.
  CgroupsIsolationModule(const CgroupsIsolationModule&);
  CgroupsIsolationModule& operator = (const CgroupsIsolationModule&);

  // The cgroup information for each live executor.
  struct CgroupInfo
  {
    FrameworkID frameworkId;
    ExecutorID executorId;

    // The UUID tag to distinguish between different launches of the same
    // executor (which have the same frameworkId and executorId).
    std::string tag;

    // PID of the leading process of the executor.
    pid_t pid;

    // Whether the executor has been killed.
    bool killed;

    // Used to cancel the OOM listening.
    process::Future<uint64_t> oomNotifier;
  };

  // The callback which will be invoked when "cpus" resource has changed.
  // @param   frameworkId   The id of the given framework.
  // @param   executorId    The id of the given executor.
  // @param   resources     The handle for the resources.
  // @return  Whether the operation successes.
  Try<bool> cpusChanged(const FrameworkID& frameworkId,
                        const ExecutorID& executorId,
                        const ResourceHints& resources);

  // The callback which will be invoked when "mem" resource has changed.
  // @param   frameworkId   The id of the given framework.
  // @param   executorId    The id of the given executor.
  // @param   resources     The handle for the resources.
  // @return  Whether the operation successes.
  Try<bool> memChanged(const FrameworkID& frameworkId,
                       const ExecutorID& executorId,
                       const ResourceHints& resources);

  // Start listening on OOM events. This function will create an eventfd and
  // start polling on it.
  // @param   frameworkId   The id of the given framework.
  // @param   executorId    The id of the given executor.
  void oomListen(const FrameworkID& frameworkId,
                 const ExecutorID& executorId);

  // This function is invoked when the polling on eventfd has a result.
  // @param   frameworkId   The id of the given framework.
  // @param   executorId    The id of the given executor.
  // @param   tag           The uuid tag.
  void oomWaited(const FrameworkID& frameworkId,
                 const ExecutorID& executorId,
                 const std::string& tag,
                 const process::Future<uint64_t>& future);

  // This function is invoked when the OOM event happens.
  // @param   frameworkId   The id of the given framework.
  // @param   executorId    The id of the given executor.
  // @param   tag           The uuid tag.
  void oom(const FrameworkID& frameworkId,
           const ExecutorID& executorId,
           const std::string& tag);

  void setupOuterOom();

  void outerOomWaited(const process::Future<uint64_t>& future);

  void outerOom();

  // Parse the output from a cgroup subsystem stat file and return a map between
  // resource name and the corresponding stat value.
  // @param   input         The content of the subsystem stat file.
  // @return  The map between resource name and the corresponding stat value.
  Try<hashmap<std::string, unsigned long> > parseStat(
      const std::string& input);

  void insertStats(const std::string& hierarchy,
                   const std::string& container,
                   const std::string& controller,
                   const std::string& prefix,
                   hashmap<std::string, int64_t>* counters);


  // This callback is invoked when destroy cgroup has a result.
  // @param   cgroup        The cgroup that is being destroyed.
  // @param   future        The future describing the destroy process.
  void destroyWaited(const std::string& cgroup,
                     const process::Future<bool>& future);

  // Register a cgroup in the isolation module.
  // @param   frameworkId   The id of the given framework.
  // @param   executorId    The id of the given executor.
  // @return  A pointer to the cgroup info registered.
  CgroupInfo* registerCgroupInfo(const FrameworkID& frameworkId,
                                 const ExecutorID& executorId);

  // Unregister a cgroup in the isolation module.
  // @param   frameworkId   The id of the given framework.
  // @param   executorId    The id of the given executor.
  void unregisterCgroupInfo(const FrameworkID& frameworkId,
                            const ExecutorID& executorId);

  // Find a registered cgroup by the PID of the leading process.
  // @param   pid           The PID of the leading process in the cgroup.
  // @return  A pointer to the cgroup info if found, NULL otherwise.
  CgroupInfo* findCgroupInfo(pid_t pid);

  // Find a registered cgroup by the frameworkId and the executorId.
  // @param   frameworkId   The id of the given framework.
  // @param   executorId    The id of the given executor.
  // @return  A pointer to the cgroup info if found, NULL otherwise.
  CgroupInfo* findCgroupInfo(const FrameworkID& frameworkId,
                             const ExecutorID& executorId);

  // Return the canonicalized name of the cgroup used by a given executor in a
  // given framework.
  // @param   frameworkId   The id of the given framework.
  // @param   executorId    The id of the given executor.
  // @return  The canonicalized name of the cgroup.
  std::string getCgroupName(const FrameworkID& frameworkId,
                            const ExecutorID& executorId);

  // Return true if the given name is a valid cgroup name used by this isolation
  // module.
  // @param   name          The name to check.
  // @return  True if the given name is valid cgroup name, False otherwise.
  bool isValidCgroupName(const std::string& name);

  Flags flags;
  bool local;
  process::PID<Slave> slave;
  bool initialized;
  Reaper* reaper;;

  // The cgroup information for each live executor.
  hashmap<FrameworkID, hashmap<ExecutorID, CgroupInfo*> > infos;

  // The path to the cgroups hierarchy root.
  std::string hierarchy;

  // The activated cgroups subsystems that can be used by the module.
  hashset<std::string> activatedSubsystems;

  // The mapping between resource name and corresponding cgroups subsystem.
  hashmap<std::string, std::string> resourceSubsystemMap;

  // Mapping between resource name to the corresponding resource changed
  // handler function.
  hashmap<std::string,
          Try<bool>(CgroupsIsolationModule::*)(
              const FrameworkID&,
              const ExecutorID&,
              const ResourceHints&)> resourceChangedHandlers;

  // XXX FIXME Hack
  hashmap<FrameworkID, hashset<ExecutorID> > recentKills;
  hashmap<FrameworkID, hashset<ExecutorID> > recentOoms;

  process::Future<uint64_t> outerOomNotifier;
  CgroupOOMPolicy oomPolicy;
};

} // namespace mesos {
} // namespace internal {
} // namespace slave {

#endif // __CGROUPS_ISOLATION_MODULE_HPP__
