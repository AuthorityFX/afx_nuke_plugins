// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Copyright (C) 2012-2016, Ryan P. Wilson
//
//      Authority FX, Inc.
//      www.authorityfx.com

#include <DDImage/Iop.h>
#include <DDImage/Row.h>
#include <DDImage/Knobs.h>
#include <DDImage/NukeWrapper.h>

#include <ipp.h>

#include <cmath>

#include "include/bounds.h"
#include "include/pixel.h"
#include "include/color_op.h"

#define ThisClass AFXToneMap

namespace nuke = DD::Image;

// The class name must match exactly what is in the meny.py: nuke.createNode(CLASS)
static const char* const CLASS = "AFXToneMap";
static const char* const HELP = "Tone mapping";

const char* metric_list[8] = {
  "Value",
  "Luminance",
  "Lightness",
  0
};

class ThisClass : public nuke::Iop {
 private:
  int k_metric_;
  float k_darks_;
  float k_clip_;
  float k_exposure_;
  float k_knee_;

  float darks_;
  float clip_;
  float exposure_;
  float knee_;
  bool do_limit_;

 public:
  explicit ThisClass(Node* node);
  void knobs(nuke::Knob_Callback);
  const char* Class() const;
  const char* node_help() const;
  static const Iop::Description d;

  void _validate(bool);
  void _request(int x, int y, int r, int t, nuke::ChannelMask channels, int count);
  void engine(int y, int x, int r, nuke::ChannelMask channels, nuke::Row& row);
};

ThisClass::ThisClass(Node* node) : nuke::Iop(node) {
  // Set inputs
  inputs(1);

  // initialize knobs
  k_metric_ = 1;
  k_darks_ = 1.0f;
  k_exposure_ = 0.0f;
  k_clip_ = 5.0f;
  k_knee_ = 0.5f;
  do_limit_ = 0;
}
void ThisClass::knobs(nuke::Knob_Callback f) {
  nuke::Enumeration_knob(f, &k_metric_, metric_list, "metric", "Metric");
  nuke::Tooltip(f, "Metric");

  nuke::Float_knob(f, &k_exposure_, "exposure", "Exposure");
  nuke::Tooltip(f, "Exposure");
  nuke::SetRange(f, -5, 5);

  nuke::Float_knob(f, &k_darks_, "darks", "Darks");
  nuke::Tooltip(f, "Adjust Darks");
  nuke::SetRange(f, 0, 2);

  nuke::Float_knob(f, &k_clip_, "clip", "Clip");
  nuke::Tooltip(f, "Clip Value");
  nuke::SetRange(f, 0, 10);

  nuke::Float_knob(f, &k_knee_, "knee", "Knee");
  nuke::Tooltip(f, "Knee Sharpness");
  nuke::SetRange(f, 0, 1);
}
const char* ThisClass::Class() const { return CLASS; }
const char* ThisClass::node_help() const { return HELP; }
static nuke::Iop* build(Node* node) { return (new nuke::NukeWrapper(new ThisClass(node)))->channelsRGBoptionalAlpha(); }
const nuke::Op::Description ThisClass::d(CLASS, "AuthorityFX/AFX Tone Map", build);
void ThisClass::_validate(bool) {
  copy_info(0);

  const float min_v = 0.008f;
  const float max_v = 125.0f;

  darks_ = afx::AfxClamp(1.0f/(k_darks_), min_v, max_v);
  exposure_ = powf(2.0f, k_exposure_);
  clip_ = afx::AfxClamp(k_clip_, min_v, max_v);
  knee_ = afx::AfxClamp(k_knee_, min_v, max_v);
}
void ThisClass::_request(int x, int y, int r, int t, nuke::ChannelMask channels, int count) {
  input0().request(x, y, r, t, channels, count);
}
void ThisClass::engine(int y, int x, int r, nuke::ChannelMask channels, nuke::Row& row) {
  // Must call get before initializing any pointers
  row.get(input0(), y, x, r, channels);

  nuke::ChannelSet done;
  foreach(z, channels) {  // Handle color channels
    if (!(done & z) && colourIndex(z) < 3) {  // Handle color channel
      bool has_all_rgb = true;
      nuke::Channel rgb_chan[3];
      for (int i = 0; i < 3; ++i) {
        rgb_chan[i] = brother(z, i);  // Find brother rgb channel
        if (rgb_chan[i] == nuke::Chan_Black || !(channels & rgb_chan[i])) { has_all_rgb = false; }  // If brother does not exist
      }
      if (has_all_rgb) {
        afx::Pixel<const float> in_px;
        afx::Pixel<float> out_px;
        for (int i = 0; i < 3; ++i) {
          done += rgb_chan[i];  // Add channel to done channel set
          in_px.SetPtr(row[rgb_chan[i]] + x, i);
          out_px.SetPtr(row.writable(rgb_chan[i]) + x, i);
        }
        for (int x0 = x; x0 < r; ++x0) {
          float metric = 0;
          switch (k_metric_) {
            case 0: {  // value
              metric = afx::max3(in_px.GetVal(0), in_px.GetVal(1), in_px.GetVal(2));
              break;
            }
            case 1: {  // luminance
              metric = 0.3f * in_px.GetVal(0) + 0.59f * in_px.GetVal(1) + 0.11f * in_px.GetVal(2);
              break;
            }
            case 2: {  // lightness - cubic root of relative Luminance
              metric = powf(0.2126 * in_px.GetVal(0) + 0.7152 * in_px.GetVal(1) + 0.0722 * in_px.GetVal(2), 1.0f / 3.0f);
              break;
            }
          }
          float scale = afx::SoftClip(exposure_ * (metric > 1.0f ? metric : powf(metric, darks_)), clip_, knee_) / fmaxf(metric, 0.000001f);
          for (int i = 0; i < 3; ++i) {
            out_px[i] = in_px[i] * scale;  // Scale RGB by the soft clip scale
          }
          in_px++;
          out_px++;
        }
      }
    }
    if (!(done & z)) {  // Handle non color channel
      done += z;
      const float* in_ptr = row[z] + x;
      float* out_ptr = row.writable(z) + x;
      for (int x0 = x; x0 < r; ++x0) {
        *out_ptr++ = afx::SoftClip(exposure_ * (*in_ptr > 1 ? *in_ptr : powf(*in_ptr, darks_)), clip_, knee_);
        in_ptr++;
      }
    }
  }
}
