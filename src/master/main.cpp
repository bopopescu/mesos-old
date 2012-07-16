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

#include "boost/scoped_ptr.hpp"
#include <stout/os.hpp>
#include <stout/stringify.hpp>
#include <stout/try.hpp>

#include "common/build.hpp"

#include "configurator/configuration.hpp"
#include "configurator/configurator.hpp"

#include "detector/detector.hpp"

#include "flags/flags.hpp"

#include "logging/flags.hpp"
#include "logging/logging.hpp"

#include "master/allocator.hpp"
#include "master/allocator_factory.hpp"
#include "master/dominant_share_allocator.hpp"
#include "master/master.hpp"
#include "master/webui.hpp"

#include "usage_log/usage_log.hpp"

using namespace mesos::internal;
using namespace mesos::internal::master;

using mesos::internal::usage_log::UsageRecorder;
using mesos::internal::usage_log::UsageLogWriter;
using mesos::internal::usage_log::BinaryFileUsageLogWriter;

using std::cerr;
using std::endl;
using std::string;


void usage(const char* argv0, const Configurator& configurator)
{
  cerr << "Usage: " << os::basename(argv0) << " [...]" << endl
       << endl
       << "Supported options:" << endl
       << configurator.getUsage();
}


int main(int argc, char** argv)
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  flags::Flags<logging::Flags, master::Flags> flags;

  // The following flags are executable specific (e.g., since we only
  // have one instance of libprocess per execution, we only want to
  // advertise the port and ip option once, here).
  short port;
  flags.add(&port, "port", "Port to listen on", 5050);

  Option<string> ip;
  flags.add(&ip, "ip", "IP address to listen on");

  string zk;
  flags.add(&zk,
            "zk",
            "ZooKeeper URL (used for leader election amongst masters)\n"
            "May be one of:\n"
            "  zk://host1:port1,host2:port2,.../path\n"
            "  zk://username:password@host1:port1,host2:port2,.../path\n"
            "  file://path/to/file (where file contains one of the above)",
            "");

  bool help;
  flags.add(&help,
            "help",
            "Prints this help message",
            false);

  Configurator configurator(flags);
  Configuration configuration;
  try {
    configuration = configurator.load(argc, argv);
  } catch (ConfigurationException& e) {
    cerr << "Configuration error: " << e.what() << endl;
    usage(argv[0], configurator);
    exit(1);
  }

  flags.load(configuration.getMap());

  if (help) {
    usage(argv[0], configurator);
    exit(1);
  }

  // Initialize libprocess.
  os::setenv("LIBPROCESS_PORT", stringify(port));

  if (ip.isSome()) {
    os::setenv("LIBPROCESS_IP", ip.get());
  }

  process::initialize("master");

  logging::initialize(argv[0], flags);

  LOG(INFO) << "Build: " << build::DATE << " by " << build::USER;
  LOG(INFO) << "Starting Mesos master";

  if (chdir(dirname(argv[0])) != 0) {
    fatalerror("Could not chdir into %s", dirname(argv[0]));
  }

  Allocator* allocator = AllocatorFactory::instantiate(flags.allocator, configuration);

  Master* master = new Master(allocator, flags);
  process::spawn(master);

  Try<MasterDetector*> detector =
    MasterDetector::create(zk, master->self(), true, flags.quiet);

  CHECK(detector.isSome())
    << "Failed to create a master detector: " << detector.error();

  string usageLog = flags.usage_log_file;
  boost::scoped_ptr<UsageRecorder> usageRecorder;
  if (usageLog != "") {
    UsageLogWriter *logWriter = new BinaryFileUsageLogWriter(usageLog);
    usageRecorder.reset(new UsageRecorder(logWriter, master->self(), 1.0));
    process::spawn(usageRecorder.get());
  }

#ifdef MESOS_WEBUI
  webui::start(master->self(), flags);
#endif

  process::wait(master->self());
  delete master;
  delete allocator;

  MasterDetector::destroy(detector.get());

  return 0;
}
