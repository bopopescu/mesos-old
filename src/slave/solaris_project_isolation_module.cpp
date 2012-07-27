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

#include <project.h>
#include <rctl.h>

#include <sys/task.h>

#include <stout/foreach.hpp>

#include "solaris_project_isolation_module.hpp"

using namespace mesos;
using namespace mesos::internal;
using namespace mesos::internal::slave;

using boost::lexical_cast;
using boost::unordered_map;
using boost::unordered_set;

using std::cerr;
using std::cout;
using std::endl;
using std::list;
using std::make_pair;
using std::ostringstream;
using std::pair;
using std::queue;
using std::string;
using std::vector;


SolarisProjectIsolationModule::SolarisProjectIsolationModule()
{
  // Launch the communicator module, which will start the projd's.
  // TODO: It would be nice to not return from the constructor
  // until the communicator is up and running.
  // TODO(*): Not great to let this escape from constructor.
  comm = new Communicator(this);
  Process::spawn(comm);
}


SolarisProjectIsolationModule::~SolarisProjectIsolationModule()
{
  comm->stop();
}


void SolarisProjectIsolationModule::initialize(Slave* slave)
{
  ProcessBasedIsolationModule::initialize(slave);
}


void SolarisProjectIsolationModule::startExecutor(Framework* framework)
{
  // Figure out which project to use.
  if (projects.empty())
    LOG(FATAL) << "trying to use more projects than were allocated";
  string project = projects.front();
  projects.pop();
  frameworkProject[framework->frameworkId] = project;
  LOG(INFO) << "Assigned framework " << framework->frameworkId << " to project " << project;

  ProcessBasedIsolationModule::startExecutor(framework);
}


void SolarisProjectIsolationModule::killExecutor(Framework* framework)
{
  // Inform project daemon to update resources and kill all processes.
  comm->send(projds[frameworkProject[framework->frameworkId]], comm->pack<S2PD_KILL_ALL>());
}


void SolarisProjectIsolationModule::resourcesChanged(Framework* framework)
{
  // Inform project daemon to update resources.
  comm->send(projds[frameworkProject[framework->frameworkId]],
             comm->pack<S2PD_UPDATE_RESOURCES>(framework->resources));
}


ExecutorLauncher* SolarisProjectIsolationModule::createExecutorLauncher(
    Framework* framework)
{
  return new ProjectLauncher(framework->frameworkId,
                             framework->executorPath,
                             framework->user,
                             slave->getWorkDirectory(framework->frameworkId),
                             slave->self(),
                             slave->getConf().get("frameworks_home", ""),
                             slave->getConf().get("home", ""),
                             slave->getConf().get("hadoop_home", ""),
                             !slave->local,
                             slave->getConf().get("switch_user", true),
                             frameworkProject[framework->frameworkId]);
}


void SolarisProjectIsolationModule::Communicator::launchProjd(
    const string& project)
{
  LOG(INFO) << "Starting projd for project " << project;

  // Get location of Mesos install in order to find projd.
  string mesosHome = slave->getConf().get("home", ".");

  pid_t pid;
  if ((pid = fork()) == -1)
    PLOG(FATAL) << "Failed to fork to launch projd";

  if (pid) {
    // In parent process
    LOG(INFO) << "Started projd, OS pid = " << pid;
  } else {
    // Add PARENT_PID to environment.
    const string& my_pid = self();
    setenv("PARENT_PID", my_pid.c_str(), true);

    // Set LIBPROCESS_PORT so that we bind to a random free port.
    setenv("LIBPROCESS_PORT", "0", true);

    if (setproject(project.c_str(), "root", TASK_FINAL) != 0)
      fatal("setproject failed");

    string projd = mesosHome + "/mesos-projd";

    // Execute projd.
    execl(projd.c_str(), "mesos-projd", (char *) NULL);
    // If we get here, the execl call failed.
    fatalerror("Could not execute %s", projd.c_str());
    // TODO: Exit the slave if this happens
  }
}


SolarisProjectIsolationModule::Communicator::Communicator(
    SolarisProjectIsolationModule* m)
  : module(m), shouldRun(true)
{}


void SolarisProjectIsolationModule::Communicator::operator() ()
{
  launchProjds();

  while (shouldRun) {
    switch (receive(1)) {
      case PD2S_PROJECT_READY: {
        string project;
        tie(project) = unpack<PD2S_PROJECT_READY>(body());
        if (shouldRun)
          module->projects.push(project);
        break;
      }
      case PROCESS_TIMEOUT: {
        break;
      }
      case PROCESS_EXIT: {
        foreachpair (const string &project, const PID &pid, module->projds)
          if (from() == pid)
            LOG(FATAL) << "projd for " << project << " disconnected!"
                       << "Committing suicide (should fix this) ...";
        break;
      }
      default: {
        LOG(FATAL) << "SolarisProjectIsolationModule::Communicator "
                   << "got unknown message " << msgid() << " from " << from();
      }
    }
  }
}


void SolarisProjectIsolationModule::Communicator::launchProjds()
{
  LOG(INFO) << "Launching project daemons";
  struct project proj;
  char proj_buf[PROJECT_BUFSZ];

  setprojent();

  while (getprojent(&proj, proj_buf, PROJECT_BUFSZ) != NULL) {
    string project(proj.pj_name);
    if (project.find("mesos.project.") != string::npos) {
      launchProjd(project);
      module->projects.push(project);
    }
  }

  endprojent();

  if (module->projects.size() == 0)
    LOG(FATAL) << "Could not find any Mesos projects to use";

  do {
    switch (receive()) {
    case PD2S_REGISTER_PROJD: {
        string project;
        unpack<PD2S_REGISTER_PROJD>(project);
        module->projds[project] = from();
        link(from());
        break;
      }
    default: {
      LOG(FATAL) << "SolarisProjectIsolationModule::Communicator "
                 << "got unknown message " << msgid() << " from " << from();
      }
    }
  } while (module->projds.size() != module->projects.size());
}


void SolarisProjectIsolationModule::Communicator::stop()
{
  shouldRun = false;
}


SolarisProjectIsolationModule::ProjectLauncher::ProjectLauncher(
    FrameworkID _frameworkId, const string& _executorPath,
    const string& _user, const string& _workDir, const string& _slavePid,
    bool _redirectIO, const string& _project)
  : ExecutorLauncher(_frameworkId, _executorPath, _user, _workDir,
                     _slavePid, _redirectIO),
    project(_project)
{}


void SolarisProjectIsolationModule::ProjectLauncher::switchUser()
{
  if (setproject(project.c_str(), user.c_str(), TASK_FINAL) != 0)
    fatal("failed (setproject)");

  Launcher::switchUser(); // Sets UID and GID
}
