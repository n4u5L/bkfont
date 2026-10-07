// Ported from: blink/renderer/platform/exported/platform.cc
// InitializeBlink and
// InitializeMainThreadCommon. Only the font/string initialization is retained.

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "initialize.h"

#include "font/font_family_names.h"
#include "geometry/length.h"
#include "language.h"
#include "base/allocator/partitions.h"
#include "base/wtf.h"

namespace bkit {

void InitializeFonts() {
  Partitions::Initialize();
  InitializeBase();
  Length::Initialize();
  font_family_names::Init();
  InitializePlatformLanguage();
}

} // namespace bkit
