/** Copyright 2020 Alibaba Group Holding Limited.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef MODULES_BASIC_STREAM_OBJECT_STREAM_H_
#define MODULES_BASIC_STREAM_OBJECT_STREAM_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "basic/stream/object_stream.vineyard.h"
#include "client/client.h"

namespace vineyard {

/**
 * @brief ObjectStreamBuilder is used for initiating object streams
 *
 */
class ObjectStreamBuilder : public ObjectStreamBaseBuilder {
 public:
  explicit ObjectStreamBuilder(Client& client)
      : ObjectStreamBaseBuilder(client) {}

  void SetParam(std::string const& key, std::string const& value) {
    this->params_.emplace(key, value);
  }

  void SetParams(
      const std::unordered_multimap<std::string, std::string>& params) {
    for (auto const& kv : params) {
      this->params_.emplace(kv.first, kv.second);
    }
  }

  void SetParams(const std::unordered_map<std::string, std::string>& params) {
    for (auto const& kv : params) {
      this->params_.emplace(kv.first, kv.second);
    }
  }

  std::shared_ptr<Object> Seal(Client& client) {
    auto object_stream = ObjectStreamBaseBuilder::Seal(client);
    VINEYARD_CHECK_OK(client.CreateObjectStream(object_stream->id()));
    return std::static_pointer_cast<Object>(object_stream);
  }
};

}  // namespace vineyard

#endif  // MODULES_BASIC_STREAM_OBJECT_STREAM_H_
