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

#include "server/util/meta_tree.h"

#include <fnmatch.h>

#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "boost/lexical_cast.hpp"

namespace boost {
// Makes the behaviour of lexical_cast compatibile with boost::property_tree.
template <>
bool lexical_cast<bool, std::string>(const std::string& arg) {
  std::istringstream ss(arg);
  bool b;
  if (std::isalpha(arg[0])) {
    ss >> std::boolalpha >> b;
  } else {
    ss >> b;
  }
  return b;
}
}  // namespace boost

namespace vineyard {

namespace meta_tree {

static void decode_value(const std::string& str, NodeType& type,
                         std::string& value) {
  if (str[0] == 'v') {
    type = NodeType::Value;
    value = str.substr(1);
  } else if (str[0] == 'l') {
    type = NodeType::Link;
    value = str.substr(1);
  } else {
    type = NodeType::InvalidType;
    value.clear();
  }
}

static void encode_value(NodeType type, const std::string& value,
                         std::string& str) {
  str.clear();
  if (type == NodeType::Value) {
    str.resize(value.size() + 1);
    str[0] = 'v';
    memcpy(&str[1], value.c_str(), value.size());
  } else if (type == NodeType::Link) {
    str.resize(value.size() + 1);
    str[0] = 'l';
    memcpy(&str[1], value.c_str(), value.size());
  }
}

static bool __attribute__((used)) is_link_node(const std::string& str) {
  return str[0] == 'l';
}

static bool __attribute__((used)) is_value_node(const std::string& str) {
  return str[0] == 'v';
}

/**
 * + for non-blob types, link is: "<name>.<type>".
 * + for blobs, link is "<name>.<type>@instance_id".
 */
static Status parse_link(const std::string& str, std::string& type,
                         std::string& name, InstanceID& instance_id) {
  std::string::size_type at = str.find('@');

  if (at != std::string::npos) {
    instance_id = std::strtoull(str.c_str() + at + 1, nullptr, 10);
  } else {
    instance_id = UnspecifiedInstanceID();
  }

  std::string::size_type l1 = str.find('.');
  std::string::size_type l2 = str.rfind('.');
  if (l1 == std::string::npos || l1 != l2) {
    type.clear();
    name.clear();
    LOG(ERROR) << "meta tree link invalid: " << str;
    return Status::MetaTreeLinkInvalid();
  }
  name = str.substr(0, l1);
  if (at != std::string::npos) {
    type = str.substr(l1 + 1, at - (l1 + 1));
  } else {
    type = str.substr(l1 + 1);
  }
  if (type.empty() || name.empty()) {
    LOG(ERROR) << "meta tree link invalid: " << type << ", " << name;
    type.clear();
    name.clear();
    return Status::MetaTreeLinkInvalid();
  }
  return Status::OK();
}

/**
 * See also: `parse_link()`.
 */
static void generate_link(const std::string& type, const std::string& name,
                          std::string& link) {
  // shorten the typename, but still leave it in the link, to make the metatree
  // more verbose.
  thread_local std::stringstream ss;
  ss.str("");
  ss << name << "." << type.substr(0, type.find_first_of('<'));
  link = ss.str();
}

/**
 * See also: `parse_link()`.
 */
static void generate_link(const std::string& type, const std::string& name,
                          const InstanceID& instance_id, std::string& link) {
  // shorten the typename, but still leave it in the link, to make the metatree
  // more verbose.
  thread_local std::stringstream ss;
  ss.str("");
  ss << name << "." << type.substr(0, type.find_first_of('<')) << "@"
     << instance_id;
  link = ss.str();
}

static Status get_sub_tree(const json& tree, const std::string& prefix,
                           const std::string& name, json& sub_tree) {
  if (name.find('/') != std::string::npos) {
    LOG(ERROR) << "meta tree name invalid. " << name;
    return Status::MetaTreeNameInvalid();
  }
  std::string path = prefix;
  if (!name.empty()) {
    path += "/" + name;
  }
  auto json_path = json::json_pointer(path);
  if (tree.contains(json_path)) {
    auto const& tmp_tree = tree[json_path];
    if (tmp_tree.is_object() && !tmp_tree.empty()) {
      sub_tree = tmp_tree;
      return Status::OK();
    }
  }
  return Status::MetaTreeSubtreeNotExists(name);
}

static bool has_sub_tree(const json& tree, const std::string& prefix,
                         const std::string& name) {
  if (name.find('/') != std::string::npos) {
    return false;
  }
  std::string path = prefix;
  if (!name.empty()) {
    path += "/" + name;
  }
  return tree.contains(json::json_pointer(path));
}

static std::string object_id_from_signature(const json& tree,
                                            const std::string& signature) {
  const std::string signature_key = "/signatures/" + signature;
  return tree[json::json_pointer(signature_key)].get_ref<std::string const&>();
}

static Status get_name(const json& tree, std::string& name,
                       bool const decode = false) {
  // name: get the object id
  json::const_iterator name_iter = tree.find("id");
  if (name_iter == tree.end()) {
    return Status::MetaTreeNameNotExists();
  }
  if (name_iter->is_object()) {
    LOG(ERROR) << "meta tree id invalid. " << *name_iter;
    return Status::MetaTreeNameInvalid();
  }
  name = name_iter->get_ref<std::string const&>();
  if (decode) {
    NodeType node_type = NodeType::InvalidType;
    decode_value(name, node_type, name);
    if (node_type != NodeType::Value) {
      return Status::MetaTreeNameInvalid();
    }
  }
  return Status::OK();
}

static Status get_type(const json& tree, std::string& type,
                       bool const decode = false) {
  // type: get the typename
  json::const_iterator type_iter = tree.find("typename");
  if (type_iter == tree.end()) {
    return Status::MetaTreeNameNotExists();
  }
  if (type_iter->is_object()) {
    LOG(ERROR) << "meta tree typename invalid. " << *type_iter;
    return Status::MetaTreeTypeInvalid();
  }
  type = type_iter->get_ref<std::string const&>();
  if (decode) {
    NodeType node_type = NodeType::InvalidType;
    decode_value(type, node_type, type);
    if (node_type != NodeType::Value) {
      return Status::MetaTreeTypeInvalid();
    }
  }
  return Status::OK();
}

static Status get_type_name(const json& tree, std::string& type,
                            std::string& name, bool const decode = false) {
  RETURN_ON_ERROR(get_type(tree, type, decode));
  RETURN_ON_ERROR(get_name(tree, name, decode));
  return Status::OK();
}

/**
 * In `MetaData::AddMember`, the parameter might be an object id. In such cases
 * the client doesn't have the full metadata json of the object, there will be
 * just an `ObjectID`.
 */
static bool is_meta_placeholder(const json& tree) {
  return tree.is_object() && tree.size() == 1 && tree.contains("id");
}

/**
 * Get metadata for an object "recursively".
 */
Status GetData(const json& tree, const ObjectID id, json& sub_tree,
               InstanceID const& current_instance_id) {
  return GetData(tree, VYObjectIDToString(id), sub_tree, current_instance_id);
}

/**
 * Get metadata for an object "recursively".
 */
Status GetData(const json& tree, const std::string& name, json& sub_tree,
               InstanceID const& current_instance_id) {
  json tmp_tree;
  sub_tree.clear();
  Status status = get_sub_tree(tree, "/data", name, tmp_tree);
  if (!status.ok()) {
    return status;
  }
  for (auto const& item : json::iterator_wrapper(tmp_tree)) {
    if (!item.value().is_string()) {
      sub_tree[item.key()] = item.value();
      continue;
    }
    std::string const& item_value = item.value().get_ref<std::string const&>();
    NodeType type;
    std::string value;
    decode_value(item_value, type, value);
    if (type == NodeType::Value) {
      sub_tree[item.key()] = value;
    } else if (type == NodeType::Link) {
      InstanceID instance_id = UnspecifiedInstanceID();
      std::string sub_sub_tree_type, sub_sub_tree_name;
      status =
          parse_link(value, sub_sub_tree_type, sub_sub_tree_name, instance_id);

      // the sub_sub_tree_name might be a signature
      if (sub_sub_tree_name[0] == 's') {
        sub_sub_tree_name = object_id_from_signature(tree, sub_sub_tree_name);
      }

      if (!status.ok()) {
        sub_tree.clear();
        return status;
      }
      json sub_sub_tree;
      status =
          GetData(tree, sub_sub_tree_name, sub_sub_tree, current_instance_id);
      if (status.ok()) {
        sub_tree[item.key()] = sub_sub_tree;
      } else {
        ObjectID sub_sub_tree_id = VYObjectIDFromString(sub_sub_tree_name);
        if (IsBlob(sub_sub_tree_id) && status.IsMetaTreeSubtreeNotExists()) {
          // make an empty blob
          sub_sub_tree["id"] = sub_sub_tree_name;
          sub_sub_tree["typename"] = "vineyard::Blob";
          sub_sub_tree["length"] = 0;
          sub_sub_tree["nbytes"] = 0;
          sub_sub_tree["instance_id"] = instance_id;
          sub_sub_tree["transient"] = true;
          sub_tree[item.key()] = sub_sub_tree;
        } else {
          sub_tree.clear();
          return status;
        }
      }
    } else {
      return Status::MetaTreeTypeInvalid();
    }
  }
  sub_tree["id"] = name;
  return Status::OK();
}

Status ListData(const json& tree, std::string const& pattern, bool const regex,
                size_t const limit, json& tree_group) {
  if (!tree.contains("data")) {
    return Status::OK();
  }

  size_t found = 0;

  std::regex regex_pattern;
  if (regex) {
    // pre-compile regex pattern, and for invalid regex pattern, return nothing.
    try {
      regex_pattern = std::regex(pattern);
    } catch (std::regex_error const&) { return Status::OK(); }
  }

  for (auto const& item : json::iterator_wrapper(tree["data"])) {
    if (found >= limit) {
      break;
    }

    if (!item.value().is_object() || item.value().empty()) {
      LOG(INFO) << "Object meta shouldn't be empty";
      return Status::MetaTreeInvalid();
    }
    std::string type;
    RETURN_ON_ERROR(get_type(item.value(), type, true));

    // match type on pattern
    bool matched = false;
    if (regex) /* regex match */ {
      std::cmatch __m;
      matched = std::regex_match(type.c_str(), __m, regex_pattern);
    } else /* wildcard match */ {
      // https://www.man7.org/linux/man-pages/man3/fnmatch.3.html
      matched = fnmatch(pattern.c_str(), type.c_str(), 0) == 0;
    }

    if (matched) {
      found += 1;
      json object_meta_tree;
      RETURN_ON_ERROR(GetData(tree, item.key(), object_meta_tree));
      tree_group[item.key()] = object_meta_tree;
    }
  }
  return Status::OK();
}

Status DelDataOps(const json& tree, const ObjectID id,
                  std::vector<IMetaService::op_t>& ops, bool& sync_remote) {
  if (IsBlob(id)) {
    ops.emplace_back(IMetaService::op_t::Del("/data/" + ObjectIDToString(id)));
    return Status::OK();
  }
  return DelDataOps(tree, VYObjectIDToString(id), ops, sync_remote);
}

Status DelDataOps(const json& tree, const std::set<ObjectID>& ids,
                  std::vector<IMetaService::op_t>& ops, bool& sync_remote) {
  for (auto const& id : ids) {
    auto s = DelDataOps(tree, id, ops, sync_remote);
    if (!s.ok() && !IsBlob(id)) {
      // here it might be a "remote" blobs.
      return s;
    }
  }
  return Status::OK();
}

Status DelDataOps(const json& tree, const std::vector<ObjectID>& ids,
                  std::vector<IMetaService::op_t>& ops, bool& sync_remote) {
  for (auto const& id : ids) {
    auto s = DelDataOps(tree, id, ops, sync_remote);
    if (!s.ok() && !IsBlob(id)) {
      // here it might be a "remote" blobs.
      return s;
    }
  }
  return Status::OK();
}

Status DelDataOps(const json& tree, const std::string& name,
                  std::vector<IMetaService::op_t>& ops, bool& sync_remote) {
  std::string data_prefix = "/data";
  auto json_path = json::json_pointer(data_prefix);
  if (tree.contains(json_path)) {
    auto const& data_tree = tree[json_path];
    if (data_tree.contains(name)) {
      // erase from etcd
      if (!sync_remote && !data_tree[name].value("transient", true)) {
        sync_remote = true;
      }
      ops.emplace_back(IMetaService::op_t::Del(data_prefix + "/" + name));
      return Status::OK();
    }
  }
  return Status::MetaTreeSubtreeNotExists(name);
}

static void generate_put_ops(const json& meta, const json& diff,
                             const json& signatures, const std::string& name,
                             std::vector<IMetaService::op_t>& ops) {
  LOG(INFO) << "generate_put_ops: diff = " << diff.dump(4);
  if (!diff.is_object() || diff.empty()) {
    return;
  }
  if (diff["typename"].get_ref<std::string const&>() == "vineyard::Blob") {
    // blobs won't come into metadata.
    return;
  }
  std::string key_prefix = "/data" + std::string("/") + name + "/";
  std::string signature_key_prefix = "/signatures/";
  for (auto const& item : json::iterator_wrapper(diff)) {
    bool global_object = diff.value("global", false);
    if (item.value().is_object() && !item.value().empty()) {
      std::string sub_type, sub_name;
      VINEYARD_SUPPRESS(get_type_name(item.value(), sub_type, sub_name));
      if (!has_sub_tree(meta, "/data", sub_name)) {
        generate_put_ops(meta, item.value(), signatures, sub_name, ops);
      }
      // for global object, we record member's signature instead, and the
      // signature mapping
      if (global_object) {
        const std::string sig_as_name =
            SignatureToString(signatures[sub_name].get<Signature>());
        ops.emplace_back(IMetaService::op_t::Put(
            signature_key_prefix + sig_as_name, sub_name));
        sub_name = sig_as_name;
      }
      std::string link;
      if (sub_type == "vineyard::Blob") {
        LOG(INFO) << "item instance id: "
                  << item.value()["instance_id"].get<InstanceID>();
        generate_link(sub_type, sub_name,
                      item.value()["instance_id"].get<InstanceID>(), link);
      } else {
        generate_link(sub_type, sub_name, link);
      }
      std::string encoded_value;
      encode_value(NodeType::Link, link, encoded_value);
      ops.emplace_back(
          IMetaService::op_t::Put(key_prefix + item.key(), encoded_value));
    } else {
      // don't repeat "id" in the etcd kvs.
      if (item.key() == "id") {
        continue;
      }
      std::string key = key_prefix + item.key();
      if (item.value().is_string()) {
        std::string encoded_value;
        encode_value(NodeType::Value,
                     item.value().get_ref<std::string const&>(), encoded_value);
        ops.emplace_back(IMetaService::op_t::Put(key, encoded_value));
      } else {
        ops.emplace_back(IMetaService::op_t::Put(key, item.value()));
      }
    }
  }
}

static void generate_persist_ops(json& diff, const std::string& name,
                                 std::vector<IMetaService::op_t>& ops,
                                 std::set<std::string>& dedup) {
  std::string data_key = "/data" + std::string("/") + name;
  if (dedup.find(data_key) != dedup.end()) {
    return;
  }
  bool global_object = diff.value("global", false);

  for (auto& item : json::iterator_wrapper(diff)) {
    if (item.value().is_object() &&
        !item.value().empty()) /* build link, and recursively generate */ {
      std::string sub_type, sub_name;
      VINEYARD_SUPPRESS(get_type_name(item.value(), sub_type, sub_name));

      // when persist global object, persist the signature record.
      if (global_object) {
        std::string sub_sig =
            SignatureToString(item.value()["signature"].get<Signature>());
        // a global object may refer a local object multiple times using
        // the different key.
        if (dedup.find(sub_sig) == dedup.end()) {
          dedup.emplace(sub_sig);
          ops.emplace_back(
              IMetaService::op_t::Put("/signatures/" + sub_sig, sub_name));
        }
        sub_name = sub_sig;
      }

      // Don't persist blob into etcd, but the link cannot be omitted.
      if (item.value()["transient"].get<bool>() &&
          sub_type != "vineyard::Blob") {
        // otherwise, skip recursively generate ops
        generate_persist_ops(item.value(), sub_name, ops, dedup);
      }
      std::string link;
      if (sub_type == "vineyard::Blob") {
        generate_link(sub_type, sub_name,
                      item.value()["instance_id"].get<InstanceID>(), link);
      } else {
        generate_link(sub_type, sub_name, link);
      }
      std::string encoded_value;
      encode_value(NodeType::Link, link, encoded_value);
      diff[item.key()] = encoded_value;
    } else /* do value transformation (encoding) */ {
      json value_to_persist;
      if (item.key() == "transient") {
        diff[item.key()] = false;
      } else if (item.value().is_string()) {
        std::string encoded_value;
        encode_value(NodeType::Value,
                     item.value().get_ref<std::string const&>(), encoded_value);
        diff[item.key()] = encoded_value;
      }
    }
  }
  // don't repeat "id" in the etcd kvs.
  diff.erase("id");
  ops.emplace_back(IMetaService::op_t::Put(data_key, diff));
  dedup.emplace(data_key);
}

/**
 * Returns:
 *
 *  diff: diff json
 *  instance_id: instance_id of members and the object itself, can represents
 *               the final instance_id of the object.
 */
static Status diff_data_meta_tree(const json& meta,
                                  const std::string& sub_tree_name,
                                  const json& sub_tree, json& diff,
                                  json& signatures, InstanceID& instance_id) {
  json old_sub_tree;
  Status status = get_sub_tree(meta, "/data", sub_tree_name, old_sub_tree);
  bool global_object = sub_tree.value("global", false);

  // subtree can be a place holder:
  //
  // the object it points to must exist.
  if (is_meta_placeholder(sub_tree)) {
    // the target exists.
    if (status.ok()) {
      std::string sub_tree_type;
      RETURN_ON_ERROR(get_type(old_sub_tree, sub_tree_type, true));
      diff["id"] = sub_tree_name;
      diff["signature"] = old_sub_tree["signature"].get<Signature>();
      diff["typename"] = sub_tree_type;
      if (old_sub_tree.contains("global")) {
        diff["global"] = old_sub_tree["global"].get<bool>();
      }
      instance_id = old_sub_tree["instance_id"].get<InstanceID>();
      return Status::OK();
    }
    // blob is special: we cannot do resolution.
    if (IsBlob(ObjectIDFromString(sub_tree_name))) {
      return Status::MetaTreeInvalid(
          "The blobs cannot be added in the placeholder fashion.");
    }
    // otherwise: error
    return status;
  }

  // handle (possible) unexpected errors
  if (!status.ok()) {
    if (status.IsMetaTreeSubtreeNotExists()) {
      if (IsBlob(ObjectIDFromString(sub_tree_name))) {
        diff = sub_tree;  // won't be a placeholder
        instance_id = sub_tree["instance_id"].get<InstanceID>();
        return Status::OK();
      } else {
        diff["transient"] = true;
      }
    } else {
      return status;
    }
  }

  // recompute instance_id: check if it refers remote objects.
  instance_id = sub_tree["instance_id"].get<InstanceID>();

  for (auto const& item : json::iterator_wrapper(sub_tree)) {
    // don't diff on id, typename and instance_id and don't update them, that
    // means, we can only update member's meta, cannot update the whole member
    // itself.
    if (item.key() == "id" || item.key() == "signature" ||
        item.key() == "typename" || item.key() == "instance_id") {
      continue;
    }

    if (!item.value().is_object() /* plain value */) {
      const json& new_value = item.value();
      if (status.ok() /* old meta exists */) {
        if (old_sub_tree.contains(item.key())) {
          auto old_value = old_sub_tree[item.key()];

          if (old_value.is_string()) {
            NodeType old_value_type;
            std::string old_value_decoded;
            decode_value(old_value.get_ref<std::string const&>(),
                         old_value_type, old_value_decoded);
            old_value = json(old_value_decoded);
          }

          bool require_update = false;
          if (item.key() == "transient") {
            // put_data wan't make persist value becomes transient, since the
            // info in the client may be out-of-date.
            require_update = old_value == true && old_value != new_value;
          } else {
            require_update = old_value != new_value;
          }

          if (require_update) {
            VLOG(10) << "DIFF: " << item.key() << ": " << old_value << " -> "
                     << new_value;
            diff[item.key()] = new_value;
          }
        } else {
          VLOG(10) << "DIFF: " << item.key() << ": [none] -> " << new_value;
          diff[item.key()] = new_value;
        }
      } else if (status.IsMetaTreeSubtreeNotExists()) {
        VLOG(10) << "DIFF: " << item.key() << ": [none] -> " << new_value;
        diff[item.key()] = new_value;
      } else {
        return status;
      }
    } else /* member object */ {
      const json& sub_sub_tree = item.value();

      // original corresponding field must be a member not a key-value
      if (status.ok() /* old meta exists */) {
        auto mb_old_sub_sub_tree = old_sub_tree.find(item.key());
        if (mb_old_sub_sub_tree != old_sub_tree.end() &&
            (!mb_old_sub_sub_tree->is_string() ||
             !is_link_node(
                 mb_old_sub_sub_tree->get_ref<std::string const&>()))) {
          return Status::MetaTreeInvalid(
              "Here it was a link node, but not it isn't.");
        }
      }

      std::string sub_sub_tree_name;
      RETURN_ON_ERROR(get_name(sub_sub_tree, sub_sub_tree_name));

      json diff_sub_tree;
      InstanceID sub_instance_id;
      RETURN_ON_ERROR(diff_data_meta_tree(meta, sub_sub_tree_name, sub_sub_tree,
                                          diff_sub_tree, signatures,
                                          sub_instance_id));

      if (!global_object && instance_id != sub_instance_id) {
        return Status::GlobalObjectInvalid(
            "Local object cannot refer remote objects");
      }
      if (global_object && ((diff_sub_tree.is_object() &&
                             diff_sub_tree.value("global", false)) ||
                            sub_sub_tree.value("global", false))) {
        return Status::GlobalObjectInvalid(
            "Global object cannot have nested structure");
      }

      if (instance_id != sub_instance_id) {
        instance_id = UnspecifiedInstanceID();
      }

      if (status.ok() /* old meta exists */) {
        // WARNING: the member object will be updated.
        if (diff_sub_tree.is_object() && !diff_sub_tree.empty()) {
          diff[item.key()] = diff_sub_tree;
        }

        // record a possible signature link
        if (global_object) {
          signatures[sub_sub_tree["id"].get_ref<std::string const&>()] =
              sub_sub_tree["signature"].get<Signature>();
        }
      } else if (status.IsMetaTreeSubtreeNotExists()) {
        if (!is_meta_placeholder(sub_sub_tree)) {
          // if sub_sub_tree is placeholder, the diff already contains id and
          // typename
          std::string sub_sub_tree_type;
          RETURN_ON_ERROR(get_type_name(sub_sub_tree, sub_sub_tree_type,
                                        sub_sub_tree_name));
          diff_sub_tree["id"] = sub_sub_tree_name;
          diff_sub_tree["typename"] = sub_sub_tree_type;

          // record a possible signature link
          if (global_object) {
            signatures[sub_sub_tree["id"].get_ref<std::string const&>()] =
                sub_sub_tree["signature"].get<Signature>();
          }
        } else {
          // record a possible signature link
          if (global_object) {
            signatures[diff_sub_tree["id"].get_ref<std::string const&>()] =
                diff_sub_tree["signature"].get<Signature>();
          }
        }
        diff[item.key()] = diff_sub_tree;
      } else {
        return status;
      }
    }
  }
  if (diff.is_object() && !diff.empty() &&
      status.IsMetaTreeSubtreeNotExists()) {
    // must not be meta placeholder
    std::string sub_tree_type;
    RETURN_ON_ERROR(get_type(sub_tree, sub_tree_type));

    diff["id"] = sub_tree_name;
    diff["signature"] = sub_tree["signature"];
    diff["typename"] = sub_tree_type;
    diff["instance_id"] = instance_id;
  }
  return Status::OK();
}

static void persist_meta_tree(const json& sub_tree, json& diff) {
  // NB: we don't need to track which objects are persist since the json
  // cached in the server will be updated by the background watcher task.
  if (sub_tree["transient"].get<bool>()) {
    for (auto const& item : json::iterator_wrapper(sub_tree)) {
      if (item.value().is_object() && !item.value().empty()) {
        const json& sub_sub_tree = item.value();
        // recursive
        json sub_diff;
        persist_meta_tree(sub_sub_tree, sub_diff);
        if (sub_diff.is_object() && !sub_diff.empty()) {
          diff[item.key()] = sub_diff;
        } else {
          // will be used to generate the link.
          diff[item.key()] = sub_sub_tree;
        }
      } else {
        diff[item.key()] = item.value();
      }
    }
  }
}

Status PutDataOps(const json& tree, const ObjectID id, const json& sub_tree,
                  std::vector<IMetaService::op_t>& ops,
                  InstanceID& computed_instance_id) {
  json diff;
  json signatures;
  std::string name = VYObjectIDToString(id);
  // recompute instance_id: check if it refers remote objects.
  Status status = diff_data_meta_tree(tree, name, sub_tree, diff, signatures,
                                      computed_instance_id);

  if (!status.ok()) {
    return status;
  }

  if (diff.is_null() || (diff.is_object() && diff.empty())) {
    return Status::OK();
  }

  generate_put_ops(tree, diff, signatures, name, ops);
  return Status::OK();
}

Status PersistOps(const json& tree, const ObjectID id,
                  std::vector<IMetaService::op_t>& ops) {
  json sub_tree, diff;
  Status status = GetData(tree, id, sub_tree);
  if (!status.ok()) {
    return status;
  }
  persist_meta_tree(sub_tree, diff);

  if (diff.is_null() || (diff.is_object() && diff.empty())) {
    return Status::OK();
  }

  std::string name = VYObjectIDToString(id);
  std::set<std::string> dedup;
  generate_persist_ops(diff, name, ops, dedup);
  return Status::OK();
}

Status Exists(const json& tree, const ObjectID id, bool& exists) {
  std::string name = VYObjectIDToString(id);
  exists = has_sub_tree(tree, "/data", name);
  return Status::OK();
}

Status ShallowCopyOps(const json& tree, const ObjectID id,
                      const ObjectID target,
                      std::vector<IMetaService::op_t>& ops, bool& transient) {
  std::string name = VYObjectIDToString(id);
  json tmp_tree;
  RETURN_ON_ERROR(get_sub_tree(tree, "/data", name, tmp_tree));
  RETURN_ON_ASSERT(
      tmp_tree.contains("transient") && tmp_tree["transient"].is_boolean(),
      "The 'transient' should a plain bool value");
  transient = tmp_tree["transient"].get<bool>();
  std::string key_prefix =
      "/data" + std::string("/") + VYObjectIDToString(target) + "/";
  for (auto const& item : json::iterator_wrapper(tmp_tree)) {
    ops.emplace_back(
        IMetaService::op_t::Put(key_prefix + item.key(), item.value()));
  }
  return Status::OK();
}

Status IfPersist(const json& tree, const ObjectID id, bool& persist) {
  std::string name = VYObjectIDToString(id);
  json tmp_tree;
  Status status = get_sub_tree(tree, "/data", name, tmp_tree);
  if (status.ok()) {
    RETURN_ON_ASSERT(
        tmp_tree.contains("transient") && tmp_tree["transient"].is_boolean(),
        "The 'transient' should a plain boolean value");
    persist = !tmp_tree["transient"].get<bool>();
  }
  return status;
}

Status FilterAtInstance(const json& tree, const InstanceID& instance_id,
                        std::vector<ObjectID>& objects) {
  if (tree.contains("data")) {
    for (auto const& item : json::iterator_wrapper(tree["data"])) {
      if (item.value().is_object() && !item.value().empty()) {
        if (item.value().contains("instance_id") &&
            item.value()["instance_id"].get<InstanceID>() == instance_id) {
          objects.emplace_back(VYObjectIDFromString(item.key()));
        }
      }
    }
  }
  return Status::OK();
}

Status DecodeObjectID(const json& tree, const std::string& value,
                      ObjectID& object_id) {
  meta_tree::NodeType type;
  std::string link_value;
  decode_value(value, type, link_value);
  if (type == NodeType::Link) {
    InstanceID instance_id = UnspecifiedInstanceID();
    std::string type_of_value, name_of_value;
    auto status =
        parse_link(link_value, type_of_value, name_of_value, instance_id);
    if (status.ok()) {
      if (name_of_value[0] == 'o') {
        object_id = VYObjectIDFromString(name_of_value);
      } else if (name_of_value[0] == 's') {
        object_id =
            VYObjectIDFromString(object_id_from_signature(tree, name_of_value));
      } else {
        return Status::Invalid("Not a name or signature: " + name_of_value);
      }
      return Status::OK();
    }
  }
  return Status::Invalid();
}

}  // namespace meta_tree

}  // namespace vineyard
