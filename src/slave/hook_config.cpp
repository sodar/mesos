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

#include "hook_config.hpp"

#include "logging/logging.hpp"

#include <fstream>


namespace mesos {
namespace internal {
namespace slave {

Option<TaskState> HookConfig::string_to_status(const std::string &str)
{
  if (str == "STAGING") {
    return Some(TASK_STAGING);
  }
  if (str == "STARTING") {
    return Some(TASK_STARTING);
  }
  if (str == "FINISHED") {
    return Some(TASK_FINISHED);
  }
  if (str == "LOST") {
    return Some(TASK_LOST);
  }
  if (str == "ERROR") {
    return Some(TASK_ERROR);
  }
  if (str == "KILLING") {
    return Some(TASK_KILLING);
  }
  if (str == "KILLED") {
    return Some(TASK_KILLED);
  }
  if (str == "RUNNING") {
    return Some(TASK_RUNNING);
  }
  if (str == "FAILED") {
    return Some(TASK_FAILED);
  }
  return None();
}

void HookConfig::parse()
{
  std::fstream fs("hooks.cfg", std::ios_base::in);
  if (!fs) {
    LOG(INFO) << "File hooks.cfg does not exist.";
    return;
  }

  while (!fs.eof()) {
    std::string status_str;
    fs >> status_str;
    if (fs.eof()) {
      break;
    }
    auto status = string_to_status(status_str);
    if (status.isNone()) {
      LOG(WARNING) << "Unrecognized status \"" << status_str << "\"";
      fs.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      continue;
    }
    if (cmd_.count((int)status.get())) {
      LOG(WARNING) << "Duplicated status \"" << status_str << "\"";
      fs.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      continue;
    }
    char c = ' ';
    while ((c == ' ' || c == '\t') && !fs.eof()) {
      fs.get(c);
    }
    if (c == '\n' || fs.eof()) {
      LOG(WARNING) << "Empty command for status \"" << status_str << "\"";
      continue;
    }
    std::string cmd;
    while (c != '\n' && !fs.eof()) {
      cmd += c;
      fs.get(c);
    }
    cmd_[(int)status.get()] = cmd;
    LOG(INFO) << "Command for status " << status_str << ": " << cmd;
  }

  fs.close();
}

std::string datetime_string()
{
  time_t rawtime;
  struct tm *timeinfo;
  char buffer[80];

  time (&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer, 80, "%d-%m-%Y %I:%M:%S", timeinfo);
  return buffer;
}

Option<std::string> HookConfig::prepareCommand(
    TaskState state, const TaskID &task,
    const FrameworkID &framework, const Option<ContainerID> &container) const
{
  auto it = cmd_.find(state);
  if (it == cmd_.end()) {
    return None();
  }
  std::string cmd;
  for (unsigned int i = 0; i < it->second.size(); ++i) {
    if (it->second[i] != '\\') {
      cmd += it->second[i];
    } else {
      ++i;
      if (i == it->second.size()) {
        break;
      }
      switch (it->second[i]) {
      case 'T':
        if (task.has_value()) {
          cmd += task.value();
        } else {
          cmd += "<unknown task id>";
        }
        break;
      case 'F':
        if (framework.has_value()) {
          cmd += framework.value();
        } else {
          cmd += "<unknown framework id>";
        }
        break;
      case 'C':
        if (container.isSome() && container.get().has_value()) {
          cmd += container.get().value();
        } else {
          cmd += "<unknown container id>";
        }
        break;
      case 'D':
        cmd += datetime_string();
        break;
      case '\\':
        cmd += '\\';
        break;
      default:
        LOG(WARNING) << "Unexpected symbol \\"
            << it->second[i] << " in hook command.";
      }
    }
  }
  return cmd;
}

} // namespace slave {
} // namespace internal {
} // namespace mesos {
