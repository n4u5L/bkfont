// Source: third_party/blink/renderer/platform/geometry/length.cc
/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller ( mueller@kde.org )
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2006 Andrew Wellington (proton@wiretapped.net)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "length.h"

#include <array>
#include <memory>
#include <utility>
#include "base/hash_map.h"

#include "blend.h"
#include "calculation_value.h"
#include "base/allocator/allocator.h"
#include "base/static_constructors.h"
#include "base/text/string_builder.h"
#include "base/wtf.h"

namespace blink {

DEFINE_GLOBAL(, Length, g_auto_length);
DEFINE_GLOBAL(, Length, g_fill_available_length);
DEFINE_GLOBAL(, Length, g_stretch_length);
DEFINE_GLOBAL(, Length, g_fit_content_length);
DEFINE_GLOBAL(, Length, g_max_content_length);
DEFINE_GLOBAL(, Length, g_min_content_length);
DEFINE_GLOBAL(, Length, g_min_intrinsic_length);

// static
void Length::Initialize() {
  new ((void*)&g_auto_length) Length(kAuto);
  new ((void*)&g_fill_available_length)
      Length(kFillAvailable);
  new ((void*)&g_stretch_length) Length(kStretch);
  new ((void*)&g_fit_content_length)
      Length(kFitContent);
  new ((void*)&g_max_content_length)
      Length(kMaxContent);
  new ((void*)&g_min_content_length)
      Length(kMinContent);
  new ((void*)&g_min_intrinsic_length)
      Length(kMinIntrinsic);
}

class CalculationValueHandleMap {
public:
  CalculationValueHandleMap() = default;
  CalculationValueHandleMap(const CalculationValueHandleMap&) = delete;
  CalculationValueHandleMap& operator=(const CalculationValueHandleMap&) =
      delete;

  struct MemberWithCount {

  public:
    std::shared_ptr<const CalculationValue> value;
    unsigned count = 1u;
  };

  int insert(std::shared_ptr<const CalculationValue> calc_value) {
    ;
    // FIXME calc(): https://bugs.webkit.org/show_bug.cgi?id=80489
    // This monotonically increasing handle generation scheme is potentially
    // wasteful of the handle space. Consider reusing empty handles.
    while (map_.Contains(index_))
      index_++;

    map_.Set(index_, MemberWithCount{std::move(calc_value), 1u});

    return index_;
  }

  const CalculationValue& Get(int index) const {
    ;
    return *map_.at(index).value;
  }

  std::shared_ptr<const CalculationValue> GetShared(int index) const {
    return map_.at(index).value;
  }

  unsigned GetCount(int index) const {
    ;
    return map_.at(index).count;
  }

  wtf_size_t GetMapSize() const {
    return map_.size();
  }

  void DecrementCount(int index) {
    ;
    auto iter = map_.find(index);
    --iter->value.count;
    if (iter->value.count == 0u) {
      map_.erase(index);
    }
  }

  void IncrementCount(int index) {
    ;
    auto iter = map_.find(index);
    ++iter->value.count;
  }

private:
  int index_ = 1;
  HashMap<int, MemberWithCount> map_;
};

static CalculationValueHandleMap& CalcHandles() {
  ;
  // Keep the upstream handle/count lifetime; shared ownership replaces Oilpan.
  static CalculationValueHandleMap handle_map;
  return handle_map;
}

Length::Length(std::shared_ptr<const CalculationValue> calc)
    : quirk_(false),
      type_(kCalculated) {
  calculation_handle_ = CalcHandles().insert(std::move(calc));
}

Length Length::BlendMixedTypes(const Length& from,
                               double progress,
                               ValueRange range) const {
  ;
  ;
  return Length(
      AsCalculationValue()->Blend(*from.AsCalculationValue(), progress, range));
}

Length Length::BlendSameTypes(const Length& from,
                              double progress,
                              ValueRange range) const {
  Length::Type result_type = GetType();
  if (IsZero())
    result_type = from.GetType();

  float blended_value =
      blink::Blend(from.GetFloatValue(), GetFloatValue(), progress);
  if (range == ValueRange::kNonNegative)
    blended_value = ClampTo<float>(blended_value, 0);
  return Length(blended_value, result_type);
}

PixelsAndPercent Length::GetPixelsAndPercent() const {
  switch (GetType()) {
  case kFixed:
    return PixelsAndPercent(Pixels());
  case kPercent:
    return PixelsAndPercent(0.0f, Percent(), /*has_explicit_pixels=*/false,
                            /*has_explicit_percent=*/true);
  case kCalculated:
    return GetCalculationValue().GetPixelsAndPercent();
  default:
    std::unreachable();
  }
}

std::shared_ptr<const CalculationValue> Length::AsCalculationValue() const {
  if (IsCalculated())
    return CalcHandles().GetShared(CalculationHandle());
  return std::make_shared<CalculationValue>(GetPixelsAndPercent(),
                                            ValueRange::kAll);
}

Length Length::SubtractFromOneHundredPercent() const {
  if (IsPercent())
    return Length::Percent(100 - Percent());
  ;
  return Length(AsCalculationValue()->SubtractFromOneHundredPercent());
}

Length Length::Add(const Length& other) const {
  ;
  if (IsFixed() && other.IsFixed()) {
    return Length::Fixed(Pixels() + other.Pixels());
  }
  if (IsPercent() && other.IsPercent()) {
    return Length::Percent(Percent() + other.Percent());
  }
  return Length(AsCalculationValue()->Add(*other.AsCalculationValue()));
}

Length Length::Zoom(double factor) const {
  switch (GetType()) {
  case kFixed:
    return Length::Fixed(GetFloatValue() * factor);
  case kCalculated:
    return Length(GetCalculationValue().Zoom(factor));
  default:
    return *this;
  }
}

const CalculationValue& Length::GetCalculationValue() const {
  ;
  return CalcHandles().Get(CalculationHandle());
}

void Length::IncrementCalculatedCount() const {
  ;
  CalcHandles().IncrementCount(CalculationHandle());
}

void Length::DecrementCalculatedCount() const {
  ;
  CalcHandles().DecrementCount(CalculationHandle());
}

unsigned Length::GetCalculatedCountForTest() const {
  ;
  return CalcHandles().GetCount(CalculationHandle());
}

wtf_size_t Length::GetCalcHandleMapSizeForTest() {
  return CalcHandles().GetMapSize();
}

float Length::NonNanCalculatedValue(float max_value,
                                    const EvaluationInput& input) const {
  ;
  float result = GetCalculationValue().Evaluate(max_value, input);
  if (std::isnan(result))
    return 0;
  return result;
}

bool Length::HasOnlyFixedAndPercent() const {
  if (GetType() == kFixed || GetType() == kPercent) {
    return true;
  }
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasOnlyFixedAndPercent();
  }
  return false;
}

bool Length::HasAuto() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasAuto();
  }
  return GetType() == kAuto;
}

bool Length::HasContentOrIntrinsic() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasContentOrIntrinsicSize();
  }
  return GetType() == kMinContent || GetType() == kMaxContent || GetType() == kFitContent || GetType() == kMinIntrinsic || GetType() == kContent;
}

bool Length::HasAutoOrContentOrIntrinsic() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasAutoOrContentOrIntrinsicSize();
  }
  return GetType() == kAuto || HasContentOrIntrinsic();
}

bool Length::HasPercent() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasPercent();
  }
  return GetType() == kPercent;
}

bool Length::HasPercentOrStretch() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasPercentOrStretch();
  }
  return GetType() == kPercent || GetType() == kStretch || GetType() == kFillAvailable;
}

bool Length::HasStretch() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasStretch();
  }
  return GetType() == kStretch || GetType() == kFillAvailable;
}

bool Length::HasMinContent() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasMinContent();
  }
  return GetType() == kMinContent;
}

bool Length::HasMaxContent() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasMaxContent();
  }
  return GetType() == kMaxContent;
}

bool Length::HasFitContent() const {
  if (GetType() == kCalculated) {
    return GetCalculationValue().HasFitContent();
  }
  return GetType() == kFitContent;
}

bool Length::IsCalculatedEqual(const Length& o) const {
  return IsCalculated() && (&GetCalculationValue() == &o.GetCalculationValue() || GetCalculationValue() == o.GetCalculationValue());
}

String Length::ToString() const {
  StringBuilder builder;
  builder.Append("Length(");
  static const auto kTypeNames = std::to_array<const char* const>(
      {"Auto", "Percent", "Fixed", "MinContent", "MaxContent", "MinIntrinsic", "FillAvailable", "Stretch", "FitContent", "Calculated", "Flex", "ExtendToZoom", "DeviceWidth", "DeviceHeight", "None", "Content"});
  if (type_ < std::size(kTypeNames))
    builder.Append(kTypeNames[type_]);
  else
    builder.Append("?");
  builder.Append(", ");
  if (IsCalculated()) {
    builder.AppendNumber(calculation_handle_);
  } else {
    builder.AppendNumber(value_);
  }
  if (quirk_)
    builder.Append(", Quirk");
  builder.Append(")");
  return builder.ToString();
}

unsigned Length::GetHash() const {
  unsigned hash = 0;
  AddFloatToHash(hash, value_);
  AddIntToHash(hash, type_);
  AddIntToHash(hash, quirk_);
  AddIntToHash(hash, calculation_handle_);
  return hash;
}

std::ostream& operator<<(std::ostream& ostream, const Length& value) {
  return ostream << value.ToString();
}

struct SameSizeAsLength {
  int32_t value;
  int32_t meta_data;
};
;

} // namespace blink
