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

#include <stout/json.hpp>


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

static std::string datetimeString()
{
  time_t rawtime;
  struct tm *timeinfo;
  char buffer[80];

  time(&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer, 80, "%d-%m-%Y %I:%M:%S", timeinfo);
  return buffer;
}

template <typename T>
static std::string getValue(const Option<T> &obj)
{
  if (obj.isSome() && obj.get().has_value()) {
    return obj.get().value();
  }
  return "?";
}

template <typename T>
static std::string getValue(const T &obj)
{
  if (obj.has_value()) {
    return obj.value();
  }
  return "?";
}

static std::string getJsonValue(const std::string &str, const std::string &key)
{
  Try<JSON::Value> maybe_json = JSON::parse(str);
  if (maybe_json.isError()) {
    LOG(WARNING) << maybe_json.error();
    return "?";
  }
  if (!maybe_json.get().is<JSON::Object>()) {
    LOG(WARNING) << "Json is not an object.";
    return "?";
  }
  const JSON::Object &obj = maybe_json.get().as<JSON::Object>();
  auto result = obj.find<JSON::Value>(key);
  if (result.isError()) {
    LOG(WARNING) << result.error();
    return "?";
  }
  if (result.isNone()) {
    LOG(WARNING) << "No such nested label (" << key << ")";
    return "?";
  }
  std::stringstream ss;
  ss << result.get();
  return ss.str();
}

static std::string getLabelValue(
    const Labels &labels, const std::string &key, const std::string &nested)
{
  assert(!key.empty());

  for (int i = 0; i < labels.labels_size(); ++i) {
    if (labels.labels(i).key() == key) {
      auto &value = labels.labels(i).value();
      if (nested.empty()) {
        return value;
      }
      return getJsonValue(value, nested);
    }
  }
  return "?";
}

Option<std::string> HookConfig::prepareCommand(
    const StatusUpdate &update, const TaskID &task,
    const FrameworkID &framework, const Option<ContainerID> &container) const
{
  if (!update.has_status() || !update.status().has_state()) {
    return None();
  }
  TaskState state = update.status().state();
  auto it = cmd_.find(state);
  if (it == cmd_.end()) {
    return None();
  }

  const Labels &labels = update.status().labels();
  std::string cmd;
  const std::string &hook_cmd = it->second;
  unsigned int pos = 0;
  std::string label_key;
  std::string nested_labels;
  bool found_dot = false;

  auto process_cmd = [&](
      const std::unordered_map<char, std::function<bool()>> &callbacks,
      std::function<bool(char)> default_callback) {
    while (pos < hook_cmd.size()) {
      int c = hook_cmd[pos];
      ++pos;
      bool cont = callbacks.count(c)
          ? callbacks.at(c)()
          : default_callback(c);
      if (!cont) {
        return;
      }
    }
  };

  // Callbacks for placeholders preceded by backslash
  std::unordered_map<char, std::function<bool()>> slash_callbacks;
  slash_callbacks['T'] = [&]() {
    cmd += getValue(task);
    return false;
  };
  slash_callbacks['F'] = [&]() {
    cmd += getValue(framework);
    return false;
  };
  slash_callbacks['C'] = [&]() {
    cmd += getValue(container);
    return false;
  };
  slash_callbacks['D'] = [&]() {
    cmd += datetimeString();
    return false;
  };
  slash_callbacks['\\'] = [&]() {
    cmd += '\\';
    return false;
  };
  slash_callbacks['{'] = [&]() {
    cmd += '{';
    return false;
  };
  auto slash_default_callback = [&](char c) {
    LOG(WARNING) << "Unexpected symbol \\"
        << c << " in hook command.";
    return false;
  };

  // Callbacks for processing '\}' inside of braces
  std::unordered_map<char, std::function<bool()>> brace_slash_callbacks;
  brace_slash_callbacks['}'] = [&]() {
    if (!found_dot) {
      label_key += '}';
    } else {
      nested_labels += '}';
    }
    return false;
  };
  brace_slash_callbacks['.'] = [&]() {
    if (!found_dot) {
      label_key += '.';
    } else {
      nested_labels += "\\.";
    }
    return false;
  };
  auto brace_slash_default_callback = [&](char c) {
    LOG(WARNING) << "Unexpected symbol \\"
        << c << " inside of braces.";
    return true;
  };

  // Callbacks for processing label name inside of braces
  std::unordered_map<char, std::function<bool()>> brace_callbacks;
  brace_callbacks['\\'] = [&]() {
    if (pos == cmd.size()) {
      LOG(WARNING) << "Unexpected '\' at the end of hook command.";
      return false;
    }
    process_cmd(brace_slash_callbacks, brace_slash_default_callback);
    return true;
  };
  brace_callbacks['}'] = [&]() {
    found_dot = false;
    return false;
  };
  brace_callbacks['.'] = [&]() {
    if (found_dot) {
      nested_labels += '.';
    } else {
      found_dot = true;
    }
    return true;
  };
  auto brace_default_callback = [&](char c) {
    if (!found_dot) {
      label_key += c;
    } else {
      nested_labels += c;
    }
    return true;
  };

  // Callbacks for special characters in hook command.
  std::unordered_map<char, std::function<bool()>> main_callbacks;
  main_callbacks['\\'] = [&]() {
    if (pos == cmd.size()) {
      LOG(WARNING) << "Unexpected '\' at the end of hook command.";
      return false;
    }
    process_cmd(slash_callbacks, slash_default_callback);
    return true;
  };
  main_callbacks['{'] = [&]() {
    process_cmd(brace_callbacks, brace_default_callback);
    cmd += getLabelValue(labels, label_key, nested_labels);
    label_key.clear();
    nested_labels.clear();
    return true;
  };
  auto main_default_callback = [&](char c) {
    cmd += c;
    return true;
  };

  process_cmd(main_callbacks, main_default_callback);
  return cmd;
}

} // namespace slave {
} // namespace internal {
} // namespace mesos {
