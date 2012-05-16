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

#ifndef __ZOOKEEPER_URL_HPP__
#define __ZOOKEEPER_URL_HPP__

#include <string>

#include "common/option.hpp"
#include "common/strings.hpp"
#include "common/try.hpp"

#include "zookeeper/authentication.hpp"

namespace zookeeper {

// Describes a ZooKeeper URL of the form:
//
//     zk://username:password@servers/path
//
// Where username:password is for the 'digest' scheme (see ZooKeeper
// documentation regarding "access controls using ACLs") and servers
// is of the form:
//
//     host1:port1,host2:port2,host3:port3
//
// Note that in the future we may want to support authentication
// mechanisms other than 'digest' and have a URL of the following
// form.
//
//     zk://scheme:credentials@servers/path
class URL
{
public:
  static Try<URL> parse(const std::string& url);

  const Option<Authentication> authentication;
  const std::string servers;
  const std::string path;

private:
  URL(const std::string& _servers,
      const std::string& _path)
    : servers(_servers),
      path(_path) {}

  URL(const std::string& credentials,
      const std::string& _servers,
      const std::string& _path)
    : servers(_servers),
      path(_path),
      authentication(Authentication("digest", credentials)) {}
};


inline Try<URL> URL::parse(const std::string& url)
{
  std::string s = strings::trim(url);

  size_t index = s.find_first_of("zk://");

  if (index != 0) {
    return Try<URL>::error("Expecting 'zk://' at the beginning of the URL");
  }

  s = s.substr(5);

  // Look for the trailing '/' (if any), that's where the path starts.
  std::string path;
  do {
    index = s.find_last_of('/');

    if (index == std::string::npos) {
      break;
    } else {
      path = s.substr(index) + path;
      s = s.substr(0, index);
    }
  } while (true);

  if (path == "") {
    path = "/";
  }

  // Look for the trailing '@' (if any), that's where servers starts.
  index = s.find_last_of('@');

  if (index != std::string::npos) {
    return URL(s.substr(0, index), s.substr(index + 1), path);
  } else {
    return URL(s, path);
  }
}

} // namespace zookeeper {

#endif // __ZOOKEEPER_URL_HPP__
