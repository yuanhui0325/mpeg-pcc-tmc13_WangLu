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

#include "geometry_octree.h"

#include <algorithm>
#include <iterator>

#include "PCCMisc.h"
#include "geometry_params.h"
#include "tables.h"

namespace pcc {

//============================================================================

Vec3<int>
oneQtBtDecision(
  const QtBtParameters& qtbt,
  Vec3<int> nodeSizeLog2,
  int maxNumQtbtBeforeOt,
  int minDepthQtbt)
{
  int maxNodeMinDimLog2ToSplitZ = qtbt.angularMaxNodeMinDimLog2ToSplitV;
  int maxDiffToSplitZ = qtbt.angularMaxDiffToSplitZ;
  int nodeMinDimLog2 = nodeSizeLog2.min();

  if (maxNumQtbtBeforeOt || nodeMinDimLog2 == minDepthQtbt) {
    int nodeMaxDimLog2 = nodeSizeLog2.max();
    for (int k = 0; k < 3; k++) {
      if (nodeSizeLog2[k] == nodeMaxDimLog2)
        nodeSizeLog2[k]--;
    }
  } else if (
    qtbt.angularTweakEnabled && minDepthQtbt >= 0
    && nodeSizeLog2[2] <= maxNodeMinDimLog2ToSplitZ
    && (maxNodeMinDimLog2ToSplitZ + maxDiffToSplitZ > 0)) {
    // do not split z
    int nodeXYMaxDimLog2 = std::max(nodeSizeLog2[0], nodeSizeLog2[1]);
    for (int k = 0; k < 2; k++) {
      if (nodeSizeLog2[k] == nodeXYMaxDimLog2)
        nodeSizeLog2[k]--;
    }
    if (
      (nodeMinDimLog2 <= maxNodeMinDimLog2ToSplitZ
       && nodeSizeLog2[2] >= nodeXYMaxDimLog2 + maxDiffToSplitZ)
      || (nodeXYMaxDimLog2 >= maxNodeMinDimLog2ToSplitZ + maxDiffToSplitZ
          && nodeSizeLog2[2] >= nodeXYMaxDimLog2))
      nodeSizeLog2[2]--;
  } else  // octree partition
    nodeSizeLog2 = nodeSizeLog2 - 1;

  return nodeSizeLog2;
}

//---------------------------------------------------------------------------

void
updateQtBtParameters(
  const Vec3<int>& nodeSizeLog2,
  bool trisoup_enabled_flag,
  int* maxNumQtbtBeforeOt,
  int* minSizeQtbt)
{
  int nodeMinDimLog2 = nodeSizeLog2.min();
  int nodeMaxDimLog2 = nodeSizeLog2.max();

  // max number of qtbt partitions before ot is bounded by difference between
  // max and min node size
  if (*maxNumQtbtBeforeOt > (nodeMaxDimLog2 - nodeMinDimLog2))
    *maxNumQtbtBeforeOt = nodeMaxDimLog2 - nodeMinDimLog2;
  // min depth of qtbt partition is bounded by min node size
  if (*minSizeQtbt > nodeMinDimLog2)
    *minSizeQtbt = nodeMinDimLog2;
  // if all dimensions have same size, min depth of qtbt should be 0
  if (nodeMaxDimLog2 == nodeMinDimLog2) {
    *minSizeQtbt = 0;
  }

  // if trisoup is enabled, perform qtbt first before ot
  if (trisoup_enabled_flag) {
    *maxNumQtbtBeforeOt = nodeMaxDimLog2 - nodeMinDimLog2;
    *minSizeQtbt = 0;
  }
}

//---------------------------------------------------------------------------

std::vector<Vec3<int>>
mkQtBtNodeSizeList(
  const GeometryParameterSet& gps,
  const QtBtParameters& qtbt,
  const GeometryBrickHeader& gbh)
{
  std::vector<Vec3<int>> nodeSizeLog2List;

  // size of the current node (each dimension can vary due to qtbt)
  Vec3<int> nodeSizeLog2 = gbh.rootNodeSizeLog2;
  nodeSizeLog2List.push_back(nodeSizeLog2);

  // update qtbt parameters
  int maxNumQtbtBeforeOt = qtbt.maxNumQtBtBeforeOt;
  int minSizeQtbt = qtbt.minQtbtSizeLog2;
  updateQtBtParameters(
    nodeSizeLog2, qtbt.trisoupEnabled, &maxNumQtbtBeforeOt, &minSizeQtbt);

  while (!isLeafNode(nodeSizeLog2)) {
    if (!gps.qtbt_enabled_flag)
      nodeSizeLog2 -= 1;
    else
      nodeSizeLog2 =
        oneQtBtDecision(qtbt, nodeSizeLog2, maxNumQtbtBeforeOt, minSizeQtbt);

    nodeSizeLog2List.push_back(nodeSizeLog2);

    if (maxNumQtbtBeforeOt)
      maxNumQtbtBeforeOt--;

    // if all dimensions have same size, then use octree for remaining nodes
    if (
      nodeSizeLog2[0] == minSizeQtbt && nodeSizeLog2[0] == nodeSizeLog2[1]
      && nodeSizeLog2[1] == nodeSizeLog2[2])
      minSizeQtbt = -1;
  }

  return nodeSizeLog2List;
}

//-------------------------------------------------------------------------
// map the @occupancy pattern bits to take into account symmetries in the
// neighbour configuration @neighPattern.
//
uint8_t
mapGeometryOccupancy(uint8_t occupancy, uint8_t neighPattern)
{
  switch (kOccMapRotateZIdFromPatternXY[neighPattern & 15]) {
  case 1: occupancy = kOccMapRotateZ090[occupancy]; break;
  case 2: occupancy = kOccMapRotateZ180[occupancy]; break;
  case 3: occupancy = kOccMapRotateZ270[occupancy]; break;
  }

  bool flag_ud = (neighPattern & 16) && !(neighPattern & 32);
  if (flag_ud) {
    occupancy = kOccMapMirrorXY[occupancy];
  }

  if (kOccMapRotateYIdFromPattern[neighPattern]) {
    occupancy = kOccMapRotateY270[occupancy];
  }

  switch (kOccMapRotateXIdFromPattern[neighPattern]) {
  case 1: occupancy = kOccMapRotateX090[occupancy]; break;
  case 2: occupancy = kOccMapRotateX270Y180[occupancy]; break;
  case 3: occupancy = kOccMapRotateX090Y180[occupancy]; break;
  }

  return occupancy;
}

//-------------------------------------------------------------------------
// map the @occupancy pattern bits to take into account symmetries in the
// neighbour configuration @neighPattern.
//
uint8_t
mapGeometryOccupancyInv(uint8_t occupancy, uint8_t neighPattern)
{
  switch (kOccMapRotateXIdFromPattern[neighPattern]) {
  case 1: occupancy = kOccMapRotateX270[occupancy]; break;
  case 2: occupancy = kOccMapRotateX270Y180[occupancy]; break;
  case 3: occupancy = kOccMapRotateX090Y180[occupancy]; break;
  }

  if (kOccMapRotateYIdFromPattern[neighPattern]) {
    occupancy = kOccMapRotateY090[occupancy];
  }

  bool flag_ud = (neighPattern & 16) && !(neighPattern & 32);
  if (flag_ud) {
    occupancy = kOccMapMirrorXY[occupancy];
  }

  switch (kOccMapRotateZIdFromPatternXY[neighPattern & 15]) {
  case 1: occupancy = kOccMapRotateZ270[occupancy]; break;
  case 2: occupancy = kOccMapRotateZ180[occupancy]; break;
  case 3: occupancy = kOccMapRotateZ090[occupancy]; break;
  }

  return occupancy;
}

//============================================================================
// Update the neighbour pattern flags for a node and the 'left' neighbour on
// each axis.  This update should be applied to each newly inserted node.
//
// @param siblingRestriction limits neighbours to direct siblings of child

void
updateGeometryNeighState(
  bool siblingRestriction,
  const ringbuf<PCCOctree3Node>::iterator& bufEnd,
  int64_t numNodesNextLvl,
  PCCOctree3Node& child,
  int childIdx,
  uint8_t neighPattern,
  uint8_t parentOccupancy)
{
  int64_t midx;
  if (!siblingRestriction) {
    midx = child.mortonIdx = mortonAddr(child.pos);
  }

  static const struct {
    int childIdxBitPos;
    int axis;
    int patternFlagUs;
    int patternFlagThem;
  } neighParamMap[] = {
    {4, 2, 1 << 1, 1 << 0},  // x
    {2, 1, 1 << 2, 1 << 3},  // y
    {1, 0, 1 << 4, 1 << 5},  // z
  };

  for (const auto& param : neighParamMap) {
    // skip expensive check if parent's flags indicate adjacent neighbour
    // is not present.
    if ((childIdx & param.childIdxBitPos) == 0) {
      // $axis co-ordinate = 0
      if (parentOccupancy & (1 << (childIdx + param.childIdxBitPos)))
        child.neighPattern |= param.patternFlagThem;

      if (!(neighPattern & param.patternFlagUs))
        continue;
    } else {
      if (parentOccupancy & (1 << (childIdx - param.childIdxBitPos)))
        child.neighPattern |= param.patternFlagUs;

      // no external search is required for $axis co-ordinate = 1
      continue;
    }

    if (siblingRestriction)
      continue;

    // calculate the morton address of the 'left' neighbour,
    // the delta is then used as the starting position for a search
    int64_t mortonIdxNeigh =
      morton3dAxisDec(midx, param.axis) & ~0x8000000000000000ull;
    int64_t mortonDelta = midx - mortonIdxNeigh;

    if (mortonDelta < 0) {
      // no neighbour due to being in zero-th col/row/plane
      continue;
    }

    // NB: fifo already contains current node, no point searching it
    auto posEnd = bufEnd;
    std::advance(posEnd, -1);

    auto posStart = bufEnd;
    std::advance(posStart, -std::min(numNodesNextLvl, mortonDelta + 2));

    auto found = std::lower_bound(
      posStart, posEnd, mortonIdxNeigh,
      [](const PCCOctree3Node& node, int64_t mortonIdx) {
        return node.mortonIdx < mortonIdx;
      });

    // NB: found is always valid (see posEnd) => can skip check.
    if (found->mortonIdx != mortonIdxNeigh) {
      // neighbour isn't present => must have been empty
      continue;
    }

    // update both node's neighbour pattern
    // NB: neighours being present implies occupancy
    child.neighPattern |= param.patternFlagUs;
    found->neighPattern |= param.patternFlagThem;
  }
}

//============================================================================

CtxMapOctreeOccupancy::CtxMapOctreeOccupancy(const CtxMapOctreeOccupancy& rhs)
  : CtxMapOctreeOccupancy()
{
  *this->map = *rhs.map;
}

//----------------------------------------------------------------------------

CtxMapOctreeOccupancy::CtxMapOctreeOccupancy(CtxMapOctreeOccupancy&& rhs)
{
  std::swap(this->map, rhs.map);
  std::swap(this->b, rhs.b);
}

//----------------------------------------------------------------------------

CtxMapOctreeOccupancy&
CtxMapOctreeOccupancy::operator=(const CtxMapOctreeOccupancy& rhs)
{
  *this->map = *rhs.map;
  return *this;
}

//----------------------------------------------------------------------------

CtxMapOctreeOccupancy&
CtxMapOctreeOccupancy::operator=(CtxMapOctreeOccupancy&& rhs)
{
  std::swap(this->map, rhs.map);
  std::swap(this->b, rhs.b);
  return *this;
}

//----------------------------------------------------------------------------

CtxMapOctreeOccupancy::CtxMapOctreeOccupancy()
{
  map.reset(new CtxIdxMap);
  b[0] = map->b0;
  b[1] = map->b1;
  b[2] = map->b2;
  b[3] = map->b3;
  b[4] = map->b4;
  b[5] = map->b5;
  b[6] = map->b6;
  b[7] = map->b7;

  using namespace std;
  fill(begin(map->b0), end(map->b0), 127);
  fill(begin(map->b1), end(map->b1), 127);
  fill(begin(map->b2), end(map->b2), 127);
  fill(begin(map->b3), end(map->b3), 127);
  fill(begin(map->b4), end(map->b4), 127);
  fill(begin(map->b5), end(map->b5), 127);
  fill(begin(map->b6), end(map->b6), 127);
  fill(begin(map->b7), end(map->b7), 127);
}

//============================================================================
// determine if a 222 block is planar

void
isPlanarNode(
  const PCCPointSet3& pointCloud,
  const PCCOctree3Node& node0,
  const Vec3<int>& nodeSizeLog2Minus1,
  uint8_t& planarMode,
  uint8_t& planePosBits,
  const bool planarEligible[3])
{
  const point_t occupMask{planarEligible[0] << nodeSizeLog2Minus1[0],
                          planarEligible[1] << nodeSizeLog2Minus1[1],
                          planarEligible[2] << nodeSizeLog2Minus1[2]};

  point_t occup = 0;
  // find occupancy N xyz-planes
  for (int k = node0.start; k < node0.end; k++) {
    occup[0] |= planarEligible[0] << bool(pointCloud[k][0] & occupMask[0]);
    occup[1] |= planarEligible[1] << bool(pointCloud[k][1] & occupMask[1]);
    occup[2] |= planarEligible[2] << bool(pointCloud[k][2] & occupMask[2]);
  }

  // determine planar
  planarMode =
    (occup[0] != 3) | ((occup[1] != 3) << 1) | ((occup[2] != 3) << 2);
  planePosBits =
    (occup[0] == 2) | ((occup[1] == 2) << 1) | ((occup[2] == 2) << 2);
}

//============================================================================
// :: Default planar buffer methods

OctreePlanarBuffer::OctreePlanarBuffer() = default;
OctreePlanarBuffer::OctreePlanarBuffer(OctreePlanarBuffer&& rhs) = default;
OctreePlanarBuffer::~OctreePlanarBuffer() = default;

OctreePlanarBuffer& OctreePlanarBuffer::
operator=(OctreePlanarBuffer&& rhs) = default;

//----------------------------------------------------------------------------
// :: Copying the planar buffer

OctreePlanarBuffer::OctreePlanarBuffer(const OctreePlanarBuffer& rhs)
{
  *this = rhs;
}

//----------------------------------------------------------------------------

OctreePlanarBuffer&
OctreePlanarBuffer::operator=(const OctreePlanarBuffer& rhs)
{
  _buf = rhs._buf;
  _col = rhs._col;
  // Afjust the column offsets to the new base address
  auto oldBase = _col[0];
  auto newBase = reinterpret_cast<Row*>(&_buf.front());
  for (auto& ptr : _col)
    ptr = ptr - oldBase + newBase;
  return *this;
}

//----------------------------------------------------------------------------
// :: Planar buffer management

void
OctreePlanarBuffer::resize(Vec3<int> numBufferRows)
{
  if (maskC < numBufferRows[0])
    numBufferRows[0] = maskC + 1;
  if (maskC < numBufferRows[1])
    numBufferRows[1] = maskC + 1;
  if (maskC < numBufferRows[2])
    numBufferRows[2] = maskC + 1;

  // NB: based upon the expected max buffer size of 32k, just allocate the
  //     maximum buffer size.
  int size = numBufferRows[0] + numBufferRows[1] + numBufferRows[2];
  _buf.clear();
  _buf.reserve(rowSize * 3 * (maskC + 1));
  _buf.resize(rowSize * size, Elmt{0, -2, 0});

  // NB: the flat backing buffer is cast with a row stride for access
  _col[0] = reinterpret_cast<Row*>(&_buf.front());
  _col[1] = _col[0] + numBufferRows[0];
  _col[2] = _col[1] + numBufferRows[1];
}

//----------------------------------------------------------------------------

void
OctreePlanarBuffer::clear()
{
  _buf.clear();
  _col = {nullptr, nullptr, nullptr};
}

//============================================================================
// intitialize planes for planar pred

OctreePlanarState::OctreePlanarState(const GeometryParameterSet& gps)
{
  _planarBufferEnabled =
    gps.geom_planar_mode_enabled_flag && !gps.planar_buffer_disabled_flag;

  _rateThreshold[0] = gps.geom_planar_threshold0 << 4;
  _rateThreshold[1] = gps.geom_planar_threshold1 << 4;
  _rateThreshold[2] = gps.geom_planar_threshold2 << 4;
}

void
OctreePlanarState::initPlanes(const Vec3<int>& depthXyz)
{
  if (!_planarBufferEnabled)
    return;

  Vec3<int> numBufferRows = {1 << depthXyz[0], 1 << depthXyz[1],
                             1 << depthXyz[2]};
  _planarBuffer.resize(numBufferRows);
}

//============================================================================
// update the plane rate depending on the occupancy

void
OctreePlanarState::updateRate(int occupancy, int numSiblings)
{
  bool isPlanarX = !((occupancy & 0xf0) && (occupancy & 0x0f));
  bool isPlanarY = !((occupancy & 0xcc) && (occupancy & 0x33));
  bool isPlanarZ = !((occupancy & 0x55) && (occupancy & 0xaa));

  _rate[0] = (255 * _rate[0] + (isPlanarX ? 256 * 8 : 0) + 128) >> 8;
  _rate[1] = (255 * _rate[1] + (isPlanarY ? 256 * 8 : 0) + 128) >> 8;
  _rate[2] = (255 * _rate[2] + (isPlanarZ ? 256 * 8 : 0) + 128) >> 8;

  _localDensity = (255 * _localDensity + 1024 * numSiblings) >> 8;
}

//============================================================================
// planar eligbility

void
OctreePlanarState::isEligible(bool eligible[3])
{
  eligible[0] = false;
  eligible[1] = false;
  eligible[2] = false;
  if (_localDensity >= 3 * 1024) {
    return;
  }

  if (_rate[0] >= _rate[1] && _rate[0] >= _rate[2]) {
    // planar x dominates
    eligible[0] = _rate[0] >= _rateThreshold[0];
    if (_rate[1] >= _rate[2]) {
      eligible[1] = _rate[1] >= _rateThreshold[1];
      eligible[2] = _rate[2] >= _rateThreshold[2];
    } else {
      eligible[2] = _rate[2] >= _rateThreshold[1];
      eligible[1] = _rate[1] >= _rateThreshold[2];
    }
  } else if (_rate[1] >= _rate[0] && _rate[1] >= _rate[2]) {
    // planar y dominates
    eligible[1] = _rate[1] >= _rateThreshold[0];
    if (_rate[0] >= _rate[2]) {
      eligible[0] = _rate[0] >= _rateThreshold[1];
      eligible[2] = _rate[2] >= _rateThreshold[2];
    } else {
      eligible[2] = _rate[2] >= _rateThreshold[1];
      eligible[0] = _rate[0] >= _rateThreshold[2];
    }
  } else if (_rate[2] >= _rate[0] && _rate[2] >= _rate[1]) {
    // planar z dominates
    eligible[2] = _rate[2] >= _rateThreshold[0];
    if (_rate[0] >= _rate[1]) {
      eligible[0] = _rate[0] >= _rateThreshold[1];
      eligible[1] = _rate[1] >= _rateThreshold[2];
    } else {
      eligible[1] = _rate[1] >= _rateThreshold[1];
      eligible[0] = _rate[0] >= _rateThreshold[2];
    }
  }
}

//----------------------------------------------------------------------------

OctreePlanarState::OctreePlanarState(const OctreePlanarState& rhs)
{
  *this = rhs;
}

//----------------------------------------------------------------------------

OctreePlanarState::OctreePlanarState(OctreePlanarState&& rhs)
{
  *this = std::move(rhs);
}

//----------------------------------------------------------------------------

OctreePlanarState&
OctreePlanarState::operator=(const OctreePlanarState& rhs)
{
  _planarBuffer = rhs._planarBuffer;
  _rate = rhs._rate;
  _localDensity = rhs._localDensity;
  _rateThreshold = rhs._rateThreshold;
  return *this;
}

//----------------------------------------------------------------------------

OctreePlanarState&
OctreePlanarState::operator=(OctreePlanarState&& rhs)
{
  _planarBuffer = std::move(rhs._planarBuffer);
  _rate = std::move(rhs._rateThreshold);
  _localDensity = std::move(rhs._localDensity);
  _rateThreshold = std::move(rhs._rateThreshold);
  return *this;
}

//============================================================================
// directional mask depending on the planarity

int
maskPlanarX(const OctreeNodePlanar& planar, bool implicitSkip)
{
  if (implicitSkip)
    return 0xf0;

  if ((planar.planarMode & 1) == 0)
    return 0;

  return (planar.planePosBits & 1) ? 0x0f : 0xf0;
}

//----------------------------------------------------------------------------

int
maskPlanarY(const OctreeNodePlanar& planar, bool implicitSkip)
{
  if (implicitSkip)
    return 0xcc;

  if ((planar.planarMode & 2) == 0)
    return 0;

  return (planar.planePosBits & 2) ? 0x33 : 0xcc;
}

//----------------------------------------------------------------------------

int
maskPlanarZ(const OctreeNodePlanar& planar, bool implicitSkip)
{
  // QTBT does not split in this direction
  //   => infer the mask low for occupancy bit coding
  if (implicitSkip)
    return 0xaa;

  if ((planar.planarMode & 4) == 0)
    return 0;

  return (planar.planePosBits & 4) ? 0x55 : 0xaa;
}

//----------------------------------------------------------------------------

// three direction mask
void
maskPlanar(OctreeNodePlanar& planar, int mask[3], const int occupancySkip)
{
  static const uint8_t kPossibleMask[3] = {6, 5, 3};
  for (int k = 0; k <= 2; k++)
    if (occupancySkip & (4 >> k)) {
      planar.planarPossible = planar.planarPossible | (1 << k);
      planar.planePosBits = planar.planePosBits & kPossibleMask[k];
      planar.planarMode = planar.planarMode | (1 << k);
    }

  mask[0] = maskPlanarX(planar, occupancySkip & 4);
  mask[1] = maskPlanarY(planar, occupancySkip & 2);
  mask[2] = maskPlanarZ(planar, occupancySkip & 1);
}

//----------------------------------------------------------------------------
// determine angular context for planar integer implementation.

int
determineContextAngleForPlanar(
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
  int* contextAnglePhiY)
{
  Vec3<int64_t> absPos = {child.pos[0] << childSizeLog2[0],
                          child.pos[1] << childSizeLog2[1],
                          child.pos[2] << childSizeLog2[2]};

  // eligibility
  Vec3<int64_t> midNode = {1 << (childSizeLog2[0] ? childSizeLog2[0] - 1 : 0),
                           1 << (childSizeLog2[1] ? childSizeLog2[1] - 1 : 0),
                           1 << (childSizeLog2[2] ? childSizeLog2[2] - 1 : 0)};
  uint64_t xLidar =
    std::abs(((absPos[0] - headPos[0] + midNode[0]) << 8) - 128);
  uint64_t yLidar =
    std::abs(((absPos[1] - headPos[1] + midNode[1]) << 8) - 128);

  uint64_t rL1 = (xLidar + yLidar) >> 1;
  uint64_t deltaAngleR = deltaAngle * rL1;
  if (deltaAngleR <= (midNode[2] << 26))
    return -1;

  // determine inverse of r  (1/sqrt(r2) = irsqrt(r2))
  uint64_t r2 = xLidar * xLidar + yLidar * yLidar;
  uint64_t rInv = irsqrt(r2);

  // determine non-corrected theta
  int64_t zLidar = ((absPos[2] - headPos[2] + midNode[2]) << 1) - 1;
  int64_t theta = zLidar * rInv;
  int theta32 = theta >= 0 ? theta >> 15 : -((-theta) >> 15);

  // determine laser
  int laserIndex = int(child.laserIndex);
  if (laserIndex == 255 || deltaAngleR <= (midNode[2] << (26 + 2))) {
    auto end = thetaLaser + numLasers - 1;
    auto it = std::upper_bound(thetaLaser + 1, end, theta32);
    if (theta32 - *std::prev(it) <= *it - theta32)
      --it;

    laserIndex = std::distance(thetaLaser, it);
    child.laserIndex = uint8_t(laserIndex);
  }

  // -- PHI  --
  //angles
  int posx = absPos[0] - headPos[0];
  int posy = absPos[1] - headPos[1];
  int phiNode = iatan2(posy + midNode[1], posx + midNode[0]);
  int phiNode0 = iatan2(posy, posx);

  // find predictor
  int predPhi = phiBuffer[laserIndex];
  if (predPhi == 0x80000000)
    predPhi = phiNode;

  // use predictor
  if (predPhi != 0x80000000) {
    // elementary shift predictor
    int Nshift =
      ((predPhi - phiNode) * phiZi.invDelta(laserIndex) + 536870912) >> 30;
    predPhi -= phiZi.delta(laserIndex) * Nshift;

    // ctx azimutal x or y
    int angleL = phiNode0 - predPhi;
    int angleR = phiNode - predPhi;
    int contextAnglePhi =
      (angleL >= 0 && angleR >= 0) || (angleL < 0 && angleR < 0) ? 2 : 0;
    angleL = std::abs(angleL);
    angleR = std::abs(angleR);
    if (angleL > angleR) {
      contextAnglePhi++;
      int temp = angleL;
      angleL = angleR;
      angleR = temp;
    }
    if (angleR > (angleL << 2))
      contextAnglePhi += 4;

    if (std::abs(posx) <= std::abs(posy))
      *contextAnglePhiX = contextAnglePhi;
    else
      *contextAnglePhiY = contextAnglePhi;
  }

  // -- THETA --
  int thetaLaserDelta = thetaLaser[laserIndex] - theta32;
  int64_t hr = zLaser[laserIndex] * rInv;
  thetaLaserDelta += hr >= 0 ? -(hr >> 17) : ((-hr) >> 17);

  int64_t zShift = (rInv << childSizeLog2[2]) >> 20;
  int thetaLaserDeltaBot = thetaLaserDelta + zShift;
  int thetaLaserDeltaTop = thetaLaserDelta - zShift;
  int contextAngle = thetaLaserDelta >= 0 ? 0 : 1;
  if (thetaLaserDeltaTop >= 0)
    contextAngle += 2;
  else if (thetaLaserDeltaBot < 0)
    contextAngle += 2;

  return contextAngle;
}

//============================================================================

int
findLaser(pcc::point_t point, const int* thetaList, const int numTheta)
{
  int64_t xLidar = int64_t(point[0]) << 8;
  int64_t yLidar = int64_t(point[1]) << 8;
  int64_t rInv = irsqrt(xLidar * xLidar + yLidar * yLidar);
  int theta32 = (point[2] * rInv) >> 14;

  auto end = thetaList + numTheta - 1;
  auto it = std::upper_bound(thetaList + 1, end, theta32);
  if (theta32 - *std::prev(it) <= *it - theta32)
    --it;

  return std::distance(thetaList, it);
}

//============================================================================

}  // namespace pcc
