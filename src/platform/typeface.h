// Ported from: skia/include/core/SkTypeface.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "base/once.h"
#include "base/text/wtf_string.h"
#include "data.h"
#include "font_arguments.h"
#include "font_parameters.h"
#include "font_style.h"
#include "paint/rect.h"
#include "stream.h"

namespace bkit {

class ScalerContext;
struct ScalerContextRec;

// SkTypeface. The typeface and intrinsic style of a font. Typefaces are
// always owned by a std::shared_ptr, which stands in for sk_sp.
class Typeface : public std::enable_shared_from_this<Typeface> {
public:
  virtual ~Typeface();
  Typeface(const Typeface&) = delete;
  Typeface& operator=(const Typeface&) = delete;

  // Returns the typeface's intrinsic style attributes.
  FontStyle GetFontStyle() const;

  // Returns true if style() has the kBold bit set.
  bool IsBold() const;

  // Returns true if style() has the kItalic bit set.
  bool IsItalic() const;

  // Returns true if the typeface claims to be fixed-pitch. This is a style
  // bit, advance widths may vary even if this returns true.
  bool IsFixedPitch() const;

  // Copy into 'coordinates' (allocated by the caller) the design variation
  // coordinates. Returns the number of axes, or -1 if there is an error.
  int GetVariationDesignPosition(std::span<FontArguments::VariationPosition::Coordinate> coordinates) const;

  // Copy into 'parameters' (allocated by the caller) the design variation
  // parameters. Returns the number of axes, or -1 if there is an error.
  int GetVariationDesignParameters(std::span<FontParameters::Variation::Axis> parameters) const;

  // Return a 32bit value for this typeface, unique for the underlying font
  // data. Will never return 0.
  std::uint32_t UniqueID() const {
    return unique_id_;
  }

  // Returns true if the two typefaces reference the same underlying font,
  // handling either being null (treating null as not equal to any font).
  static bool Equal(const Typeface* facea, const Typeface* faceb);

  // Returns a non-null typeface which contains no glyphs.
  static std::shared_ptr<Typeface> MakeEmpty();

  // Return a new typeface based on this typeface but parameterized as
  // specified in the FontArguments. If the FontArguments does not supply an
  // argument for a parameter in the font then the value from this typeface
  // will be used as the value for that argument. If the cloned typeface would
  // be exaclty the same as this typeface then this typeface may be returned.
  // If the FontArguments specify an argument for a parameter in the font
  // which is not supported, then null may be returned.
  std::shared_ptr<Typeface> MakeClone(const FontArguments& args) const;

  // Given an array of UTF32 character codes, return their corresponding glyph
  // IDs.
  void UnicharsToGlyphs(std::span<const std::int32_t> unis, std::span<std::uint16_t> glyphs) const;

  // Return the glyphID that corresponds to the specified unicode code-point
  // (in UTF32 encoding). If the unichar is not supported, returns 0.
  std::uint16_t UnicharToGlyph(std::int32_t unichar) const;

  // Return the number of glyphs in the typeface.
  int CountGlyphs() const;

  // Return the number of tables in the font.
  int CountTables() const;

  // Copy into tags[] (allocated by the caller) the list of table tags in the
  // font, and return the number. This will be the same as CountTables() or 0
  // if an error occured. If tags is empty, this only returns the count.
  int ReadTableTags(std::span<std::uint32_t> tags) const;

  // Given a table tag, return the size of its contents, or 0 if not present.
  std::size_t GetTableSize(std::uint32_t tag) const;

  // Copy the contents of a table into data (allocated by the caller). Note
  // that the contents of the table will be in their native endian order
  // (which for most truetype tables is big endian). If the table tag is not
  // found, or there is an error copying the data, then 0 is returned. If this
  // happens, it is possible that some or all of the memory pointed to by data
  // may have been written to, even though an error has occured.
  std::size_t GetTableData(std::uint32_t tag, std::size_t offset, std::size_t length, void* data) const;

  // Return an immutable copy of the requested font table, or null if that
  // table was not found. This can sometimes be faster than calling
  // GetTableData() twice: once to find the length, and then again to copy the
  // data.
  std::shared_ptr<Data> CopyTableData(std::uint32_t tag) const;

  // Return the units-per-em value for this typeface, or zero if there is an
  // error.
  int GetUnitsPerEm() const;

  struct LocalizedString {
    String string;
    String language;
  };
  class LocalizedStrings {
  public:
    LocalizedStrings() = default;
    virtual ~LocalizedStrings() = default;
    LocalizedStrings(const LocalizedStrings&) = delete;
    LocalizedStrings& operator=(const LocalizedStrings&) = delete;
    virtual bool Next(LocalizedString* localized_string) = 0;
  };

  // Returns an iterator which will attempt to enumerate all of the family
  // names specified by the font. It is the caller's responsibility to delete
  // the returned iterator, which the unique_ptr does.
  std::unique_ptr<LocalizedStrings> CreateFamilyNameIterator() const;

  // Return the family name for this typeface. It will always be returned
  // encoded as UTF8, but the language of the name is whatever the host
  // platform chooses.
  String GetFamilyName() const;

  // Return the PostScript name for this typeface. Value may change based on
  // variation parameters. Returns false if no PostScript name is available.
  bool GetPostScriptName(String* name) const;

  // Return a stream for the contents of the font data, or null on failure. If
  // ttc_index is not null, it is set to the TrueTypeCollection index of this
  // typeface within the stream, or 0 if the stream is not a collection.
  std::unique_ptr<StreamAsset> OpenStream(int* ttc_index) const;

  // Return a scalercontext for the given descriptor. It may return a stub
  // scalercontext that will not crash, but will draw nothing.
  std::unique_ptr<ScalerContext> CreateScalerContext(const ScalerContextRec& rec) const;

  // Returns whether glyph masks need the foreground color.
  bool GlyphMaskNeedsCurrentColor() const;

  // Return a rectangle (scaled to 1-pt) that represents the union of the
  // bounds of all of the glyphs, but each one positioned at (0,). This may be
  // conservatively large, and will not take into account any hinting or other
  // size-specific adjustments.
  ScalarRect GetBounds() const;

  // PRIVATE / EXPERIMENTAL -- do not call
  void FilterRec(ScalerContextRec* rec) const {
    OnFilterRec(rec);
  }

protected:
  explicit Typeface(const FontStyle& style, bool is_fixed_pitch = false);

  // Sets the fixed pitch bit. If used, must be called in the constructor.
  void SetIsFixedPitch(bool is_fixed_pitch) {
    is_fixed_pitch_ = is_fixed_pitch;
  }
  // Sets the font style. If used, must be called in the constructor.
  void SetFontStyle(FontStyle style) {
    style_ = style;
  }

  virtual FontStyle OnGetFontStyle() const; // TODO: = 0;

  virtual bool OnGetFixedPitch() const; // TODO: = 0;

  // Must return a valid scaler context. It can not return null.
  virtual std::unique_ptr<ScalerContext> OnCreateScalerContext(const ScalerContextRec& rec) const = 0;
  virtual std::unique_ptr<ScalerContext> OnCreateScalerContextAsProxyTypeface(const ScalerContextRec& rec, Typeface* proxy_typeface) const;
  virtual void OnFilterRec(ScalerContextRec* rec) const = 0;

  virtual std::unique_ptr<StreamAsset> OnOpenStream(int* ttc_index) const = 0;

  virtual std::shared_ptr<Typeface> OnMakeClone(const FontArguments& args) const = 0;

  virtual bool OnGlyphMaskNeedsCurrentColor() const = 0;

  virtual int OnGetVariationDesignPosition(std::span<FontArguments::VariationPosition::Coordinate> coordinates) const = 0;

  virtual int OnGetVariationDesignParameters(std::span<FontParameters::Variation::Axis> parameters) const = 0;

  virtual void OnCharsToGlyphs(std::span<const std::int32_t> unis, std::span<std::uint16_t> glyphs) const = 0;
  virtual int OnCountGlyphs() const = 0;

  virtual int OnGetUPEM() const = 0;

  // Returns an iterator over the family names in the font.
  virtual std::unique_ptr<LocalizedStrings> OnCreateFamilyNameIterator() const = 0;

  // Sets family_name to the name of the family.
  virtual void OnGetFamilyName(String* family_name) const = 0;
  virtual bool OnGetPostScriptName(String* post_script_name) const = 0;

  virtual int OnGetTableTags(std::span<std::uint32_t> tags) const = 0;
  virtual std::size_t OnGetTableData(std::uint32_t tag, std::size_t offset, std::size_t length, void* data) const = 0;
  virtual std::shared_ptr<Data> OnCopyTableData(std::uint32_t tag) const;

  virtual bool OnComputeBounds(ScalarRect* bounds) const;

  // std::shared_ptr from this typeface, as sk_ref_sp(const_cast<...>(this)).
  std::shared_ptr<Typeface> RefThis() const {
    return std::const_pointer_cast<Typeface>(shared_from_this());
  }

private:
  friend class TypefaceProxy;

  std::uint32_t unique_id_;
  FontStyle style_;
  bool is_fixed_pitch_;

  mutable ScalarRect bounds_;
  mutable Once bounds_once_;
};

} // namespace bkit
