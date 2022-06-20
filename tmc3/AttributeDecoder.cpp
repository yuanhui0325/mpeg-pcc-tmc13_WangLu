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

#include "AttributeDecoder.h"
#include "colourspace.h"
#include "AttributeCommon.h"
#include "DualLutCoder.h"
#include "constants.h"
#include "entropy.h"
#include "io_hls.h"
#include "RAHT.h"
#include "FixedPoint.h"

namespace pcc {

//============================================================================
// An encapsulation of the entropy decoding methods used in attribute coding

class PCCResidualsDecoder : protected AttributeContexts {
public:
  PCCResidualsDecoder(
    const AttributeBrickHeader& abh, const AttributeContexts& ctxtMem);

  EntropyDecoder arithmeticDecoder;

  const AttributeContexts& getCtx() const { return *this; }

  void start(const SequenceParameterSet& sps, const char* buf, int buf_len);
  void stop();

  std::vector<int8_t> decodeLastCompPredCoeffs(int numLods);
  int decodePredMode(int max);
  int decodeRunLength();
  int decodeSymbol(int k1, int k2, int k3);
  void decode(int32_t values[3]);
  int32_t decode();
};

//----------------------------------------------------------------------------

PCCResidualsDecoder::PCCResidualsDecoder(
  const AttributeBrickHeader& abh, const AttributeContexts& ctxtMem)
  : AttributeContexts(ctxtMem)
{}

//----------------------------------------------------------------------------

void
PCCResidualsDecoder::start(
  const SequenceParameterSet& sps, const char* buf, int buf_len)
{
  arithmeticDecoder.setBuffer(buf_len, buf);
  arithmeticDecoder.enableBypassStream(sps.cabac_bypass_stream_enabled_flag);
  arithmeticDecoder.start();
}

//----------------------------------------------------------------------------

void
PCCResidualsDecoder::stop()
{
  arithmeticDecoder.stop();
}

//----------------------------------------------------------------------------

std::vector<int8_t>
PCCResidualsDecoder::decodeLastCompPredCoeffs(int numLods)
{
  // todo: should this be in the slice header?
  std::vector<int8_t> coeffs(numLods, 0);
  for (int lod = 0; lod < numLods; lod++) {
    bool last_comp_pred_coeff_ne0 = arithmeticDecoder.decode();
    if (last_comp_pred_coeff_ne0) {
      bool last_comp_pred_coeff_sign = arithmeticDecoder.decode();
      coeffs[lod] = 1 - 2 * last_comp_pred_coeff_sign;
    }
  }

  return coeffs;
}

//----------------------------------------------------------------------------

int
PCCResidualsDecoder::decodePredMode(int maxMode)
{
  int mode = 0;

  if (maxMode == 0)
    return mode;

  int ctxIdx = 0;
  while (arithmeticDecoder.decode(ctxPredMode[ctxIdx])) {
    ctxIdx = 1;
    mode++;
    if (mode == maxMode)
      break;
  }

  return mode;
}

//----------------------------------------------------------------------------

int
PCCResidualsDecoder::decodeRunLength()
{
  int runLength = 0;
  auto* ctx = ctxRunLen;
  for (; runLength < 3; runLength++, ctx++) {
    int bin = arithmeticDecoder.decode(*ctx);
    if (!bin)
      return runLength;
  }

  for (int i = 0; i < 4; i++) {
    int bin = arithmeticDecoder.decode(*ctx);
    if (!bin) {
      runLength += arithmeticDecoder.decode();
      return runLength;
    }
    runLength += 2;
  }

  runLength += arithmeticDecoder.decodeExpGolomb(2, *++ctx);
  return runLength;
}

//----------------------------------------------------------------------------

int
PCCResidualsDecoder::decodeSymbol(int k1, int k2, int k3)
{
  if (arithmeticDecoder.decode(ctxCoeffEqN[0][k1]))
    return 0;

  if (arithmeticDecoder.decode(ctxCoeffEqN[1][k2]))
    return 1;

  int coeff_abs_minus2 = arithmeticDecoder.decodeExpGolomb(
    1, ctxCoeffRemPrefix[k3], ctxCoeffRemSuffix[k3]);

  return coeff_abs_minus2 + 2;
}

//----------------------------------------------------------------------------

void
PCCResidualsDecoder::decode(int32_t value[3])
{
  value[1] = decodeSymbol(0, 0, 1);
  int b0 = value[1] == 0;
  int b1 = value[1] <= 1;
  value[2] = decodeSymbol(1 + b0, 1 + b1, 1);
  int b2 = value[2] == 0;
  int b3 = value[2] <= 1;
  value[0] = decodeSymbol(3 + (b0 << 1) + b2, 3 + (b1 << 1) + b3, 0);

  if (b0 && b2)
    value[0] += 1;

  if (value[0] && arithmeticDecoder.decode())
    value[0] = -value[0];
  if (value[1] && arithmeticDecoder.decode())
    value[1] = -value[1];
  if (value[2] && arithmeticDecoder.decode())
    value[2] = -value[2];
}

//----------------------------------------------------------------------------

int32_t
PCCResidualsDecoder::decode()
{
  auto mag = decodeSymbol(0, 0, 0) + 1;
  bool sign = arithmeticDecoder.decode();
  return sign ? -mag : mag;
}

//============================================================================
// AttributeDecoderIntf

AttributeDecoderIntf::~AttributeDecoderIntf() = default;

//============================================================================
// AttributeDecoder factory

std::unique_ptr<AttributeDecoderIntf>
makeAttributeDecoder()
{
  return std::unique_ptr<AttributeDecoder>(new AttributeDecoder());
}

//============================================================================
// AttributeDecoder Members

void
AttributeDecoder::decode(
  const SequenceParameterSet& sps,
  const AttributeDescription& attr_desc,
  const AttributeParameterSet& attr_aps,
  const AttributeBrickHeader& abh,
  int geom_num_points_minus1,
  int minGeomNodeSizeLog2,
  const char* payload,
  size_t payloadLen,
  AttributeContexts& ctxtMem,
  PCCPointSet3& pointCloud)
{
  QpSet qpSet = deriveQpSet(attr_desc, attr_aps, abh);

  PCCResidualsDecoder decoder(abh, ctxtMem);
  decoder.start(sps, payload, payloadLen);

  // generate LoDs if necessary
  if (attr_aps.lodParametersPresent() && _lods.empty())
    _lods.generate(
      attr_aps, abh, geom_num_points_minus1, minGeomNodeSizeLog2, pointCloud);

  if (attr_desc.attr_num_dimensions_minus1 == 0) {
    switch (attr_aps.attr_encoding) {
    case AttributeEncoding::kRAHTransform:
      decodeReflectancesRaht(attr_desc, attr_aps, qpSet, decoder, pointCloud);
      break;

    case AttributeEncoding::kPredictingTransform:
      decodeReflectancesPred(attr_desc, attr_aps, qpSet, decoder, pointCloud);
      break;

    case AttributeEncoding::kLiftingTransform:
      decodeReflectancesLift(
        attr_desc, attr_aps, qpSet, geom_num_points_minus1,
        minGeomNodeSizeLog2, decoder, pointCloud);
      break;
    }
  } else if (attr_desc.attr_num_dimensions_minus1 == 2) {
    switch (attr_aps.attr_encoding) {
    case AttributeEncoding::kRAHTransform:
      decodeColorsRaht(attr_desc, attr_aps, qpSet, decoder, pointCloud);
      break;

    case AttributeEncoding::kPredictingTransform:
      decodeColorsPred(attr_desc, attr_aps, qpSet, decoder, pointCloud);
      break;

    case AttributeEncoding::kLiftingTransform:
      decodeColorsLift(
        attr_desc, attr_aps, qpSet, geom_num_points_minus1,
        minGeomNodeSizeLog2, decoder, pointCloud);
      break;
    }
  } else {
    assert(
      attr_desc.attr_num_dimensions_minus1 == 0
      || attr_desc.attr_num_dimensions_minus1 == 2);
  }

  decoder.stop();

  // save the context state for re-use by a future slice if required
  ctxtMem = decoder.getCtx();
}

//----------------------------------------------------------------------------

bool
AttributeDecoder::isReusable(
  const AttributeParameterSet& aps, const AttributeBrickHeader& abh) const
{
  return _lods.isReusable(aps, abh);
}

//----------------------------------------------------------------------------

void
AttributeDecoder::computeReflectancePredictionWeights(
  const AttributeParameterSet& aps,
  const PCCPointSet3& pointCloud,
  const std::vector<uint32_t>& indexes,
  PCCPredictor& predictor,
  PCCResidualsDecoder& decoder)
{
  predictor.predMode = 0;
  int64_t maxDiff = 0;

  if (predictor.neighborCount > 1 && aps.max_num_direct_predictors) {
    int64_t minValue = 0;
    int64_t maxValue = 0;
    for (int i = 0; i < predictor.neighborCount; ++i) {
      const attr_t reflectanceNeighbor = pointCloud.getReflectance(
        indexes[predictor.neighbors[i].predictorIndex]);
      if (i == 0 || reflectanceNeighbor < minValue) {
        minValue = reflectanceNeighbor;
      }
      if (i == 0 || reflectanceNeighbor > maxValue) {
        maxValue = reflectanceNeighbor;
      }
    }
    maxDiff = maxValue - minValue;
  }

  if (maxDiff >= aps.adaptive_prediction_threshold) {
    predictor.predMode = decoder.decodePredMode(aps.max_num_direct_predictors);
  }
}

//----------------------------------------------------------------------------

void
AttributeDecoder::decodeReflectancesPred(
  const AttributeDescription& desc,
  const AttributeParameterSet& aps,
  const QpSet& qpSet,
  PCCResidualsDecoder& decoder,
  PCCPointSet3& pointCloud)
{
  const size_t pointCount = pointCloud.getPointCount();
  const int64_t maxReflectance = (1ll << desc.bitdepth) - 1;
  int zero_cnt = decoder.decodeRunLength();
  int quantLayer = 0;
  for (size_t predictorIndex = 0; predictorIndex < pointCount;
       ++predictorIndex) {
    if (predictorIndex == _lods.numPointsInLod[quantLayer]) {
      quantLayer = std::min(int(qpSet.layers.size()) - 1, quantLayer + 1);
    }
    const uint32_t pointIndex = _lods.indexes[predictorIndex];
    auto quant = qpSet.quantizers(pointCloud[pointIndex], quantLayer);
    auto& predictor = _lods.predictors[predictorIndex];

    computeReflectancePredictionWeights(
      aps, pointCloud, _lods.indexes, predictor, decoder);
    attr_t& reflectance = pointCloud.getReflectance(pointIndex);
    int32_t attValue0 = 0;
    if (zero_cnt > 0) {
      zero_cnt--;
    } else {
      attValue0 = decoder.decode();
      zero_cnt = decoder.decodeRunLength();
    }
    const int64_t quantPredAttValue =
      predictor.predictReflectance(pointCloud, _lods.indexes);
    const int64_t delta =
      divExp2RoundHalfUp(quant[0].scale(attValue0), kFixedPointAttributeShift);
    const int64_t reconstructedQuantAttValue = quantPredAttValue + delta;
    reflectance =
      attr_t(PCCClip(reconstructedQuantAttValue, int64_t(0), maxReflectance));
  }
}

//----------------------------------------------------------------------------

void
AttributeDecoder::computeColorPredictionWeights(
  const AttributeParameterSet& aps,
  const PCCPointSet3& pointCloud,
  const std::vector<uint32_t>& indexes,
  PCCPredictor& predictor,
  PCCResidualsDecoder& decoder)
{
  int64_t maxDiff = 0;

  if (predictor.neighborCount > 1 && aps.max_num_direct_predictors) {
    int64_t minValue[3] = {0, 0, 0};
    int64_t maxValue[3] = {0, 0, 0};
    for (int i = 0; i < predictor.neighborCount; ++i) {
      const Vec3<attr_t> colorNeighbor =
        pointCloud.getColor(indexes[predictor.neighbors[i].predictorIndex]);
      for (size_t k = 0; k < 3; ++k) {
        if (i == 0 || colorNeighbor[k] < minValue[k]) {
          minValue[k] = colorNeighbor[k];
        }
        if (i == 0 || colorNeighbor[k] > maxValue[k]) {
          maxValue[k] = colorNeighbor[k];
        }
      }
    }
    maxDiff = (std::max)(
      maxValue[2] - minValue[2],
      (std::max)(maxValue[0] - minValue[0], maxValue[1] - minValue[1]));
  }

  if (maxDiff >= aps.adaptive_prediction_threshold) {
    predictor.predMode = decoder.decodePredMode(aps.max_num_direct_predictors);
  }
}

//----------------------------------------------------------------------------

void
AttributeDecoder::decodeColorsPred(
  const AttributeDescription& desc,
  const AttributeParameterSet& aps,
  const QpSet& qpSet,
  PCCResidualsDecoder& decoder,
  PCCPointSet3& pointCloud)
{
  const size_t pointCount = pointCloud.getPointCount();

  Vec3<int64_t> clipMax{(1 << desc.bitdepth) - 1,
                        (1 << desc.bitdepthSecondary) - 1,
                        (1 << desc.bitdepthSecondary) - 1};

  int32_t values[3];
  ///////////////////////////////////////////////////////////////////////////////����ʵֵ
  int64_t A = _lods.numPointsInLod[6];
  std::vector<double> bP[3];
  for (int i = 0; i < 3; i++) {
    bP[i].resize(pointCount + 1);
  }
  std::vector<double> P[3];
  for (int i = 0; i < 3; i++) {
    P[i].resize(pointCount + 1);
  }


  std::vector<Vec3<uint32_t>> color_real;
  int32_t real_value[3];
  int u = 0;
  int s = 0;
  double R_laser = 50.0;
  double H_laser = 1;
  double Ht = 1 / H_laser;
  int64_t r = 0;
  int64_t z = 0;
  int64_t r1 = 0;
  int64_t x_ = 0;
  float_t K_ = 0;
  Vec3<attr_t> realColor = 0;
  Vec3<attr_t> back_realColor = 0;
  Vec3<attr_t> realColor1 = 0;
  Vec3<attr_t> recon_color;
  //Vec3<attr_t> realColor;
  //Vec3<attr_t> realColor1;
  Vec3<attr_t> realColor2;
  Vec3<attr_t> recon_color0;
  Vec3<attr_t> back_recon_color=0;
  Vec3<attr_t> recon_color1;
  Vec3<attr_t> recon_color2;
  //std::vector<Vec3<uint32_t>> kft_color;
  //   //////////////////////////////////////////////////////////////////////////////////

  P[0][1] = 200.0;
  P[1][1] = 500.0;
  P[2][1] = 450.0;
  bP[0][3] = 200.0;
  bP[1][3] = 500.0;
  bP[2][3] = 450.0;

  if (_lods.numPointsInLod[3] - _lods.numPointsInLod[2] < 8) {
    u = 3;
  } else {
    if (_lods.numPointsInLod[2] - _lods.numPointsInLod[1] < 8) {
      u = 2;
    } else {
      u = 1;
    }
  }
  int t = u;
  std::vector<Vec3<int32_t>> kft_color;
  int I = 6;
  int flag_var = decoder.decodeRunLength();
  int j = decoder.decodeRunLength();
  int n = j;
  int k = j - n;
  for (int i = 0; i < j; i++) {
    decoder.decode(real_value);
    Vec3<int32_t> color_temp;
    for (int j = 0; j < 3; j++) {
      color_temp[j] = real_value[j];
    }
    color_real.push_back(color_temp);
  }
  // Vec3<attr_t>& color;
  ///////////////////////////////////////////////////////////////////////////////////////
  int zero_cnt = decoder.decodeRunLength();
  int quantLayer = 0;
  for (size_t predictorIndex = 0; predictorIndex < pointCount;
       ++predictorIndex) {
    if (predictorIndex == _lods.numPointsInLod[quantLayer]) {
      quantLayer = std::min(int(qpSet.layers.size()) - 1, quantLayer + 1);
    }
    const uint32_t pointIndex = _lods.indexes[predictorIndex];
	//----------------------------------------------
  //  std::cout << "predictorIndex:\t" << predictorIndex << std::endl;
    //----------------------------------------------
    auto quant = qpSet.quantizers(pointCloud[pointIndex], quantLayer);
    auto& predictor = _lods.predictors[predictorIndex];

    computeColorPredictionWeights(
      aps, pointCloud, _lods.indexes, predictor, decoder);
    if (zero_cnt > 0) {
      values[0] = values[1] = values[2] = 0;
      zero_cnt--;
    } else {
      decoder.decode(values);
      zero_cnt = decoder.decodeRunLength();
    }
    Vec3<attr_t>& color = pointCloud.getColor(pointIndex);

	    if (predictorIndex > 0) {
      recon_color = pointCloud.getColor(_lods.indexes[predictorIndex - 1]);
    }
      if (predictorIndex > 2) {
        recon_color0 = pointCloud.getColor(_lods.indexes[predictorIndex - 3]);
        recon_color1 = pointCloud.getColor(_lods.indexes[predictorIndex - 2]);
        recon_color2 = pointCloud.getColor(_lods.indexes[predictorIndex - 1]);
      }
    const Vec3<attr_t> predictedColor =
      predictor.predictColor(pointCloud, _lods.indexes);


	/////////////////////////////////////////////////////////////////////////////////////����

    if (predictorIndex > 0) {
      if (predictorIndex < A) {
        if (predictorIndex < _lods.numPointsInLod[6]) {
          if (flag_var > 0) {
            int m =
              (_lods.numPointsInLod[u + 1] - _lods.numPointsInLod[u]) / 8;
            if (predictorIndex < _lods.numPointsInLod[t]) {
              for (int i = 0; i < 3; i++) {
                recon_color[i] = color_real[k][i];
              }
              if (predictorIndex > 0) {
              
              for (int k = 0; k < 3; k++) {
                int64_t y0 = recon_color[k] - predictedColor[k];

                float_t S0 = H_laser * P[k][predictorIndex] * Ht + R_laser;
                float_t Si0 = 1 / S0;
                K_ = P[k][predictorIndex] * Ht * Si0;

                x_ = predictedColor[k] + K_ * y0;

                realColor[k] = attr_t(PCCClip(x_, int64_t(0), clipMax[k]));

                P[k][predictorIndex + 1] =
                  (1 - K_ * H_laser) * P[k][predictorIndex];
              }
			  }
              for (int i = 0; i < 3; i++) {
                realColor[i] = color_real[k][i];
              }
              k++;
            }
            if (predictorIndex >= _lods.numPointsInLod[t]) {
              if (
                predictorIndex == _lods.numPointsInLod[u]
                || predictorIndex == _lods.numPointsInLod[u] + m
                || predictorIndex == _lods.numPointsInLod[u] + 2 * m
                || predictorIndex == _lods.numPointsInLod[u] + 3 * m
                || predictorIndex == _lods.numPointsInLod[u] + 4 * m
                || predictorIndex == _lods.numPointsInLod[u] + 5 * m
                || predictorIndex == _lods.numPointsInLod[u] + 6 * m
                || predictorIndex == _lods.numPointsInLod[u] - m) {
                for (int i = 0; i < 3; i++) {
                  recon_color[i] = color_real[k][i];
                }
              }

              for (int k = 0; k < 3; k++) {
                int64_t y0 = recon_color[k] - predictedColor[k];

                float_t S0 = H_laser * P[k][predictorIndex] * Ht + R_laser;
                float_t Si0 = 1 / S0;
                K_ = P[k][predictorIndex] * Ht * Si0;

                x_ = predictedColor[k] + K_ * y0;

                realColor[k] = attr_t(PCCClip(x_, int64_t(0), clipMax[k]));

                P[k][predictorIndex + 1] =
                  (1 - K_ * H_laser) * P[k][predictorIndex];
              }

              if (
                predictorIndex == _lods.numPointsInLod[u]
                || predictorIndex == _lods.numPointsInLod[u] + m
                || predictorIndex == _lods.numPointsInLod[u] + 2 * m
                || predictorIndex == _lods.numPointsInLod[u] + 3 * m
                || predictorIndex == _lods.numPointsInLod[u] + 4 * m
                || predictorIndex == _lods.numPointsInLod[u] + 5 * m
                || predictorIndex == _lods.numPointsInLod[u] + 6 * m
                || predictorIndex == _lods.numPointsInLod[u] - m) {
                for (int i = 0; i < 3; i++) {
                  realColor[i] = color_real[k][i];
                }
                k++;
              }
            }

            if (predictorIndex == _lods.numPointsInLod[u + 1] - 1) {
              u++;
            }
          } else {
            realColor = predictedColor;
          }
        } else {
     

        /////////////////////////////////////////////////////////////////////////////////////////
        if (predictorIndex >= _lods.numPointsInLod[6]) {
          int m1 = (_lods.numPointsInLod[u + 1] - _lods.numPointsInLod[u]) / 8;

          if (flag_var > 0) {
            if (
              predictorIndex == _lods.numPointsInLod[u]
              || predictorIndex == _lods.numPointsInLod[u] + m1
              || predictorIndex == _lods.numPointsInLod[u] + 2 * m1
              || predictorIndex == _lods.numPointsInLod[u] + 3 * m1
              || predictorIndex == _lods.numPointsInLod[u] + 4 * m1
              || predictorIndex == _lods.numPointsInLod[u] + 5 * m1
              || predictorIndex == _lods.numPointsInLod[u] + 6 * m1
              || predictorIndex == _lods.numPointsInLod[u] - m1) {
              for (int i = 0; i < 3; i++) {
                recon_color[i] = color_real[k][i];
              }
            }
            for (int k = 0; k < 3; k++) {
              int64_t y0 = recon_color[k] - predictedColor[k];

              float_t S0 = H_laser * P[k][predictorIndex] * Ht + R_laser;
              float_t Si0 = 1 / S0;
              K_ = P[k][predictorIndex] * Ht * Si0;

              x_ = predictedColor[k] + K_ * y0;

              realColor[k] = attr_t(PCCClip(x_, int64_t(0), clipMax[k]));

              P[k][predictorIndex + 1] =
                (1 - K_ * H_laser) * P[k][predictorIndex];
            }

            if (
              predictorIndex == _lods.numPointsInLod[u]
              || predictorIndex == _lods.numPointsInLod[u] + m1
              || predictorIndex == _lods.numPointsInLod[u] + 2 * m1
              || predictorIndex == _lods.numPointsInLod[u] + 3 * m1
              || predictorIndex == _lods.numPointsInLod[u] + 4 * m1
              || predictorIndex == _lods.numPointsInLod[u] + 5 * m1
              || predictorIndex == _lods.numPointsInLod[u] + 6 * m1
              || predictorIndex == _lods.numPointsInLod[u] - m1) {
              for (int i = 0; i < 3; i++) {
                realColor[i] = color_real[k][i];
              }
              k++;
            }

            if (predictorIndex == _lods.numPointsInLod[u + 1] - 1) {
              u++;
            }

          } else {
            realColor = predictedColor;
          }
        }
		}
      } else {
        A = _lods.numPointsInLod[I + 1];
        realColor = predictedColor;
        /* if (flag_var[I - 6][2] == 1) {
       realColor = pointCloud.getColor(_lods.indexes[_lods.numPointsInLod[I]]);
        }*/
        if (I == 6) {
          P[0][predictorIndex + 1] = 200.0;
          P[1][predictorIndex + 1] = 500.0;
          P[2][predictorIndex + 1] = 450.0;
        }
        flag_var = flag_var - 1;
        I++;
      }
    }
    ////////////////////////////////////////////////////////////////////////////////////


    int64_t residual0 = 0;
    for (int k = 0; k < 3; ++k) {
      const auto& q = quant[std::min(k, 1)];
      s = q.stepSize();
      const int64_t residual =
        divExp2RoundHalfUp(q.scale(values[k]), kFixedPointAttributeShift);
      int64_t recon = realColor[k] + residual + residual0;
      if (predictorIndex == 0)
        int64_t recon = predictedColor[k] + residual + residual0;
      color[k] = attr_t(PCCClip(recon, int64_t(0), clipMax[k]));

      if (!k && aps.inter_component_prediction_enabled_flag)
        residual0 = residual;
    }

	     if (s < 2048) {
          back_realColor = transformGbrToYCbCrBt709(color);
         kft_color.push_back(back_realColor);
        }

        if (s >= 2048) {
          if (predictorIndex > 2) {
            auto out_yuv_color = transformGbrToYCbCrBt709(color);
            for (int k = 0; k < 3; k++) {
              back_recon_color[k] =
                (recon_color0[k] + recon_color1[k] + recon_color2[k]) / 3;
            }

            auto out_yuv_recon = transformGbrToYCbCrBt709(back_recon_color);

            back_realColor[0] = out_yuv_color[0];  //��y���������˲�����
            for (int k = 1; k < 3; k++) {
              int64_t y0 = out_yuv_recon[k] - out_yuv_color[k];

              float_t S0 = H_laser * bP[k][predictorIndex] * Ht + R_laser;
              float_t Si0 = 1 / S0;
              K_ = bP[k][predictorIndex] * Ht * Si0;

              x_ = out_yuv_color[k] + K_ * y0;

              back_realColor[k] =
                attr_t(PCCClip(x_, int64_t(0), clipMax[k]));

              bP[k][predictorIndex + 1] =
                (1 - K_ * H_laser) * bP[k][predictorIndex];
            }

          } else {
            back_realColor = transformGbrToYCbCrBt709(color);
          }


          kft_color.push_back(back_realColor);
        }
     
  }
  ////////////////////////////////////////////////////////////////////////////////////////////
    for (size_t predictorIndex1 = 0; predictorIndex1 < pointCount;
         ++predictorIndex1) {
      uint32_t pointIndex1 = _lods.indexes[predictorIndex1];
      Vec3<attr_t>& color = pointCloud.getColor(pointIndex1);
      Vec3<attr_t> realColor_1 = kft_color[predictorIndex1];
      Vec3<attr_t> realColor_2;
      realColor_2[2] = realColor_1[0];
      realColor_2[1] = realColor_1[2];
      realColor_2[0] = realColor_1[1];
      color = realColor_2;
    }
}

//----------------------------------------------------------------------------

void
AttributeDecoder::decodeReflectancesRaht(
  const AttributeDescription& desc,
  const AttributeParameterSet& aps,
  const QpSet& qpSet,
  PCCResidualsDecoder& decoder,
  PCCPointSet3& pointCloud)
{
  const int voxelCount = int(pointCloud.getPointCount());
  std::vector<MortonCodeWithIndex> packedVoxel(voxelCount);
  for (int n = 0; n < voxelCount; n++) {
    packedVoxel[n].mortonCode = mortonAddr(pointCloud[n]);
    packedVoxel[n].index = n;
  }
  sort(packedVoxel.begin(), packedVoxel.end());

  // Morton codes
  std::vector<int64_t> mortonCode(voxelCount);
  for (int n = 0; n < voxelCount; n++) {
    mortonCode[n] = packedVoxel[n].mortonCode;
  }

  // Entropy decode
  const int attribCount = 1;
  std::vector<int> coefficients(attribCount * voxelCount);
  std::vector<Qps> pointQpOffsets(voxelCount);
  int zero_cnt = decoder.decodeRunLength();
  for (int n = 0; n < voxelCount; ++n) {
    uint32_t value = 0;
    if (zero_cnt > 0) {
      zero_cnt--;
    } else {
      value = decoder.decode();
      zero_cnt = decoder.decodeRunLength();
    }
    coefficients[n] = value;
    pointQpOffsets[n] = qpSet.regionQpOffset(pointCloud[packedVoxel[n].index]);
  }

  std::vector<int> attributes(attribCount * voxelCount);
  const int rahtPredThreshold[2] = {aps.raht_prediction_threshold0,
                                    aps.raht_prediction_threshold1};

  regionAdaptiveHierarchicalInverseTransform(
    aps.raht_prediction_enabled_flag, rahtPredThreshold, qpSet,
    pointQpOffsets.data(), mortonCode.data(), attributes.data(), attribCount,
    voxelCount, coefficients.data());

  const int64_t maxReflectance = (1 << desc.bitdepth) - 1;
  const int64_t minReflectance = 0;
  for (int n = 0; n < voxelCount; n++) {
    int64_t val = attributes[attribCount * n];
    const attr_t reflectance =
      attr_t(PCCClip(val, minReflectance, maxReflectance));
    pointCloud.setReflectance(packedVoxel[n].index, reflectance);
  }
}

//----------------------------------------------------------------------------

void
AttributeDecoder::decodeColorsRaht(
  const AttributeDescription& desc,
  const AttributeParameterSet& aps,
  const QpSet& qpSet,
  PCCResidualsDecoder& decoder,
  PCCPointSet3& pointCloud)
{
  const int voxelCount = int(pointCloud.getPointCount());
  std::vector<MortonCodeWithIndex> packedVoxel(voxelCount);
  for (int n = 0; n < voxelCount; n++) {
    packedVoxel[n].mortonCode = mortonAddr(pointCloud[n]);
    packedVoxel[n].index = n;
  }
  sort(packedVoxel.begin(), packedVoxel.end());

  // Morton codes
  std::vector<int64_t> mortonCode(voxelCount);
  for (int n = 0; n < voxelCount; n++) {
    mortonCode[n] = packedVoxel[n].mortonCode;
  }

  // Entropy decode
  const int attribCount = 3;
  int zero_cnt = decoder.decodeRunLength();
  std::vector<int> coefficients(attribCount * voxelCount);
  std::vector<Qps> pointQpOffsets(voxelCount);

  for (int n = 0; n < voxelCount; ++n) {
    int32_t values[3];
    if (zero_cnt > 0) {
      values[0] = values[1] = values[2] = 0;
      zero_cnt--;
    } else {
      decoder.decode(values);
      zero_cnt = decoder.decodeRunLength();
    }
    for (int d = 0; d < attribCount; ++d) {
      coefficients[voxelCount * d + n] = values[d];
    }
    pointQpOffsets[n] = qpSet.regionQpOffset(pointCloud[packedVoxel[n].index]);
  }

  std::vector<int> attributes(attribCount * voxelCount);
  const int rahtPredThreshold[2] = {aps.raht_prediction_threshold0,
                                    aps.raht_prediction_threshold1};

  regionAdaptiveHierarchicalInverseTransform(
    aps.raht_prediction_enabled_flag, rahtPredThreshold, qpSet,
    pointQpOffsets.data(), mortonCode.data(), attributes.data(), attribCount,
    voxelCount, coefficients.data());

  Vec3<int> clipMax{(1 << desc.bitdepth) - 1,
                    (1 << desc.bitdepthSecondary) - 1,
                    (1 << desc.bitdepthSecondary) - 1};

  for (int n = 0; n < voxelCount; n++) {
    const int r = attributes[attribCount * n];
    const int g = attributes[attribCount * n + 1];
    const int b = attributes[attribCount * n + 2];
    Vec3<attr_t> color;
    color[0] = attr_t(PCCClip(r, 0, clipMax[0]));
    color[1] = attr_t(PCCClip(g, 0, clipMax[1]));
    color[2] = attr_t(PCCClip(b, 0, clipMax[2]));
    pointCloud.setColor(packedVoxel[n].index, color);
  }
}

//----------------------------------------------------------------------------

void
AttributeDecoder::decodeColorsLift(
  const AttributeDescription& desc,
  const AttributeParameterSet& aps,
  const QpSet& qpSet,
  int geom_num_points_minus1,
  int minGeomNodeSizeLog2,
  PCCResidualsDecoder& decoder,
  PCCPointSet3& pointCloud)
{
  const size_t pointCount = pointCloud.getPointCount();
  std::vector<uint64_t> weights;

  if (!aps.scalable_lifting_enabled_flag) {
    PCCComputeQuantizationWeights(_lods.predictors, weights);
  } else {
    computeQuantizationWeightsScalable(
      _lods.predictors, _lods.numPointsInLod, geom_num_points_minus1 + 1,
      minGeomNodeSizeLog2, weights);
  }

  const size_t lodCount = _lods.numPointsInLod.size();
  std::vector<Vec3<int64_t>> colors;
  colors.resize(pointCount);

  // decompress
  // Per level-of-detail coefficients {-1,0,1} for last component prediction
  int lod = 0;
  int8_t lastCompPredCoeff = 0;
  std::vector<int8_t> lastCompPredCoeffs;
  if (aps.last_component_prediction_enabled_flag) {
    lastCompPredCoeffs = decoder.decodeLastCompPredCoeffs(lodCount);
    lastCompPredCoeff = lastCompPredCoeffs[0];
  }

  int zero_cnt = decoder.decodeRunLength();
  int quantLayer = 0;
  for (size_t predictorIndex = 0; predictorIndex < pointCount;
       ++predictorIndex) {
    if (predictorIndex == _lods.numPointsInLod[quantLayer]) {
      quantLayer = std::min(int(qpSet.layers.size()) - 1, quantLayer + 1);
    }

    if (predictorIndex == _lods.numPointsInLod[lod]) {
      lod++;
      if (aps.last_component_prediction_enabled_flag)
        lastCompPredCoeff = lastCompPredCoeffs[lod];
    }

    const uint32_t pointIndex = _lods.indexes[predictorIndex];
    auto quant = qpSet.quantizers(pointCloud[pointIndex], quantLayer);

    int32_t values[3];
    if (zero_cnt > 0) {
      values[0] = values[1] = values[2] = 0;
      zero_cnt--;
    } else {
      decoder.decode(values);
      zero_cnt = decoder.decodeRunLength();
    }

    const int64_t iQuantWeight = irsqrt(weights[predictorIndex]);
    auto& color = colors[predictorIndex];

    int64_t scaled = quant[0].scale(values[0]);
    color[0] = divExp2RoundHalfInf(scaled * iQuantWeight, 40);

    scaled = quant[1].scale(values[1]);
    color[1] = divExp2RoundHalfInf(scaled * iQuantWeight, 40);

    scaled *= lastCompPredCoeff;
    scaled += quant[1].scale(values[2]);
    color[2] = divExp2RoundHalfInf(scaled * iQuantWeight, 40);
  }

  // reconstruct
  for (size_t lodIndex = 1; lodIndex < lodCount; ++lodIndex) {
    const size_t startIndex = _lods.numPointsInLod[lodIndex - 1];
    const size_t endIndex = _lods.numPointsInLod[lodIndex];
    PCCLiftUpdate(
      _lods.predictors, weights, startIndex, endIndex, false, colors);
    PCCLiftPredict(_lods.predictors, startIndex, endIndex, false, colors);
  }

  Vec3<int64_t> clipMax{(1 << desc.bitdepth) - 1,
                        (1 << desc.bitdepthSecondary) - 1,
                        (1 << desc.bitdepthSecondary) - 1};

  for (size_t f = 0; f < pointCount; ++f) {
    const auto color0 =
      divExp2RoundHalfInf(colors[f], kFixedPointAttributeShift);
    Vec3<attr_t> color;
    for (size_t d = 0; d < 3; ++d) {
      color[d] = attr_t(PCCClip(color0[d], int64_t(0), clipMax[d]));
    }
    pointCloud.setColor(_lods.indexes[f], color);
  }
}

//----------------------------------------------------------------------------

void
AttributeDecoder::decodeReflectancesLift(
  const AttributeDescription& desc,
  const AttributeParameterSet& aps,
  const QpSet& qpSet,
  int geom_num_points_minus1,
  int minGeomNodeSizeLog2,
  PCCResidualsDecoder& decoder,
  PCCPointSet3& pointCloud)
{
  const size_t pointCount = pointCloud.getPointCount();
  std::vector<uint64_t> weights;

  if (!aps.scalable_lifting_enabled_flag) {
    PCCComputeQuantizationWeights(_lods.predictors, weights);
  } else {
    computeQuantizationWeightsScalable(
      _lods.predictors, _lods.numPointsInLod, geom_num_points_minus1 + 1,
      minGeomNodeSizeLog2, weights);
  }

  const size_t lodCount = _lods.numPointsInLod.size();
  std::vector<int64_t> reflectances;
  reflectances.resize(pointCount);

  // decompress
  int zero_cnt = decoder.decodeRunLength();
  int quantLayer = 0;
  for (size_t predictorIndex = 0; predictorIndex < pointCount;
       ++predictorIndex) {
    if (predictorIndex == _lods.numPointsInLod[quantLayer]) {
      quantLayer = std::min(int(qpSet.layers.size()) - 1, quantLayer + 1);
    }
    const uint32_t pointIndex = _lods.indexes[predictorIndex];
    auto quant = qpSet.quantizers(pointCloud[pointIndex], quantLayer);

    int64_t detail = 0;
    if (zero_cnt > 0) {
      zero_cnt--;
    } else {
      detail = decoder.decode();
      zero_cnt = decoder.decodeRunLength();
    }
    const int64_t iQuantWeight = irsqrt(weights[predictorIndex]);
    auto& reflectance = reflectances[predictorIndex];
    const int64_t delta = detail;
    const int64_t reconstructedDelta = quant[0].scale(delta);
    reflectance = divExp2RoundHalfInf(reconstructedDelta * iQuantWeight, 40);
  }

  // reconstruct
  for (size_t lodIndex = 1; lodIndex < lodCount; ++lodIndex) {
    const size_t startIndex = _lods.numPointsInLod[lodIndex - 1];
    const size_t endIndex = _lods.numPointsInLod[lodIndex];
    PCCLiftUpdate(
      _lods.predictors, weights, startIndex, endIndex, false, reflectances);
    PCCLiftPredict(
      _lods.predictors, startIndex, endIndex, false, reflectances);
  }
  const int64_t maxReflectance = (1 << desc.bitdepth) - 1;
  for (size_t f = 0; f < pointCount; ++f) {
    const auto refl =
      divExp2RoundHalfInf(reflectances[f], kFixedPointAttributeShift);
    pointCloud.setReflectance(
      _lods.indexes[f], attr_t(PCCClip(refl, int64_t(0), maxReflectance)));
  }
}

//============================================================================

} /* namespace pcc */
