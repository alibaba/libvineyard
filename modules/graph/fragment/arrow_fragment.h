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

#ifndef MODULES_GRAPH_FRAGMENT_ARROW_FRAGMENT_H_
#define MODULES_GRAPH_FRAGMENT_ARROW_FRAGMENT_H_

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "arrow/table.h"
#include "arrow/util/config.h"
#include "arrow/util/key_value_metadata.h"
#include "boost/algorithm/string.hpp"
#include "grape/graph/adj_list.h"
#include "grape/utils/vertex_array.h"

#include "basic/ds/arrow.h"
#include "common/util/functions.h"
#include "common/util/typename.h"

#include "graph/fragment/fragment_traits.h"
#include "graph/fragment/graph_schema.h"
#include "graph/fragment/property_graph_types.h"
#include "graph/fragment/property_graph_utils.h"
#include "graph/utils/context_protocols.h"
#include "graph/utils/error.h"
#include "graph/utils/thread_group.h"
#include "graph/vertex_map/arrow_vertex_map.h"

namespace gs {

template <typename OID_T, typename VID_T, typename VDATA_T, typename EDATA_T>
class ArrowProjectedFragment;

}  // namespace gs

namespace vineyard {

template <typename OID_T, typename VID_T>
class ArrowFragmentBuilder;

inline std::string generate_name_with_suffix(
    const std::string& prefix, property_graph_types::LABEL_ID_TYPE label) {
  return prefix + "_" + std::to_string(label);
}

inline std::string generate_name_with_suffix(
    const std::string& prefix, property_graph_types::LABEL_ID_TYPE v_label,
    property_graph_types::LABEL_ID_TYPE e_label) {
  return prefix + "_" + std::to_string(v_label) + "_" + std::to_string(e_label);
}

inline std::string arrow_type_to_string(
    const std::shared_ptr<arrow::DataType>& type) {
  if (type->Equals(arrow::int32())) {
    return "int32_t";
  } else if (type->Equals(arrow::int64())) {
    return "int64_t";
  } else if (type->Equals(arrow::float32())) {
    return "float";
  } else if (type->Equals(arrow::float64())) {
    return "double";
  } else if (type->Equals(arrow::uint32())) {
    return "uint32_t";
  } else if (type->Equals(arrow::uint64())) {
    return "uint64_t";
  } else if (type->Equals(arrow::utf8()) || type->Equals(arrow::large_utf8())) {
    return "string";
  } else {
    return "undefined";
  }
}

class ArrowFragmentBase : public vineyard::Object {
 public:
  using prop_id_t = property_graph_types::PROP_ID_TYPE;
  using label_id_t = property_graph_types::LABEL_ID_TYPE;

  virtual ~ArrowFragmentBase() = default;

  virtual vineyard::ObjectID AddVertexColumns(
      vineyard::Client& client,
      const std::map<
          label_id_t,
          std::vector<std::pair<std::string, std::shared_ptr<arrow::Array>>>>
          columns) = 0;

  virtual vineyard::ObjectID vertex_map_id() const = 0;
};

inline const void* get_arrow_array_ptr(std::shared_ptr<arrow::Array> array) {
  if (array->type()->Equals(arrow::int8())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::Int8Array>(array)->raw_values());
  } else if (array->type()->Equals(arrow::uint8())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::UInt8Array>(array)->raw_values());
  } else if (array->type()->Equals(arrow::int16())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::Int16Array>(array)->raw_values());
  } else if (array->type()->Equals(arrow::uint16())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::UInt16Array>(array)->raw_values());
  } else if (array->type()->Equals(arrow::int32())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::Int32Array>(array)->raw_values());
  } else if (array->type()->Equals(arrow::uint32())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::UInt32Array>(array)->raw_values());
  } else if (array->type()->Equals(arrow::int64())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::Int64Array>(array)->raw_values());
  } else if (array->type()->Equals(arrow::uint64())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::UInt64Array>(array)->raw_values());
  } else if (array->type()->Equals(arrow::float32())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::FloatArray>(array)->raw_values());
  } else if (array->type()->Equals(arrow::float64())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::DoubleArray>(array)->raw_values());
  } else if (array->type()->Equals(arrow::utf8())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::StringArray>(array).get());
  } else if (array->type()->Equals(arrow::large_utf8())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::LargeStringArray>(array).get());
  } else if (array->type()->Equals(arrow::null())) {
    return reinterpret_cast<const void*>(
        std::dynamic_pointer_cast<arrow::NullArray>(array).get());
  } else {
    LOG(FATAL) << "Array type - " << array->type()->ToString()
               << " is not supported yet...";
    return NULL;
  }
}

template <typename OID_T, typename VID_T>
class ArrowFragment
    : public ArrowFragmentBase,
      public vineyard::BareRegistered<ArrowFragment<OID_T, VID_T>> {
 public:
  using oid_t = OID_T;
  using vid_t = VID_T;
  using internal_oid_t = typename InternalType<oid_t>::type;
  using eid_t = property_graph_types::EID_TYPE;
  using prop_id_t = property_graph_types::PROP_ID_TYPE;
  using label_id_t = property_graph_types::LABEL_ID_TYPE;
  using vertex_range_t = grape::VertexRange<vid_t>;
  using nbr_t = property_graph_utils::Nbr<vid_t, eid_t>;
  using nbr_unit_t = property_graph_utils::NbrUnit<vid_t, eid_t>;
  using adj_list_t = property_graph_utils::AdjList<vid_t, eid_t>;
  using raw_adj_list_t = property_graph_utils::RawAdjList<vid_t, eid_t>;
  using vertex_map_t = ArrowVertexMap<internal_oid_t, vid_t>;
  using vertex_t = grape::Vertex<vid_t>;

  using vid_array_t = typename vineyard::ConvertToArrowType<vid_t>::ArrayType;
  using eid_array_t = typename vineyard::ConvertToArrowType<eid_t>::ArrayType;

  using vid_builder_t = typename ConvertToArrowType<vid_t>::BuilderType;

  template <typename DATA_T>
  using vertex_array_t = grape::VertexArray<DATA_T, vid_t>;
  static constexpr grape::LoadStrategy load_strategy =
      grape::LoadStrategy::kBothOutIn;

  static std::shared_ptr<vineyard::Object> Create() __attribute__((used)) {
    return std::static_pointer_cast<vineyard::Object>(
        std::make_shared<ArrowFragment<oid_t, vid_t>>());
  }

 public:
  ~ArrowFragment() = default;

  vineyard::ObjectID vertex_map_id() const override { return vm_ptr_->id(); }

#define CONSTRUCT_ARRAY_VECTOR(type, vec, label_num, prefix)                   \
  do {                                                                         \
    vec.resize(label_num);                                                     \
    for (label_id_t i = 0; i < label_num; ++i) {                               \
      vineyard::NumericArray<type> array;                                      \
      array.Construct(                                                         \
          meta.GetMemberMeta(vineyard::generate_name_with_suffix(prefix, i))); \
      vec[i] = array.GetArray();                                               \
    }                                                                          \
  } while (0)

#define CONSTRUCT_ARRAY_VECTOR_VECTOR(type, vec, v_label_num, e_label_num, \
                                      prefix)                              \
  do {                                                                     \
    vec.resize(v_label_num);                                               \
    for (label_id_t i = 0; i < v_label_num; ++i) {                         \
      vec[i].resize(e_label_num);                                          \
      for (label_id_t j = 0; j < e_label_num; ++j) {                       \
        vineyard::NumericArray<type> array;                                \
        array.Construct(meta.GetMemberMeta(                                \
            vineyard::generate_name_with_suffix(prefix, i, j)));           \
        vec[i][j] = array.GetArray();                                      \
      }                                                                    \
    }                                                                      \
  } while (0)

#define CONSTRUCT_BINARY_ARRAY_VECTOR_VECTOR(vec, v_label_num, e_label_num, \
                                             prefix)                        \
  do {                                                                      \
    vec.resize(v_label_num);                                                \
    for (label_id_t i = 0; i < v_label_num; ++i) {                          \
      vec[i].resize(e_label_num);                                           \
      for (label_id_t j = 0; j < e_label_num; ++j) {                        \
        vineyard::FixedSizeBinaryArray array;                               \
        array.Construct(meta.GetMemberMeta(                                 \
            vineyard::generate_name_with_suffix(prefix, i, j)));            \
        vec[i][j] = array.GetArray();                                       \
      }                                                                     \
    }                                                                       \
  } while (0)

#define CONSTRUCT_TABLE_VECTOR(vec, label_num, prefix)                         \
  do {                                                                         \
    vec.resize(label_num);                                                     \
    for (label_id_t i = 0; i < label_num; ++i) {                               \
      vineyard::Table table;                                                   \
      table.Construct(                                                         \
          meta.GetMemberMeta(vineyard::generate_name_with_suffix(prefix, i))); \
      vec[i] = table.GetTable();                                               \
    }                                                                          \
  } while (0)

  void Construct(const vineyard::ObjectMeta& meta) override {
    this->meta_ = meta;
    this->id_ = meta.GetId();

    this->fid_ = meta.GetKeyValue<fid_t>("fid");
    this->fnum_ = meta.GetKeyValue<fid_t>("fnum");
    this->directed_ = (meta.GetKeyValue<int>("directed") != 0);
    this->vertex_label_num_ = meta.GetKeyValue<label_id_t>("vertex_label_num");
    this->edge_label_num_ = meta.GetKeyValue<label_id_t>("edge_label_num");

    this->schema_.FromJSONString(meta.GetKeyValue("schema"));

    vid_parser_.Init(fnum_, vertex_label_num_);

    this->ivnums_.Construct(meta.GetMemberMeta("ivnums"));
    this->ovnums_.Construct(meta.GetMemberMeta("ovnums"));
    this->tvnums_.Construct(meta.GetMemberMeta("tvnums"));

    CONSTRUCT_TABLE_VECTOR(vertex_tables_, vertex_label_num_, "vertex_tables");
    CONSTRUCT_ARRAY_VECTOR(vid_t, ovgid_lists_, vertex_label_num_,
                           "ovgid_lists");
    ovg2l_maps_.resize(vertex_label_num_);
    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      ovg2l_maps_[i] = std::make_shared<vineyard::Hashmap<vid_t, vid_t>>();
      ovg2l_maps_[i]->Construct(
          meta.GetMemberMeta(generate_name_with_suffix("ovg2l_maps", i)));
    }

    CONSTRUCT_TABLE_VECTOR(edge_tables_, edge_label_num_, "edge_tables");

    if (directed_) {
      CONSTRUCT_BINARY_ARRAY_VECTOR_VECTOR(ie_lists_, vertex_label_num_,
                                           edge_label_num_, "ie_lists");
    }
    CONSTRUCT_BINARY_ARRAY_VECTOR_VECTOR(oe_lists_, vertex_label_num_,
                                         edge_label_num_, "oe_lists");

    if (directed_) {
      CONSTRUCT_ARRAY_VECTOR_VECTOR(int64_t, ie_offsets_lists_,
                                    vertex_label_num_, edge_label_num_,
                                    "ie_offsets_lists");
    }
    CONSTRUCT_ARRAY_VECTOR_VECTOR(int64_t, oe_offsets_lists_, vertex_label_num_,
                                  edge_label_num_, "oe_offsets_lists");
    vm_ptr_ = std::make_shared<vertex_map_t>();
    vm_ptr_->Construct(meta.GetMemberMeta("vertex_map"));

    initPointers();
  }

  fid_t fid() const { return fid_; }

  fid_t fnum() const { return fnum_; }

  label_id_t vertex_label_num() const { return vertex_label_num_; }

  label_id_t vertex_label(const vertex_t& v) const {
    return vid_parser_.GetLabelId(v.GetValue());
  }

  int64_t vertex_offset(const vertex_t& v) const {
    return vid_parser_.GetOffset(v.GetValue());
  }

  label_id_t edge_label_num() const { return edge_label_num_; }

  prop_id_t vertex_property_num(label_id_t label) const {
    return static_cast<prop_id_t>(vertex_tables_[label]->num_columns());
  }

  std::shared_ptr<arrow::DataType> vertex_property_type(label_id_t label,
                                                        prop_id_t prop) const {
    return vertex_tables_[label]->schema()->field(prop)->type();
  }

  prop_id_t edge_property_num(label_id_t label) const {
    return static_cast<prop_id_t>(edge_tables_[label]->num_columns());
  }

  std::shared_ptr<arrow::DataType> edge_property_type(label_id_t label,
                                                      prop_id_t prop) const {
    return edge_tables_[label]->schema()->field(prop)->type();
  }

  std::shared_ptr<arrow::Table> vertex_data_table(label_id_t i) const {
    return vertex_tables_[i];
  }

  std::shared_ptr<arrow::Table> edge_data_table(label_id_t i) const {
    return edge_tables_[i];
  }

  template <typename DATA_T>
  property_graph_utils::EdgeDataColumn<DATA_T, nbr_unit_t> edge_data_column(
      label_id_t label, prop_id_t prop) const {
    if (edge_tables_[label]->num_rows() == 0) {
      return property_graph_utils::EdgeDataColumn<DATA_T, nbr_unit_t>();
    } else {
      return property_graph_utils::EdgeDataColumn<DATA_T, nbr_unit_t>(
          edge_tables_[label]->column(prop)->chunk(0));
    }
  }

  template <typename DATA_T>
  property_graph_utils::VertexDataColumn<DATA_T, vertex_t> vertex_data_column(
      label_id_t label, prop_id_t prop) const {
    if (vertex_tables_[label]->num_rows() == 0) {
      return property_graph_utils::VertexDataColumn<DATA_T, vertex_t>(
          InnerVertices(label));
    } else {
      return property_graph_utils::VertexDataColumn<DATA_T, vertex_t>(
          InnerVertices(label), vertex_tables_[label]->column(prop)->chunk(0));
    }
  }

  vertex_range_t Vertices(label_id_t label_id) const {
    return vertex_range_t(
        vid_parser_.GenerateId(0, label_id, 0),
        vid_parser_.GenerateId(0, label_id, tvnums_[label_id]));
  }

  vertex_range_t InnerVertices(label_id_t label_id) const {
    return vertex_range_t(
        vid_parser_.GenerateId(0, label_id, 0),
        vid_parser_.GenerateId(0, label_id, ivnums_[label_id]));
  }

  vertex_range_t OuterVertices(label_id_t label_id) const {
    return vertex_range_t(
        vid_parser_.GenerateId(0, label_id, ivnums_[label_id]),
        vid_parser_.GenerateId(0, label_id, tvnums_[label_id]));
  }

  inline vid_t GetVerticesNum(label_id_t label_id) const {
    return tvnums_[label_id];
  }

  bool GetVertex(label_id_t label, const oid_t& oid, vertex_t& v) const {
    vid_t gid;
    if (vm_ptr_->GetGid(label, internal_oid_t(oid), gid)) {
      return (vid_parser_.GetFid(gid) == fid_) ? InnerVertexGid2Vertex(gid, v)
                                               : OuterVertexGid2Vertex(gid, v);
    } else {
      return false;
    }
  }

  oid_t GetId(const vertex_t& v) const {
    return IsInnerVertex(v) ? GetInnerVertexId(v) : GetOuterVertexId(v);
  }

  fid_t GetFragId(const vertex_t& u) const {
    return IsInnerVertex(u) ? fid_ : vid_parser_.GetFid(GetOuterVertexGid(u));
  }

  size_t GetTotalNodesNum() const { return vm_ptr_->GetTotalNodesNum(); }
  size_t GetTotalVerticesNum() const { return vm_ptr_->GetTotalNodesNum(); }
  size_t GetTotalVerticesNum(label_id_t label) const {
    return vm_ptr_->GetTotalNodesNum(label);
  }

  template <typename T>
  T GetData(const vertex_t& v, prop_id_t prop_id) const {
    return property_graph_utils::ValueGetter<T>::Value(
        vertex_tables_columns_[vid_parser_.GetLabelId(v.GetValue())][prop_id],
        vid_parser_.GetOffset(v.GetValue()));
  }

  bool HasChild(const vertex_t& v, label_id_t e_label) const {
    return GetLocalOutDegree(v, e_label) != 0;
  }

  bool HasParent(const vertex_t& v, label_id_t e_label) const {
    return GetLocalInDegree(v, e_label) != 0;
  }

  int GetLocalOutDegree(const vertex_t& v, label_id_t e_label) const {
    return GetOutgoingAdjList(v, e_label).Size();
  }

  int GetLocalInDegree(const vertex_t& v, label_id_t e_label) const {
    return GetIncomingAdjList(v, e_label).Size();
  }

  // FIXME: grape message buffer compatibility
  bool Gid2Vertex(const vid_t& gid, vertex_t& v) const {
    return (vid_parser_.GetFid(gid) == fid_) ? InnerVertexGid2Vertex(gid, v)
                                             : OuterVertexGid2Vertex(gid, v);
  }

  vid_t Vertex2Gid(const vertex_t& v) const {
    return IsInnerVertex(v) ? GetInnerVertexGid(v) : GetOuterVertexGid(v);
  }

  inline vid_t GetInnerVerticesNum(label_id_t label_id) const {
    return ivnums_[label_id];
  }

  inline vid_t GetOuterVerticesNum(label_id_t label_id) const {
    return ovnums_[label_id];
  }

  inline bool IsInnerVertex(const vertex_t& v) const {
    return vid_parser_.GetOffset(v.GetValue()) <
           static_cast<int64_t>(ivnums_[vid_parser_.GetLabelId(v.GetValue())]);
  }

  inline bool IsOuterVertex(const vertex_t& v) const {
    vid_t offset = vid_parser_.GetOffset(v.GetValue());
    label_id_t label = vid_parser_.GetLabelId(v.GetValue());
    return offset < tvnums_[label] && offset >= ivnums_[label];
  }

  bool GetInnerVertex(label_id_t label, const oid_t& oid, vertex_t& v) const {
    vid_t gid;
    if (vm_ptr_->GetGid(label, internal_oid_t(oid), gid)) {
      if (vid_parser_.GetFid(gid) == fid_) {
        v.SetValue(vid_parser_.GetLid(gid));
        return true;
      }
    }
    return false;
  }

  bool GetOuterVertex(label_id_t label, const oid_t& oid, vertex_t& v) const {
    vid_t gid;
    if (vm_ptr_->GetGid(label, internal_oid_t(oid), gid)) {
      return OuterVertexGid2Vertex(gid, v);
    }
    return false;
  }

  inline oid_t GetInnerVertexId(const vertex_t& v) const {
    internal_oid_t internal_oid;
    vid_t gid =
        vid_parser_.GenerateId(fid_, vid_parser_.GetLabelId(v.GetValue()),
                               vid_parser_.GetOffset(v.GetValue()));
    CHECK(vm_ptr_->GetOid(gid, internal_oid));
    return oid_t(internal_oid);
  }

  inline oid_t GetOuterVertexId(const vertex_t& v) const {
    vid_t gid = GetOuterVertexGid(v);
    internal_oid_t internal_oid;
    CHECK(vm_ptr_->GetOid(gid, internal_oid));
    return oid_t(internal_oid);
  }

  inline oid_t Gid2Oid(const vid_t& gid) const {
    internal_oid_t internal_oid;
    CHECK(vm_ptr_->GetOid(gid, internal_oid));
    return oid_t(internal_oid);
  }

  inline bool Oid2Gid(label_id_t label, const oid_t& oid, vid_t& gid) const {
    return vm_ptr_->GetGid(label, internal_oid_t(oid), gid);
  }

  inline bool InnerVertexGid2Vertex(const vid_t& gid, vertex_t& v) const {
    v.SetValue(vid_parser_.GetLid(gid));
    return true;
  }

  inline bool OuterVertexGid2Vertex(const vid_t& gid, vertex_t& v) const {
    auto map = ovg2l_maps_ptr_[vid_parser_.GetLabelId(gid)];
    auto iter = map->find(gid);
    if (iter != map->end()) {
      v.SetValue(iter->second);
      return true;
    } else {
      return false;
    }
  }

  inline vid_t GetOuterVertexGid(const vertex_t& v) const {
    label_id_t v_label = vid_parser_.GetLabelId(v.GetValue());
    return ovgid_lists_ptr_[v_label][vid_parser_.GetOffset(v.GetValue()) -
                                     static_cast<int64_t>(ivnums_[v_label])];
  }
  inline vid_t GetInnerVertexGid(const vertex_t& v) const {
    return vid_parser_.GenerateId(fid_, vid_parser_.GetLabelId(v.GetValue()),
                                  vid_parser_.GetOffset(v.GetValue()));
  }

  inline adj_list_t GetIncomingAdjList(const vertex_t& v,
                                       label_id_t e_label) const {
    vid_t vid = v.GetValue();
    label_id_t v_label = vid_parser_.GetLabelId(vid);
    int64_t v_offset = vid_parser_.GetOffset(vid);
    const int64_t* offset_array = ie_offsets_ptr_lists_[v_label][e_label];
    const nbr_unit_t* ie = ie_ptr_lists_[v_label][e_label];
    return adj_list_t(&ie[offset_array[v_offset]],
                      &ie[offset_array[v_offset + 1]],
                      flatten_edge_tables_columns_[e_label]);
  }

  inline raw_adj_list_t GetIncomingRawAdjList(const vertex_t& v,
                                              label_id_t e_label) const {
    vid_t vid = v.GetValue();
    label_id_t v_label = vid_parser_.GetLabelId(vid);
    int64_t v_offset = vid_parser_.GetOffset(vid);
    const int64_t* offset_array = ie_offsets_ptr_lists_[v_label][e_label];
    const nbr_unit_t* ie = ie_ptr_lists_[v_label][e_label];
    return raw_adj_list_t(&ie[offset_array[v_offset]],
                          &ie[offset_array[v_offset + 1]]);
  }

  inline adj_list_t GetOutgoingAdjList(const vertex_t& v,
                                       label_id_t e_label) const {
    vid_t vid = v.GetValue();
    label_id_t v_label = vid_parser_.GetLabelId(vid);
    int64_t v_offset = vid_parser_.GetOffset(vid);
    const int64_t* offset_array = oe_offsets_ptr_lists_[v_label][e_label];
    const nbr_unit_t* oe = oe_ptr_lists_[v_label][e_label];
    return adj_list_t(&oe[offset_array[v_offset]],
                      &oe[offset_array[v_offset + 1]],
                      flatten_edge_tables_columns_[e_label]);
  }

  inline raw_adj_list_t GetOutgoingRawAdjList(const vertex_t& v,
                                              label_id_t e_label) const {
    vid_t vid = v.GetValue();
    label_id_t v_label = vid_parser_.GetLabelId(vid);
    int64_t v_offset = vid_parser_.GetOffset(vid);
    const int64_t* offset_array = oe_offsets_ptr_lists_[v_label][e_label];
    const nbr_unit_t* oe = oe_ptr_lists_[v_label][e_label];
    return raw_adj_list_t(&oe[offset_array[v_offset]],
                          &oe[offset_array[v_offset + 1]]);
  }

  /**
   * N.B.: as an temporary solution, for POC of graph-learn, will be removed
   * later.
   */
  inline std::pair<int64_t, int64_t> GetOutgoingAdjOffsets(
      const vertex_t& v, label_id_t e_label) const {
    vid_t vid = v.GetValue();
    label_id_t v_label = vid_parser_.GetLabelId(vid);
    int64_t v_offset = vid_parser_.GetOffset(vid);
    const int64_t* offset_array = oe_offsets_ptr_lists_[v_label][e_label];
    const nbr_unit_t* oe = oe_ptr_lists_[v_label][e_label];
    return std::make_pair(offset_array[v_offset], offset_array[v_offset + 1]);
  }

  inline grape::DestList IEDests(const vertex_t& v, label_id_t e_label) const {
    int64_t offset = vid_parser_.GetOffset(v.GetValue());
    auto v_label = vertex_label(v);

    return grape::DestList(idoffset_[v_label][e_label][offset],
                           idoffset_[v_label][e_label][offset + 1]);
  }

  inline grape::DestList OEDests(const vertex_t& v, label_id_t e_label) const {
    int64_t offset = vid_parser_.GetOffset(v.GetValue());
    auto v_label = vertex_label(v);

    return grape::DestList(odoffset_[v_label][e_label][offset],
                           odoffset_[v_label][e_label][offset + 1]);
  }

  inline grape::DestList IOEDests(const vertex_t& v, label_id_t e_label) const {
    int64_t offset = vid_parser_.GetOffset(v.GetValue());
    auto v_label = vertex_label(v);

    return grape::DestList(iodoffset_[v_label][e_label][offset],
                           iodoffset_[v_label][e_label][offset + 1]);
  }

  bool directed() const { return directed_; }

  std::shared_ptr<vertex_map_t> GetVertexMap() { return vm_ptr_; }

  const PropertyGraphSchema& schema() const { return schema_; }

  void PrepareToRunApp(grape::MessageStrategy strategy, bool need_split_edges) {
    if (strategy == grape::MessageStrategy::kAlongEdgeToOuterVertex) {
      initDestFidList(true, true, iodst_, iodoffset_);
    } else if (strategy ==
               grape::MessageStrategy::kAlongIncomingEdgeToOuterVertex) {
      initDestFidList(true, false, idst_, idoffset_);
    } else if (strategy ==
               grape::MessageStrategy::kAlongOutgoingEdgeToOuterVertex) {
      initDestFidList(false, true, odst_, odoffset_);
    }
  }

#define ASSIGN_IDENTICAL_VEC_META(prefix, num)                \
  for (label_id_t i = 0; i < num; ++i) {                      \
    std::string _name = generate_name_with_suffix(prefix, i); \
    new_meta.AddMember(_name, old_meta.GetMemberMeta(_name)); \
    nbytes += old_meta.GetMemberMeta(_name).GetNBytes();      \
  }

#define ASSIGN_IDENTICAL_VEC_VEC_META(prefix, x, y)                \
  for (label_id_t i = 0; i < x; ++i) {                             \
    for (label_id_t j = 0; j < y; ++j) {                           \
      std::string _name = generate_name_with_suffix(prefix, i, j); \
      new_meta.AddMember(_name, old_meta.GetMemberMeta(_name));    \
      nbytes += old_meta.GetMemberMeta(_name).GetNBytes();         \
    }                                                              \
  }

#define GENERATE_VEC_META(prefix, vec, label_num, start_num)               \
  do {                                                                     \
    for (label_id_t i = 0; i < label_num; ++i) {                           \
      new_meta.AddMember(generate_name_with_suffix(prefix, i + start_num), \
                         vec[i]->meta());                                  \
      nbytes += vec[i]->nbytes();                                          \
    }                                                                      \
  } while (0);

#define GENERATE_VEC_VEC_META(prefix, vec, v_label_num, e_label_num,          \
                              start_v_num, start_e_num)                       \
  do {                                                                        \
    for (label_id_t i = 0; i < v_label_num; ++i) {                            \
      for (label_id_t j = 0; j < e_label_num; ++j) {                          \
        new_meta.AddMember(generate_name_with_suffix(prefix, i + start_v_num, \
                                                     j + start_e_num),        \
                           vec[i][j]->meta());                                \
        nbytes += vec[i][j]->nbytes();                                        \
      }                                                                       \
    }                                                                         \
  } while (0);

  boost::leaf::result<ObjectID> AddVertices(
      Client& client,
      std::vector<std::shared_ptr<arrow::Table>>&& vertex_tables,
      ObjectID vm_id) {
    int extra_vertex_label_num = vertex_tables.size();
    int total_vertex_label_num = vertex_label_num_ + extra_vertex_label_num;

    auto vm_ptr =
        std::dynamic_pointer_cast<vertex_map_t>(client.GetObject(vm_id));

    std::vector<vid_t> ivnums(total_vertex_label_num);
    std::vector<vid_t> ovnums(total_vertex_label_num);
    std::vector<vid_t> tvnums(total_vertex_label_num);
    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      ivnums[i] = ivnums_[i];
      ovnums[i] = ovnums_[i];
      tvnums[i] = tvnums_[i];
    }
    for (size_t i = 0; i < vertex_tables.size(); ++i) {
#if defined(ARROW_VERSION) && ARROW_VERSION < 17000
      ARROW_OK_OR_RAISE(vertex_tables[i]->CombineChunks(
          arrow::default_memory_pool(), &vertex_tables[i]));
#else
      ARROW_OK_ASSIGN_OR_RAISE(
          vertex_tables[i],
          vertex_tables[i]->CombineChunks(arrow::default_memory_pool()));
#endif
      ivnums[vertex_label_num_ + i] =
          vm_ptr->GetInnerVertexSize(fid_, vertex_label_num_ + i);
      ovnums[vertex_label_num_ + i] = 0;
      tvnums[vertex_label_num_ + i] = ivnums[vertex_label_num_ + i];
    }
    vineyard::ObjectMeta old_meta, new_meta;
    VINEYARD_CHECK_OK(client.GetMetaData(this->id_, old_meta));

    new_meta.SetTypeName(type_name<ArrowFragment<oid_t, vid_t>>());
    new_meta.AddKeyValue("fid", fid_);
    new_meta.AddKeyValue("fnum", fnum_);
    new_meta.AddKeyValue("directed", static_cast<int>(directed_));
    new_meta.AddKeyValue("oid_type", TypeName<oid_t>::Get());
    new_meta.AddKeyValue("vid_type", TypeName<vid_t>::Get());
    new_meta.AddKeyValue("vertex_label_num", total_vertex_label_num);
    new_meta.AddKeyValue("edge_label_num", edge_label_num_);

    size_t nbytes = 0;
    for (int i = 0; i < extra_vertex_label_num; ++i) {
      int cur_label_id = vertex_label_num_ + i;
      std::string table_name =
          generate_name_with_suffix("vertex_tables", cur_label_id);
      TableBuilder vt(client, vertex_tables[i]);
      auto vy_vertex_table =
          std::dynamic_pointer_cast<vineyard::Table>(vt.Seal(client));
      new_meta.AddMember(table_name, vy_vertex_table->meta());
      nbytes += vy_vertex_table->nbytes();
      auto table = vertex_tables[i];
      std::unordered_map<std::string, std::string> kvs;
      table->schema()->metadata()->ToUnorderedMap(&kvs);
      prop_id_t prop_num = table->num_columns();
      std::string label = kvs["label"];
      new_meta.AddKeyValue("vertex_label_name_" + std::to_string(cur_label_id),
                           label);
      new_meta.AddKeyValue(
          "vertex_property_num_" + std::to_string(cur_label_id), prop_num);
      std::string name_prefix =
          "vertex_property_name_" + std::to_string(i) + "_";
      std::string type_prefix =
          "vertex_property_type_" + std::to_string(i) + "_";
      auto& entry = schema_.CreateEntry(label, "VERTEX");
      for (prop_id_t j = 0; j < prop_num; ++j) {
        new_meta.AddKeyValue(name_prefix + std::to_string(j),
                             table->field(j)->name());
        new_meta.AddKeyValue(type_prefix + std::to_string(j),
                             arrow_type_to_string(table->field(j)->type()));
        entry->AddProperty(table->field(j)->name(), table->field(j)->type());
      }
    }
    new_meta.AddKeyValue("schema", schema_.ToJSONString());

    vineyard::ArrayBuilder<vid_t> ivnums_builder(client, ivnums);
    vineyard::ArrayBuilder<vid_t> ovnums_builder(client, ovnums);
    vineyard::ArrayBuilder<vid_t> tvnums_builder(client, tvnums);
    auto vy_ivnums = *std::dynamic_pointer_cast<vineyard::Array<vid_t>>(
        ivnums_builder.Seal(client));
    auto vy_ovnums = *std::dynamic_pointer_cast<vineyard::Array<vid_t>>(
        ovnums_builder.Seal(client));
    auto vy_tvnums = *std::dynamic_pointer_cast<vineyard::Array<vid_t>>(
        tvnums_builder.Seal(client));
    new_meta.AddMember("ivnums", vy_ivnums.meta());
    nbytes += vy_ivnums.nbytes();
    new_meta.AddMember("ovnums", vy_ovnums.meta());
    nbytes += vy_ovnums.nbytes();
    new_meta.AddMember("tvnums", vy_tvnums.meta());
    nbytes += vy_tvnums.nbytes();
    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      std::shared_ptr<arrow::Table> table = this->vertex_tables_[i];
      prop_id_t prop_num = table->num_columns();
      new_meta.AddKeyValue(
          "vertex_label_name_" + std::to_string(i),
          old_meta.GetKeyValue("vertex_label_name_" + std::to_string(i)));
      new_meta.AddKeyValue("vertex_property_num_" + std::to_string(i),
                           std::to_string(prop_num));
      std::string name_prefix =
          "vertex_property_name_" + std::to_string(i) + "_";
      std::string type_prefix =
          "vertex_property_type_" + std::to_string(i) + "_";
      for (prop_id_t j = 0; j < prop_num; ++j) {
        new_meta.AddKeyValue(name_prefix + std::to_string(j),
                             table->field(j)->name());
        new_meta.AddKeyValue(type_prefix + std::to_string(j),
                             arrow_type_to_string(table->field(j)->type()));
      }
    }
    for (label_id_t i = 0; i < edge_label_num_; ++i) {
      std::shared_ptr<arrow::Table> table = this->edge_tables_[i];
      prop_id_t prop_num = table->num_columns();
      new_meta.AddKeyValue(
          "edge_label_name_" + std::to_string(i),
          old_meta.GetKeyValue("edge_label_name_" + std::to_string(i)));
      new_meta.AddKeyValue("edge_property_num_" + std::to_string(i),
                           std::to_string(prop_num));
      std::string name_prefix = "edge_property_name_" + std::to_string(i) + "_";
      std::string type_prefix = "edge_property_type_" + std::to_string(i) + "_";
      for (prop_id_t j = 0; j < prop_num; ++j) {
        new_meta.AddKeyValue(name_prefix + std::to_string(j),
                             table->field(j)->name());
        new_meta.AddKeyValue(type_prefix + std::to_string(j),
                             arrow_type_to_string(table->field(j)->type()));
      }
    }

    ASSIGN_IDENTICAL_VEC_META("vertex_tables", vertex_label_num_)
    ASSIGN_IDENTICAL_VEC_META("edge_tables", edge_label_num_)

    ASSIGN_IDENTICAL_VEC_META("ovgid_lists", vertex_label_num_)
    ASSIGN_IDENTICAL_VEC_META("ovg2l_maps", vertex_label_num_)

    if (directed_) {
      ASSIGN_IDENTICAL_VEC_VEC_META("ie_lists", vertex_label_num_,
                                    edge_label_num_);
      ASSIGN_IDENTICAL_VEC_VEC_META("ie_offsets_lists", vertex_label_num_,
                                    edge_label_num_);
    }
    ASSIGN_IDENTICAL_VEC_VEC_META("oe_lists", vertex_label_num_,
                                  edge_label_num_);
    ASSIGN_IDENTICAL_VEC_VEC_META("oe_offsets_lists", vertex_label_num_,
                                  edge_label_num_);
    // Assign additional meta for new vertex labels
    std::vector<std::vector<std::shared_ptr<vineyard::FixedSizeBinaryArray>>>
        vy_ie_lists, vy_oe_lists;
    std::vector<std::vector<std::shared_ptr<vineyard::NumericArray<int64_t>>>>
        vy_ie_offsets_lists, vy_oe_offsets_lists;

    for (int i = 0; i < extra_vertex_label_num; ++i) {
      vid_builder_t builder;
      std::shared_ptr<vid_array_t> array;
      builder.Finish(&array);
      vineyard::NumericArrayBuilder<vid_t> ovgid_list_builder(client, array);
      auto ovgid_list =
          std::dynamic_pointer_cast<vineyard::NumericArray<vid_t>>(
              ovgid_list_builder.Seal(client));
      new_meta.AddMember(
          generate_name_with_suffix("ovgid_lists", vertex_label_num_ + i),
          ovgid_list->meta());
      nbytes += ovgid_list->nbytes();

      vineyard::HashmapBuilder<vid_t, vid_t> ovg2l_builder(client);
      auto ovg2l = std::dynamic_pointer_cast<vineyard::Hashmap<vid_t, vid_t>>(
          ovg2l_builder.Seal(client));
      new_meta.AddMember(
          generate_name_with_suffix("ovg2l_maps", vertex_label_num_ + i),
          ovg2l->meta());
      nbytes += ovg2l->nbytes();
    }

    if (directed_) {
      vy_ie_lists.resize(extra_vertex_label_num);
      vy_ie_offsets_lists.resize(extra_vertex_label_num);
    }
    vy_oe_lists.resize(extra_vertex_label_num);
    vy_oe_offsets_lists.resize(extra_vertex_label_num);

    for (label_id_t i = 0; i < extra_vertex_label_num; ++i) {
      if (directed_) {
        vy_ie_lists[i].resize(edge_label_num_);
        vy_ie_offsets_lists[i].resize(edge_label_num_);
      }
      vy_oe_lists[i].resize(edge_label_num_);
      vy_oe_offsets_lists[i].resize(edge_label_num_);
    }

    for (label_id_t i = 0; i < extra_vertex_label_num; ++i) {
      for (label_id_t j = 0; j < edge_label_num_; ++j) {
        if (directed_) {
          PodArrayBuilder<nbr_unit_t> binary_builder;
          std::shared_ptr<arrow::FixedSizeBinaryArray> ie_array;
          binary_builder.Finish(&ie_array);
          vineyard::FixedSizeBinaryArrayBuilder ie_builder(client, ie_array);

          auto ie = std::dynamic_pointer_cast<vineyard::FixedSizeBinaryArray>(
              ie_builder.Seal(client));
          new_meta.AddMember(
              generate_name_with_suffix("ie_lists", vertex_label_num_ + i, j),
              ie->meta());
          nbytes += ie->nbytes();

          arrow::Int64Builder int64_builder;
          std::shared_ptr<arrow::Int64Array> ie_offset_array;
          int64_builder.Finish(&ie_offset_array);
          vineyard::NumericArrayBuilder<int64_t> ie_offset_builder(
              client, ie_offset_array);
          auto ie_offset =
              std::dynamic_pointer_cast<vineyard::NumericArray<int64_t>>(
                  ie_offset_builder.Seal(client));
          new_meta.AddMember(generate_name_with_suffix(
                                 "ie_offsets_lists", vertex_label_num_ + i, j),
                             ie_offset->meta());
          nbytes += ie_offset->nbytes();
        }
        PodArrayBuilder<nbr_unit_t> binary_builder;
        std::shared_ptr<arrow::FixedSizeBinaryArray> oe_array;
        binary_builder.Finish(&oe_array);
        vineyard::FixedSizeBinaryArrayBuilder oe_builder(client, oe_array);

        auto oe = std::dynamic_pointer_cast<vineyard::FixedSizeBinaryArray>(
            oe_builder.Seal(client));
        new_meta.AddMember(
            generate_name_with_suffix("oe_lists", vertex_label_num_ + i, j),
            oe->meta());
        nbytes += oe->nbytes();

        arrow::Int64Builder int64_builder;
        std::shared_ptr<arrow::Int64Array> oe_offset_array;
        int64_builder.Finish(&oe_offset_array);
        vineyard::NumericArrayBuilder<int64_t> oe_offset_builder(
            client, oe_offset_array);
        auto oe_offset =
            std::dynamic_pointer_cast<vineyard::NumericArray<int64_t>>(
                oe_offset_builder.Seal(client));
        new_meta.AddMember(generate_name_with_suffix("oe_offsets_lists",
                                                     vertex_label_num_ + i, j),
                           oe_offset->meta());
        nbytes += oe_offset->nbytes();
      }
    }

    new_meta.AddMember("vertex_map", vm_ptr->meta());

    new_meta.SetNBytes(nbytes);

    vineyard::ObjectID ret;
    VINEYARD_CHECK_OK(client.CreateMetaData(new_meta, ret));
    return ret;
  }

  boost::leaf::result<ObjectID> AddEdges(
      Client& client, std::vector<std::shared_ptr<arrow::Table>>&& edge_tables,
      const std::vector<std::set<std::pair<std::string, std::string>>>&
          edge_relations,
      int concurrency) {
    int extra_edge_label_num = edge_tables.size();
    int total_edge_label_num = edge_label_num_ + extra_edge_label_num;

    // Newly constructed data structures
    vineyard::Array<vid_t> vy_ovnums, vy_tvnums;
    std::vector<std::shared_ptr<vineyard::NumericArray<vid_t>>> vy_ovgid_lists;
    std::vector<std::shared_ptr<vineyard::Hashmap<vid_t, vid_t>>> vy_ovg2l_maps;
    std::vector<std::shared_ptr<vineyard::Table>> vy_edge_tables;

    std::vector<std::vector<std::shared_ptr<vineyard::FixedSizeBinaryArray>>>
        vy_ie_lists, vy_oe_lists;
    std::vector<std::vector<std::shared_ptr<vineyard::NumericArray<int64_t>>>>
        vy_ie_offsets_lists, vy_oe_offsets_lists;

    // Init size
    vy_ovgid_lists.resize(vertex_label_num_);
    vy_ovg2l_maps.resize(vertex_label_num_);
    vy_edge_tables.resize(extra_edge_label_num);

    if (directed_) {
      vy_ie_lists.resize(vertex_label_num_);
      vy_ie_offsets_lists.resize(vertex_label_num_);
    }
    vy_oe_lists.resize(vertex_label_num_);
    vy_oe_offsets_lists.resize(vertex_label_num_);

    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      if (directed_) {
        vy_ie_lists[i].resize(extra_edge_label_num);
        vy_ie_offsets_lists[i].resize(extra_edge_label_num);
      }
      vy_oe_lists[i].resize(extra_edge_label_num);
      vy_oe_offsets_lists[i].resize(extra_edge_label_num);
    }

    // Collect extra outer vertices.
    auto collect_extra_outer_vertices =
        [this](const std::shared_ptr<vid_array_t>& gid_array,
               std::vector<std::vector<vid_t>>& extra_ovgids) {
          const VID_T* arr = gid_array->raw_values();
          for (int64_t i = 0; i < gid_array->length(); ++i) {
            fid_t fid = vid_parser_.GetFid(arr[i]);
            label_id_t label_id = vid_parser_.GetLabelId(arr[i]);
            auto cur_map = ovg2l_maps_ptr_[label_id];
            if (fid != fid_ && cur_map->find(arr[i]) == cur_map->end()) {
              extra_ovgids[vid_parser_.GetLabelId(arr[i])].push_back(arr[i]);
            }
          }
        };

    std::vector<std::vector<vid_t>> extra_ovgids(vertex_label_num_);
    for (int i = 0; i < extra_edge_label_num; ++i) {
#if defined(ARROW_VERSION) && ARROW_VERSION < 17000
      ARROW_OK_OR_RAISE(edge_tables[i]->CombineChunks(
          arrow::default_memory_pool(), &edge_tables[i]));
#else
      ARROW_OK_ASSIGN_OR_RAISE(
          edge_tables[i],
          edge_tables[i]->CombineChunks(arrow::default_memory_pool()));
#endif

      collect_extra_outer_vertices(
          std::dynamic_pointer_cast<
              typename vineyard::ConvertToArrowType<vid_t>::ArrayType>(
              edge_tables[i]->column(0)->chunk(0)),
          extra_ovgids);
      collect_extra_outer_vertices(
          std::dynamic_pointer_cast<
              typename vineyard::ConvertToArrowType<vid_t>::ArrayType>(
              edge_tables[i]->column(1)->chunk(0)),
          extra_ovgids);
    }

    // Construct the new start value of lid of extra outer vertices
    std::vector<vid_t> start_ids(vertex_label_num_);
    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      start_ids[i] = vid_parser_.GenerateId(0, i, ivnums_[i]) + ovnums_[i];
    }

    // Make a copy of ovg2l map, since we need to add some extra outer vertices
    // pulled in this fragment by new edges.
    std::vector<ska::flat_hash_map<vid_t, vid_t>> ovg2l_maps(vertex_label_num_);
    for (int i = 0; i < vertex_label_num_; ++i) {
      for (auto iter = ovg2l_maps_ptr_[i]->begin();
           iter != ovg2l_maps_ptr_[i]->end(); ++iter) {
        ovg2l_maps[i].emplace(iter->first, iter->second);
      }
    }

    std::vector<std::shared_ptr<vid_array_t>> extra_ovgid_lists(
        vertex_label_num_);
    // Add extra outer vertices to ovg2l map, and collect distinct gid of extra
    // outer vertices.
    generate_outer_vertices_map(extra_ovgids, start_ids, vertex_label_num_,
                                ovg2l_maps, extra_ovgid_lists);
    extra_ovgids.clear();

    std::vector<std::shared_ptr<vid_array_t>> ovgid_lists(vertex_label_num_);
    // Append extra ovgid_lists with origin ovgid_lists to make it complete
    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      vid_builder_t ovgid_list_builder;
      ovgid_list_builder.AppendValues(ovgid_lists_[i]->raw_values(),
                                      ovgid_lists_[i]->length());
      ovgid_list_builder.AppendValues(extra_ovgid_lists[i]->raw_values(),
                                      extra_ovgid_lists[i]->length());
      ovgid_list_builder.Finish(&ovgid_lists[i]);
    }
    std::vector<vid_t> ovnums(vertex_label_num_), tvnums(vertex_label_num_);
    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      ovnums[i] = ovgid_lists[i]->length();
      tvnums[i] = ivnums_[i] + ovnums[i];
    }

    // Gather all local id of new edges.
    // And delete the src/dst column in edge tables.
    std::vector<std::shared_ptr<vid_array_t>> edge_src, edge_dst;
    edge_src.resize(extra_edge_label_num);
    edge_dst.resize(extra_edge_label_num);
    for (int i = 0; i < extra_edge_label_num; ++i) {
      generate_local_id_list(vid_parser_,
                             std::dynamic_pointer_cast<vid_array_t>(
                                 edge_tables[i]->column(0)->chunk(0)),
                             fid_, ovg2l_maps, concurrency, edge_src[i]);
      generate_local_id_list(vid_parser_,
                             std::dynamic_pointer_cast<vid_array_t>(
                                 edge_tables[i]->column(1)->chunk(0)),
                             fid_, ovg2l_maps, concurrency, edge_dst[i]);
      std::shared_ptr<arrow::Table> tmp_table0;
#if defined(ARROW_VERSION) && ARROW_VERSION < 17000
      ARROW_OK_OR_RAISE(edge_tables[i]->RemoveColumn(0, &tmp_table0));
      ARROW_OK_OR_RAISE(tmp_table0->RemoveColumn(0, &edge_tables[i]));
#else
      ARROW_OK_ASSIGN_OR_RAISE(tmp_table0, edge_tables[i]->RemoveColumn(0));
      ARROW_OK_ASSIGN_OR_RAISE(edge_tables[i], tmp_table0->RemoveColumn(0));
#endif
    }

    // Generate CSR vector of new edge tables.

    std::vector<std::vector<std::shared_ptr<arrow::FixedSizeBinaryArray>>>
        ie_lists(vertex_label_num_);
    std::vector<std::vector<std::shared_ptr<arrow::FixedSizeBinaryArray>>>
        oe_lists(vertex_label_num_);
    std::vector<std::vector<std::shared_ptr<arrow::Int64Array>>>
        ie_offsets_lists(vertex_label_num_);
    std::vector<std::vector<std::shared_ptr<arrow::Int64Array>>>
        oe_offsets_lists(vertex_label_num_);

    for (label_id_t v_label = 0; v_label < vertex_label_num_; ++v_label) {
      oe_lists[v_label].resize(extra_edge_label_num);
      oe_offsets_lists[v_label].resize(extra_edge_label_num);
      if (directed_) {
        ie_lists[v_label].resize(extra_edge_label_num);
        ie_offsets_lists[v_label].resize(extra_edge_label_num);
      }
    }

    for (label_id_t e_label = 0; e_label < extra_edge_label_num; ++e_label) {
      std::vector<std::shared_ptr<arrow::FixedSizeBinaryArray>> sub_ie_lists(
          vertex_label_num_);
      std::vector<std::shared_ptr<arrow::FixedSizeBinaryArray>> sub_oe_lists(
          vertex_label_num_);
      std::vector<std::shared_ptr<arrow::Int64Array>> sub_ie_offset_lists(
          vertex_label_num_);
      std::vector<std::shared_ptr<arrow::Int64Array>> sub_oe_offset_lists(
          vertex_label_num_);
      if (directed_) {
        generate_directed_csr<vid_t, eid_t>(
            vid_parser_, edge_src[e_label], edge_dst[e_label], tvnums,
            vertex_label_num_, concurrency, sub_oe_lists, sub_oe_offset_lists);
        generate_directed_csr<vid_t, eid_t>(
            vid_parser_, edge_dst[e_label], edge_src[e_label], tvnums,
            vertex_label_num_, concurrency, sub_ie_lists, sub_ie_offset_lists);
      } else {
        generate_undirected_csr<vid_t, eid_t>(
            vid_parser_, edge_src[e_label], edge_dst[e_label], tvnums,
            vertex_label_num_, concurrency, sub_oe_lists, sub_oe_offset_lists);
      }

      for (label_id_t v_label = 0; v_label < vertex_label_num_; ++v_label) {
        if (directed_) {
          ie_lists[v_label][e_label] = sub_ie_lists[v_label];
          ie_offsets_lists[v_label][e_label] = sub_ie_offset_lists[v_label];
        }
        oe_lists[v_label][e_label] = sub_oe_lists[v_label];
        oe_offsets_lists[v_label][e_label] = sub_oe_offset_lists[v_label];
      }
    }

    vineyard::ObjectMeta old_meta, new_meta;
    VINEYARD_CHECK_OK(client.GetMetaData(this->id_, old_meta));

    new_meta.SetTypeName(type_name<ArrowFragment<oid_t, vid_t>>());
    new_meta.AddKeyValue("fid", fid_);
    new_meta.AddKeyValue("fnum", fnum_);
    new_meta.AddKeyValue("directed", static_cast<int>(directed_));
    new_meta.AddKeyValue("oid_type", TypeName<oid_t>::Get());
    new_meta.AddKeyValue("vid_type", TypeName<vid_t>::Get());
    new_meta.AddKeyValue("vertex_label_num", vertex_label_num_);
    // Increase edge label num
    new_meta.AddKeyValue("edge_label_num", total_edge_label_num);

    size_t nbytes = 0;
    for (label_id_t i = 0; i < total_edge_label_num; ++i) {
      if (i >= edge_label_num_) {
        vineyard::TableBuilder et(client, edge_tables[i - edge_label_num_]);
        vy_edge_tables[i - edge_label_num_] =
            std::dynamic_pointer_cast<vineyard::Table>(et.Seal(client));

        std::unordered_map<std::string, std::string> kvs;
        auto table = edge_tables[i - edge_label_num_];
        table->schema()->metadata()->ToUnorderedMap(&kvs);
        new_meta.AddKeyValue("edge_label_name_" + std::to_string(i),
                             kvs["label"]);
        new_meta.AddKeyValue("edge_property_num_" + std::to_string(i),
                             std::to_string(table->num_columns()));
        std::string name_prefix =
            "edge_property_name_" + std::to_string(i) + "_";
        std::string type_prefix =
            "edge_property_type_" + std::to_string(i) + "_";
        auto entry = schema_.CreateEntry(kvs["label"], "EDGE");
        for (prop_id_t j = 0; j < table->num_columns(); ++j) {
          new_meta.AddKeyValue(name_prefix + std::to_string(j),
                               table->field(j)->name());
          new_meta.AddKeyValue(type_prefix + std::to_string(j),
                               arrow_type_to_string(table->field(j)->type()));
          entry->AddProperty(table->field(j)->name(), table->field(j)->type());
        }
        for (const auto& rel : edge_relations[i - edge_label_num_]) {
          entry->AddRelation(rel.first, rel.second);
        }
      } else {
        std::string table_name = generate_name_with_suffix("edge_tables", i);
        auto table_meta = old_meta.GetMemberMeta(table_name);
        new_meta.AddMember(table_name, table_meta);
        nbytes += table_meta.GetNBytes();
        auto table = edge_tables_[i];
        prop_id_t prop_num = table->num_columns();
        new_meta.AddKeyValue(
            "edge_label_name_" + std::to_string(i),
            old_meta.GetKeyValue("edge_label_name_" + std::to_string(i)));
        new_meta.AddKeyValue("edge_property_num_" + std::to_string(i),
                             std::to_string(prop_num));
        std::string name_prefix =
            "edge_property_name_" + std::to_string(i) + "_";
        std::string type_prefix =
            "edge_property_type_" + std::to_string(i) + "_";
        for (prop_id_t j = 0; j < prop_num; ++j) {
          new_meta.AddKeyValue(name_prefix + std::to_string(j),
                               table->field(j)->name());
          new_meta.AddKeyValue(type_prefix + std::to_string(j),
                               arrow_type_to_string(table->field(j)->type()));
        }
      }
    }
    new_meta.AddKeyValue("schema", schema_.ToJSONString());

    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      std::shared_ptr<arrow::Table> table = this->vertex_tables_[i];
      prop_id_t prop_num = table->num_columns();
      new_meta.AddKeyValue(
          "vertex_label_name_" + std::to_string(i),
          old_meta.GetKeyValue("vertex_label_name_" + std::to_string(i)));
      new_meta.AddKeyValue("vertex_property_num_" + std::to_string(i),
                           std::to_string(prop_num));
      std::string name_prefix =
          "vertex_property_name_" + std::to_string(i) + "_";
      std::string type_prefix =
          "vertex_property_type_" + std::to_string(i) + "_";
      for (prop_id_t j = 0; j < prop_num; ++j) {
        new_meta.AddKeyValue(name_prefix + std::to_string(j),
                             table->field(j)->name());
        new_meta.AddKeyValue(type_prefix + std::to_string(j),
                             arrow_type_to_string(table->field(j)->type()));
      }
    }

    new_meta.AddMember("ivnums", old_meta.GetMemberMeta("ivnums"));
    nbytes += old_meta.GetMemberMeta("ivnums").GetNBytes();

    ThreadGroup tg;
    {
      auto fn = [this, &vy_ovnums, &vy_tvnums, &ovnums,
                 &tvnums](Client& client) {
        vineyard::ArrayBuilder<vid_t> ovnums_builder(client, ovnums);
        vineyard::ArrayBuilder<vid_t> tvnums_builder(client, tvnums);
        vy_ovnums = *std::dynamic_pointer_cast<vineyard::Array<vid_t>>(
            ovnums_builder.Seal(client));
        vy_tvnums = *std::dynamic_pointer_cast<vineyard::Array<vid_t>>(
            tvnums_builder.Seal(client));
        return Status::OK();
      };
      tg.AddTask(fn, std::ref(client));
    }

    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      auto fn = [this, i, &vy_ovgid_lists, &vy_ovg2l_maps, &ovgid_lists,
                 &ovg2l_maps](Client& client) {
        vineyard::NumericArrayBuilder<vid_t> ovgid_list_builder(client,
                                                                ovgid_lists[i]);
        vy_ovgid_lists[i] =
            std::dynamic_pointer_cast<vineyard::NumericArray<vid_t>>(
                ovgid_list_builder.Seal(client));

        vineyard::HashmapBuilder<vid_t, vid_t> ovg2l_builder(
            client, std::move(ovg2l_maps[i]));
        vy_ovg2l_maps[i] =
            std::dynamic_pointer_cast<vineyard::Hashmap<vid_t, vid_t>>(
                ovg2l_builder.Seal(client));
        return Status::OK();
      };
      tg.AddTask(fn, std::ref(client));
    }

    ASSIGN_IDENTICAL_VEC_META("vertex_tables", vertex_label_num_)

    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      std::shared_ptr<arrow::Table> table = vertex_tables_[i];
      prop_id_t prop_num = table->num_columns();
      new_meta.AddKeyValue(
          "vertex_label_name_" + std::to_string(i),
          old_meta.GetKeyValue("vertex_label_name_" + std::to_string(i)));
      new_meta.AddKeyValue("vertex_property_num_" + std::to_string(i),
                           std::to_string(prop_num));
      std::string name_prefix =
          "vertex_property_name_" + std::to_string(i) + "_";
      std::string type_prefix =
          "vertex_property_type_" + std::to_string(i) + "_";
      for (prop_id_t j = 0; j < prop_num; ++j) {
        new_meta.AddKeyValue(name_prefix + std::to_string(j),
                             table->field(j)->name());
        new_meta.AddKeyValue(type_prefix + std::to_string(j),
                             arrow_type_to_string(table->field(j)->type()));
      }
    }

    if (directed_) {
      ASSIGN_IDENTICAL_VEC_VEC_META("ie_lists", vertex_label_num_,
                                    edge_label_num_)
      ASSIGN_IDENTICAL_VEC_VEC_META("ie_offsets_lists", vertex_label_num_,
                                    edge_label_num_)
    }
    ASSIGN_IDENTICAL_VEC_VEC_META("oe_lists", vertex_label_num_,
                                  edge_label_num_)
    ASSIGN_IDENTICAL_VEC_VEC_META("oe_offsets_lists", vertex_label_num_,
                                  edge_label_num_);

    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      for (label_id_t j = 0; j < extra_edge_label_num; ++j) {
        auto fn = [this, i, j, &vy_ie_lists, &vy_oe_lists, &vy_ie_offsets_lists,
                   &vy_oe_offsets_lists, &ie_lists, &oe_lists,
                   &ie_offsets_lists, &oe_offsets_lists](Client& client) {
          if (directed_) {
            vineyard::FixedSizeBinaryArrayBuilder ie_builder(client,
                                                             ie_lists[i][j]);
            vy_ie_lists[i][j] =
                std::dynamic_pointer_cast<vineyard::FixedSizeBinaryArray>(
                    ie_builder.Seal(client));

            vineyard::NumericArrayBuilder<int64_t> ieo_builder(
                client, ie_offsets_lists[i][j]);
            vy_ie_offsets_lists[i][j] =
                std::dynamic_pointer_cast<vineyard::NumericArray<int64_t>>(
                    ieo_builder.Seal(client));
          }
          vineyard::FixedSizeBinaryArrayBuilder oe_builder(client,
                                                           oe_lists[i][j]);
          vy_oe_lists[i][j] =
              std::dynamic_pointer_cast<vineyard::FixedSizeBinaryArray>(
                  oe_builder.Seal(client));

          vineyard::NumericArrayBuilder<int64_t> oeo_builder(
              client, oe_offsets_lists[i][j]);
          vy_oe_offsets_lists[i][j] =
              std::dynamic_pointer_cast<vineyard::NumericArray<int64_t>>(
                  oeo_builder.Seal(client));
          return Status::OK();
        };
        tg.AddTask(fn, std::ref(client));
      }
    }
    tg.TakeResults();

    new_meta.AddMember("ovnums", vy_ovnums.meta());
    nbytes += vy_ovnums.meta().GetNBytes();
    new_meta.AddMember("tvnums", vy_tvnums.meta());
    nbytes += vy_tvnums.meta().GetNBytes();

    GENERATE_VEC_META("edge_tables", vy_edge_tables, extra_edge_label_num,
                      edge_label_num_)
    GENERATE_VEC_META("ovgid_lists", vy_ovgid_lists, vertex_label_num_, 0)
    GENERATE_VEC_META("ovg2l_maps", vy_ovg2l_maps, vertex_label_num_, 0)
    if (directed_) {
      GENERATE_VEC_VEC_META("ie_lists", vy_ie_lists, vertex_label_num_,
                            extra_edge_label_num, 0, edge_label_num_)
      GENERATE_VEC_VEC_META("ie_offsets_lists", vy_ie_offsets_lists,
                            vertex_label_num_, extra_edge_label_num, 0,
                            edge_label_num_)
    }
    GENERATE_VEC_VEC_META("oe_lists", vy_oe_lists, vertex_label_num_,
                          extra_edge_label_num, 0, edge_label_num_);
    GENERATE_VEC_VEC_META("oe_offsets_lists", vy_oe_offsets_lists,
                          vertex_label_num_, extra_edge_label_num, 0,
                          edge_label_num_);

    new_meta.AddMember("vertex_map", old_meta.GetMemberMeta("vertex_map"));

    new_meta.SetNBytes(nbytes);

    vineyard::ObjectID ret;
    VINEYARD_CHECK_OK(client.CreateMetaData(new_meta, ret));
    return ret;
  }

  vineyard::ObjectID AddVertexColumns(
      vineyard::Client& client,
      const std::map<
          label_id_t,
          std::vector<std::pair<std::string, std::shared_ptr<arrow::Array>>>>
          columns) override {
    vineyard::ObjectMeta old_meta, new_meta;
    VINEYARD_CHECK_OK(client.GetMetaData(this->id_, old_meta));

    new_meta.SetTypeName(type_name<ArrowFragment<oid_t, vid_t>>());
    new_meta.AddKeyValue("fid", fid_);
    new_meta.AddKeyValue("fnum", fnum_);
    new_meta.AddKeyValue("directed", static_cast<int>(directed_));
    new_meta.AddKeyValue("oid_type", TypeName<oid_t>::Get());
    new_meta.AddKeyValue("vid_type", TypeName<vid_t>::Get());
    new_meta.AddKeyValue("vertex_label_num", vertex_label_num_);
    new_meta.AddKeyValue("edge_label_num", edge_label_num_);

    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      std::string table_name = generate_name_with_suffix("vertex_tables", i);
      if (columns.find(i) != columns.end()) {
        std::shared_ptr<vineyard::Table> old_table =
            std::make_shared<vineyard::Table>();
        old_table->Construct(old_meta.GetMemberMeta(table_name));
        prop_id_t old_prop_num = old_table->num_columns();
        vineyard::TableExtender extender(client, old_table);
        auto& vec = columns.at(i);
        for (auto& pair : vec) {
          auto status = extender.AddColumn(client, pair.first, pair.second);
          CHECK(status.ok());
        }
        std::shared_ptr<vineyard::Table> new_table =
            std::dynamic_pointer_cast<vineyard::Table>(extender.Seal(client));
        new_meta.AddMember(table_name, new_table->meta());
        std::shared_ptr<arrow::Table> arrow_table = new_table->GetTable();
        prop_id_t prop_num = arrow_table->num_columns();
        std::string label =
            old_meta.GetKeyValue("vertex_label_name_" + std::to_string(i));
        new_meta.AddKeyValue("vertex_label_name_" + std::to_string(i), label);
        new_meta.AddKeyValue("vertex_property_num_" + std::to_string(i),
                             prop_num);
        std::string name_prefix =
            "vertex_property_name_" + std::to_string(i) + "_";
        std::string type_prefix =
            "vertex_property_type_" + std::to_string(i) + "_";
        auto& entry = schema_.GetMutableEntry(label, "VERTEX");
        for (prop_id_t j = 0; j < prop_num; ++j) {
          new_meta.AddKeyValue(name_prefix + std::to_string(j),
                               arrow_table->field(j)->name());
          new_meta.AddKeyValue(
              type_prefix + std::to_string(j),
              arrow_type_to_string(arrow_table->field(j)->type()));
          if (j >= old_prop_num) {
            entry.AddProperty(arrow_table->field(j)->name(),
                              arrow_table->field(j)->type());
          }
        }
      } else {
        new_meta.AddMember(table_name, old_meta.GetMemberMeta(table_name));

        std::shared_ptr<arrow::Table> table = this->vertex_tables_[i];
        prop_id_t prop_num = table->num_columns();
        new_meta.AddKeyValue(
            "vertex_label_name_" + std::to_string(i),
            old_meta.GetKeyValue("vertex_label_name_" + std::to_string(i)));
        new_meta.AddKeyValue("vertex_property_num_" + std::to_string(i),
                             std::to_string(prop_num));
        std::string name_prefix =
            "vertex_property_name_" + std::to_string(i) + "_";
        std::string type_prefix =
            "vertex_property_type_" + std::to_string(i) + "_";
        for (prop_id_t j = 0; j < prop_num; ++j) {
          new_meta.AddKeyValue(name_prefix + std::to_string(j),
                               table->field(j)->name());
          new_meta.AddKeyValue(type_prefix + std::to_string(j),
                               arrow_type_to_string(table->field(j)->type()));
        }
      }
    }
    new_meta.AddKeyValue("schema", schema_.ToJSONString());

    size_t nbytes = 0;
    new_meta.AddMember("ivnums", old_meta.GetMemberMeta("ivnums"));
    nbytes += old_meta.GetMemberMeta("ivnums").GetNBytes();
    new_meta.AddMember("ovnums", old_meta.GetMemberMeta("ovnums"));
    nbytes += old_meta.GetMemberMeta("ovnums").GetNBytes();
    new_meta.AddMember("tvnums", old_meta.GetMemberMeta("tvnums"));
    nbytes += old_meta.GetMemberMeta("tvnums").GetNBytes();

    ASSIGN_IDENTICAL_VEC_META("ovgid_lists", vertex_label_num_);
    ASSIGN_IDENTICAL_VEC_META("ovg2l_maps", vertex_label_num_);
    ASSIGN_IDENTICAL_VEC_META("edge_tables", edge_label_num_);

    for (label_id_t i = 0; i < edge_label_num_; ++i) {
      std::shared_ptr<arrow::Table> table = this->edge_tables_[i];
      prop_id_t prop_num = table->num_columns();
      new_meta.AddKeyValue(
          "edge_label_name_" + std::to_string(i),
          old_meta.GetKeyValue("edge_label_name_" + std::to_string(i)));
      new_meta.AddKeyValue("edge_property_num_" + std::to_string(i),
                           std::to_string(prop_num));
      std::string name_prefix = "edge_property_name_" + std::to_string(i) + "_";
      std::string type_prefix = "edge_property_type_" + std::to_string(i) + "_";
      for (prop_id_t j = 0; j < prop_num; ++j) {
        new_meta.AddKeyValue(name_prefix + std::to_string(j),
                             table->field(j)->name());
        new_meta.AddKeyValue(type_prefix + std::to_string(j),
                             arrow_type_to_string(table->field(j)->type()));
      }
    }

    if (directed_) {
      ASSIGN_IDENTICAL_VEC_VEC_META("ie_lists", vertex_label_num_,
                                    edge_label_num_);
      ASSIGN_IDENTICAL_VEC_VEC_META("ie_offsets_lists", vertex_label_num_,
                                    edge_label_num_);
    }
    ASSIGN_IDENTICAL_VEC_VEC_META("oe_lists", vertex_label_num_,
                                  edge_label_num_);
    ASSIGN_IDENTICAL_VEC_VEC_META("oe_offsets_lists", vertex_label_num_,
                                  edge_label_num_);

    new_meta.AddMember("vertex_map", old_meta.GetMemberMeta("vertex_map"));

    new_meta.SetNBytes(nbytes);

    vineyard::ObjectID ret;
    VINEYARD_CHECK_OK(client.CreateMetaData(new_meta, ret));
    return ret;
  }

#undef ASSIGN_IDENTICAL_VEC_META
#undef ASSIGN_IDENTICAL_VEC_VEC_META
#undef GENERATE_VEC_META
#undef GENERATE_VEC_VEC_META

 private:
  void initPointers() {
    edge_tables_columns_.resize(edge_label_num_);
    flatten_edge_tables_columns_.resize(edge_label_num_);
    for (label_id_t i = 0; i < edge_label_num_; ++i) {
      prop_id_t prop_num =
          static_cast<prop_id_t>(edge_tables_[i]->num_columns());
      edge_tables_columns_[i].resize(prop_num);
      if (edge_tables_[i]->num_rows() == 0) {
        continue;
      }
      for (prop_id_t j = 0; j < prop_num; ++j) {
        edge_tables_columns_[i][j] =
            get_arrow_array_ptr(edge_tables_[i]->column(j)->chunk(0));
      }
      flatten_edge_tables_columns_[i] = &edge_tables_columns_[i][0];
    }

    vertex_tables_columns_.resize(vertex_label_num_);
    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      prop_id_t prop_num =
          static_cast<prop_id_t>(vertex_tables_[i]->num_columns());
      vertex_tables_columns_[i].resize(prop_num);
      if (vertex_tables_[i]->num_rows() == 0) {
        continue;
      }
      for (prop_id_t j = 0; j < prop_num; ++j) {
        vertex_tables_columns_[i][j] =
            get_arrow_array_ptr(vertex_tables_[i]->column(j)->chunk(0));
      }
    }

    oe_ptr_lists_.resize(vertex_label_num_);
    oe_offsets_ptr_lists_.resize(vertex_label_num_);

    idst_.resize(vertex_label_num_);
    odst_.resize(vertex_label_num_);
    iodst_.resize(vertex_label_num_);

    idoffset_.resize(vertex_label_num_);
    odoffset_.resize(vertex_label_num_);
    iodoffset_.resize(vertex_label_num_);

    ovgid_lists_ptr_.resize(vertex_label_num_);
    ovg2l_maps_ptr_.resize(vertex_label_num_);
    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      ovgid_lists_ptr_[i] = ovgid_lists_[i]->raw_values();
      ovg2l_maps_ptr_[i] = ovg2l_maps_[i].get();

      oe_ptr_lists_[i].resize(edge_label_num_);
      oe_offsets_ptr_lists_[i].resize(edge_label_num_);

      idst_[i].resize(edge_label_num_);
      odst_[i].resize(edge_label_num_);
      iodst_[i].resize(edge_label_num_);

      idoffset_[i].resize(edge_label_num_);
      odoffset_[i].resize(edge_label_num_);
      iodoffset_[i].resize(edge_label_num_);

      for (label_id_t j = 0; j < edge_label_num_; ++j) {
        oe_ptr_lists_[i][j] =
            reinterpret_cast<const nbr_unit_t*>(oe_lists_[i][j]->GetValue(0));
        oe_offsets_ptr_lists_[i][j] = oe_offsets_lists_[i][j]->raw_values();
      }
    }

    if (directed_) {
      ie_ptr_lists_.resize(vertex_label_num_);
      ie_offsets_ptr_lists_.resize(vertex_label_num_);
      for (label_id_t i = 0; i < vertex_label_num_; ++i) {
        ie_ptr_lists_[i].resize(edge_label_num_);
        ie_offsets_ptr_lists_[i].resize(edge_label_num_);
        for (label_id_t j = 0; j < edge_label_num_; ++j) {
          ie_ptr_lists_[i][j] =
              reinterpret_cast<const nbr_unit_t*>(ie_lists_[i][j]->GetValue(0));
          ie_offsets_ptr_lists_[i][j] = ie_offsets_lists_[i][j]->raw_values();
        }
      }
    } else {
      ie_ptr_lists_ = oe_ptr_lists_;
      ie_offsets_ptr_lists_ = oe_offsets_ptr_lists_;
    }
  }

  void initDestFidList(
      bool in_edge, bool out_edge,
      std::vector<std::vector<std::vector<fid_t>>>& fid_lists,
      std::vector<std::vector<std::vector<fid_t*>>>& fid_lists_offset) {
    for (auto v_label_id = 0; v_label_id < vertex_label_num_; v_label_id++) {
      auto ivnum_ = ivnums_[v_label_id];
      auto inner_vertices = InnerVertices(v_label_id);

      for (auto e_label_id = 0; e_label_id < edge_label_num_; e_label_id++) {
        std::vector<int> id_num(ivnum_, 0);
        std::set<fid_t> dstset;
        vertex_t v = inner_vertices.begin();
        auto& fid_list = fid_lists[v_label_id][e_label_id];
        auto& fid_list_offset = fid_lists_offset[v_label_id][e_label_id];

        if (!fid_list_offset.empty()) {
          return;
        }
        fid_list_offset.resize(ivnum_ + 1, NULL);
        for (vid_t i = 0; i < ivnum_; ++i) {
          dstset.clear();
          if (in_edge) {
            auto es = GetIncomingAdjList(v, e_label_id);
            for (auto& e : es) {
              fid_t f = GetFragId(e.neighbor());
              if (f != fid_) {
                dstset.insert(f);
              }
            }
          }
          if (out_edge) {
            auto es = GetOutgoingAdjList(v, e_label_id);
            for (auto& e : es) {
              fid_t f = GetFragId(e.neighbor());
              if (f != fid_) {
                dstset.insert(f);
              }
            }
          }
          id_num[i] = dstset.size();
          for (auto fid : dstset) {
            fid_list.push_back(fid);
          }
          ++v;
        }

        fid_list.shrink_to_fit();
        fid_list_offset[0] = fid_list.data();
        for (vid_t i = 0; i < ivnum_; ++i) {
          fid_list_offset[i + 1] = fid_list_offset[i] + id_num[i];
        }
      }
    }
  }

  fid_t fid_, fnum_;
  bool directed_;
  label_id_t vertex_label_num_;
  label_id_t edge_label_num_;

  vineyard::Array<vid_t> ivnums_, ovnums_, tvnums_;

  std::vector<std::shared_ptr<arrow::Table>> vertex_tables_;
  std::vector<std::vector<const void*>> vertex_tables_columns_;

  std::vector<std::shared_ptr<vid_array_t>> ovgid_lists_;
  std::vector<const vid_t*> ovgid_lists_ptr_;

  std::vector<std::shared_ptr<vineyard::Hashmap<vid_t, vid_t>>> ovg2l_maps_;
  std::vector<vineyard::Hashmap<vid_t, vid_t>*> ovg2l_maps_ptr_;

  std::vector<std::shared_ptr<arrow::Table>> edge_tables_;
  std::vector<std::vector<const void*>> edge_tables_columns_;
  std::vector<const void**> flatten_edge_tables_columns_;

  std::vector<std::vector<std::shared_ptr<arrow::FixedSizeBinaryArray>>>
      ie_lists_, oe_lists_;
  std::vector<std::vector<const nbr_unit_t*>> ie_ptr_lists_, oe_ptr_lists_;
  std::vector<std::vector<std::shared_ptr<arrow::Int64Array>>>
      ie_offsets_lists_, oe_offsets_lists_;
  std::vector<std::vector<const int64_t*>> ie_offsets_ptr_lists_,
      oe_offsets_ptr_lists_;

  std::vector<std::vector<std::vector<fid_t>>> idst_, odst_, iodst_;
  std::vector<std::vector<std::vector<fid_t*>>> idoffset_, odoffset_,
      iodoffset_;

  std::shared_ptr<vertex_map_t> vm_ptr_;

  vineyard::IdParser<vid_t> vid_parser_;

  PropertyGraphSchema schema_;

  template <typename _OID_T, typename _VID_T>
  friend class ArrowFragmentBuilder;

  template <typename _OID_T, typename _VID_T, typename VDATA_T,
            typename EDATA_T>
  friend class gs::ArrowProjectedFragment;
};

template <typename OID_T, typename VID_T>
class ArrowFragmentBuilder : public vineyard::ObjectBuilder {
  using oid_t = OID_T;
  using vid_t = VID_T;
  using internal_oid_t = typename InternalType<oid_t>::type;
  using eid_t = property_graph_types::EID_TYPE;
  using label_id_t = property_graph_types::LABEL_ID_TYPE;
  using prop_id_t = property_graph_types::PROP_ID_TYPE;
  using vertex_map_t = ArrowVertexMap<internal_oid_t, vid_t>;

 public:
  explicit ArrowFragmentBuilder(vineyard::Client& client) {}

  void set_fid(fid_t fid) { fid_ = fid; }
  void set_fnum(fid_t fnum) { fnum_ = fnum; }
  void set_directed(bool directed) { directed_ = directed; }

  void set_label_num(label_id_t vertex_label_num, label_id_t edge_label_num) {
    vertex_label_num_ = vertex_label_num;
    edge_label_num_ = edge_label_num;

    vertex_tables_.resize(vertex_label_num_);
    ovgid_lists_.resize(vertex_label_num_);
    ovg2l_maps_.resize(vertex_label_num_);

    edge_tables_.resize(edge_label_num_);

    if (directed_) {
      ie_lists_.resize(vertex_label_num_);
      ie_offsets_lists_.resize(vertex_label_num_);
    }
    oe_lists_.resize(vertex_label_num_);
    oe_offsets_lists_.resize(vertex_label_num_);

    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      if (directed_) {
        ie_lists_[i].resize(edge_label_num_);
        ie_offsets_lists_[i].resize(edge_label_num_);
      }
      oe_lists_[i].resize(edge_label_num_);
      oe_offsets_lists_[i].resize(edge_label_num_);
    }
  }

  void set_ivnums(const vineyard::Array<vid_t>& ivnums) { ivnums_ = ivnums; }

  void set_ovnums(const vineyard::Array<vid_t>& ovnums) { ovnums_ = ovnums; }

  void set_tvnums(const vineyard::Array<vid_t>& tvnums) { tvnums_ = tvnums; }

  void set_vertex_table(label_id_t label,
                        std::shared_ptr<vineyard::Table> table) {
    assert(vertex_tables_.size() > static_cast<size_t>(label));
    vertex_tables_[label] = table;
  }

  void set_ovgid_list(
      label_id_t label,
      std::shared_ptr<vineyard::NumericArray<vid_t>> ovgid_list) {
    assert(ovgid_lists_.size() > static_cast<size_t>(label));
    ovgid_lists_[label] = ovgid_list;
  }

  void set_ovg2l_map(
      label_id_t label,
      std::shared_ptr<vineyard::Hashmap<vid_t, vid_t>> ovg2l_map) {
    assert(ovg2l_maps_.size() > static_cast<size_t>(label));
    ovg2l_maps_[label] = ovg2l_map;
  }

  void set_property_graph_schema(PropertyGraphSchema& schema) {
    schema_ = schema;
  }

  void set_edge_table(label_id_t label,
                      std::shared_ptr<vineyard::Table> table) {
    assert(edge_tables_.size() > static_cast<size_t>(label));
    edge_tables_[label] = table;
  }

  void set_in_edge_list(
      label_id_t v_label, label_id_t e_label,
      std::shared_ptr<vineyard::FixedSizeBinaryArray> in_edge_list) {
    assert(ie_lists_.size() > static_cast<size_t>(v_label));
    assert(ie_lists_[v_label].size() > static_cast<size_t>(e_label));
    ie_lists_[v_label][e_label] = in_edge_list;
  }

  void set_out_edge_list(
      label_id_t v_label, label_id_t e_label,
      std::shared_ptr<vineyard::FixedSizeBinaryArray> out_edge_list) {
    assert(oe_lists_.size() > static_cast<size_t>(v_label));
    assert(oe_lists_[v_label].size() > static_cast<size_t>(e_label));
    oe_lists_[v_label][e_label] = out_edge_list;
  }

  void set_in_edge_offsets(
      label_id_t v_label, label_id_t e_label,
      std::shared_ptr<vineyard::NumericArray<int64_t>> in_edge_offsets) {
    assert(ie_offsets_lists_.size() > static_cast<size_t>(v_label));
    assert(ie_offsets_lists_[v_label].size() > static_cast<size_t>(e_label));
    ie_offsets_lists_[v_label][e_label] = in_edge_offsets;
  }

  void set_out_edge_offsets(
      label_id_t v_label, label_id_t e_label,
      std::shared_ptr<vineyard::NumericArray<int64_t>> out_edge_offsets) {
    assert(oe_offsets_lists_.size() > static_cast<size_t>(v_label));
    assert(oe_offsets_lists_[v_label].size() > static_cast<size_t>(e_label));
    oe_offsets_lists_[v_label][e_label] = out_edge_offsets;
  }

  void set_vertex_map(std::shared_ptr<vertex_map_t> vm_ptr) {
    vm_ptr_ = vm_ptr;
  }

#define ASSIGN_ARRAY_VECTOR(src_array_vec, dst_array_vec) \
  do {                                                    \
    dst_array_vec.resize(src_array_vec.size());           \
    for (size_t i = 0; i < src_array_vec.size(); ++i) {   \
      dst_array_vec[i] = src_array_vec[i]->GetArray();    \
    }                                                     \
  } while (0)

#define ASSIGN_ARRAY_VECTOR_VECTOR(src_array_vec, dst_array_vec) \
  do {                                                           \
    dst_array_vec.resize(src_array_vec.size());                  \
    for (size_t i = 0; i < src_array_vec.size(); ++i) {          \
      dst_array_vec[i].resize(src_array_vec[i].size());          \
      for (size_t j = 0; j < src_array_vec[i].size(); ++j) {     \
        dst_array_vec[i][j] = src_array_vec[i][j]->GetArray();   \
      }                                                          \
    }                                                            \
  } while (0)

#define ASSIGN_TABLE_VECTOR(src_table_vec, dst_table_vec) \
  do {                                                    \
    dst_table_vec.resize(src_table_vec.size());           \
    for (size_t i = 0; i < src_table_vec.size(); ++i) {   \
      dst_table_vec[i] = src_table_vec[i]->GetTable();    \
    }                                                     \
  } while (0)

#define GENERATE_VEC_META(prefix, vec, label_num)                           \
  do {                                                                      \
    for (label_id_t i = 0; i < label_num; ++i) {                            \
      frag->meta_.AddMember(vineyard::generate_name_with_suffix(prefix, i), \
                            vec[i]->meta());                                \
      nbytes += vec[i]->nbytes();                                           \
    }                                                                       \
  } while (0)

#define GENERATE_VEC_VEC_META(prefix, vec, v_label_num, e_label_num) \
  do {                                                               \
    for (label_id_t i = 0; i < v_label_num; ++i) {                   \
      for (label_id_t j = 0; j < e_label_num; ++j) {                 \
        frag->meta_.AddMember(                                       \
            vineyard::generate_name_with_suffix(prefix, i, j),       \
            vec[i][j]->meta());                                      \
        nbytes += vec[i][j]->nbytes();                               \
      }                                                              \
    }                                                                \
  } while (0)

  std::shared_ptr<vineyard::Object> _Seal(vineyard::Client& client) override {
    // ensure the builder hasn't been sealed yet.
    ENSURE_NOT_SEALED(this);

    VINEYARD_CHECK_OK(this->Build(client));

    auto frag = std::make_shared<ArrowFragment<oid_t, vid_t>>();

    frag->fid_ = fid_;
    frag->fnum_ = fnum_;
    frag->directed_ = directed_;
    frag->vertex_label_num_ = vertex_label_num_;
    frag->edge_label_num_ = edge_label_num_;

    frag->ivnums_ = ivnums_;
    frag->ovnums_ = ovnums_;
    frag->tvnums_ = tvnums_;

    ASSIGN_TABLE_VECTOR(vertex_tables_, frag->vertex_tables_);
    ASSIGN_ARRAY_VECTOR(ovgid_lists_, frag->ovgid_lists_);

    ASSIGN_TABLE_VECTOR(edge_tables_, frag->edge_tables_);

    if (directed_) {
      ASSIGN_ARRAY_VECTOR_VECTOR(ie_lists_, frag->ie_lists_);
      ASSIGN_ARRAY_VECTOR_VECTOR(ie_offsets_lists_, frag->ie_offsets_lists_);
    }

    ASSIGN_ARRAY_VECTOR_VECTOR(oe_lists_, frag->oe_lists_);
    ASSIGN_ARRAY_VECTOR_VECTOR(oe_offsets_lists_, frag->oe_offsets_lists_);

    frag->meta_.SetTypeName(type_name<ArrowFragment<oid_t, vid_t>>());

    frag->meta_.AddKeyValue("fid", fid_);
    frag->meta_.AddKeyValue("fnum", fnum_);
    frag->meta_.AddKeyValue("directed", static_cast<int>(directed_));
    frag->meta_.AddKeyValue("vertex_label_num", vertex_label_num_);
    frag->meta_.AddKeyValue("oid_type", TypeName<oid_t>::Get());
    frag->meta_.AddKeyValue("vid_type", TypeName<vid_t>::Get());

    frag->meta_.AddKeyValue("schema", schema_.ToJSONString());

    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      std::shared_ptr<arrow::Table> table = frag->vertex_tables_[i];
      int prop_num = table->num_columns();
      std::unordered_map<std::string, std::string> kvs;
      table->schema()->metadata()->ToUnorderedMap(&kvs);

      frag->meta_.AddKeyValue("vertex_label_name_" + std::to_string(i),
                              kvs["label"]);
      frag->meta_.AddKeyValue("vertex_property_num_" + std::to_string(i),
                              std::to_string(prop_num));
      std::string name_prefix =
          "vertex_property_name_" + std::to_string(i) + "_";
      std::string type_prefix =
          "vertex_property_type_" + std::to_string(i) + "_";
      for (prop_id_t j = 0; j < prop_num; ++j) {
        frag->meta_.AddKeyValue(name_prefix + std::to_string(j),
                                table->field(j)->name());
        frag->meta_.AddKeyValue(type_prefix + std::to_string(j),
                                arrow_type_to_string(table->field(j)->type()));
      }
    }

    frag->meta_.AddKeyValue("edge_label_num", edge_label_num_);
    for (label_id_t i = 0; i < edge_label_num_; ++i) {
      std::shared_ptr<arrow::Table> table = frag->edge_tables_[i];
      int prop_num = table->num_columns();
      std::unordered_map<std::string, std::string> kvs;
      table->schema()->metadata()->ToUnorderedMap(&kvs);

      frag->meta_.AddKeyValue("edge_label_name_" + std::to_string(i),
                              kvs["label"]);
      frag->meta_.AddKeyValue("edge_property_num_" + std::to_string(i),
                              std::to_string(prop_num));
      std::string name_prefix = "edge_property_name_" + std::to_string(i) + "_";
      std::string type_prefix = "edge_property_type_" + std::to_string(i) + "_";
      for (prop_id_t j = 0; j < prop_num; ++j) {
        frag->meta_.AddKeyValue(name_prefix + std::to_string(j),
                                table->field(j)->name());
        frag->meta_.AddKeyValue(type_prefix + std::to_string(j),
                                arrow_type_to_string(table->field(j)->type()));
      }
    }

    size_t nbytes = 0;
    frag->meta_.AddMember("ivnums", ivnums_.meta());
    nbytes += ivnums_.nbytes();
    frag->meta_.AddMember("ovnums", ovnums_.meta());
    nbytes += ovnums_.nbytes();
    frag->meta_.AddMember("tvnums", tvnums_.meta());
    nbytes += tvnums_.nbytes();

    GENERATE_VEC_META("vertex_tables", vertex_tables_, vertex_label_num_);
    GENERATE_VEC_META("ovgid_lists", ovgid_lists_, vertex_label_num_);
    GENERATE_VEC_META("ovg2l_maps", ovg2l_maps_, vertex_label_num_);
    GENERATE_VEC_META("edge_tables", edge_tables_, edge_label_num_);
    if (directed_) {
      GENERATE_VEC_VEC_META("ie_lists", ie_lists_, vertex_label_num_,
                            edge_label_num_);
      GENERATE_VEC_VEC_META("ie_offsets_lists", ie_offsets_lists_,
                            vertex_label_num_, edge_label_num_);
    }
    GENERATE_VEC_VEC_META("oe_lists", oe_lists_, vertex_label_num_,
                          edge_label_num_);
    GENERATE_VEC_VEC_META("oe_offsets_lists", oe_offsets_lists_,
                          vertex_label_num_, edge_label_num_);

    frag->meta_.AddMember("vertex_map", vm_ptr_->meta());

    frag->meta_.SetNBytes(nbytes);

    VINEYARD_CHECK_OK(client.CreateMetaData(frag->meta_, frag->id_));
    this->set_sealed(true);

    VINEYARD_CHECK_OK(client.GetMetaData(frag->id_, frag->meta_));
    frag->Construct(frag->meta_);
    return std::static_pointer_cast<vineyard::Object>(frag);
  }

#undef ASSIGN_ARRAY_VECTOR
#undef ASSIGN_ARRAY_VECTOR_VECTOR
#undef ASSIGN_TABLE_VECTOR
#undef GENERATE_VEC_META
#undef GENERATE_VEC_VEC_META

 private:
  fid_t fid_, fnum_;
  bool directed_;
  label_id_t vertex_label_num_;
  label_id_t edge_label_num_;

  vineyard::Array<vid_t> ivnums_, ovnums_, tvnums_;

  std::vector<std::shared_ptr<vineyard::Table>> vertex_tables_;
  std::vector<std::shared_ptr<vineyard::NumericArray<vid_t>>> ovgid_lists_;
  std::vector<std::shared_ptr<vineyard::Hashmap<vid_t, vid_t>>> ovg2l_maps_;

  std::vector<std::shared_ptr<vineyard::Table>> edge_tables_;

  std::vector<std::vector<std::shared_ptr<vineyard::FixedSizeBinaryArray>>>
      ie_lists_, oe_lists_;
  std::vector<std::vector<std::shared_ptr<vineyard::NumericArray<int64_t>>>>
      ie_offsets_lists_, oe_offsets_lists_;

  std::shared_ptr<vertex_map_t> vm_ptr_;
  PropertyGraphSchema schema_;
};

template <typename OID_T, typename VID_T>
class BasicArrowFragmentBuilder : public ArrowFragmentBuilder<OID_T, VID_T> {
  using oid_t = OID_T;
  using vid_t = VID_T;
  using internal_oid_t = typename InternalType<oid_t>::type;
  using eid_t = property_graph_types::EID_TYPE;
  using label_id_t = property_graph_types::LABEL_ID_TYPE;
  using vertex_map_t = ArrowVertexMap<internal_oid_t, vid_t>;
  using nbr_unit_t = property_graph_utils::NbrUnit<vid_t, eid_t>;
  using vid_array_t = typename vineyard::ConvertToArrowType<vid_t>::ArrayType;

 public:
  explicit BasicArrowFragmentBuilder(vineyard::Client& client,
                                     std::shared_ptr<vertex_map_t> vm_ptr)
      : ArrowFragmentBuilder<oid_t, vid_t>(client), vm_ptr_(vm_ptr) {}

  vineyard::Status Build(vineyard::Client& client) override {
    this->set_fid(fid_);
    this->set_fnum(fnum_);
    this->set_directed(directed_);
    this->set_label_num(vertex_label_num_, edge_label_num_);
    this->set_property_graph_schema(schema_);

    ThreadGroup tg;
    {
      auto fn = [this](Client& client) {
        vineyard::ArrayBuilder<vid_t> ivnums_builder(client, ivnums_);
        vineyard::ArrayBuilder<vid_t> ovnums_builder(client, ovnums_);
        vineyard::ArrayBuilder<vid_t> tvnums_builder(client, tvnums_);
        this->set_ivnums(*std::dynamic_pointer_cast<vineyard::Array<vid_t>>(
            ivnums_builder.Seal(client)));
        this->set_ovnums(*std::dynamic_pointer_cast<vineyard::Array<vid_t>>(
            ovnums_builder.Seal(client)));
        this->set_tvnums(*std::dynamic_pointer_cast<vineyard::Array<vid_t>>(
            tvnums_builder.Seal(client)));
        return Status::OK();
      };

      tg.AddTask(fn, std::ref(client));
    }

    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      auto fn = [this, i](Client& client) {
        vineyard::TableBuilder vt(client, vertex_tables_[i]);
        this->set_vertex_table(
            i, std::dynamic_pointer_cast<vineyard::Table>(vt.Seal(client)));

        vineyard::NumericArrayBuilder<vid_t> ovgid_list_builder(
            client, ovgid_lists_[i]);
        this->set_ovgid_list(
            i, std::dynamic_pointer_cast<vineyard::NumericArray<vid_t>>(
                   ovgid_list_builder.Seal(client)));

        vineyard::HashmapBuilder<vid_t, vid_t> ovg2l_builder(
            client, std::move(ovg2l_maps_[i]));
        this->set_ovg2l_map(
            i, std::dynamic_pointer_cast<vineyard::Hashmap<vid_t, vid_t>>(
                   ovg2l_builder.Seal(client)));
        return Status::OK();
      };
      tg.AddTask(fn, std::ref(client));
    }

    for (label_id_t i = 0; i < edge_label_num_; ++i) {
      auto fn = [this, i](Client& client) {
        vineyard::TableBuilder et(client, edge_tables_[i]);
        this->set_edge_table(
            i, std::dynamic_pointer_cast<vineyard::Table>(et.Seal(client)));
        return Status::OK();
      };
      tg.AddTask(fn, std::ref(client));
    }

    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      for (label_id_t j = 0; j < edge_label_num_; ++j) {
        auto fn = [this, i, j](Client& client) {
          if (directed_) {
            vineyard::FixedSizeBinaryArrayBuilder ie_builder(client,
                                                             ie_lists_[i][j]);
            this->set_in_edge_list(
                i, j,
                std::dynamic_pointer_cast<vineyard::FixedSizeBinaryArray>(
                    ie_builder.Seal(client)));
          }
          {
            vineyard::FixedSizeBinaryArrayBuilder oe_builder(client,
                                                             oe_lists_[i][j]);
            this->set_out_edge_list(
                i, j,
                std::dynamic_pointer_cast<vineyard::FixedSizeBinaryArray>(
                    oe_builder.Seal(client)));
          }
          if (directed_) {
            vineyard::NumericArrayBuilder<int64_t> ieo(client,
                                                       ie_offsets_lists_[i][j]);
            this->set_in_edge_offsets(
                i, j,
                std::dynamic_pointer_cast<vineyard::NumericArray<int64_t>>(
                    ieo.Seal(client)));
          }
          {
            vineyard::NumericArrayBuilder<int64_t> oeo(client,
                                                       oe_offsets_lists_[i][j]);
            this->set_out_edge_offsets(
                i, j,
                std::dynamic_pointer_cast<vineyard::NumericArray<int64_t>>(
                    oeo.Seal(client)));
          }
          return Status::OK();
        };
        tg.AddTask(fn, std::ref(client));
      }
    }

    tg.TakeResults();

    this->set_vertex_map(vm_ptr_);
    return vineyard::Status::OK();
  }

  boost::leaf::result<void> Init(
      fid_t fid, fid_t fnum,
      std::vector<std::shared_ptr<arrow::Table>>&& vertex_tables,
      std::vector<std::shared_ptr<arrow::Table>>&& edge_tables,
      bool directed = true, int concurrency = 1) {
    fid_ = fid;
    fnum_ = fnum;
    directed_ = directed;
    vertex_label_num_ = vertex_tables.size();
    edge_label_num_ = edge_tables.size();

    vid_parser_.Init(fnum_, vertex_label_num_);

    BOOST_LEAF_CHECK(initVertices(std::move(vertex_tables)));
    BOOST_LEAF_CHECK(initEdges(std::move(edge_tables), concurrency));
    return {};
  }

  boost::leaf::result<void> SetPropertyGraphSchema(
      PropertyGraphSchema&& schema) {
    schema_ = std::move(schema);
    return {};
  }

 private:
  // | prop_0 | prop_1 | ... |
  boost::leaf::result<void> initVertices(
      std::vector<std::shared_ptr<arrow::Table>>&& vertex_tables) {
    assert(vertex_tables.size() == static_cast<size_t>(vertex_label_num_));
    vertex_tables_.resize(vertex_label_num_);
    ivnums_.resize(vertex_label_num_);
    ovnums_.resize(vertex_label_num_);
    tvnums_.resize(vertex_label_num_);
    for (size_t i = 0; i < vertex_tables.size(); ++i) {
#if defined(ARROW_VERSION) && ARROW_VERSION < 17000
      ARROW_OK_OR_RAISE(vertex_tables[i]->CombineChunks(
          arrow::default_memory_pool(), &vertex_tables_[i]));
#else
      ARROW_OK_ASSIGN_OR_RAISE(
          vertex_tables_[i],
          vertex_tables[i]->CombineChunks(arrow::default_memory_pool()));
#endif
      ivnums_[i] = vm_ptr_->GetInnerVertexSize(fid_, i);
    }
    return {};
  }

  // | src_id(generated) | dst_id(generated) | prop_0 | prop_1
  // | ... |
  boost::leaf::result<void> initEdges(
      std::vector<std::shared_ptr<arrow::Table>>&& edge_tables,
      int concurrency) {
    assert(edge_tables.size() == static_cast<size_t>(edge_label_num_));
    std::vector<std::shared_ptr<vid_array_t>> edge_src, edge_dst;
    edge_src.resize(edge_label_num_);
    edge_dst.resize(edge_label_num_);

    edge_tables_.resize(edge_label_num_);
    std::vector<std::vector<vid_t>> collected_ovgids(vertex_label_num_);

    for (size_t i = 0; i < edge_tables.size(); ++i) {
      std::shared_ptr<arrow::Table> combined_table;
#if defined(ARROW_VERSION) && ARROW_VERSION < 17000
      ARROW_OK_OR_RAISE(edge_tables[i]->CombineChunks(
          arrow::default_memory_pool(), &combined_table));
#else
      ARROW_OK_ASSIGN_OR_RAISE(
          combined_table,
          edge_tables[i]->CombineChunks(arrow::default_memory_pool()));
#endif
      edge_tables[i].swap(combined_table);

      collect_outer_vertices(vid_parser_,
                             std::dynamic_pointer_cast<vid_array_t>(
                                 edge_tables[i]->column(0)->chunk(0)),
                             fid_, collected_ovgids);
      collect_outer_vertices(vid_parser_,
                             std::dynamic_pointer_cast<vid_array_t>(
                                 edge_tables[i]->column(1)->chunk(0)),
                             fid_, collected_ovgids);
    }
    std::vector<vid_t> start_ids(vertex_label_num_);
    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      start_ids[i] = vid_parser_.GenerateId(0, i, ivnums_[i]);
    }
    generate_outer_vertices_map<vid_t>(collected_ovgids, start_ids,
                                       vertex_label_num_, ovg2l_maps_,
                                       ovgid_lists_);
    collected_ovgids.clear();
    for (label_id_t i = 0; i < vertex_label_num_; ++i) {
      ovnums_[i] = ovgid_lists_[i]->length();
      tvnums_[i] = ivnums_[i] + ovnums_[i];
    }

    for (size_t i = 0; i < edge_tables.size(); ++i) {
      generate_local_id_list(vid_parser_,
                             std::dynamic_pointer_cast<vid_array_t>(
                                 edge_tables[i]->column(0)->chunk(0)),
                             fid_, ovg2l_maps_, concurrency, edge_src[i]);
      generate_local_id_list(vid_parser_,
                             std::dynamic_pointer_cast<vid_array_t>(
                                 edge_tables[i]->column(1)->chunk(0)),
                             fid_, ovg2l_maps_, concurrency, edge_dst[i]);

      std::shared_ptr<arrow::Table> tmp_table0;
#if defined(ARROW_VERSION) && ARROW_VERSION < 17000
      ARROW_OK_OR_RAISE(edge_tables[i]->RemoveColumn(0, &tmp_table0));
      ARROW_OK_OR_RAISE(tmp_table0->RemoveColumn(0, &edge_tables_[i]));
#else
      ARROW_OK_ASSIGN_OR_RAISE(tmp_table0, edge_tables[i]->RemoveColumn(0));
      ARROW_OK_ASSIGN_OR_RAISE(edge_tables_[i], tmp_table0->RemoveColumn(0));
#endif

      edge_tables[i].reset();
    }

    oe_lists_.resize(vertex_label_num_);
    oe_offsets_lists_.resize(vertex_label_num_);
    if (directed_) {
      ie_lists_.resize(vertex_label_num_);
      ie_offsets_lists_.resize(vertex_label_num_);
    }

    for (label_id_t v_label = 0; v_label < vertex_label_num_; ++v_label) {
      oe_lists_[v_label].resize(edge_label_num_);
      oe_offsets_lists_[v_label].resize(edge_label_num_);
      if (directed_) {
        ie_lists_[v_label].resize(edge_label_num_);
        ie_offsets_lists_[v_label].resize(edge_label_num_);
      }
    }
    for (label_id_t e_label = 0; e_label < edge_label_num_; ++e_label) {
      std::vector<std::shared_ptr<arrow::FixedSizeBinaryArray>> sub_ie_lists(
          vertex_label_num_);
      std::vector<std::shared_ptr<arrow::FixedSizeBinaryArray>> sub_oe_lists(
          vertex_label_num_);
      std::vector<std::shared_ptr<arrow::Int64Array>> sub_ie_offset_lists(
          vertex_label_num_);
      std::vector<std::shared_ptr<arrow::Int64Array>> sub_oe_offset_lists(
          vertex_label_num_);
      if (directed_) {
        generate_directed_csr<vid_t, eid_t>(
            vid_parser_, edge_src[e_label], edge_dst[e_label], tvnums_,
            vertex_label_num_, concurrency, sub_oe_lists, sub_oe_offset_lists);
        generate_directed_csr<vid_t, eid_t>(
            vid_parser_, edge_dst[e_label], edge_src[e_label], tvnums_,
            vertex_label_num_, concurrency, sub_ie_lists, sub_ie_offset_lists);
      } else {
        generate_undirected_csr<vid_t, eid_t>(
            vid_parser_, edge_src[e_label], edge_dst[e_label], tvnums_,
            vertex_label_num_, concurrency, sub_oe_lists, sub_oe_offset_lists);
      }

      for (label_id_t v_label = 0; v_label < vertex_label_num_; ++v_label) {
        if (directed_) {
          ie_lists_[v_label][e_label] = sub_ie_lists[v_label];
          ie_offsets_lists_[v_label][e_label] = sub_ie_offset_lists[v_label];
        }
        oe_lists_[v_label][e_label] = sub_oe_lists[v_label];
        oe_offsets_lists_[v_label][e_label] = sub_oe_offset_lists[v_label];
      }
    }
    return {};
  }

  fid_t fid_, fnum_;
  bool directed_;
  label_id_t vertex_label_num_;
  label_id_t edge_label_num_;

  std::vector<vid_t> ivnums_, ovnums_, tvnums_;

  std::vector<std::shared_ptr<arrow::Table>> vertex_tables_;
  std::vector<std::shared_ptr<vid_array_t>> ovgid_lists_;
  std::vector<ska::flat_hash_map<vid_t, vid_t>> ovg2l_maps_;

  std::vector<std::shared_ptr<arrow::Table>> edge_tables_;

  std::vector<std::vector<std::shared_ptr<arrow::FixedSizeBinaryArray>>>
      ie_lists_, oe_lists_;
  std::vector<std::vector<std::shared_ptr<arrow::Int64Array>>>
      ie_offsets_lists_, oe_offsets_lists_;

  std::shared_ptr<vertex_map_t> vm_ptr_;

  IdParser<vid_t> vid_parser_;

  PropertyGraphSchema schema_;
};

}  // namespace vineyard

#endif  // MODULES_GRAPH_FRAGMENT_ARROW_FRAGMENT_H_
