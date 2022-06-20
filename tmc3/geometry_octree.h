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

#include <cstdint>

#include "DualLutCoder.h"
#include "PCCMath.h"
#include "PCCPointSet.h"
#include "entropy.h"
#include "geometry_params.h"
#include "hls.h"
#include "ringbuf.h"
#include "tables.h"

namespace pcc {

//============================================================================

const int MAX_NUM_DM_LEAF_POINTS = 2;

//============================================================================

struct PCCOctree3Node {
  // 3D position of the current node's origin (local x,y,z = 0).
  Vec3<int32_t> pos;
  // 3D position of the current node's origin (local x,y,z = 0) for decoder
  // reconstruction with in-tree geometry quantization.
  Vec3<int32_t> posQ;

  // Range of point indexes spanned by node
  uint32_t start;
  uint32_t end;

  // address of the current node in 3D morton order.
  int64_t mortonIdx;

  // pattern denoting occupied neighbour nodes.
  //    32 8 (y)
  //     |/
  //  2--n--1 (x)
  //    /|
  //   4 16 (z)
  uint8_t neighPattern = 0;

  // The current node's number of siblings plus one.
  // ie, the number of child nodes present in this node's parent.
  uint8_t numSiblingsPlus1;

  // The occupancy map used describing the current node and its siblings.
  uint8_t siblingOccupancy;

  // Indicatest hat the current node qualifies for IDCM
  bool idcmEligible;

  // The qp used for geometry quantisation.
  // NB: this qp value always uses a step size doubling interval of 8 qps
  int8_t qp;

  // angular
  uint8_t laserIndex = 255;
};

//============================================================================

struct OctreeNodePlanar {
  // planar; first bit for x, second bit for y, third bit for z
  uint8_t planarPossible = 7;
  uint8_t planePosBits = 0;
  uint8_t planarMode = 0;
};

//---------------------------------------------------------------------------
uint8_t mapGeometryOccupancy(uint8_t occupancy, uint8_t neighPattern);
uint8_t mapGeometryOccupancyInv(uint8_t occupancy, uint8_t neighPattern);

void updateGeometryNeighState(
  bool siblingRestriction,
  const ringbuf<PCCOctree3Node>::iterator& bufEnd,
  int64_t numNodesNextLvl,
  PCCOctree3Node& child,
  int childIdx,
  uint8_t neighPattern,
  uint8_t parentOccupancy);

//---------------------------------------------------------------------------
// Determine if a node is a leaf node based on size.
// A node with all dimension = 0 is a leaf node.
// NB: some dimensions may be less than zero if coding of that dimension
// has already terminated.

inline bool
isLeafNode(const Vec3<int>& sizeLog2)
{
  return sizeLog2[0] <= 0 && sizeLog2[1] <= 0 && sizeLog2[2] <= 0;
}

//---------------------------------------------------------------------------
// Determine if direct coding is permitted.
// If tool is enabled:
//   - Block must not be near the bottom of the tree
//   - The parent / grandparent are sparsely occupied

inline bool
isDirectModeEligible(
  int intensity,
  int nodeSizeLog2,
  const PCCOctree3Node& node,
  const PCCOctree3Node& child)
{
  if (!intensity)
    return false;

  if (intensity == 1)
    return (nodeSizeLog2 >= 2) && (node.neighPattern == 0)
      && (child.numSiblingsPlus1 == 1) && (node.numSiblingsPlus1 <= 2);

  if (intensity == 2)
    return (nodeSizeLog2 >= 2) && (node.neighPattern == 0);

  // This is basically unconditionally enabled.
  // If a node is that is IDCM-eligible is not coded with IDCM and has only
  // one child, then it is likely that the child would also not be able to
  // be coded with IDCM (eg, it still contains > 2 unique points).
  if (intensity == 3)
    return (nodeSizeLog2 >= 2) && (child.numSiblingsPlus1 > 1);

  return false;
}

//---------------------------------------------------------------------------
// Select the neighbour pattern reduction table according to GPS config.

inline const uint8_t*
neighPattern64toR1(const GeometryParameterSet& gps)
{
  if (gps.neighbour_avail_boundary_log2 > 0)
    return kNeighPattern64to9;
  return kNeighPattern64to6;
}

//---------------------------------------------------------------------------

struct CtxModelOctreeOccupancy {
  AdaptiveBitModelFast contexts[256];
  static const int kCtxFactorShift = 3;

  AdaptiveBitModelFast& operator[](int idx)
  {
    return contexts[idx >> kCtxFactorShift];
  }
};

//---------------------------------------------------------------------------
// Encapsulates the derivation of ctxIdx for occupancy coding.

class CtxMapOctreeOccupancy {
public:
  struct CtxIdxMap {
    uint8_t b0[9];
    uint8_t b1[18];
    uint8_t b2[35];
    uint8_t b3[68];
    uint8_t b4[69];
    uint8_t b5[134];
    uint8_t b6[135];
    uint8_t b7[136];
  };

  CtxMapOctreeOccupancy();
  CtxMapOctreeOccupancy(const CtxMapOctreeOccupancy&);
  CtxMapOctreeOccupancy(CtxMapOctreeOccupancy&&);
  CtxMapOctreeOccupancy& operator=(const CtxMapOctreeOccupancy&);
  CtxMapOctreeOccupancy& operator=(CtxMapOctreeOccupancy&&);

  const uint8_t* operator[](int bit) const { return b[bit]; }

  uint8_t* operator[](int bit) { return b[bit]; }

  // return *ctxIdx and update *ctxIdx according to bit
  static uint8_t evolve(bool bit, uint8_t* ctxIdx);

private:
  std::unique_ptr<CtxIdxMap> map;
  std::array<uint8_t*, 8> b;
};

//----------------------------------------------------------------------------

inline uint8_t
CtxMapOctreeOccupancy::evolve(bool bit, uint8_t* ctxIdx)
{
  uint8_t retval = *ctxIdx;

  if (bit)
    *ctxIdx += kCtxMapOctreeOccupancyDelta[(255 - *ctxIdx) >> 4];
  else
    *ctxIdx -= kCtxMapOctreeOccupancyDelta[*ctxIdx >> 4];

  return retval;
}

//---------------------------------------------------------------------------
// generate an array of node sizes according to subsequent qtbt decisions

std::vector<Vec3<int>> mkQtBtNodeSizeList(
  const GeometryParameterSet& gps,
  const QtBtParameters& qtbt,
  const GeometryBrickHeader& gbh);

//---------------------------------------------------------------------------

inline Vec3<int>
qtBtChildSize(const Vec3<int>& nodeSizeLog2, const Vec3<int>& childSizeLog2)
{
  Vec3<int> bitpos = 0;
  for (int k = 0; k < 3; k++) {
    if (childSizeLog2[k] != nodeSizeLog2[k])
      bitpos[k] = 1 << childSizeLog2[k];
  }
  return bitpos;
}

//---------------------------------------------------------------------------

inline int
nonSplitQtBtAxes(const Vec3<int>& nodeSizeLog2, const Vec3<int>& childSizeLog2)
{
  int indicator = 0;
  for (int k = 0; k < 3; k++) {
    indicator <<= 1;
    indicator |= nodeSizeLog2[k] == childSizeLog2[k];
  }
  return indicator;
}

//============================================================================

class AzimuthalPhiZi {
public:
  AzimuthalPhiZi(int numLasers, const std::vector<int>& numPhi)
    : _delta(numLasers), _invDelta(numLasers)
  {
    for (int laserIndex = 0; laserIndex < numLasers; laserIndex++) {
      constexpr int k2pi = 6588397;  // 2**20 * 2 * pi
      _delta[laserIndex] = k2pi / numPhi[laserIndex];
      _invDelta[laserIndex] =
        int64_t((int64_t(numPhi[laserIndex]) << 30) / k2pi);
    }
  }

  const int delta(size_t idx) const { return _delta[idx]; }
  const int64_t invDelta(size_t idx) const { return _invDelta[idx]; }

private:
  std::vector<int> _delta;
  std::vector<int64_t> _invDelta;
};

//============================================================================

struct OctreePlanarBuffer {
  static constexpr unsigned numBitsC = 14;
  static constexpr unsigned numBitsAb = 7;
  static constexpr unsigned rowSize = 1;
  static_assert(numBitsC >= 0 && numBitsC <= 32, "0 <= numBitsC <= 32");
  static_assert(numBitsAb >= 0 && numBitsAb <= 32, "0 <= numBitsAb <= 32");
  static_assert(rowSize > 0, "rowSize must be greater than 0");
  static constexpr unsigned shiftAb = 1;
  static constexpr int maskAb = ((1 << numBitsAb) - 1) << shiftAb;
  static constexpr int maskC = (1 << numBitsC) - 1;

#pragma pack(push)
#pragma pack(1)
  struct Elmt {
    // (a, b) are (s, t) for planar v,
    //            (s, v) for planar t, and
    //            (t, v) for planar s
    unsigned int a : numBitsAb;

    // -2: not used, -1: not planar, 0: plane 0, 1: plane 1
    int planeIdx : 2;
    unsigned int b : numBitsAb;
  };
#pragma pack(pop)

  typedef Elmt Row[rowSize];

  OctreePlanarBuffer();
  OctreePlanarBuffer(const OctreePlanarBuffer& rhs);
  OctreePlanarBuffer(OctreePlanarBuffer&& rhs);
  ~OctreePlanarBuffer();

  OctreePlanarBuffer& operator=(const OctreePlanarBuffer& rhs);
  OctreePlanarBuffer& operator=(OctreePlanarBuffer&& rhs);

  void resize(Vec3<int> numBufferRows);
  void clear();

  // Access to a particular buffer column (dimension)
  Row* getBuffer(int dim) { return _col[dim]; }

private:
  // Backing storage for the underlying buffer
  std::vector<Elmt> _buf;

  // Base pointers for the first, second and third position components.
  std::array<Row*, 3> _col = {{nullptr, nullptr, nullptr}};
};

//============================================================================

struct OctreePlanarState {
  OctreePlanarState(const GeometryParameterSet&);

  OctreePlanarState(const OctreePlanarState&);
  OctreePlanarState(OctreePlanarState&&);
  OctreePlanarState& operator=(const OctreePlanarState&);
  OctreePlanarState& operator=(OctreePlanarState&&);

  bool _planarBufferEnabled;
  OctreePlanarBuffer _planarBuffer;

  std::array<int, 3> _rate{{128 * 8, 128 * 8, 128 * 8}};
  int _localDensity = 1024 * 4;

  std::array<int, 3> _rateThreshold;

  void initPlanes(const Vec3<int>& planarDepth);
  void updateRate(int occupancy, int numSiblings);
  void isEligible(bool eligible[3]);
};

// determine if a 222 block is planar
void isPlanarNode(
  const PCCPointSet3& pointCloud,
  const PCCOctree3Node& node0,
  const Vec3<int>& sizeLog2,
  uint8_t& planarMode,
  uint8_t& planePosBits,
  const bool planarEligible[3]);

int maskPlanarX(const OctreeNodePlanar& planar, bool activatable);
int maskPlanarY(const OctreeNodePlanar& planar, bool activatable);
int maskPlanarZ(const OctreeNodePlanar& planar, bool activatable);

void
maskPlanar(OctreeNodePlanar& planar, int mask[3], const int occupancySkip);

int determineContextAngleForPlanar(
  PCCOctree3Node& child,
  const Vec3<int>& headPos,
  Vec3<int> childSizeLog2,
  const int* zLaser,
  const int* thetaLaser,
  const int numLasers,
  int deltaAngle,
  const AzimuthalPhiZi& phiZi,
  int* phiBuffer,
  int* contextAnglePhiX,
  int* contextAnglePhiY);
;

//----------------------------------------------------------------------------

int findLaser(point_t point, const int* thetaList, const int numTheta);

//============================================================================

class GeometryOctreeContexts {
public:
  void reset();

protected:
  AdaptiveBitModel _ctxSingleChild;
  AdaptiveBitModel _ctxSinglePointPerBlock;
  AdaptiveBitModel _ctxSingleIdcmDupPoint;
  AdaptiveBitModel _ctxPointCountPerBlock;
  AdaptiveBitModel _ctxBlockSkipTh;
  AdaptiveBitModel _ctxNumIdcmPointsGt1;
  AdaptiveBitModel _ctxSameZ;

  // IDCM unordered
  AdaptiveBitModel _ctxSameBitHighx[5];
  AdaptiveBitModel _ctxSameBitHighy[5];
  AdaptiveBitModel _ctxSameBitHighz[5];

  // residual laser index
  AdaptiveBitModel _ctxThetaResIsZero;
  AdaptiveBitModel _ctxThetaResSign;
  AdaptiveBitModel _ctxThetaResIsOne;
  AdaptiveBitModel _ctxThetaResIsTwo;
  AdaptiveBitModel _ctxThetaResExp;

  AdaptiveBitModel _ctxPhiResIsZero;
  AdaptiveBitModel _ctxPhiSign;
  AdaptiveBitModel _ctxPhiResIsOne;
  AdaptiveBitModel _ctxPhiResIsTwo;
  AdaptiveBitModel _ctxPhiResExp;

  AdaptiveBitModel _ctxQpOffsetIsZero;
  AdaptiveBitModel _ctxQpOffsetSign;
  AdaptiveBitModel _ctxQpOffsetAbsEgl;

  // for planar mode xyz
  AdaptiveBitModel _ctxPlanarMode[3];
  AdaptiveBitModel _ctxPlanarPlaneLastIndex[3][4][6];
  AdaptiveBitModel _ctxPlanarPlaneLastIndexZ[3];
  AdaptiveBitModel _ctxPlanarPlaneLastIndexAngular[4];
  AdaptiveBitModel _ctxPlanarPlaneLastIndexAngularIdcm[4];

  AdaptiveBitModel _ctxPlanarPlaneLastIndexAngularPhi[8];
  AdaptiveBitModel _ctxPlanarPlaneLastIndexAngularPhiIDCM[8];

  // For bitwise occupancy coding
  CtxModelOctreeOccupancy _ctxOccupancy;
  CtxMapOctreeOccupancy _ctxIdxMaps[18];

  // For bytewise occupancy coding
  DualLutCoder<true> _bytewiseOccupancyCoder[10];
};

//----------------------------------------------------------------------------

inline void
GeometryOctreeContexts::reset()
{
  this->~GeometryOctreeContexts();
  new (this) GeometryOctreeContexts;
}

//============================================================================
// :: octree encoder exposing internal ringbuffer

void encodeGeometryOctree(
  const OctreeEncOpts& opt,
  const GeometryParameterSet& gps,
  GeometryBrickHeader& gbh,
  PCCPointSet3& pointCloud,
  GeometryOctreeContexts& ctxtMem,
  std::vector<std::unique_ptr<EntropyEncoder>>& arithmeticEncoders,
  pcc::ringbuf<PCCOctree3Node>* nodesRemaining);

void decodeGeometryOctree(
  const GeometryParameterSet& gps,
  const GeometryBrickHeader& gbh,
  int skipLastLayers,
  PCCPointSet3& pointCloud,
  GeometryOctreeContexts& ctxtMem,
  std::vector<std::unique_ptr<EntropyDecoder>>& arithmeticDecoders,
  pcc::ringbuf<PCCOctree3Node>* nodesRemaining);

//============================================================================

}  // namespace pcc
