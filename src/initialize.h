// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

namespace bkfont {

// Call exactly once on the main thread before constructing any font or WTF
// string. ICU data and the host allocation/container implementations must be
// available before this call. There is no Blink renderer or Oilpan lifetime.
// The embedding host supplies system font families/heights and antialiasing
// preferences through FontCache's setters, as RendererPreferences did upstream.
void InitializeFonts();

} // namespace bkfont
