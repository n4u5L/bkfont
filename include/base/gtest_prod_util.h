// Ported from Chromium: base/gtest_prod_util.h.
// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

// Expand GoogleTest's FRIEND_TEST directly so public library headers do not
// require GoogleTest. The generated friend names match the upstream macros.
#define FRIEND_TEST_ALL_PREFIXES(test_case_name, test_name)    \
  friend class test_case_name##_##test_name##_Test;            \
  friend class test_case_name##_##DISABLED_##test_name##_Test; \
  friend class test_case_name##_##FLAKY_##test_name##_Test

#define FORWARD_DECLARE_TEST(test_case_name, test_name) \
  class test_case_name##_##test_name##_Test;            \
  class test_case_name##_##DISABLED_##test_name##_Test; \
  class test_case_name##_##FLAKY_##test_name##_Test
