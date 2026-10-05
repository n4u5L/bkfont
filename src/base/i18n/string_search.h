// Ported from: chromium/base/i18n/string_search.h
// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

#include <string>
#include <string_view>
#include <memory>

#include "base/compiler_specific.h"
#include "base/i18n/base_i18n_export.h"

struct UStringSearch;

namespace bkfont::base {

namespace i18n {

// The ICU search object is owned, not a Chromium-instrumented borrowed pointer.
struct StringSearchDeleter {
  void operator()(UStringSearch* search) const;
};

// Returns true if |in_this| contains |find_this|. If |match_index| or
// |match_length| are non-NULL, they are assigned the start position and total
// length of the match.
//
// Only differences between base letters are taken into consideration. Case and
// accent differences are ignored. Please refer to 'primary level' in
// http://userguide.icu-project.org/collation/concepts for additional details.

bool StringSearchIgnoringCaseAndAccents(std::u16string find_this,
                                        std::u16string_view in_this,
                                        size_t* match_index,
                                        size_t* match_length);

// Returns true if |in_this| contains |find_this|. If |match_index| or
// |match_length| are non-NULL, they are assigned the start position and total
// length of the match.
//
// When |case_sensitive| is false, only differences between base letters are
// taken into consideration. Case and accent differences are ignored.
// Please refer to 'primary level' in
// http://userguide.icu-project.org/collation/concepts for additional details.
// When |forward_search| is true, finds the first instance of |find_this|,
// otherwise finds the last instance

bool StringSearch(std::u16string find_this,
                  std::u16string_view in_this,
                  size_t* match_index,
                  size_t* match_length,
                  bool case_sensitive,
                  bool forward_search);

// This class is for speeding up multiple StringSearch()
// with the same |find_this| argument. |find_this| is passed as the constructor
// argument, and precomputation for searching is done only at that time.
class GSL_POINTER FixedPatternStringSearch {
public:
  FixedPatternStringSearch(std::u16string find_this, bool case_sensitive);
  ~FixedPatternStringSearch();

  // Returns true if |in_this| contains |find_this|. If |match_index| or
  // |match_length| are non-NULL, they are assigned the start position and total
  // length of the match.
  bool Search(std::u16string_view in_this,
              size_t* match_index,
              size_t* match_length,
              bool forward_search);

private:
  std::u16string find_this_;
  std::unique_ptr<UStringSearch, StringSearchDeleter> search_;
};

// This class is for speeding up multiple StringSearchIgnoringCaseAndAccents()
// with the same |find_this| argument. |find_this| is passed as the constructor
// argument, and precomputation for searching is done only at that time.
class GSL_POINTER
    FixedPatternStringSearchIgnoringCaseAndAccents {
public:
  explicit FixedPatternStringSearchIgnoringCaseAndAccents(
      std::u16string find_this);

  // Returns true if |in_this| contains |find_this|. If |match_index| or
  // |match_length| are non-NULL, they are assigned the start position and total
  // length of the match.
  bool Search(std::u16string_view in_this,
              size_t* match_index,
              size_t* match_length);

private:
  FixedPatternStringSearch base_search_;
};

// This class is for performing all matches of `find_this` in `in_this`.
// `find_this` and `in_this` are passed as arguments in constructor.
class GSL_POINTER RepeatingStringSearch {
public:
  RepeatingStringSearch(std::u16string find_this,
                        std::u16string in_this,
                        bool case_sensitive);
  ~RepeatingStringSearch();

  // Returns true if the next match exists. `match_index` and `match_length` are
  // assigned the start position and total length of the match.
  bool NextMatchResult(int& match_index, int& match_length);

private:
  std::u16string find_this_;
  std::u16string in_this_;
  std::unique_ptr<UStringSearch, StringSearchDeleter> search_;
};

} // namespace i18n

} // namespace bkfont::base
