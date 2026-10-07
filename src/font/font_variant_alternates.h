// Ported from: blink/renderer/platform/fonts/font_variant_alternates.h
// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>

#include <functional>
#include "resolved_font_features.h"
#include "base/text/atomic_string.h"

#include <cstdint>
#include "base/text/wtf_string.h"

namespace bkit {

class FontVariantAlternates {
public:
  static std::shared_ptr<FontVariantAlternates> Create() {
    return std::shared_ptr<FontVariantAlternates>(new FontVariantAlternates());
  }

  void SetStylistic(AtomicString);
  void SetHistoricalForms();
  void SetSwash(AtomicString);
  void SetOrnaments(AtomicString);
  void SetAnnotation(AtomicString);

  void SetStyleset(Vector<AtomicString>);
  void SetCharacterVariant(Vector<AtomicString>);

  const AtomicString* Stylistic() const {
    return stylistic_ ? &(*stylistic_) : nullptr;
  }
  bool HistoricalForms() const {
    return historical_forms_;
  }
  const AtomicString* Swash() const {
    return swash_ ? &(*swash_) : nullptr;
  }
  const AtomicString* Ornaments() const {
    return ornaments_ ? &(*ornaments_) : nullptr;
  }
  const AtomicString* Annotation() const {
    return annotation_ ? &(*annotation_) : nullptr;
  }

  const Vector<AtomicString>& Styleset() const {
    return styleset_;
  }
  const Vector<AtomicString>& CharacterVariant() const {
    return character_variant_;
  }

  using ResolverFunction =
      std::function<Vector<uint32_t>(const AtomicString&)>;
  /* Resolves the internal feature configuration with aliases against resolution
   * functions to get the actual OpenType feature indices for each alias.
   * Produces a resolved copy on which it is possible to call
   * GetResolvedFontFeatures(). Can be called with empty resolution functions
   * for converting just the historical-forms flag to a resolved OpenType
   * feature. */
  std::shared_ptr<FontVariantAlternates> Resolve(
      ResolverFunction resolve_stylistic,
      ResolverFunction resolve_styleset_,
      ResolverFunction resolve_character_variant,
      ResolverFunction resolve_swash,
      ResolverFunction resolve_ornaments,
      ResolverFunction resolve_annotation) const;

  const ResolvedFontFeatures& GetResolvedFontFeatures() const;

  unsigned GetHash() const;

  bool IsNormal() const;

  bool operator==(const FontVariantAlternates& other) const;
  bool operator!=(const FontVariantAlternates& other) const {
    return !(*this == other);
  }

private:
  FontVariantAlternates();

  // RefCounted<> has a deleted copy constructor. Since we're inheriting
  // from it, we can't re-enable it and have to manually implement a
  // Clone() method for the case of making a changed copy in
  // CSSFontSelector.
  static std::shared_ptr<FontVariantAlternates> Clone(
      const FontVariantAlternates& other);

  std::optional<AtomicString> stylistic_ = std::nullopt;
  std::optional<AtomicString> swash_ = std::nullopt;
  std::optional<AtomicString> ornaments_ = std::nullopt;
  std::optional<AtomicString> annotation_ = std::nullopt;

  Vector<AtomicString> styleset_;
  Vector<AtomicString> character_variant_;
  bool historical_forms_ = false;

  ResolvedFontFeatures resolved_features_;
};

} // namespace bkit
