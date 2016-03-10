// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HOOK_CONFIG_HPP_
#define HOOK_CONFIG_HPP_

#include <unordered_map>

#include "mesos/mesos.hpp"

#include <stout/option.hpp>

namespace mesos {
namespace internal {
namespace slave {

class HookConfig {
public:
  HookConfig() {}
  ~HookConfig() {}

  void parse();
  Option<std::string> prepareCommand(
      TaskState state, const TaskID &task,
      const FrameworkID &framework, const Option<ContainerID> &container) const;

  static Option<TaskState> string_to_status(const std::string &str);

private:
  std::unordered_map<int, std::string> cmd_;
};

} // namespace slave {
} // namespace internal {
} // namespace mesos {

#endif /* HOOK_CONFIG_HPP_ */
