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

#include <unistd.h>

#include <sys/types.h>

#include <gmock/gmock.h>

#include "common/option.hpp"
#include "common/stringify.hpp"
#include "common/strings.hpp"
#include "common/utils.hpp"

#include "linux/cgroups.hpp"


using namespace mesos::internal;


// Define the test fixture for the cgroups tests.
class CgroupsTest : public ::testing::Test
{
protected:
  virtual void SetUp()
  {
    cleanup();
    prepare();
  }

  virtual void TearDown()
  {
    cleanup();
  }

  // Prepare the testing hierarchy and cgroups.
  void prepare()
  {
    // Create a hierarchy for test.
    std::string subsystems = "cpu,memory,freezer";
    ASSERT_TRUE(cgroups::createHierarchy(hierarchy, subsystems).isSome());

    // Create cgroups for test.
    ASSERT_TRUE(cgroups::createCgroup(hierarchy, "prof").isSome());
    ASSERT_TRUE(cgroups::createCgroup(hierarchy, "stu").isSome());
    ASSERT_TRUE(cgroups::createCgroup(hierarchy, "stu/grad").isSome());
    ASSERT_TRUE(cgroups::createCgroup(hierarchy, "stu/under").isSome());
    ASSERT_TRUE(cgroups::createCgroup(hierarchy, "stu/under/senior").isSome());
  }

  void cleanup()
  {
    Try<bool> check = cgroups::checkHierarchy(hierarchy);
    if (check.isSome()) {
      // Remove all cgroups.
      Try<std::vector<std::string> > cgroups = cgroups::getCgroups(hierarchy);
      ASSERT_TRUE(cgroups.isSome());
      foreach (const std::string& cgroup, cgroups.get()) {
        ASSERT_TRUE(cgroups::removeCgroup(hierarchy, cgroup).isSome());
      }

      // Remove the hierarchy.
      ASSERT_TRUE(cgroups::removeHierarchy(hierarchy).isSome());
    }

    // Remove the directory if still exists.
    if (utils::os::exists(hierarchy)) {
      utils::os::rmdir(hierarchy);
    }
  }

  // Path to the root hierarchy for tests.
  static const std::string hierarchy;
};


// Define the test fixture for the simple cgroups tests. Simple cgroups tests do
// not prepare testing hierarchy and cgroups.
class CgroupsSimpleTest : public CgroupsTest
{
protected:
  virtual void SetUp()
  {
    cleanup();
  }
};


const std::string CgroupsTest::hierarchy = "/tmp/mesos_cgroups_test_hierarchy";


TEST_F(CgroupsSimpleTest, ROOT_CGROUPS_Enabled)
{
  Try<bool> result = false;

  result = cgroups::enabled("cpu");
  ASSERT_TRUE(result.isSome());
  EXPECT_TRUE(result.get());

  result = cgroups::enabled(",cpu");
  ASSERT_TRUE(result.isSome());
  EXPECT_TRUE(result.get());

  result = cgroups::enabled("cpu,memory");
  ASSERT_TRUE(result.isSome());
  EXPECT_TRUE(result.get());

  result = cgroups::enabled("cpu,memory,");
  ASSERT_TRUE(result.isSome());
  EXPECT_TRUE(result.get());

  result = cgroups::enabled("invalid");
  EXPECT_TRUE(result.isError());

  result = cgroups::enabled("cpu,invalid");
  EXPECT_TRUE(result.isError());

  result = cgroups::enabled(",");
  EXPECT_TRUE(result.isError());

  result = cgroups::enabled("");
  EXPECT_TRUE(result.isError());
}


TEST_F(CgroupsTest, ROOT_CGROUPS_Busy)
{
  Try<bool> result = false;

  result = cgroups::busy("invalid");
  EXPECT_TRUE(result.isError());

  result = cgroups::busy("cpu,invalid");
  EXPECT_TRUE(result.isError());

  result = cgroups::busy(",");
  EXPECT_TRUE(result.isError());

  result = cgroups::busy("");
  EXPECT_TRUE(result.isError());

  result = cgroups::busy("cpu");
  ASSERT_TRUE(result.isSome());
  EXPECT_TRUE(result.get());

  result = cgroups::busy(",cpu");
  ASSERT_TRUE(result.isSome());
  EXPECT_TRUE(result.get());

  result = cgroups::busy("cpu,memory");
  ASSERT_TRUE(result.isSome());
  EXPECT_TRUE(result.get());

  result = cgroups::busy("cpu,memory,");
  ASSERT_TRUE(result.isSome());
  EXPECT_TRUE(result.get());
}


TEST_F(CgroupsSimpleTest, ROOT_CGROUPS_Subsystems)
{
  Try<std::set<std::string> > names = cgroups::subsystems();
  ASSERT_TRUE(names.isSome());

  Option<std::string> cpu;
  Option<std::string> memory;
  foreach (const std::string& name, names.get()) {
    if (name == "cpu") {
      cpu = name;
    } else if (name == "memory") {
      memory = name;
    }
  }

  EXPECT_TRUE(cpu.isSome());
  EXPECT_TRUE(memory.isSome());
}


TEST_F(CgroupsTest, ROOT_CGROUPS_SubsystemsHierarchy)
{
  Try<std::set<std::string> > names = cgroups::subsystems(hierarchy);
  ASSERT_TRUE(names.isSome());

  Option<std::string> cpu;
  Option<std::string> memory;
  foreach (const std::string& name, names.get()) {
    if (name == "cpu") {
      cpu = name;
    } else if (name == "memory") {
      memory = name;
    }
  }

  EXPECT_TRUE(cpu.isSome());
  EXPECT_TRUE(memory.isSome());
}


TEST_F(CgroupsSimpleTest, ROOT_CGROUPS_CreateRemoveHierarchy)
{
  Try<bool> result = false;

  result = cgroups::createHierarchy("/tmp", "cpu");
  EXPECT_TRUE(result.isError());

  result = cgroups::createHierarchy(hierarchy, "invalid");
  EXPECT_TRUE(result.isError());

  result = cgroups::createHierarchy(hierarchy, "cpu,memory");
  ASSERT_TRUE(result.isSome());

  result = cgroups::createHierarchy(hierarchy, "cpuset");
  EXPECT_TRUE(result.isError());

  result = cgroups::removeHierarchy("/tmp");
  EXPECT_TRUE(result.isError());

  result = cgroups::removeHierarchy(hierarchy);
  ASSERT_TRUE(result.isSome());
}


TEST_F(CgroupsTest, ROOT_CGROUPS_CheckHierarchy)
{
  Try<bool> result = false;

  result = cgroups::checkHierarchy("/tmp-nonexist");
  EXPECT_TRUE(result.isError());

  result = cgroups::checkHierarchy("/tmp");
  EXPECT_TRUE(result.isError());

  result = cgroups::checkHierarchy(hierarchy);
  EXPECT_TRUE(result.isSome());

  result = cgroups::checkHierarchy(hierarchy + "/");
  EXPECT_TRUE(result.isSome());

  result = cgroups::checkHierarchy(hierarchy + "/stu");
  EXPECT_TRUE(result.isError());
}


TEST_F(CgroupsTest, ROOT_CGROUPS_CheckHierarchySubsystems)
{
  Try<bool> result = false;

  result = cgroups::checkHierarchy("/tmp-nonexist", "cpu");
  EXPECT_TRUE(result.isError());

  result = cgroups::checkHierarchy("/tmp", "cpu,memory");
  EXPECT_TRUE(result.isError());

  result = cgroups::checkHierarchy("/tmp", "cpu");
  EXPECT_TRUE(result.isError());

  result = cgroups::checkHierarchy("/tmp", "invalid");
  EXPECT_TRUE(result.isError());

  result = cgroups::checkHierarchy(hierarchy, "cpu,memory");
  EXPECT_TRUE(result.isSome());

  result = cgroups::checkHierarchy(hierarchy, "memory");
  EXPECT_TRUE(result.isSome());

  result = cgroups::checkHierarchy(hierarchy, "invalid");
  EXPECT_TRUE(result.isError());

  result = cgroups::checkHierarchy(hierarchy + "/stu", "cpu");
  EXPECT_TRUE(result.isError());
}


TEST_F(CgroupsSimpleTest, ROOT_CGROUPS_CreateRemoveCgroup)
{
  Try<bool> result = false;

  result = cgroups::createCgroup("/tmp", "test");
  EXPECT_TRUE(result.isError());

  result = cgroups::createHierarchy(hierarchy, "cpu,memory");
  ASSERT_TRUE(result.isSome());

  result = cgroups::createCgroup(hierarchy, "test/1");
  EXPECT_TRUE(result.isError());

  result = cgroups::createCgroup(hierarchy, "test");
  ASSERT_TRUE(result.isSome());

  result = cgroups::removeCgroup(hierarchy, "invalid");
  EXPECT_TRUE(result.isError());

  result = cgroups::removeCgroup(hierarchy, "test");
  ASSERT_TRUE(result.isSome());

  result = cgroups::removeHierarchy(hierarchy);
  ASSERT_TRUE(result.isSome());
}


TEST_F(CgroupsTest, ROOT_CGROUPS_ReadControl)
{
  Try<std::string> result = std::string();
  std::string pid = stringify(::getpid());

  result = cgroups::readControl(hierarchy, "/stu", "invalid");
  EXPECT_TRUE(result.isError());

  result = cgroups::readControl(hierarchy, "/", "tasks");
  ASSERT_TRUE(result.isSome());
  EXPECT_TRUE(strings::contains(result.get(), pid));
}


TEST_F(CgroupsTest, ROOT_CGROUPS_WriteControl)
{
  Try<bool> result = false;
  std::string pid = stringify(::getpid());

  result = cgroups::writeControl(hierarchy, "/prof", "invalid", "invalid");
  EXPECT_TRUE(result.isError());

  result = cgroups::writeControl(hierarchy, "/prof", "tasks", pid);
  ASSERT_TRUE(result.isSome());

  Try<std::set<pid_t> > tasks = cgroups::getTasks(hierarchy, "/prof");
  ASSERT_TRUE(tasks.isSome());

  std::set<pid_t> pids = tasks.get();
  EXPECT_NE(pids.find(::getpid()), pids.end());

  result = cgroups::writeControl(hierarchy, "/", "tasks", pid);
  ASSERT_TRUE(result.isSome());
}


TEST_F(CgroupsTest, ROOT_CGROUPS_GetCgroups)
{
  Try<std::vector<std::string> > cgroups = cgroups::getCgroups(hierarchy);
  ASSERT_TRUE(cgroups.isSome());

  EXPECT_EQ(cgroups.get()[0], "/stu/under/senior");
  EXPECT_EQ(cgroups.get()[1], "/stu/under");
  EXPECT_EQ(cgroups.get()[2], "/stu/grad");
  EXPECT_EQ(cgroups.get()[3], "/stu");
  EXPECT_EQ(cgroups.get()[4], "/prof");

  cgroups = cgroups::getCgroups(hierarchy, "/stu");
  ASSERT_TRUE(cgroups.isSome());

  EXPECT_EQ(cgroups.get()[0], "/stu/under/senior");
  EXPECT_EQ(cgroups.get()[1], "/stu/under");
  EXPECT_EQ(cgroups.get()[2], "/stu/grad");

  cgroups = cgroups::getCgroups(hierarchy, "/prof");
  ASSERT_TRUE(cgroups.isSome());

  EXPECT_TRUE(cgroups.get().empty());
}


TEST_F(CgroupsTest, ROOT_CGROUPS_GetTasks)
{
  Try<std::set<pid_t> > tasks = cgroups::getTasks(hierarchy, "/");
  ASSERT_TRUE(tasks.isSome());

  std::set<pid_t> pids = tasks.get();
  EXPECT_NE(pids.find(1), pids.end());
  EXPECT_NE(pids.find(::getpid()), pids.end());
}
