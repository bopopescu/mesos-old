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

#include <process/defer.hpp>
#include <process/delay.hpp>
#include <process/timer.hpp>

#include <stout/foreach.hpp>
#include <stout/timer.hpp>

#include "logging/logging.hpp"

#include "master/dominant_share_allocator.hpp"
#include "master/flags.hpp"

using std::sort;
using std::string;
using std::vector;

namespace mesos {
namespace internal {
namespace master {

// Used to represent "filters" for resources unused in offers.
class Filter
{
public:
  virtual ~Filter() {}
  virtual bool filter(const SlaveID& slaveId, const Resources& resources) = 0;
};

class RefusedFilter : public Filter
{
public:
  RefusedFilter(const SlaveID& _slaveId,
                const Resources& _resources,
                const Timeout& _timeout)
    : slaveId(_slaveId),
      resources(_resources),
      timeout(_timeout) {}

  virtual bool filter(const SlaveID& slaveId, const Resources& resources)
  {
    LOG(INFO) << "Checking " << slaveId << " " << resources
              << " v " << this->slaveId << " " << this->resources
              << " t " << timeout.remaining();
    return slaveId == this->slaveId &&
      resources <= this->resources && // Refused resources are superset.
      timeout.remaining() > 0.0;
  }

  virtual ~RefusedFilter() {
    process::timers::cancel(expireTimer);
  }

  const SlaveID slaveId;
  const Resources resources;
  const Timeout timeout;
  process::Timer expireTimer;
};


struct DominantShareComparator
{
  DominantShareComparator(const Resources& _resources,
                          const hashmap<FrameworkID, Resources>& _allocated)
    : resources(_resources),
      allocated(_allocated)
  {}

  bool operator () (const FrameworkID& frameworkId1,
                    const FrameworkID& frameworkId2)
  {
    double share1 = 0;
    double share2 = 0;

    // TODO(benh): This implementaion of "dominant resource fairness"
    // currently does not take into account resources that are not
    // scalars.

    foreach (const Resource& resource, resources) {
      if (resource.type() == Value::SCALAR) {
        double total = resource.scalar().value();

        if (total > 0) {
          Value::Scalar none;
          const Value::Scalar& scalar1 =
            allocated[frameworkId1].get(resource.name(), none);
          const Value::Scalar& scalar2 =
            allocated[frameworkId2].get(resource.name(), none);
          share1 = std::max(share1, scalar1.value() / total);
          share2 = std::max(share2, scalar2.value() / total);
        }
      }
    }

    if (share1 == share2) {
      // Make the sort deterministic for unit testing.
      return frameworkId1 < frameworkId2;
    } else {
      return share1 < share2;
    }
  }

  const Resources resources;
  hashmap<FrameworkID, Resources> allocated; // Not const for the '[]' operator.
};


void DominantShareAllocator::initialize(
    const Flags& _flags,
    const process::PID<Master>& _master)
{
  flags = _flags;
  master = _master;
  initialized = true;

  delay(flags.batch_seconds, self(), &DominantShareAllocator::batch);
}


void DominantShareAllocator::frameworkAdded(
    const FrameworkID& frameworkId,
    const FrameworkInfo& frameworkInfo,
    const Resources& used)
{
  CHECK(initialized);

  CHECK(!frameworks.contains(frameworkId));
  CHECK(!allocated.contains(frameworkId));

  frameworks[frameworkId] = frameworkInfo;
  allocated[frameworkId] = used;

  LOG(INFO) << "Added framework " << frameworkId;

  allocate();
}


void DominantShareAllocator::frameworkRemoved(const FrameworkID& frameworkId)
{
  CHECK(initialized);

  // Might not be in 'frameworks' because it was previously
  // deactivated and never re-added.

  frameworks.erase(frameworkId);

  allocated.erase(frameworkId);

  foreach (Filter* filter, filters.get(frameworkId)) {
    filters.remove(frameworkId, filter);

    // Do not delete the filter, see comments in
    // DominantShareAllocator::offersRevived and
    // DominantShareAllocator::expire.
  }

  filters.remove(frameworkId);

  LOG(INFO) << "Removed framework " << frameworkId;
}


void DominantShareAllocator::frameworkActivated(
    const FrameworkID& frameworkId,
    const FrameworkInfo& frameworkInfo)
{
  CHECK(initialized);

  CHECK(!frameworks.contains(frameworkId));

  frameworks[frameworkId] = frameworkInfo;

  LOG(INFO) << "Activated framework " << frameworkId;

  allocate();
}


void DominantShareAllocator::frameworkDeactivated(
    const FrameworkID& frameworkId)
{
  CHECK(initialized);

  frameworks.erase(frameworkId);

  // Note that we *do not* remove the resources allocated to this
  // framework (i.e., 'allocated.erase(frameworkId)'). For now, this
  // is important because we might have already dispatched a
  // Master::offer and we'll soon be getting back an
  // Allocator::resourcesRecovered where we'll update 'allocated'
  // appropriately. We might be able to collapse the added/removed and
  // activated/deactivated in the future.

  foreach (Filter* filter, filters.get(frameworkId)) {
    filters.remove(frameworkId, filter);

    // Do not delete the filter, see comments in
    // DominantShareAllocator::offersRevived and
    // DominantShareAllocator::expire.
  }

  filters.remove(frameworkId);

  LOG(INFO) << "Deactivated framework " << frameworkId;
}


void DominantShareAllocator::slaveAdded(
    const SlaveID& slaveId,
    const SlaveInfo& slaveInfo,
    const hashmap<FrameworkID, Resources>& used)
{
  CHECK(initialized);

  CHECK(!slaves.contains(slaveId));

  slaves[slaveId] = slaveInfo;

  resources += slaveInfo.resources();

  Resources unused = slaveInfo.resources();

  foreachpair (const FrameworkID& frameworkId, const Resources& resources, used) {
    if (frameworks.contains(frameworkId)) {
      allocated[frameworkId] += resources;
    }
    unused -= resources; // Only want to allocate resources that are not used!
  }

  allocatable[slaveId] = unused;

  LOG(INFO) << "Added slave " << slaveId << " (" << slaveInfo.hostname()
            << ") with " << slaveInfo.resources()
            << " (and " << unused << " available)";

  allocate(slaveId);
}


void DominantShareAllocator::slaveRemoved(const SlaveID& slaveId)
{
  CHECK(initialized);

  CHECK(slaves.contains(slaveId));

  resources -= slaves[slaveId].resources();

  slaves.erase(slaveId);

  allocatable.erase(slaveId);

  // Note that we DO NOT actually delete any filters associated with
  // this slave, that will occur when the delayed
  // DominantShareAllocator::expire gets invoked (or the framework
  // that applied the filters gets removed).

  LOG(INFO) << "Removed slave " << slaveId;
}


void DominantShareAllocator::updateWhitelist(
    const Option<hashset<string> >& _whitelist)
{
  CHECK(initialized);

  whitelist = _whitelist;

  if (whitelist.isSome()) {
    LOG(INFO) << "Updated slave white list:";
    foreach (const string& hostname, whitelist.get()) {
      LOG(INFO) << "\t" << hostname;
    }
  }
}


void DominantShareAllocator::resourcesRequested(
    const FrameworkID& frameworkId,
    const vector<Request>& requests)
{
  CHECK(initialized);

  LOG(INFO) << "Received resource request from framework " << frameworkId;
}


void DominantShareAllocator::resourcesUnused(
    const FrameworkID& frameworkId,
    const SlaveID& slaveId,
    const ResourceHints& resourceHints,
    const Option<Filters>& filters)
{
  const Resources& resources = resourceHints.expectedResources;
  CHECK(initialized);

  if (resources.allocatable().size() == 0) {
    return;
  }

  VLOG(1) << "Framework " << frameworkId
          << " left " << resources.allocatable()
          << " unused on slave " << slaveId;

  // Updated resources allocated to framework.
  CHECK(allocated.contains(frameworkId));
  allocated[frameworkId] -= resources;

  // Update resources allocatable on slave.
  CHECK(allocatable.contains(slaveId));
  allocatable[slaveId] += resources;

  // Create a refused resources filter.
  double timeout = filters.isSome()
    ? filters.get().refuse_seconds()
    : Filters().refuse_seconds();

  if (timeout != 0.0) {
    VLOG(1) << "Framework " << frameworkId
            << " refused resources on slave " << slaveId
            << "(" << slaves[slaveId].hostname() << "), "
            << " creating " << timeout << " second filter";

    // Create a new filter and delay it's expiration.
    RefusedFilter* filter = new RefusedFilter(slaveId, resources, timeout);
    this->filters.put(frameworkId, filter);

    // TODO(benh): Use 'this' and '&This::' as appropriate.
    filter->expireTimer =
      delay(timeout, PID<DominantShareAllocator>(this),
          &DominantShareAllocator::expire,
	  frameworkId, filter);
  }
}


void DominantShareAllocator::resourcesRecovered(
    const FrameworkID& frameworkId,
    const SlaveID& slaveId,
    const ResourceHints& resourceHints)
{
  const Resources& resources = resourceHints.expectedResources;
  CHECK(initialized);

  if (resources.allocatable().size() == 0) {
    return;
  }

  // Updated resources allocated to framework (if framework still
  // exists, which it might not in the event that we dispatched
  // Master::offer before we received Allocator::frameworkRemoved).
  if (allocated.contains(frameworkId)) {
    allocated[frameworkId] -= resources;
  }

  // Update resources allocatable on slave (if slave still exists,
  // which it might not in the event that we dispatched Master::offer
  // before we received Allocator::slaveRemoved).
  if (allocatable.contains(slaveId)) {
    allocatable[slaveId] += resources;

    VLOG(1) << "Recovered " << resources.allocatable()
            << " on slave " << slaveId
            << " from framework " << frameworkId;
  }
}


void DominantShareAllocator::offersRevived(const FrameworkID& frameworkId)
{
  CHECK(initialized);

  foreach (Filter* filter, filters.get(frameworkId)) {
    filters.remove(frameworkId, filter);

    // We delete each actual Filter when
    // DominantShareAllocator::expire gets invoked. If we delete the
    // Filter here it's possible that the same Filter (i.e., same
    // address) could get reused and DominantShareAllocator::expire
    // would expire that filter too soon. Note that this only works
    // right now because ALL Filter types "expire".
  }

  filters.remove(frameworkId);

  LOG(INFO) << "Removed filters for framework " << frameworkId;

  allocate();
}


void DominantShareAllocator::batch()
{
  CHECK(initialized);
  allocate();
  delay(flags.batch_seconds, self(), &DominantShareAllocator::batch);
}


void DominantShareAllocator::allocate()
{
  CHECK(initialized);

  ::Timer timer;
  timer.start();

  allocate(slaves.keys());

  LOG(INFO) << "Performed allocation for "
            << slaves.size() << " slaves in "
            << timer.elapsed().millis() << " milliseconds";
}


void DominantShareAllocator::allocate(const SlaveID& slaveId)
{
  CHECK(initialized);

  hashset<SlaveID> slaveIds;
  slaveIds.insert(slaveId);

  ::Timer timer;
  timer.start();

  allocate(slaveIds);

  LOG(INFO) << "Performed allocation for slave "
            << slaveId << " in "
            << timer.elapsed().millis() << " milliseconds";
}


void DominantShareAllocator::allocate(const hashset<SlaveID>& slaveIds)
{
  CHECK(initialized);

  // Order frameworks by dominant resource fairness.
  if (frameworks.size() == 0) {
    VLOG(1) << "No frameworks to allocate resources!";
    return;
  }

  vector<FrameworkID> frameworkIds;

  foreachkey (const FrameworkID& frameworkId, frameworks) {
    frameworkIds.push_back(frameworkId);
  }

  DominantShareComparator comparator(resources, allocated);
  sort(frameworkIds.begin(), frameworkIds.end(), comparator);

  // Get out only "available" resources (i.e., resources that are
  // allocatable and above a certain threshold, see below).
  hashmap<SlaveID, Resources> available;
  foreachpair (const SlaveID& slaveId, Resources resources, allocatable) {
    if (!slaveIds.contains(slaveId)) {
      continue;
    }

    if (isWhitelisted(slaveId)) {
      resources = resources.allocatable(); // Make sure they're allocatable.

      // TODO(benh): For now, only make offers when there is some cpu
      // and memory left. This is an artifact of the original code
      // that only offered when there was at least 1 cpu "unit"
      // available, and without doing this a framework might get
      // offered resources with only memory available (which it
      // obviously will decline) and then end up waiting the default
      // Filters::refuse_seconds (unless the framework set it to
      // something different).

      Value::Scalar none;
      Value::Scalar cpus = resources.get("cpus", none);
      Value::Scalar mem = resources.get("mem", none);

      if (cpus.value() >= MIN_CPUS && mem.value() > MIN_MEM) {
        VLOG(1) << "Found available resources: " << resources
                << " on slave " << slaveId;
        available[slaveId] = resources;
      }
    }
  }

  if (available.size() == 0) {
    VLOG(1) << "No resources available to allocate!";
    return;
  }

  foreach (const FrameworkID& frameworkId, frameworkIds) {
    // Check if we should offer resources to this framework.
    hashmap<SlaveID, ResourceHints> offerable;
    foreachpair (const SlaveID& slaveId, const Resources& resources, available) {
      // Check whether or not this framework filters this slave.
      bool filtered = false;

      foreach (Filter* filter, filters.get(frameworkId)) {
        LOG(INFO) << "Checking filter for " << frameworkId;
        if (filter->filter(slaveId, resources)) {
          VLOG(1) << "Filtered " << resources
                  << " on slave " << slaveId
                  << " for framework " << frameworkId;
          filtered = true;
          break;
        }
      }

      if (!filtered) {
        VLOG(1) << "Offering " << resources
                << " on slave " << slaveId
                << " to framework " << frameworkId;
        offerable[slaveId] = ResourceHints(resources, Resources());

        // Update framework and slave resources.
        allocated[frameworkId] += resources;
        allocatable[slaveId] -= resources;
      }
    }

    if (offerable.size() > 0) {
      foreachkey (const SlaveID& slaveId, offerable) {
        available.erase(slaveId);
      }
      dispatch(master, &Master::offer, frameworkId, offerable);
    }
  }
}


void DominantShareAllocator::expire(
    const FrameworkID& frameworkId,
    Filter* filter)
{
  LOG(INFO) << "expiring a filter for " << frameworkId;
  // Framework might have been removed, in which case it's filters
  // should also already have been deleted.
  if (frameworks.contains(frameworkId)) {
    // Check and see if the filter was already removed in
    // DominantShareAllocator::offersRevived (but not deleted).
    if (filters.contains(frameworkId, filter)) {
      filters.remove(frameworkId, filter);
    }
  }

  delete filter;
}


bool DominantShareAllocator::isWhitelisted(const SlaveID& slaveId)
{
  CHECK(initialized);

  CHECK(slaves.contains(slaveId));

  return whitelist.isNone() ||
    whitelist.get().contains(slaves[slaveId].hostname());
}

} // namespace master {
} // namespace internal {
} // namespace mesos {
