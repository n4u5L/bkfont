# bkit

A standalone C++20 port of Blink's text stack: fonts, shaping, inline layout,
styling, painting and editing, without the rest of the renderer. It follows
the Chromium 140 sources (`third_party/blink` and the Skia pieces they rely on);
everything lives in the `bkit` namespace.

| Directory      | Contents                                                        |
| -------------- | --------------------------------------------------------------- |
| `src/base`     | WTF and Chromium base: strings, containers, hashing, threading  |
| `src/font`     | Font matching, caches, fallback and `@font-face` data           |
| `src/shaping`  | HarfBuzz shaping, line breaking, OpenType MATH                  |
| `src/text`     | Break iterators, hyphenation, bidi, writing modes               |
| `src/geometry` | `Length` and calc() expressions                                 |
| `src/style`    | CSS values and properties, cascade, `ComputedStyle`             |
| `src/layout`   | `LayoutUnit` and inline layout (`InlineFormattingContext`)      |
| `src/paint`    | CPU rasterizer, glyph and text fragment painting                |
| `src/editing`  | Caret positions and bidi caret affinity                         |
| `src/platform` | Typefaces and font managers (DirectWrite, Fontconfig, FreeType) |

## Building

Windows x64 with MSVC (Visual Studio 2026) or Linux, CMake 3.28+:

```sh
cmake -S . -B build -G "Ninja Multi-Config"
cmake --build build --config Release
```

Dependencies are prebuilt under `BKIT_PACKAGES_ROOT` (ICU, HarfBuzz,
FreeType, libpng, zlib; Brotli and Fontconfig on Linux). The OTS and WOFF2
sources are compiled from the Chromium checkout named by `BKIT_CHROMIUM_ROOT`.
Set `BKIT_BUILD_EXAMPLES=OFF` to skip the examples, which use GLFW.

## Using

Link `bkit::bkit`, include `fonts.h` (or `inline_layout.h` for layout), and
call `bkit::InitializeFonts()` once on the main thread before creating any
font or string.

## Examples

- `bkit_mushoku_tensei_example`: multilingual cards covering the supported
  CSS text properties, vertical text and `@font-face` loading.
- `bkit_rich_text_example`: a Word-style rich text editor built on the inline
  layout and paint pipeline.

## License

BSD-style, as in Chromium (`LICENSE`); some files derived from WebKit carry
the Apple (`LICENSE-APPLE`) or LGPL (`LICENSE-LGPL-2`, `LICENSE-LGPL-2.1`)
notices.
