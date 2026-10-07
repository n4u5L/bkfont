// Local implementation: standalone font initialization API.
// Upstream reference: blink/renderer/platform/exported/platform.cc
// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

namespace bkit {

// Call exactly once on the main thread before constructing any font or WTF
// string. ICU data and the host allocation/container implementations must be
// available before this call. There is no Blink renderer or Oilpan lifetime.
// Native matching uses DirectWrite on Windows and Fontconfig on Linux; glyph
// metrics, shaping and rasterization share the Linux/FreeType implementation.
// The host supplies system font metadata through FontCache's platform setters.
// Rendering uses fixed common defaults, not OS antialiasing preferences.
// Use InlineFormattingContext::SetZoomFactors before layout/paint on a display.
// Low-level clients update FontCache::UpdateDeviceScaleFactor and resolve
// computed font sizes/lengths with DSF before shaping. Paint in framebuffer
// coordinates, with no additional root-canvas device-scale transform.
void InitializeFonts();

} // namespace bkit
