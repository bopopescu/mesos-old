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

#include <algorithm>
#include <utility>

#include <glog/logging.h>

#include "norequest/allocator.hpp"
#include <process/process.hpp>

using boost::unordered_map;
using boost::unordered_set;

namespace mesos {
namespace internal {
namespace norequest {

using std::vector;

void
NoRequestAllocator::frameworkAdded(Framework* framework) {
  LOG(INFO) << "add framework";
  allRefusers.clear();
  makeNewOffers(master->getActiveSlaves());
}

void
NoRequestAllocator::frameworkRemoved(Framework* framework) {
  LOG(INFO) << "remove framework " << framework->id;
  foreachvalue (boost::unordered_set<FrameworkID>& refuserSet, refusers) {
    refuserSet.erase(framework->id);
  }
}

void
NoRequestAllocator::slaveAdded(Slave* slave) {
  CHECK_EQ(0, refusers.count(slave));
  LOG(INFO) << "add slave";
  totalResources += slave->info.resources();
  tracker->setCapacity(slave->id, slave->info.resources());
  std::vector<Slave*> slave_alone;
  slave_alone.push_back(slave);
  makeNewOffers(slave_alone);
}

void
NoRequestAllocator::slaveRemoved(Slave* slave) {
  totalResources -= slave->info.resources();
  tracker->setCapacity(slave->id, Resources());
  refusers.erase(slave);
  allRefusers.erase(slave);
}

void
NoRequestAllocator::taskAdded(Task* task) {
  LOG(INFO) << "add task";
  placeUsage(task->framework_id(), task->executor_id(), task->slave_id(),
             task, 0, Option<ExecutorInfo>::none());
}

void
NoRequestAllocator::taskRemoved(Task* task) {
  LOG(INFO) << "remove task";
  placeUsage(task->framework_id(), task->executor_id(), task->slave_id(),
             0, task, Option<ExecutorInfo>::none());
  Slave* slave = master->getSlave(task->slave_id());
  refusers.erase(slave);
  allRefusers.erase(slave);
  std::vector<Slave*> slave_alone;
  slave_alone.push_back(slave);
  makeNewOffers(slave_alone);
}

void
NoRequestAllocator::executorAdded(const FrameworkID& frameworkId,
                                  const SlaveID& slaveId,
                                  const ExecutorInfo& info) {
  LOG(INFO) << "executor added " << info.DebugString();
  placeUsage(frameworkId, info.executor_id(), slaveId, 0, 0, info);
}

void
NoRequestAllocator::executorRemoved(const FrameworkID& frameworkId,
                                    const SlaveID& slaveId,
                                    const ExecutorInfo& info) {
  LOG(INFO) << "executor removed " << info.DebugString();
  tracker->forgetExecutor(frameworkId, info.executor_id(), slaveId);
  knownTasks.erase(ExecutorKey(frameworkId, info.executor_id(), slaveId));
  Slave* slave = master->getSlave(slaveId);
  refusers.erase(slave);
  allRefusers.erase(slave);
  std::vector<Slave*> slave_alone;
  slave_alone.push_back(slave);
  // TODO(Charles): Unit test for this happening
  makeNewOffers(slave_alone);
}


void
NoRequestAllocator::placeUsage(const FrameworkID& frameworkId,
                               const ExecutorID& executorId,
                               const SlaveID& slaveId,
                               Task* newTask, Task* removedTask,
                               Option<ExecutorInfo> maybeExecutorInfo) {
  Resources minResources = tracker->gaurenteedForExecutor(
      slaveId, frameworkId, executorId);
  LOG(INFO) << "min = " << minResources;
  boost::unordered_set<Task*>* tasks = &knownTasks[
    ExecutorKey(frameworkId, executorId, slaveId)];
  // TODO(charles): estimate resources more intelligently
  //                in usage tracker to centralize policy?
  Option<Resources> estimate = Option<Resources>::none();
  if (newTask) {
    // TODO(Charles): Take into account Executor usage if executorAdded()
    //                not yet called.
    tasks->insert(newTask);
    estimate = Option<Resources>(
        tracker->nextUsedForExecutor(slaveId, frameworkId, executorId) +
        newTask->resources());
    minResources += newTask->min_resources();
  } else if (maybeExecutorInfo.isSome()) {
    estimate = Option<Resources>(
        tracker->nextUsedForExecutor(slaveId, frameworkId, executorId) +
        maybeExecutorInfo.get().resources());
    LOG(INFO) << "estimate = " << estimate.get();
    minResources += maybeExecutorInfo.get().min_resources();
  } else if (removedTask) {
    CHECK_EQ(1, tasks->count(removedTask));
    tasks->erase(removedTask);
    minResources -= removedTask->min_resources();
    if (tasks->size() == 0) {
      // TODO(charles): wrong for memory
      estimate = Option<Resources>::some(Resources());
    }
  }

  tracker->placeUsage(frameworkId, executorId, slaveId, minResources, estimate,
                      tasks->size());
}

namespace {

struct ChargedShareComparator {
  ChargedShareComparator(UsageTracker* _tracker, Resources _totalResources,
                         bool _useCharge)
      : tracker(_tracker), totalResources(_totalResources),
        useCharge(_useCharge) {}

  bool operator()(Framework* first, Framework* second) {
    double firstShare = dominantShareOf(first);
    double secondShare = dominantShareOf(second);
    if (firstShare == secondShare) {
      VLOG(3) << "shares equal; comparing "
              << first->id.value() << " and " << second->id.value()
              << " --> " << (first->id.value() < second->id.value());
      return first->id.value() < second->id.value();
    } else {
      return firstShare < secondShare;
    }
  }

  double dominantShareOf(Framework* framework) {
    // TODO(charles): is the right metric?
    // TODO(Charles): Test for this!
    Resources charge = useCharge ?
      tracker->chargeForFramework(framework->id) :
      tracker->nextUsedForFramework(framework->id);
    charge += framework->offeredResources;
    double share = 0.0;
    foreach (const Resource& resource, charge) {
      if (resource.type() == Value::SCALAR) {
        double total =
            totalResources.get(resource.name(), Value::Scalar()).value();
        if (total > 0.0) {
          share = std::max(share, resource.scalar().value() / total);
        }
      }
    }
    VLOG(3) << "computed share of " << framework->id << " = " << share;
    return share;
  }

  UsageTracker *tracker;
  Resources totalResources;
  bool useCharge;
};

}

vector<Framework*>
NoRequestAllocator::getOrderedFrameworks() {
  vector<Framework*> frameworks = master->getActiveFrameworks();
  std::sort(frameworks.begin(), frameworks.end(),
            ChargedShareComparator(tracker, totalResources, useCharge));
  return frameworks;
}

namespace {

bool enoughResources(Resources res) {
  const double kMinCPU = 0.01;
  const double kMinMem = 16;
  return (res.get("cpus", Value::Scalar()).value() > kMinCPU &&
          res.get("mem", Value::Scalar()).value() > kMinMem);
}

Resource kNoCPU = Resources::parse("cpus", "0.0");
Resource kNoMem = Resources::parse("mem", "0.0");

void fixResources(Resources* res) {
  if (res->get(kNoCPU).isNone()) {
    *res += kNoCPU;
  }
  if (res->get(kNoMem).isNone()) {
    *res += kNoMem;
  }
  foreach (Resource& resource, *res) {
    if (resource.scalar().value() < 0.0) {
      resource.mutable_scalar()->set_value(0.0);
    }
  }
}

}

void
NoRequestAllocator::makeNewOffers(const std::vector<Slave*>& slaves) {
  if (dontMakeOffers) return;
  LOG(INFO) << "makeNewOffers for " << slaves.size() << " slaves";
  vector<Framework*> orderedFrameworks = getOrderedFrameworks();

  // expected, min
  unordered_map<Slave*, ResourceHints> freeResources;
  foreach(Slave* slave, slaves) {
    LOG(INFO) << "slave " << slave << "; active = " << slave->active;
    if (!slave->active) continue;
    // TODO(charles): FIXME offered but unlaunched tracking
    Resources offered = slave->resourcesOffered.expectedResources;
    Resources gaurenteedOffered = slave->resourcesOffered.minResources;
    Resources free = tracker->freeForSlave(slave->id).allocatable() - offered;
    Resources gaurenteed =
      tracker->gaurenteedFreeForSlave(slave->id).allocatable() -
      gaurenteedOffered;
    if (enoughResources(free) || enoughResources(gaurenteed)) {
      ResourceHints offer;
      fixResources(&free);
      fixResources(&gaurenteed);
      offer.expectedResources = free;
      offer.minResources = gaurenteed;
      freeResources[slave] = offer;
    } else {
      LOG(INFO) << "not enough for " << slave->id << ": "
                << free << " and " << gaurenteed;
      LOG(INFO) << "offered = " << slave->resourcesOffered;
      LOG(INFO) << "[in use] = " << slave->resourcesInUse;
      LOG(INFO) << "[observed] = "  << slave->resourcesObservedUsed;
    }
  }

  // Clear refusers on any slave that has been refused by everyone.
  // TODO(charles): consider case where offer is filtered??
  foreachkey (Slave* slave, freeResources) {
    if (refusers.count(slave) &&
        refusers[slave].size() == orderedFrameworks.size()) {
      if (allRefusers.count(slave) == 0) {
        VLOG(1) << "Clearing refusers for slave " << slave->id
                << " because EVERYONE has refused resources from it";
        refusers.erase(slave);
        allRefusers.insert(slave);
      } else {
        VLOG(1) << "EVERYONE has refused offers from " << slave->id
                << " but we've already had it completely refused twice.";
      }
    }
  }

  foreach (Framework* framework, orderedFrameworks) {
    hashmap<Slave*, ResourceHints> offerable;
    // TODO(charles): offer both separately;
    //                ideally frameworks should be allowed to get gaurentees
    //                of some resources (e.g. memory) and not others (e.g. CPU)
    foreachpair (Slave* slave,
                 const ResourceHints& offerRes,
                 freeResources) {
      if (!(refusers.count(slave) && refusers[slave].count(framework->id)) &&
          !framework->filters(slave, offerRes)) {
        offerable[slave] = offerRes;
        VLOG(1) << "offering " << framework->id << " "
                  << offerRes << " on slave " << slave->id;
      } else {
        VLOG(2) << framework->id << " not accepting offer on " << slave->id
                << " -- refuser? "
                << ((refusers.count(slave) &&
                     refusers[slave].count(framework->id)) ? "yes" : "no")
                << " -- filtered " << framework->filters(slave, offerRes)
                << " -- offerRes " << offerRes;
      }
    }

    if (offerable.size() > 0) {
      LOG(INFO) << "have " << offerable.size() << " offers for "
                << framework->id;
    }

    if (offerable.size() > 0) {
      foreachkey(Slave* slave, offerable) {
        freeResources.erase(slave);
      }
      master->makeOffers(framework, offerable);
    }
  }
}

void NoRequestAllocator::resourcesUnused(const FrameworkID& frameworkId,
                                         const SlaveID& slaveId,
                                         const ResourceHints& unusedResources) {
  LOG(INFO) << "resourcesUnused: " << frameworkId.value() << ", "
            << slaveId.value() << unusedResources;
  /* Before recording a framework as a refuser, make sure we would offer
   * them at least as many resources now. If not, give them a chance to get the
   * resources we reclaimed asynchronously.
   */
  if (tracker->freeForSlave(slaveId) <= unusedResources.expectedResources &&
      tracker->gaurenteedFreeForSlave(slaveId) <= unusedResources.minResources) {
    refusers[master->getSlave(slaveId)].insert(frameworkId);
  }
  if (aggressiveReoffer) {
    makeNewOffers(master->getActiveSlaves());
  } else {
    std::vector<Slave*> returnedSlave;
    returnedSlave.push_back(master->getSlave(slaveId));
    makeNewOffers(returnedSlave);
  }
}

void NoRequestAllocator::resourcesRecovered(const FrameworkID& frameworkId,
                                            const SlaveID& slaveId,
                                            const ResourceHints& unusedResources) {
  // FIXME: do we need to inform usagetracker about this?
  Slave* slave = master->getSlave(slaveId);
  refusers[slave].erase(frameworkId);
  allRefusers.erase(slave);
  if (aggressiveReoffer) {
    makeNewOffers(master->getActiveSlaves());
  } else {
    std::vector<Slave*> returnedSlave;
    returnedSlave.push_back(master->getSlave(slaveId));
    makeNewOffers(returnedSlave);
  }
}

void NoRequestAllocator::offersRevived(Framework* framework) {
  LOG(INFO) << "offersRevived for " << framework->id;
  std::vector<Slave*> revivedSlaves;
  foreachpair (Slave* slave, boost::unordered_set<FrameworkID>& refuserSet,
               refusers) {
    if (refuserSet.count(framework->id)) {
      refuserSet.erase(framework->id);
      revivedSlaves.push_back(slave);
    }
  }
  allRefusers.clear();
  // TODO(Charles): Can we get away with doing this for jsut revivedSlaves
  // plus allRefusers entries we actually cleared?
  makeNewOffers(master->getActiveSlaves());
}

void NoRequestAllocator::timerTick() {
  tracker->timerTick(process::Clock::now());
  if (aggressiveReoffer) {
    // FIXME: Charles -- this is a workaround for an unknown bug where we miss
    // some time where we're supposed to remove something from refusers.
    foreachvalue (boost::unordered_set<FrameworkID>& refuserSet, refusers) {
      refuserSet.clear();
    }
  }
  allRefusers.clear();
  makeNewOffers(master->getActiveSlaves());
}

void NoRequestAllocator::gotUsage(const UsageMessage& update) {
  // TODO(Charles): Check whether we actually got more free resources on the
  // slave to short-circuit the reoffer; or defer reoffers until we likely have
  // a full set of usage updates.
  tracker->recordUsage(update);
  Slave* slave = master->getSlave(update.slave_id());
  if (slave) {
    if (aggressiveReoffer) {
      // TODO(charles): replace or remove this hack
      foreach (Framework* framework, master->getActiveFrameworks()) {
        framework->slaveFilter.erase(slave);
      }
    }
    refusers.erase(slave);
    allRefusers.erase(slave);
    vector<Slave*> singleSlave;
    singleSlave.push_back(slave);
    LOG(INFO) << "Trying to make new offers based on usage update for "
              << update.slave_id();
    if (aggressiveReoffer) {
      makeNewOffers(master->getActiveSlaves());
    } else {
      makeNewOffers(singleSlave);
    }
  } else {
    LOG(WARNING) << "Got usage from non-slave " << update.slave_id();
  }
}

} // namespace norequest
} // namespace internal
} // namespace mesos
