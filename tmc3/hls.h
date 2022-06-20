/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2017-2018, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of the ISO/IEC nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "PCCMath.h"

#include <cstdint>
#include <ostream>
#include <vector>

namespace pcc {

//============================================================================

enum class PayloadType
{
  kSequenceParameterSet = 0,
  kGeometryParameterSet = 1,
  kGeometryBrick = 2,
  kAttributeParameterSet = 3,
  kAttributeBrick = 4,
  kTileInventory = 5,
  kFrameBoundaryMarker = 6,
  kConstantAttribute = 7,
};

//============================================================================

enum class KnownAttributeLabel : uint32_t
{
  kColour = 0,
  kReflectance = 1,
  kFrameIndex = 2,
  kMaterialId = 3,
  kTransparency = 4,
  kNormal = 5,

  // Indicates that the attrabute label is described by an Oid
  kOid = 0xffffffff,
};

//============================================================================

struct Oid {
  // A sequence of encoded subidentifiers according to Rec. ITU-T X.690 |
  // ISO/IEC 8825-1.  NB: this does not include any identifier octets, length
  // octets or end-of-content octets of the basic encoding rules.
  std::vector<uint8_t> contents;

  Oid() = default;
  Oid(const std::string& str);

  // Convert the oid to a string representation
  operator std::string() const;

  friend bool operator==(const Oid& lhs, const Oid& rhs);
};

//============================================================================

struct AttributeLabel {
  KnownAttributeLabel known_attribute_label;
  Oid oid;

  //--------------------------------------------------------------------------

  AttributeLabel() = default;

  AttributeLabel(KnownAttributeLabel known_attribute_label)
    : known_attribute_label(known_attribute_label)
  {}

  //--------------------------------------------------------------------------

  friend bool
  operator==(const AttributeLabel& lhs, const KnownAttributeLabel& rhs)
  {
    return lhs.known_attribute_label == rhs;
  }

  //--------------------------------------------------------------------------

  bool known_attribute_label_flag() const
  {
    return known_attribute_label != KnownAttributeLabel::kOid;
  }
};

//============================================================================

std::ostream& operator<<(std::ostream& os, const AttributeLabel& label);

//============================================================================

enum class AttributeEncoding
{
  kPredictingTransform = 0,
  kRAHTransform = 1,
  kLiftingTransform = 2,
};

//============================================================================

enum class AxisOrder
{
  kZYX = 0,
  kXYZ = 1,
  kXZY = 2,
  kYZX = 3,
  kZYX_4 = 4,
  kZXY = 5,
  kYXZ = 6,
  kXYZ_7 = 7,
};

// Permute the internal STV axes to XYZ order.
template<typename T>
Vec3<T>
toXyz(AxisOrder order, const Vec3<T>& stv)
{
  switch (order) {
  case AxisOrder::kZYX: return {stv.v(), stv.t(), stv.s()};
  case AxisOrder::kXYZ: return {stv.s(), stv.t(), stv.v()};
  case AxisOrder::kXZY: return {stv.s(), stv.v(), stv.t()};
  case AxisOrder::kYZX: return {stv.v(), stv.s(), stv.t()};
  case AxisOrder::kZYX_4: return {stv.v(), stv.t(), stv.s()};
  case AxisOrder::kZXY: return {stv.t(), stv.v(), stv.s()};
  case AxisOrder::kYXZ: return {stv.t(), stv.s(), stv.v()};
  case AxisOrder::kXYZ_7: return {stv.s(), stv.t(), stv.v()};
  }
}

// Permute the an XYZ axis order to the internal STV order.
template<typename T>
Vec3<T>
fromXyz(AxisOrder order, const Vec3<T>& xyz)
{
  switch (order) {
  case AxisOrder::kZYX: return {xyz.z(), xyz.y(), xyz.x()};
  case AxisOrder::kXYZ: return {xyz.x(), xyz.y(), xyz.z()};
  case AxisOrder::kXZY: return {xyz.x(), xyz.z(), xyz.y()};
  case AxisOrder::kYZX: return {xyz.y(), xyz.z(), xyz.x()};
  case AxisOrder::kZYX_4: return {xyz.z(), xyz.y(), xyz.x()};
  case AxisOrder::kZXY: return {xyz.z(), xyz.x(), xyz.y()};
  case AxisOrder::kYXZ: return {xyz.y(), xyz.x(), xyz.z()};
  case AxisOrder::kXYZ_7: return {xyz.x(), xyz.y(), xyz.z()};
  }
}

//============================================================================
// ISO/IEC 23001-8 codec independent code points
enum class ColourMatrix : uint8_t
{
  kIdentity = 0,
  kBt709 = 1,
  kUnspecified = 2,
  kReserved_3 = 3,
  kUsa47Cfr73dot682a20 = 4,
  kBt601 = 5,
  kSmpte170M = 6,
  kSmpte240M = 7,
  kYCgCo = 8,
  kBt2020Ncl = 9,
  kBt2020Cl = 10,
  kSmpte2085 = 11,
};

//============================================================================

enum class AttributeParameterType : uint8_t
{
  kItuT35 = 0,
  kOid = 1,
  kCicp = 2,
  kScaling = 3,
  kDefaultValue = 4,
  /* [3, 127] are reserved for future use */
  /* [128, 255] are specified according to the attribute label */
};

//============================================================================

struct OpaqueAttributeParameter {
  // the type of the data
  AttributeParameterType attr_param_type;

  // identifies the type of attr_param_byte data when attr_param_type = 0.
  int attr_param_itu_t_t35_country_code;
  int attr_param_itu_t_t35_country_code_extension;

  // identifies the type of attr_param_byte data when attr_param_type = 1.
  Oid attr_param_oid;

  // the attribute data excluding type0/type1 identification bytes */
  std::vector<uint8_t> attr_param_byte;
};

//============================================================================

// invariant properties
struct AttributeDescription {
  int attr_num_dimensions_minus1;

  // NB: the instance id is not the attribute id / attrId used in the decoding
  // process.  The instance id is used to distinguish between, in the decoded
  // output, multiple attributes with the same label.  Eg, rgb0 and rgb1.
  int attr_instance_id;

  int bitdepth;
  int bitdepthSecondary;

  AttributeLabel attributeLabel;

  // Known attribute parameters

  // indicates if the cicp attribute parameter is valid
  bool cicpParametersPresent;
  int cicp_colour_primaries_idx;
  int cicp_transfer_characteristics_idx;
  ColourMatrix cicp_matrix_coefficients_idx;
  bool cicp_video_full_range_flag;

  // attribute scaling
  bool scalingParametersPresent;
  int source_attr_scale_log2;
  int source_attr_offset_log2;

  // soft default attribute values.
  // If empty, use hard default values.
  std::vector<int> attr_default_value;

  // Unknown attribute parameters
  std::vector<OpaqueAttributeParameter> opaqueParameters;
};

//============================================================================

struct ProfileCompatibility {
  // indicates conformance with the "main" profile
  bool main_profile_compatibility_flag;

  // reserved for future use
  int reserved_profile_compatibility_21bits;

  // indicates that the bistream may break if slices are reordered
  bool slice_reordering_constraint_flag;

  // indicates that there are no duplicate points in the reconstructed frames
  bool unique_point_positions_constraint_flag;
};

//============================================================================

enum class ScaleUnit : bool
{
  kDimensionless = 0,
  kPointsPerMetre = 1,
};

//============================================================================

struct SequenceParameterSet {
  int sps_seq_parameter_set_id;

  ProfileCompatibility profile;
  int level;

  // Number of bits used to code seqBoundingBoxOrigin
  int sps_bounding_box_offset_bits_minus1;

  // the bounding box origin (in stv axis order).
  Vec3<int> seqBoundingBoxOrigin;

  // Number of bits used to code seqBoundingBoxSize
  int sps_bounding_box_size_bits_minus1;

  // the size of the bounding box (in stv axis order).
  Vec3<int> seqBoundingBoxSize;

  // A value describing the scaling of the source positions prior to encoding.
  float seq_geom_scale;

  // Indicates that units used to interpret seq_geom_scale.
  ScaleUnit seq_geom_scale_unit_flag;

  // NB: attributeSets.size() = num_attribute_sets
  std::vector<AttributeDescription> attributeSets;

  // The number of bits to use for frame_idx
  int log2_max_frame_idx;

  // Defines the ordering of the position components (eg, xyz vs zyx)
  AxisOrder geometry_axis_order;

  // Controls whether bypass bins are written to a seperate sub-stream, or
  // encoded as ep bins via CABAC.
  bool cabac_bypass_stream_enabled_flag;

  // Indicates that context state may be propagated between slices.
  bool entropy_continuation_enabled_flag;
};

//============================================================================

struct GeometryParameterSet {
  int gps_geom_parameter_set_id;
  int gps_seq_parameter_set_id;

  // Indicates the presence of gps_geom_box_log2_scale and
  // geom_box_log2_scale.
  bool geom_box_log2_scale_present_flag;

  // Default scaling factor for per-slice geometry box origin
  int gps_geom_box_log2_scale;

  // Selects between predictive and octree geometry coding methods.
  bool predgeom_enabled_flag;

  // Controls the ability to represent multiple points (with associated
  // attributes) at the same spatial position.
  bool geom_unique_points_flag;

  // Defines the size of the neighbour availiability volume (aka
  // look-ahead cube size) for occupancy searches.  A value of 0
  // indicates that only neighbours that are direct siblings are available.
  int neighbour_avail_boundary_log2;

  // Controls the use of early termination of the geometry tree
  // by directly coding the position of isolated points.
  int inferred_direct_coding_mode;

  // Permits coding the common prefix of two idcm points
  bool joint_2pt_idcm_enabled_flag;

  // Selects between bitwise and bytewise occupancy coding
  bool bitwise_occupancy_coding_flag;

  // Controlls contextualization of occupancy bits and refinement of
  // the neighbour pattern according to the occupancy of adjacent
  // children in neighbouring nodes.
  bool adjacent_child_contextualization_enabled_flag;

  // Maximum node size where intra prediction is enabled
  int intra_pred_max_node_size_log2;

  // Enables trisoup
  bool trisoup_enabled_flag;

  // sampling value of trisoup decoding process
  // a value of zero set the automatic sampling value setting to avoid over point of slice MAX points(sliceMaxPoints)
  int trisoup_sampling_value;

  // controls the ability to perform in-loop geometry scaling
  bool geom_scaling_enabled_flag;

  // factor by which to shift geometry QPs before use
  int geom_qp_multiplier_log2;

  // intial qp for geometry scaling, scaled by the qp multiplier
  int geom_base_qp;

  // initial qp (offset) for idcm nodes, scaled by the qp multiplier
  int geom_idcm_qp_offset;

  // Enables/disables non-cubic geometry nodes
  bool qtbt_enabled_flag;

  // Controls the use of planar mode
  bool geom_planar_mode_enabled_flag;
  int geom_planar_threshold0;
  int geom_planar_threshold1;
  int geom_planar_threshold2;
  int geom_planar_idcm_threshold;

  // Controls the use of xyz-planar mode
  bool geom_angular_mode_enabled_flag;

  // Sequence bounding box relative origin for angular mode computations
  // (in stv axis order).
  Vec3<int> geomAngularOrigin;

  int geom_angular_num_lidar_lasers() const
  {
    return geom_angular_theta_laser.size();
  }

  std::vector<int> geom_angular_theta_laser;
  std::vector<int> geom_angular_z_laser;
  std::vector<int> geom_angular_num_phi_per_turn;

  int geomAngularThetaPred(int i) const
  {
    if (!--i)
      return geom_angular_theta_laser[i];
    return 2 * geom_angular_theta_laser[i] - geom_angular_theta_laser[i - 1];
  }

  // disable the use of planar buffer when angular mode is enabled
  bool planar_buffer_disabled_flag;

  // block size (i.e. number of points per block) in predictive geometry coding
  int geom_qp_offset_intvl_log2;

  // scale factor for azimuth in coding predictive geometry coding
  int geom_angular_azimuth_scale_log2;
  int geom_angular_azimuth_speed;

  // inverse scale factor for radius coding in predictive geometry coding
  int geom_angular_radius_inv_scale_log2;

  // Indicates that the geometry footer contains a count of point
  // in each octree level.
  bool octree_point_count_list_present_flag;
};

//============================================================================

struct GeometryBrickFooter {
  // The actual number of points present in the slice
  int geom_num_points_minus1;

  // The number of points that can be decoded at a particular octree level
  std::vector<int> octree_lvl_num_points_minus1;
};

//============================================================================

struct GeometryBrickHeader {
  int geom_geom_parameter_set_id;
  int geom_tile_id;
  int geom_slice_id;
  int frame_idx;

  // Origin of the reconstructed geometry, relative to sequence bounding box
  // (in stv axis order).
  Vec3<int> geomBoxOrigin;
  int geom_box_log2_scale;

  // Number of bits to represent geomBoxOrigin >> geom_box_log2_scale
  int geom_box_origin_bits_minus1;

  // the size of the root geometry node
  // NB: this is only needed for the initial node size determination at
  //     the encoder
  Vec3<int> rootNodeSizeLog2;

  Vec3<int> pgeom_resid_abs_log2_bits;

  // the largest dimension of the root geometry node
  mutable int maxRootNodeDimLog2;

  std::vector<int8_t> tree_lvl_coded_axis_list;

  int tree_depth_minus1() const { return tree_lvl_coded_axis_list.size() - 1; }

  // qp offset for geometry scaling (if enabled)
  int geom_slice_qp_offset;

  int sliceQp(const GeometryParameterSet& gps) const
  {
    return (gps.geom_base_qp + geom_slice_qp_offset)
      << gps.geom_qp_multiplier_log2;
  }

  // octree depth at which qp offsets whould be signalled
  int geom_octree_qp_offset_depth;

  // block size offset for predictive geometry coding (if enabled)
  int geom_qp_offset_intvl_log2_delta;

  // number of entropy streams used to encode the octree
  int geom_stream_cnt_minus1;

  // length of each entropy stream
  std::vector<size_t> geom_stream_len;

  // number of bits to signal entropy stream lengths
  int geom_stream_len_bits;

  int geomBoxLog2Scale(const GeometryParameterSet& gps) const
  {
    if (!gps.geom_box_log2_scale_present_flag)
      return gps.gps_geom_box_log2_scale;
    return geom_box_log2_scale;
  }

  // size of triangle nodes (reconstructed surface) in trisoup geometry.
  int trisoup_node_size_log2;

  // downsampling rate used in tringle voxelisation
  int trisoup_sampling_value_minus1;

  int num_unique_segments_minus1;

  // Number of bits to represent num_unique_segments_minus1
  int num_unique_segments_bits_minus1;

  // 'Header' information that appears at the end of the data unit
  GeometryBrickFooter footer;

  // Indicates the current slice reuses contexts from the prevous slice
  bool entropy_continuation_flag;

  // The id of the previous slice in bitsream order
  int prev_slice_id;
};

//============================================================================
// NB: when updating this, remember to update AttributeLods::isReusable(...)

struct AttributeParameterSet {
  int aps_attr_parameter_set_id;
  int aps_seq_parameter_set_id;
  AttributeEncoding attr_encoding;

  bool lodParametersPresent() const
  {
    return attr_encoding == AttributeEncoding::kLiftingTransform
      || attr_encoding == AttributeEncoding::kPredictingTransform;
  }

  //--- lifting/predicting transform parameters

  bool lod_decimation_enabled_flag;
  bool canonical_point_order_flag;
  int num_pred_nearest_neighbours_minus1;
  int max_num_direct_predictors;
  int adaptive_prediction_threshold;
  int intra_lod_search_range;
  int inter_lod_search_range;

  // NB: in stv order
  Vec3<int32_t> lodNeighBias;

  bool intra_lod_prediction_enabled_flag;
  bool inter_component_prediction_enabled_flag;
  bool last_component_prediction_enabled_flag;

  // NB: derived from num_detail_levels_minus1
  int num_detail_levels;
  std::vector<int> lodSamplingPeriod;

  int dist2;
  bool aps_slice_dist2_deltas_present_flag;

  // NB: these parameters are shared by all transform implementations
  int init_qp_minus4;
  int aps_chroma_qp_offset;
  bool aps_slice_qp_deltas_present_flag;

  //--- raht parameters
  bool raht_prediction_enabled_flag;
  int raht_prediction_threshold0;
  int raht_prediction_threshold1;

  //--- lifting parameters
  bool scalable_lifting_enabled_flag;
  int max_neigh_range;

  // indicates that attribute coding should be performed in
  // pseudo-spherical domain
  bool spherical_coord_flag;
};

//============================================================================

struct AttributeBrickHeader {
  int attr_sps_attr_idx;
  int attr_attr_parameter_set_id;
  int attr_geom_slice_id;

  int attr_qp_delta_luma;
  int attr_qp_delta_chroma;

  std::vector<int> attr_layer_qp_delta_luma;
  std::vector<int> attr_layer_qp_delta_chroma;

  bool attr_layer_qp_present_flag() const
  {
    return !attr_layer_qp_delta_luma.empty();
  }

  int attr_num_qp_layers_minus1() const
  {
    return attr_layer_qp_delta_luma.size() - 1;
  }

  struct QpRegion {
    // NB: in stv order
    Vec3<int> regionOrigin;

    // NB: in stv order
    Vec3<int> regionSize;

    std::array<int, 2> attr_region_qp_offset;
  };

  std::vector<QpRegion> qpRegions;

  // Number of bits to represent regionOrigin and regionSize
  int attr_region_bits_minus1;

  int32_t attr_dist2_delta;

  // (r, phi, laserid) scale factors for domain conversion
  Vec3<int> attr_coord_conv_scale;
};

//============================================================================

struct ConstantAttributeDataUnit {
  int constattr_sps_attr_idx;
  int constattr_attr_parameter_set_id;
  int constattr_geom_slice_id;

  std::vector<int> constattr_default_value;
};

//============================================================================

struct TileInventory {
  struct Entry {
    // The tile id (either manually specified, or the implicit value).
    int tile_id;

    // NB: in stv order
    Vec3<int> tileOrigin;

    // NB: in stv order
    Vec3<int> tileSize;
  };

  // id of an applicable sequence parameter set
  int ti_seq_parameter_set_id;

  // Number of bits, if any, used to signal tile_id
  bool tile_id_bits;

  // the origin of the tiles (in stv axis order).  Likely the sps origin
  Vec3<int> origin;

  // Number of bits to represent the inventory origin
  int ti_origin_bits_minus1;

  std::vector<Entry> tiles;

  // Number of bits to represent each tile's origin
  int tile_origin_bits_minus1;

  // Number of bits to represent each tile's size
  int tile_size_bits_minus1;
};

//============================================================================

void convertXyzToStv(SequenceParameterSet*);
void convertXyzToStv(const SequenceParameterSet&, GeometryParameterSet*);
void convertXyzToStv(const SequenceParameterSet&, AttributeParameterSet*);
void convertXyzToStv(const SequenceParameterSet&, TileInventory*);

//============================================================================

}  // namespace pcc
