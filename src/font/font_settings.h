// Port source: third_party/blink/renderer/platform/fonts/opentype/font_settings.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include "wtf/text/atomic_string.h"
#include "wtf/text/string_builder.h"
#include "wtf/vector.h"

namespace blink {

uint32_t AtomicStringToFourByteTag(const AtomicString& tag);
AtomicString FourByteTagToAtomicString(uint32_t tag);

template <typename T>
class FontTagValuePair {

public:
  FontTagValuePair(uint32_t tag, T value)
      : tag_(tag),
        value_(value) {
    // ensure tag is either valid or zero
  }
  FontTagValuePair(const AtomicString& tag, T value)
      : tag_(AtomicStringToFourByteTag(tag)),
        value_(value) {
    // ensure tag is valid
  }
  bool operator==(const FontTagValuePair& other) const {
    return tag_ == other.tag_ && value_ == other.value_;
  }
  bool operator<(const FontTagValuePair& other) const {
    return tag_ < other.tag_;
  }

  uint32_t Tag() const {
    return tag_;
  }
  AtomicString TagString() const {
    return FourByteTagToAtomicString(tag_);
  }
  T Value() const {
    return value_;
  }

private:
  uint32_t tag_;
  T value_;
};

template <typename T>
class FontSettings {
public:
  FontSettings(const FontSettings&) = delete;
  FontSettings& operator=(const FontSettings&) = delete;

  void Append(const T& feature) {
    list_.push_back(feature);
  }
  wtf_size_t size() const {
    return list_.size();
  }
  const T& operator[](wtf_size_t index) const {
    return list_[index];
  }
  const T& at(wtf_size_t index) const {
    return list_.at(index);
  }
  bool operator==(const FontSettings& other) const {
    return list_ == other.list_;
  }
  bool operator!=(const FontSettings& other) const {
    return !(*this == other);
  }
  String ToString() const {
    StringBuilder builder;
    wtf_size_t num_features = size();
    for (wtf_size_t i = 0; i < num_features; ++i) {
      if (i > 0)
        builder.Append(",");
      builder.Append(at(i).TagString());
      builder.Append("=");
      builder.AppendNumber(at(i).Value());
    }
    return builder.ToString();
  }

  bool FindPair(uint32_t tag, T* found_pair) const {
    if (!found_pair)
      return false;

    for (auto& pair : list_) {
      if (pair.Tag() == tag) {
        *found_pair = pair;
        return true;
      }
    }
    return false;
  }

  typename Vector<T, 0>::const_iterator begin() const {
    return list_.begin();
  }
  typename Vector<T, 0>::const_iterator end() const {
    return list_.end();
  }
  typename Vector<T, 0>::iterator begin() {
    return list_.begin();
  }
  typename Vector<T, 0>::iterator end() {
    return list_.end();
  }

protected:
  FontSettings() = default;

private:
  Vector<T, 0> list_;
};

using FontFeature = FontTagValuePair<int>;
using FontVariationAxis = FontTagValuePair<float>;

class FontFeatureSettings
    : public FontSettings<FontFeature> {
public:
  static std::shared_ptr<FontFeatureSettings> Create() {
    return std::shared_ptr<FontFeatureSettings>(new FontFeatureSettings());
  }

  FontFeatureSettings(const FontFeatureSettings&) = delete;
  FontFeatureSettings& operator=(const FontFeatureSettings&) = delete;

private:
  FontFeatureSettings() = default;
};

class FontVariationSettings
    : public FontSettings<FontVariationAxis> {
public:
  static std::shared_ptr<FontVariationSettings> Create() {
    return std::shared_ptr<FontVariationSettings>(new FontVariationSettings());
  }

  FontVariationSettings(const FontVariationSettings&) = delete;
  FontVariationSettings& operator=(const FontVariationSettings&) = delete;

  unsigned GetHash() const;

private:
  FontVariationSettings() = default;
};

} // namespace blink
