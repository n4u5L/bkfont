/*
 * Copyright 2011, 2012, 2014 Google Inc.
 * BSD license retained in dwrite_internal.h.
 * Source: third_party/skia/src/ports/SkScalerContext_win_dw.cpp;
 * src/core/SkScalerContext.cpp, SkStrikeSpec.cpp, SkFont.cpp,
 * SkTextFormatParams.h; src/utils/win/SkDWriteGeometrySink.cpp.
 *
 * This is the Windows font measurement subset, for Blink's identity-device
 * SkFont getWidth/getBounds/getMetrics calls. It keeps scaler mode selection,
 * canonical sizing, synthetic style parameters, color-font precedence,
 * outward rounding and bounded glyph storage. No Skia runtime is used.
 * Synthetic stroke geometry uses Direct2D, whose curve subdivision and bounds
 * can differ from Skia's stroker. Such results are native geometry results,
 * not a claim of numerical equivalence to the removed Skia renderer.
 */
#include "dwrite_internal.h"

#include <d2d1.h>
#include <wincodec.h>

#include <bit>
#include <cmath>
#include <optional>

namespace bkfont {
namespace {

struct Bounds {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;
  bool Empty() const {
    return !(left < right && top < bottom);
  }
  void Join(const Bounds& other) {
    if (other.Empty())
      return;
    if (Empty()) {
      *this = other;
      return;
    }
    left = std::min(left, other.left);
    top = std::min(top, other.top);
    right = std::max(right, other.right);
    bottom = std::max(bottom, other.bottom);
  }
  void RoundOut() {
    left = std::floor(left);
    top = std::floor(top);
    right = std::ceil(right);
    bottom = std::ceil(bottom);
  }
};

D2D1_POINT_2F MapPoint(const DWRITE_MATRIX& matrix, D2D1_POINT_2F point) {
  return {matrix.m11 * point.x + matrix.m21 * point.y + matrix.dx,
          matrix.m12 * point.x + matrix.m22 * point.y + matrix.dy};
}

Bounds MapBounds(const DWRITE_MATRIX& matrix, const Bounds& bounds) {
  const D2D1_POINT_2F points[] = {
      MapPoint(matrix, {bounds.left, bounds.top}),
      MapPoint(matrix, {bounds.right, bounds.top}),
      MapPoint(matrix, {bounds.right, bounds.bottom}),
      MapPoint(matrix, {bounds.left, bounds.bottom})};
  Bounds result{points[0].x, points[0].y, points[0].x, points[0].y};
  for (const D2D1_POINT_2F& point : points) {
    result.left = std::min(result.left, point.x);
    result.top = std::min(result.top, point.y);
    result.right = std::max(result.right, point.x);
    result.bottom = std::max(result.bottom, point.y);
  }
  return result;
}

DWRITE_MATRIX Concat(const DWRITE_MATRIX& first, const DWRITE_MATRIX& second) {
  return {first.m11 * second.m11 + first.m21 * second.m12,
          first.m12 * second.m11 + first.m22 * second.m12,
          first.m11 * second.m21 + first.m21 * second.m22,
          first.m12 * second.m21 + first.m22 * second.m22,
          first.m11 * second.dx + first.m21 * second.dy + first.dx,
          first.m12 * second.dx + first.m22 * second.dy + first.dy};
}

constexpr DWRITE_MATRIX kIdentity{1, 0, 0, 1, 0, 0};

std::uint16_t Read16(const std::uint8_t* bytes) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
}
std::uint32_t Read32(const std::uint8_t* bytes) {
  return (static_cast<std::uint32_t>(Read16(bytes)) << 16) | Read16(bytes + 2);
}

struct GaspRange {
  int minimum;
  int maximum;
  int version;
  std::uint16_t flags;
};

// get_gasp_range / is_gridfit_only.
bool GetGaspRange(const FontFace& face, int size, GaspRange* range) {
  const Vector<std::uint8_t> table = face.TableData(0x67617370u);
  if (table.size() < 4)
    return false;
  const std::uint16_t version = Read16(table.data());
  const std::uint16_t count = Read16(table.data() + 2);
  if ((version != 0 && version != 1) || count > 1024 || table.size() < 4u + 4u * count)
    return false;
  int minimum = -1;
  for (std::uint16_t i = 0; i < count; ++i) {
    const std::uint8_t* record = table.data() + 4 + 4 * i;
    const int maximum = Read16(record);
    if (minimum < size && size <= maximum) {
      *range = {minimum + 1, maximum, version, Read16(record + 2)};
      return true;
    }
    minimum = maximum;
  }
  return false;
}

// has_bitmap_strike. The early returns and EBLC raw glyph-index comparison
// follow the source (including its endian-sensitive comparison).
bool HasBitmapStrike(const FontFace& face, const GaspRange& range) {
  const Vector<std::uint8_t> eblc = face.TableData(0x45424c43u);
  if (eblc.size() < 8 || Read32(eblc.data()) != 0x00020000u)
    return false;
  const std::uint32_t count = Read32(eblc.data() + 4);
  if (count > 1024 || eblc.size() < 8u + 48u * count)
    return false;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint8_t* record = eblc.data() + 8 + 48 * i;
    if (record[44] == record[45] && range.minimum <= record[44] && record[44] <= range.maximum) {
      std::uint16_t first = 0;
      std::uint16_t last = 0;
      std::memcpy(&first, record + 40, sizeof(first));
      std::memcpy(&last, record + 42, sizeof(last));
      if (last >= first + 3)
        return true;
    }
  }
  // SkOTTableEmbeddedBitmapScaling spells its source TAG "ESBC". Retain it;
  // changing this to the specification's EBSC would alter upstream behavior.
  const Vector<std::uint8_t> ebsc = face.TableData(0x45534243u);
  if (ebsc.size() < 8 || Read32(ebsc.data()) != 0x00020000u)
    return false;
  const std::uint32_t scaling_count = Read32(ebsc.data() + 4);
  if (scaling_count > 1024 || ebsc.size() < 8u + 28u * scaling_count)
    return false;
  for (std::uint32_t i = 0; i < scaling_count; ++i) {
    const std::uint8_t* record = ebsc.data() + 8 + 28 * i;
    if (record[24] == record[25] && range.minimum <= record[24] && record[24] <= range.maximum)
      return true;
  }
  return false;
}

bool IsHinted(const FontFace& face) {
  const Vector<std::uint8_t> maxp = face.TableData(0x6d617870u);
  return maxp.size() >= 32 && Read32(maxp.data()) == 0x00010000u && Read16(maxp.data() + 26) != 0;
}

// SkFloatingPoint<float, 10>::AlmostEquals used by SkDWriteGeometrySink.
bool ApproximatelyEqual(float first, float second) {
  if (std::isnan(first) || std::isnan(second))
    return false;
  const auto biased = [](float number) -> std::uint32_t {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(number);
    return bits & 0x80000000u ? ~bits + 1 : 0x80000000u | bits;
  };
  const std::uint32_t a = biased(first);
  const std::uint32_t b = biased(second);
  return (a >= b ? a - b : b - a) <= 10;
}

// SkDWriteGeometrySink's degenerate-segment removal and cubic-to-quadratic
// recovery are retained because SkPath::getBounds includes control points.
// The optional D2D sink receives the same normalized geometry for bold stroke.
class OutlineBoundsSink final : public IDWriteGeometrySink {
public:
  explicit OutlineBoundsSink(const DWRITE_MATRIX& transform,
                             ID2D1GeometrySink* sink = nullptr)
      : transform_(transform),
        sink_(sink) {
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
    if (!result)
      return E_INVALIDARG;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteGeometrySink)) {
      *result = static_cast<IDWriteGeometrySink*>(this);
      AddRef();
      return S_OK;
    }
    *result = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&references_);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG count = InterlockedDecrement(&references_);
    if (!count)
      delete this;
    return count;
  }
  void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE mode) override {
    if (sink_)
      sink_->SetFillMode(mode);
  }
  void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {
  }
  void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) override {
    started_ = false;
    current_ = start;
  }
  void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT count) override {
    for (UINT i = 0; i < count; ++i) {
      if (!Different(points[i]))
        continue;
      GoingTo(points[i]);
      const D2D1_POINT_2F point = AddPoint(points[i]);
      if (sink_)
        sink_->AddLine(point);
    }
  }
  void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* curves, UINT count) override {
    for (UINT i = 0; i < count; ++i) {
      const D2D1_BEZIER_SEGMENT& curve = curves[i];
      if (!Different(curve.point1) && !Different(curve.point2) && !Different(curve.point3))
        continue;
      const D2D1_POINT_2F middle{
          current_.x + (curve.point1.x - current_.x) * 3 / 2,
          current_.y + (curve.point1.y - current_.y) * 3 / 2};
      const bool quadratic = ApproximatelyEqual(middle.x,
                                                (curve.point2.x - curve.point3.x) * 3 / 2 + curve.point3.x)
                             && ApproximatelyEqual(middle.y,
                                                   (curve.point2.y - curve.point3.y) * 3 / 2 + curve.point3.y);
      GoingTo(curve.point3);
      if (quadratic) {
        const D2D1_QUADRATIC_BEZIER_SEGMENT converted{AddPoint(middle), AddPoint(curve.point3)};
        if (sink_)
          sink_->AddQuadraticBezier(converted);
      } else {
        const D2D1_BEZIER_SEGMENT converted{
            AddPoint(curve.point1),
            AddPoint(curve.point2),
            AddPoint(curve.point3)};
        if (sink_)
          sink_->AddBezier(converted);
      }
    }
  }
  void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) override {
    if (started_ && sink_)
      sink_->EndFigure(D2D1_FIGURE_END_CLOSED);
  }
  HRESULT STDMETHODCALLTYPE Close() override {
    return S_OK;
  }
  Bounds GetBounds() const {
    return has_points_ ? bounds_ : Bounds{};
  }

private:
  ~OutlineBoundsSink() = default;
  bool Different(D2D1_POINT_2F point) const {
    return point.x != current_.x || point.y != current_.y;
  }
  void GoingTo(D2D1_POINT_2F point) {
    if (!started_) {
      started_ = true;
      const D2D1_POINT_2F first = AddPoint(current_);
      if (sink_)
        sink_->BeginFigure(first, D2D1_FIGURE_BEGIN_FILLED);
    }
    current_ = point;
  }
  D2D1_POINT_2F AddPoint(D2D1_POINT_2F point) {
    point = MapPoint(transform_, point);
    if (!has_points_) {
      bounds_ = {point.x, point.y, point.x, point.y};
      has_points_ = true;
    } else {
      bounds_.left = std::min(bounds_.left, point.x);
      bounds_.top = std::min(bounds_.top, point.y);
      bounds_.right = std::max(bounds_.right, point.x);
      bounds_.bottom = std::max(bounds_.bottom, point.y);
    }
    return point;
  }
  ULONG references_ = 1;
  DWRITE_MATRIX transform_;
  ComPtr<ID2D1GeometrySink> sink_;
  bool started_ = false;
  bool has_points_ = false;
  D2D1_POINT_2F current_{0, 0};
  Bounds bounds_;
};

class NativeFontScaler final {
public:
  NativeFontScaler(const FontFace& public_face, IDWriteFactory* factory,
                   IDWriteFontFace* face, float size, FontRenderOptions options)
      : public_face_(public_face),
        factory_(factory),
        face_(face),
        options_(options) {
    factory_->QueryInterface(IID_PPV_ARGS(&factory2_));
    face_->QueryInterface(IID_PPV_ARGS(&face2_));
    face_->QueryInterface(IID_PPV_ARGS(&face4_));
    // SkFont::valid_size and SkStrikeSpec::MakeCanonicalized/ShouldDrawAsPath.
    size = std::max(0.0f, size);
    const float skew = options.synthetic_italic ? -0.25f : 0;
    float text_size = size;
    if (size * size > 256.0f * 256.0f || size * size * (1 + skew * skew) > 256.0f * 256.0f) {
      source_scale_ = size / 64;
      text_size = 64;
      options_.embedded_bitmaps = false;
      options_.subpixel_positioning = true;
      options_.hinting = false;
      if (options_.subpixel_rendering) {
        options_.subpixel_rendering = false;
        options_.anti_alias = true;
      }
    }
    float real_size = text_size;
    transform_ = {1, 0, skew, 1, 0, 0};
    // computeMatrices' singular/nonfinite branch, with identity device matrix.
    if (text_size <= 1.0f / 4096 || !std::isfinite(text_size)) {
      real_size = 1;
      transform_ = {0, 0, 0, 0, 0, 0};
    }
    float gdi_size = std::floor(real_size * 64 + 0.5f) / 64;
    if (gdi_size == 0)
      gdi_size = 1;
    bool bitmap = false;
    if (options_.embedded_bitmaps) {
      const int ppem = static_cast<int>(gdi_size);
      GaspRange bitmap_range{ppem, ppem, 0, 0};
      if (GetGaspRange(public_face_, ppem, &bitmap_range) && bitmap_range.flags != 1)
        bitmap_range = {ppem, ppem, 0, 0};
      bitmap = HasBitmapStrike(public_face_, bitmap_range);
    }
    GaspRange gasp{0, 0xffff, 0, 0};
    const bool aliased = !options_.anti_alias && !options_.subpixel_rendering;
    if (aliased) {
      render_size_ = measure_size_ = gdi_size;
      rendering_ = DWRITE_RENDERING_MODE_ALIASED;
      texture_ = DWRITE_TEXTURE_ALIASED_1x1;
      measuring_ = DWRITE_MEASURING_MODE_GDI_CLASSIC;
    } else if (bitmap && !options_.synthetic_italic) {
      render_size_ = measure_size_ = gdi_size;
      rendering_ = DWRITE_RENDERING_MODE_GDI_CLASSIC;
      measuring_ = DWRITE_MEASURING_MODE_GDI_CLASSIC;
    } else if (bitmap) {
      render_size_ = measure_size_ = gdi_size;
      rendering_ = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
      measuring_ = DWRITE_MEASURING_MODE_GDI_CLASSIC;
    } else if (GetGaspRange(public_face_, static_cast<int>(std::floor(gdi_size + 0.5f)), &gasp) && gasp.version >= 1) {
      render_size_ = measure_size_ = real_size;
      rendering_ = gasp.flags & 8 ? DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC : DWRITE_RENDERING_MODE_NATURAL;
    } else if (real_size > 20 || !IsHinted(public_face_)) {
      render_size_ = measure_size_ = real_size;
      rendering_ = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
    } else {
      render_size_ = gdi_size;
      measure_size_ = real_size;
      rendering_ = DWRITE_RENDERING_MODE_NATURAL;
    }
    // MakeCanonicalized uses unknown surface pixel geometry. Requested LCD is
    // converted to A8-from-LCD; only plain A8 takes DWrite2 grayscale here.
    a8_from_lcd_ = options_.subpixel_rendering;
    if (factory2_ && face2_ && options_.anti_alias && !a8_from_lcd_) {
      texture_ = DWRITE_TEXTURE_ALIASED_1x1;
      antialias_ = DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
    }
    // DWriteFontTypeface::onFilterRec forces hinting when DWrite2 is absent.
    if (!options_.hinting && factory2_ && face2_) {
      grid_fit_ = DWRITE_GRID_FIT_MODE_DISABLED;
      if (rendering_ != DWRITE_RENDERING_MODE_ALIASED)
        rendering_ = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
    }
    if (options_.linear_metrics) {
      measure_size_ = real_size;
      measuring_ = DWRITE_MEASURING_MODE_NATURAL;
    }
    if (measuring_ != DWRITE_MEASURING_MODE_NATURAL && public_face_.HasTable(0x43424454u))
      measuring_ = DWRITE_MEASURING_MODE_NATURAL;
    if (options_.synthetic_bold) {
      // useStrokeForFakeBold and SkTextFormatParams.h (width, not radius).
      const float fraction = text_size <= 9 ? 1.0f / 24 : text_size >= 36 ? 1.0f / 32
                                                                          : (1.0f / 24 + (text_size - 9) / 27 * (1.0f / 32 - 1.0f / 24));
      stroke_width_ = text_size * fraction;
    }
  }

  PlatformFontMetrics FontMetrics() const;
  bool GlyphMetrics(std::uint16_t glyph, PlatformGlyphMetrics* result) const;

private:
  bool RasterBounds(std::uint16_t glyph, DWRITE_RENDERING_MODE rendering,
                    DWRITE_TEXTURE_TYPE texture, Bounds* result) const;
  bool OutlineBounds(std::uint16_t glyph, bool bold, Bounds* result) const;
  bool ColorBounds(std::uint16_t glyph, Bounds* result) const;
  bool ColorV1Bounds(std::uint16_t glyph, Bounds* result) const;
  bool PngBounds(std::uint16_t glyph, Bounds* result) const;
  bool SvgBounds(std::uint16_t glyph, Bounds* result) const;
#if defined(NTDDI_WIN11_ZN) && NTDDI_VERSION >= NTDDI_WIN11_ZN
  bool ColorV1PaintBounds(DWRITE_MATRIX* matrix, Bounds* bounds,
                          IDWritePaintReader& reader,
                          const DWRITE_PAINT_ELEMENT& element) const;
#endif
  const FontFace& public_face_;
  ComPtr<IDWriteFactory> factory_;
  ComPtr<IDWriteFactory2> factory2_;
  ComPtr<IDWriteFontFace> face_;
  ComPtr<IDWriteFontFace2> face2_;
  ComPtr<IDWriteFontFace4> face4_;
  FontRenderOptions options_;
  DWRITE_MATRIX transform_ = kIdentity;
  float source_scale_ = 1;
  float render_size_ = 0;
  float measure_size_ = 0;
  float stroke_width_ = 0;
  bool a8_from_lcd_ = false;
  DWRITE_RENDERING_MODE rendering_ = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
  DWRITE_TEXTURE_TYPE texture_ = DWRITE_TEXTURE_CLEARTYPE_3x1;
  DWRITE_MEASURING_MODE measuring_ = DWRITE_MEASURING_MODE_NATURAL;
  DWRITE_TEXT_ANTIALIAS_MODE antialias_ = DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
  DWRITE_GRID_FIT_MODE grid_fit_ = DWRITE_GRID_FIT_MODE_ENABLED;
};

bool NativeFontScaler::RasterBounds(std::uint16_t glyph,
                                    DWRITE_RENDERING_MODE rendering, DWRITE_TEXTURE_TYPE texture,
                                    Bounds* result) const {
  // SkScalerContext_DW::generateDWMetrics, at the zero subpixel offset used by
  // SkFont getBounds/getWidths (the paint/device raster cache is out of scope).
  const FLOAT advance = 0;
  const DWRITE_GLYPH_OFFSET offset{0, 0};
  const DWRITE_GLYPH_RUN run{face_.Get(), render_size_, 1, &glyph, &advance, &offset, FALSE, 0};
  ComPtr<IDWriteGlyphRunAnalysis> analysis;
  HRESULT status = S_OK;
  if (factory2_ && (grid_fit_ == DWRITE_GRID_FIT_MODE_DISABLED || antialias_ == DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE)) {
    status = factory2_->CreateGlyphRunAnalysis(&run, &transform_, rendering, measuring_, grid_fit_, antialias_, 0, 0, &analysis);
  } else {
    status = factory_->CreateGlyphRunAnalysis(&run, 1, &transform_, rendering, measuring_, 0, 0, &analysis);
  }
  RECT rectangle{};
  if (FAILED(status) || FAILED(analysis->GetAlphaTextureBounds(texture, &rectangle)) || rectangle.left >= rectangle.right || rectangle.top >= rectangle.bottom)
    return false;
  *result = {static_cast<float>(rectangle.left), static_cast<float>(rectangle.top), static_cast<float>(rectangle.right), static_cast<float>(rectangle.bottom)};
  return true;
}

bool NativeFontScaler::OutlineBounds(std::uint16_t glyph, bool bold,
                                     Bounds* result) const {
  ComPtr<ID2D1Factory> geometry_factory;
  ComPtr<ID2D1PathGeometry> geometry;
  ComPtr<ID2D1GeometrySink> geometry_sink;
  if (bold && stroke_width_ > 0) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 __uuidof(ID2D1Factory),
                                 nullptr,
                                 reinterpret_cast<void**>(geometry_factory.GetAddressOf())))
        || FAILED(geometry_factory->CreatePathGeometry(&geometry)) || FAILED(geometry->Open(&geometry_sink)))
      return false;
  }
  ComPtr<OutlineBoundsSink> sink;
  sink.Attach(new OutlineBoundsSink(transform_, geometry_sink.Get()));
  if (FAILED(face_->GetGlyphRunOutline(render_size_, &glyph, nullptr, nullptr, 1, FALSE, FALSE, sink.Get())))
    return false;
  *result = sink->GetBounds();
  if (geometry_sink) {
    if (FAILED(geometry_sink->Close()))
      return false;
    D2D1_STROKE_STYLE_PROPERTIES properties{};
    properties.startCap = D2D1_CAP_STYLE_FLAT;
    properties.endCap = D2D1_CAP_STYLE_FLAT;
    properties.dashCap = D2D1_CAP_STYLE_FLAT;
    properties.lineJoin = D2D1_LINE_JOIN_MITER;
    properties.miterLimit = 4;
    properties.dashStyle = D2D1_DASH_STYLE_SOLID;
    ComPtr<ID2D1StrokeStyle> stroke;
    if (FAILED(geometry_factory->CreateStrokeStyle(properties, nullptr, 0, &stroke)))
      return false;
    D2D1_RECT_F widened{};
    // This is the deliberate native replacement for SkStrokeRec::applyToPath.
    // Width and miter parameters come from useStrokeForFakeBold, but Direct2D's
    // flattening/bound computation may differ from Skia's output control points.
    if (FAILED(geometry->GetWidenedBounds(stroke_width_, stroke.Get(), nullptr, D2D1_DEFAULT_FLATTENING_TOLERANCE, &widened)))
      return false;
    result->Join({widened.left, widened.top, widened.right, widened.bottom});
  }
  return true;
}

bool NativeFontScaler::ColorBounds(std::uint16_t glyph, Bounds* result) const {
  // generateColorMetrics / getColorGlyphRun. As upstream, palette index zero
  // selects the layer geometry; palette color overrides do not affect bounds.
  if (!factory2_)
    return false;
  const FLOAT advance = 0;
  const DWRITE_GLYPH_OFFSET offset{0, 0};
  const DWRITE_GLYPH_RUN run{face_.Get(), render_size_, 1, &glyph, &advance, &offset, FALSE, 0};
  ComPtr<IDWriteColorGlyphRunEnumerator> layers;
  if (FAILED(factory2_->TranslateColorGlyphRun(0, 0, &run, nullptr, measuring_, &transform_, 0, &layers)))
    return false;
  Bounds bounds;
  BOOL has_next = FALSE;
  while (SUCCEEDED(layers->MoveNext(&has_next)) && has_next) {
    const DWRITE_COLOR_GLYPH_RUN* layer = nullptr;
    if (FAILED(layers->GetCurrentRun(&layer)))
      return false;
    ComPtr<OutlineBoundsSink> sink;
    sink.Attach(new OutlineBoundsSink(kIdentity));
    const DWRITE_GLYPH_RUN& color_run = layer->glyphRun;
    if (FAILED(color_run.fontFace->GetGlyphRunOutline(color_run.fontEmSize,
                                                      color_run.glyphIndices,
                                                      color_run.glyphAdvances,
                                                      color_run.glyphOffsets,
                                                      color_run.glyphCount,
                                                      color_run.isSideways,
                                                      color_run.bidiLevel % 2,
                                                      sink.Get())))
      return false;
    bounds.Join(sink->GetBounds());
  }
  *result = MapBounds(transform_, bounds);
  return true;
}

class GlyphImageData final {
public:
  GlyphImageData(IDWriteFontFace4* face, std::uint16_t glyph, float size,
                 DWRITE_GLYPH_IMAGE_FORMATS format)
      : face_(face) {
    valid_ = SUCCEEDED(face_->GetGlyphImageData(glyph, static_cast<UINT32>(size), format, &data, &context_));
  }
  ~GlyphImageData() {
    if (valid_)
      face_->ReleaseGlyphImageData(context_);
  }
  bool Valid() const {
    return valid_;
  }
  DWRITE_GLYPH_IMAGE_DATA data{};

private:
  ComPtr<IDWriteFontFace4> face_;
  void* context_ = nullptr;
  bool valid_ = false;
};

bool NativeFontScaler::PngBounds(std::uint16_t glyph, Bounds* result) const {
  if (!face4_)
    return false;
  DWRITE_GLYPH_IMAGE_FORMATS formats{};
  if (FAILED(face4_->GetGlyphImageFormats(glyph, 0, UINT32_MAX, &formats)) || !(formats & DWRITE_GLYPH_IMAGE_FORMATS_PNG))
    return false;
  GlyphImageData image(face4_.Get(), glyph, render_size_, DWRITE_GLYPH_IMAGE_FORMATS_PNG);
  if (!image.Valid())
    return false;
  // generatePngMetrics decoded the PNG's info through SkPngDecoder. WIC is the
  // native decoder substitution; malformed-data acceptance may differ.
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  struct ComApartment {
    HRESULT status;
    ~ComApartment() {
      if (SUCCEEDED(status)) CoUninitialize();
    }
  } apartment{initialized};
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    return false;
  ComPtr<IWICImagingFactory> codec_factory;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&codec_factory))) || FAILED(codec_factory->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(static_cast<const BYTE*>(image.data.imageData)), image.data.imageDataSize)) || FAILED(codec_factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) || FAILED(decoder->GetFrame(0, &frame)))
    return false;
  UINT width = 0;
  UINT height = 0;
  if (FAILED(frame->GetSize(&width, &height)))
    return false;
  const float scale = render_size_ / image.data.pixelsPerEm;
  DWRITE_MATRIX matrix = Concat(transform_, {scale, 0, 0, scale, -scale * image.data.horizontalLeftOrigin.x, -scale * image.data.horizontalLeftOrigin.y});
  *result = MapBounds(matrix, {0, 0, static_cast<float>(width), static_cast<float>(height)});
  return true;
}

bool NativeFontScaler::SvgBounds(std::uint16_t glyph, Bounds* result) const {
  // drawSVGImage requires a registered OpenType SVG decoder, even upstream.
  // This independent font library exposes that integration point without
  // carrying Blink's renderer or replacing SVG ink bounds with a design box.
  if (!face4_ || !options_.svg_bounds_provider)
    return false;
  DWRITE_GLYPH_IMAGE_FORMATS formats{};
  if (FAILED(face4_->GetGlyphImageFormats(glyph, 0, UINT32_MAX, &formats)) || !(formats & DWRITE_GLYPH_IMAGE_FORMATS_SVG))
    return false;
  GlyphImageData image(face4_.Get(), glyph, render_size_, DWRITE_GLYPH_IMAGE_FORMATS_SVG);
  if (!image.Valid())
    return false;
  const std::uint16_t units = public_face_.UnitsPerEm();
  const float scale = render_size_ / units;
  const DWRITE_MATRIX matrix = Concat(transform_, {scale, 0, 0, scale, -scale * image.data.horizontalLeftOrigin.x, -scale * image.data.horizontalLeftOrigin.y});
  const Vector<std::uint32_t> palette = public_face_.PaletteColors();
  PlatformGlyphMetrics measured;
  if (!options_.svg_bounds_provider->Measure(
          {static_cast<const std::uint8_t*>(image.data.imageData), image.data.imageDataSize},
          units,
          glyph,
          options_.foreground_color,
          {palette.data(), palette.size()},
          {matrix.m11, matrix.m12, matrix.m21, matrix.m22, matrix.dx, matrix.dy},
          &measured))
    return false;
  *result = {measured.left, measured.top, measured.left + measured.width, measured.top + measured.height};
  result->RoundOut();
  return true;
}

#if defined(NTDDI_WIN11_ZN) && NTDDI_VERSION >= NTDDI_WIN11_ZN
bool NativeFontScaler::ColorV1PaintBounds(DWRITE_MATRIX* matrix, Bounds* bounds,
                                          IDWritePaintReader& reader, const DWRITE_PAINT_ELEMENT& element) const {
  // generateColorV1PaintBounds, retaining child traversal and clip behavior.
  const DWRITE_MATRIX previous = *matrix;
  struct Restore {
    DWRITE_MATRIX* target;
    DWRITE_MATRIX value;
    ~Restore() {
      *target = value;
    }
  } restore{matrix, previous};
  const auto children = [&](UINT32 count) -> bool {
    if (!count)
      return true;
    DWRITE_PAINT_ELEMENT child{};
    if (FAILED(reader.MoveToFirstChild(&child)))
      return false;
    ColorV1PaintBounds(matrix, bounds, reader, child);
    for (UINT32 i = 1; i < count; ++i) {
      if (FAILED(reader.MoveToNextSibling(&child)))
        return false;
      ColorV1PaintBounds(matrix, bounds, reader, child);
    }
    return SUCCEEDED(reader.MoveToParent());
  };
  switch (element.paintType) {
  case DWRITE_PAINT_TYPE_NONE:
    return false;
  case DWRITE_PAINT_TYPE_LAYERS:
    return children(element.paint.layers.childCount);
  case DWRITE_PAINT_TYPE_SOLID_GLYPH:
  case DWRITE_PAINT_TYPE_GLYPH: {
    const UINT16 glyph = static_cast<UINT16>(element.paintType == DWRITE_PAINT_TYPE_GLYPH ? element.paint.glyph.glyphIndex : element.paint.solidGlyph.glyphIndex);
    const float inverse = 1 / render_size_;
    const DWRITE_MATRIX mapped = Concat(*matrix, {inverse, 0, 0, inverse, 0, 0});
    ComPtr<OutlineBoundsSink> sink;
    sink.Attach(new OutlineBoundsSink(mapped));
    if (FAILED(face_->GetGlyphRunOutline(render_size_, &glyph, nullptr, nullptr, 1, FALSE, FALSE, sink.Get())))
      return false;
    bounds->Join(sink->GetBounds());
    return true;
  }
  case DWRITE_PAINT_TYPE_SOLID:
  case DWRITE_PAINT_TYPE_LINEAR_GRADIENT:
  case DWRITE_PAINT_TYPE_RADIAL_GRADIENT:
  case DWRITE_PAINT_TYPE_SWEEP_GRADIENT:
    return true;
  case DWRITE_PAINT_TYPE_COLOR_GLYPH: {
    const D2D_RECT_F& clip = element.paint.colorGlyph.clipBox;
    const Bounds rectangle{clip.left, clip.top, clip.right, clip.bottom};
    if (rectangle.Empty())
      return children(1);
    // Upstream calls ctm->mapRect(r) without consuming its return value.
    // Preserve that clip-box branch instead of silently changing the source.
    bounds->Join(rectangle);
    return true;
  }
  case DWRITE_PAINT_TYPE_TRANSFORM: {
    const auto& value = element.paint.transform;
    *matrix = Concat(*matrix, {value.m11, value.m12, value.m21, value.m22, value.dx, value.dy});
    return children(1);
  }
  case DWRITE_PAINT_TYPE_COMPOSITE:
    return children(2);
  default:
    return false;
  }
}
#endif

bool NativeFontScaler::ColorV1Bounds(std::uint16_t glyph, Bounds* result) const {
#if defined(NTDDI_WIN11_ZN) && NTDDI_VERSION >= NTDDI_WIN11_ZN
  ComPtr<IDWriteFontFace7> face7;
  ComPtr<IDWritePaintReader> reader;
  if (FAILED(face_.As(&face7)) || FAILED(face7->CreatePaintReader(DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE, DWRITE_PAINT_FEATURE_LEVEL_COLR_V1, &reader)))
    return false;
  DWRITE_PAINT_ELEMENT element{};
  D2D_RECT_F clip{};
  DWRITE_PAINT_ATTRIBUTES attributes{};
  if (FAILED(reader->SetCurrentGlyph(glyph, &element, &clip, &attributes)) || element.paintType == DWRITE_PAINT_TYPE_NONE)
    return false;
  DWRITE_MATRIX matrix = Concat(transform_, {render_size_, 0, 0, render_size_, 0, 0});
  const Bounds clip_bounds{clip.left, clip.top, clip.right, clip.bottom};
  if (!clip_bounds.Empty()) {
    *result = MapBounds(matrix, clip_bounds);
    return true;
  }
  Bounds bounds;
  if (!ColorV1PaintBounds(&matrix, &bounds, *reader.Get(), element))
    return false;
  *result = bounds;
  return true;
#else
  return false;
#endif
}

template <typename Integer>
Integer Saturate(float value) {
  // SkScalerContext::sk_saturate_cast: NaN selects the upper bound.
  value = value < std::numeric_limits<Integer>::max() ? value : static_cast<float>(std::numeric_limits<Integer>::max());
  value = value > std::numeric_limits<Integer>::min() ? value : static_cast<float>(std::numeric_limits<Integer>::min());
  return static_cast<Integer>(value);
}

bool NativeFontScaler::GlyphMetrics(std::uint16_t glyph,
                                    PlatformGlyphMetrics* result) const {
  *result = {};
  if (glyph >= face_->GetGlyphCount())
    return false;
  DWRITE_GLYPH_METRICS design{};
  HRESULT status = S_OK;
  if (measuring_ == DWRITE_MEASURING_MODE_NATURAL) {
    status = face_->GetDesignGlyphMetrics(&glyph, 1, &design, FALSE);
  } else {
    status = face_->GetGdiCompatibleGlyphMetrics(measure_size_, 1, nullptr, measuring_ == DWRITE_MEASURING_MODE_GDI_NATURAL, &glyph, 1, &design, FALSE);
  }
  if (FAILED(status))
    return false;
  DWRITE_FONT_METRICS metrics{};
  face_->GetMetrics(&metrics);
  float advance = measure_size_ * design.advanceWidth / metrics.designUnitsPerEm;
  if (measuring_ != DWRITE_MEASURING_MODE_NATURAL)
    advance = std::floor(advance + 0.5f);
  result->advance_x = transform_.m11 * advance * source_scale_;
  result->advance_y = transform_.m12 * advance * source_scale_;

  Bounds bounds;
  bool never_path = false;
  if (factory2_ && face2_ && face2_->IsColorFont()) {
    never_path = ColorV1Bounds(glyph, &bounds) || ColorBounds(glyph, &bounds) || SvgBounds(glyph, &bounds) || PngBounds(glyph, &bounds);
  }
  if (!never_path) {
    bool has_raster = RasterBounds(glyph, rendering_, texture_, &bounds);
    bool aliased_fallback = false;
    if (!has_raster && (texture_ != DWRITE_TEXTURE_ALIASED_1x1 || antialias_ == DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE)) {
      has_raster = RasterBounds(glyph, DWRITE_RENDERING_MODE_ALIASED, DWRITE_TEXTURE_ALIASED_1x1, &bounds);
      aliased_fallback = has_raster;
    }
    if (!has_raster || options_.synthetic_bold) {
      if (!OutlineBounds(glyph, options_.synthetic_bold, &bounds))
        return false;
      // GenerateMetricsFromPath adds horizontal filter support for A8-from-LCD.
      if (!bounds.Empty() && a8_from_lcd_ && !aliased_fallback) {
        bounds.RoundOut();
        bounds.left -= 1;
        bounds.right += 1;
      }
    }
  }
  bounds.RoundOut();
  const std::int16_t left = Saturate<std::int16_t>(bounds.left);
  const std::int16_t top = Saturate<std::int16_t>(bounds.top);
  const std::uint16_t width = Saturate<std::uint16_t>(bounds.right - bounds.left);
  const std::uint16_t height = Saturate<std::uint16_t>(bounds.bottom - bounds.top);
  if (width && height) {
    result->left = left * source_scale_;
    result->top = top * source_scale_;
    result->width = width * source_scale_;
    result->height = height * source_scale_;
  }
  return true;
}

PlatformFontMetrics NativeFontScaler::FontMetrics() const {
  PlatformFontMetrics result = public_face_.GetMetrics(render_size_);
  DWRITE_FONT_METRICS native{};
  if (measuring_ == DWRITE_MEASURING_MODE_NATURAL) {
    face_->GetMetrics(&native);
  } else if (FAILED(face_->GetGdiCompatibleMetrics(render_size_, 1, &transform_, &native))) {
    return {};
  }
  const float scale = render_size_ / native.designUnitsPerEm;
  result.ascender = scale * native.ascent;
  result.descender = scale * native.descent;
  result.line_gap = scale * native.lineGap;
  result.x_height = scale * native.xHeight;
  result.cap_height = scale * native.capHeight;
  result.underline_position = scale * native.underlinePosition;
  result.underline_thickness = scale * native.underlineThickness;
  result.strikeout_position = scale * native.strikethroughPosition;
  result.strikeout_thickness = scale * native.strikethroughThickness;
  // SkFontPriv::ScaleFontMetrics after MakeCanonicalized.
  float PlatformFontMetrics::* const fields[] = {
      &PlatformFontMetrics::ascender,
      &PlatformFontMetrics::descender,
      &PlatformFontMetrics::line_gap,
      &PlatformFontMetrics::x_height,
      &PlatformFontMetrics::cap_height,
      &PlatformFontMetrics::underline_position,
      &PlatformFontMetrics::underline_thickness,
      &PlatformFontMetrics::strikeout_position,
      &PlatformFontMetrics::strikeout_thickness,
      &PlatformFontMetrics::top,
      &PlatformFontMetrics::bottom,
      &PlatformFontMetrics::x_min,
      &PlatformFontMetrics::x_max,
      &PlatformFontMetrics::avg_char_width,
      &PlatformFontMetrics::max_char_width};
  for (float PlatformFontMetrics::* field : fields)
    result.*field *= source_scale_;
  return result;
}

} // namespace

bool FontFace::MeasureGlyph(std::uint16_t glyph, float size,
                            const FontRenderOptions& options,
                            PlatformGlyphMetrics* metrics) const {
  if (!metrics)
    return false;
  NativeFontScaler scaler(*this, impl_->factory.Get(), impl_->face.Get(), size, options);
  DWriteMutexLock lock(impl_->face.Get());
  return scaler.GlyphMetrics(glyph, metrics);
}

PlatformFontMetrics FontFace::GetFontMetrics(float size,
                                             const FontRenderOptions& options) const {
  DWriteMutexLock lock(impl_->face.Get());
  NativeFontScaler scaler(*this, impl_->factory.Get(), impl_->face.Get(), size, options);
  return scaler.FontMetrics();
}

} // namespace bkfont
