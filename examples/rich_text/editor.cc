// Word-style rich text example. Body and UI text are rendered by bkfont.
// Build: cmake --build build --config Release --target bkfont_rich_text_example
// Run:   bkfont_rich_text_example [--file document.json] [--snapshot view.bmp]
//        bkfont_rich_text_example --write-sample sample.json
// Ctrl+N/O/S, Ctrl+Shift+S, Ctrl+Z/Y, Ctrl+A/C/X/V, Ctrl+B/I/U, Ctrl+L/E/R/J,
// Ctrl+Shift+</>, Ctrl+[/], Ctrl+Shift+= (superscript), Ctrl+Shift+C/V (copy
// and paste formatting), Ctrl+Shift+8 (paragraph marks), Ctrl+D (格式 pane).
// Ctrl+wheel zooms, Shift+wheel pans. The 格式 pane exposes the CSS catalogue.
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <commdlg.h>
#include <imm.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utf16.h>

#include "document.h"
#include "document_view.h"
#include "json.h"
#include "fonts.h"
#include "paint/path.h"
#include "platform/font_manager.h"
#if defined(__linux__)
#include "platform/fontconfig_util.h"
#endif

namespace rich_text {
namespace {

using namespace bkfont;
constexpr float kTitleHeight = 40, kTabBottom = 70, kRibbonBottom = 170, kRulerHeight = 26;
constexpr float kStatusHeight = 26, kPaneWidth = 300, kScrollbar = 14, kPageMargin = 64, kBackstageSide = 220;
// Word 2016 "colorful" palette: blue title/tab strip and status bar, a light
// gray ribbon, a gray canvas behind white paper.
constexpr ColorARGB kBlue = 0xff185abd, kBlueHover = 0xff3a72c9, kBlueActive = 0xff0f4699;
constexpr ColorARGB kInk = 0xff262626, kIcon = 0xff444444, kMuted = 0xff6e6e6e, kDisabled = 0xffb0b0b0;
constexpr ColorARGB kRibbon = 0xfff3f3f3, kLine = 0xffd6d6d6, kBorder = 0xffc2c2c2, kCanvas = 0xffe3e3e3;
constexpr ColorARGB kHover = 0xffdedede, kChecked = 0xffcacaca, kCheckedHover = 0xffbcbcbc, kWhite = 0xffffffff;
constexpr ColorARGB kMenuHover = 0xffe1ecf8, kBodyInk = 0xff243247;

struct Rect {
  float x, y, width, height;
  bool operator==(const Rect&) const = default;
  bool Empty() const {
    return width <= 0 || height <= 0;
  }
  bool Contains(float px, float py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }
};
Rect Union(Rect a, Rect b) {
  if (a.Empty()) return b;
  if (b.Empty()) return a;
  const float x = std::min(a.x, b.x), y = std::min(a.y, b.y);
  return {x, y, std::max(a.x + a.width, b.x + b.width) - x, std::max(a.y + a.height, b.y + b.height) - y};
}
Rect Intersect(Rect a, Rect b) {
  const float x = std::max(a.x, b.x), y = std::max(a.y, b.y);
  const float right = std::min(a.x + a.width, b.x + b.width), bottom = std::min(a.y + a.height, b.y + b.height);
  return right > x && bottom > y ? Rect{x, y, right - x, bottom - y} : Rect{};
}
// A hit area. Disabled controls keep their tooltip but have no action.
struct Button {
  Rect rect;
  std::function<void()> action;
  std::string hint; // "Title" or "Title\nDescription".
};
std::string Filename(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string FoldFontName(std::string_view name) {
  return std::string(String::FromUTF8(name).FoldCase().Utf8().c_str());
}
std::string QuoteFontName(std::string_view name) {
  std::string value = "\"";
  for (char c : name) {
    if (c == '\\' || c == '"') value += '\\';
    value += c;
  }
  return value + '"';
}
std::string FontNameLabel(std::string_view value) {
  if (value.size() < 2 || value.front() != '"' || value.back() != '"') return std::string(value);
  std::string label;
  for (size_t i = 1; i + 1 < value.size(); ++i) {
    if (value[i] == '"') return std::string(value); // A family list, not one name.
    if (value[i] == '\\' && i + 2 < value.size()) ++i;
    label += value[i];
  }
  return label;
}
struct FontChoice {
  std::string name, value, search, key;
  std::vector<std::string> aliases;
  bool Matches(const std::string& current, const std::string& folded) const {
    if (value == current) return true;
    const auto& generic = FindProperty("font-family")->choices;
    return value.front() == '"' && std::find(generic.begin(), generic.end(), current) == generic.end() &&
           std::find(aliases.begin(), aliases.end(), folded) != aliases.end();
  }
};

int FontNameLanguagePriority(const std::string& language) {
  // This example has a Simplified Chinese UI. Name localization is a UI
  // preference, independent of the host's regional format or font matching.
  if (language == "zh-cn") return 0;
  if (language == "zh-hans" || language.starts_with("zh-hans-")) return 1;
  if (language == "zh-sg") return 2;
  if (language == "zh" || language.starts_with("zh-")) return 3;
  return 100;
}

std::vector<FontChoice> InstalledFonts() {
  std::vector<FontChoice> result;
  const auto add = [&](const std::string& name, const Vector<Typeface::LocalizedString>& localized) {
    if (name.empty()) return;
    const auto key = FoldFontName(name);
    FontChoice choice{name, QuoteFontName(name), {}, key, {key}};
    int preferred = 100;
    for (const auto& entry : localized) {
      const std::string alias(entry.string.Utf8().c_str());
      if (alias.empty()) continue;
      const auto folded = FoldFontName(alias);
      if (std::find(choice.aliases.begin(), choice.aliases.end(), folded) == choice.aliases.end()) choice.aliases.push_back(folded);
      const int priority = FontNameLanguagePriority(FoldFontName(std::string(entry.language.Utf8().c_str())));
      if (priority < preferred) {
        choice.name = alias;
        preferred = priority;
      }
    }
    choice.search = FoldFontName(choice.name);
    for (const auto& alias : choice.aliases) choice.search += '\n' + alias;
    result.push_back(std::move(choice));
  };
#if defined(__linux__)
  // The FCI manager intentionally does not enumerate. Reuse bkfont's locked
  // Fontconfig configuration, without changing its upstream matching API.
  FontconfigLocker lock;
  FontconfigPattern pattern(FcPatternCreate());
  FontconfigObjectSet objects(FcObjectSetCreate());
  if (pattern && objects) {
    FcObjectSetAdd(objects.get(), FC_FAMILY);
    FcObjectSetAdd(objects.get(), FC_FAMILYLANG);
    FontconfigFontSet fonts(FcFontList(GetGlobalFontConfig(), pattern.get(), objects.get()));
    if (fonts)
      for (int i = 0; i < fonts->nfont; ++i) {
        const char* name = GetFontconfigString(fonts->fonts[i], FC_FAMILY);
        if (!name) continue;
        Vector<Typeface::LocalizedString> localized;
        for (int j = 0; const char* alias = GetFontconfigString(fonts->fonts[i], FC_FAMILY, j); ++j) {
          const char* language = GetFontconfigString(fonts->fonts[i], FC_FAMILYLANG, j);
          localized.push_back(Typeface::LocalizedString{String::FromUTF8(alias), language ? String::FromUTF8(language) : String()});
        }
        add(name, localized);
      }
  }
#else
  if (const auto manager = FontCache::Get().GetFontManager()) {
    const int count = manager->CountFamilies();
    result.reserve(count);
    for (int i = 0; i < count; ++i) add(std::string(manager->GetFamilyName(i).Utf8().c_str()), manager->GetFamilyNames(i));
  }
#endif
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
  result.erase(std::unique(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.key == b.key; }), result.end());
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.search < b.search; });
  return result;
}

ColorARGB Mix(ColorARGB a, ColorARGB b, float t) {
  const auto channel = [&](int shift) {
    const float value = ((a >> shift) & 255) * (1 - t) + ((b >> shift) & 255) * t;
    return static_cast<ColorARGB>(std::lround(value)) << shift;
  };
  return 0xff000000 | channel(16) | channel(8) | channel(0);
}
std::optional<ColorARGB> ParseColor(std::string_view text) {
  if (text.size() != 7 || text[0] != '#') return std::nullopt;
  uint32_t rgb = 0;
  const auto parsed = std::from_chars(text.data() + 1, text.data() + 7, rgb, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + 7) return std::nullopt;
  return 0xff000000 | rgb;
}
std::string ColorText(ColorARGB color) {
  char buffer[8];
  std::snprintf(buffer, sizeof buffer, "#%06x", static_cast<unsigned>(color & 0xffffff));
  return buffer;
}
// Word's color picker: 10 theme columns, each with five tints/shades, followed
// by the 10 standard colors (indices 60-69).
ColorARGB PaletteColor(int index) {
  static constexpr ColorARGB kTheme[] = {0xffffffff, 0xff000000, 0xffe7e6e6, 0xff44546a, 0xff4472c4,
                                         0xffed7d31, 0xffa5a5a5, 0xffffc000, 0xff5b9bd5, 0xff70ad47};
  static constexpr ColorARGB kStandard[] = {0xffc00000, 0xffff0000, 0xffffc000, 0xffffff00, 0xff92d050,
                                            0xff00b050, 0xff00b0f0, 0xff0070c0, 0xff002060, 0xff7030a0};
  if (index >= 60) return kStandard[index - 60];
  const int column = index % 10, row = index / 10;
  const ColorARGB base = kTheme[column];
  if (row == 0) return base;
  static constexpr float kWhiteShade[] = {0.05f, 0.15f, 0.25f, 0.35f, 0.5f};
  static constexpr float kBlackTint[] = {0.5f, 0.35f, 0.25f, 0.15f, 0.05f};
  static constexpr float kLightShade[] = {0.1f, 0.25f, 0.5f, 0.75f, 0.9f};
  static constexpr float kAccent[] = {0.8f, 0.6f, 0.4f, 0.25f, 0.5f};
  if (column == 0) return Mix(base, 0xff000000, kWhiteShade[row - 1]);
  if (column == 1) return Mix(base, 0xffffffff, kBlackTint[row - 1]);
  if (column == 2) return Mix(base, 0xff000000, kLightShade[row - 1]);
  return row <= 3 ? Mix(base, 0xffffffff, kAccent[row - 1]) : Mix(base, 0xff000000, kAccent[row - 1]);
}

// Word's count: every Han/kana character is a word; other words are runs of
// letters and digits.
size_t CountWords(std::u16string_view text) {
  size_t count = 0;
  bool word = false;
  for (size_t i = 0; i < text.size();) {
    UChar32 c;
    U16_NEXT(text.data(), i, text.size(), c);
    UErrorCode error = U_ZERO_ERROR;
    const UScriptCode script = uscript_getScript(c, &error);
    if (script == USCRIPT_HAN || script == USCRIPT_HIRAGANA || script == USCRIPT_KATAKANA) {
      ++count;
      word = false;
    } else if (u_isalnum(c)) {
      count += !word;
      word = true;
    } else
      word = false;
  }
  return count;
}

std::string LanguageName(const std::string& locale) {
  if (locale == "zh-CN") return "中文(中国)";
  if (locale == "en-US") return "英语(美国)";
  if (locale == "ja") return "日语";
  if (locale == "ko") return "韩语";
  if (locale == "ar") return "阿拉伯语";
  if (locale == "th") return "泰语";
  return locale;
}

float ParseLength(std::string_view text, float em, float percent, float fallback) {
  float value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || !std::isfinite(value)) return fallback;
  const std::string_view unit(parsed.ptr, text.data() + text.size() - parsed.ptr);
  if (unit == "px" || (unit.empty() && value == 0)) return value;
  if (unit == "em" || unit == "rem") return value * em;
  if (unit == "pt") return value * 4 / 3;
  if (unit == "%") return value * percent / 100;
  return fallback;
}
std::string PixelText(float px) {
  char buffer[32];
  if (px == std::floor(px))
    std::snprintf(buffer, sizeof buffer, "%dpx", static_cast<int>(px));
  else
    std::snprintf(buffer, sizeof buffer, "%.1fpx", px);
  return buffer;
}

// Ribbon layout for the catalogue tabs. Every property of tabs 1-3 appears in
// exactly one group; the 格式 pane lists the same properties by section.
struct RibbonGroup {
  int tab;
  const char* label;
  std::vector<const char*> properties;
};
const std::vector<RibbonGroup>& RibbonGroups() {
  static const std::vector<RibbonGroup> groups = {
      {1, "宽度与间距", {"font-stretch", "letter-spacing", "word-spacing", "font-kerning", "font-size-adjust", "font-optical-sizing"}},
      {1, "字形变体", {"font-variant-caps", "font-variant-ligatures", "font-variant-numeric", "font-variant-east-asian", "font-variant-position", "font-variant-emoji"}},
      {1, "OpenType", {"font-feature-settings", "font-variation-settings", "font-palette"}},
      {1, "合成与渲染", {"font-synthesis-weight", "font-synthesis-style", "font-synthesis-small-caps", "text-rendering", "-webkit-font-smoothing"}},
      {2, "对齐与缩进", {"text-align", "text-align-last", "text-indent", "line-height", "tab-size"}},
      {2, "换行", {"white-space-collapse", "text-wrap-mode", "text-wrap-style", "word-break", "overflow-wrap", "line-break"}},
      {2, "连字符", {"hyphens", "hyphenate-character", "hyphenate-limit-chars"}},
      {2, "方向", {"direction", "unicode-bidi", "writing-mode", "text-orientation"}},
      {2, "东亚排版", {"text-spacing-trim", "text-autospace", "-webkit-locale"}},
      {3, "装饰线", {"text-decoration-style", "text-decoration-color", "text-decoration-thickness", "text-decoration-skip-ink", "text-underline-offset", "text-underline-position"}},
      {3, "着重号", {"text-emphasis-style", "text-emphasis-color", "text-emphasis-position"}},
      {3, "填充与描边", {"text-shadow", "-webkit-text-fill-color", "-webkit-text-stroke-color", "-webkit-text-stroke-width"}},
      {3, "其他", {"text-combine-upright", "visibility"}},
  };
  return groups;
}
constexpr const char* kSectionNames[] = {"常用文字", "字体与 OpenType", "段落与方向", "装饰与效果"};
const std::array<std::vector<size_t>, 4>& SectionSpecs() {
  static const auto sections = [] {
    std::array<std::vector<size_t>, 4> result;
    const auto catalog = PropertyCatalog();
    for (size_t i = 0; i < catalog.size(); ++i) result[catalog[i].tab].push_back(i);
    return result;
  }();
  return sections;
}
size_t IndexOf(std::string_view name) {
  return static_cast<size_t>(FindProperty(name) - PropertyCatalog().data());
}

// Quick styles. Character properties are kept sorted, as Document interns them.
struct StylePreset {
  const char* name;
  Properties properties;
};
const std::array<StylePreset, 5>& StylePresets() {
  static const std::array<StylePreset, 5> presets = {{
      {"正文", {}},
      {"标题 1", {{"color", "#185abd"}, {"font-size", "40px"}, {"font-weight", "700"}}},
      {"标题 2", {{"font-size", "24px"}, {"font-weight", "600"}}},
      {"副标题", {{"color", "#808080"}, {"font-size", "14px"}, {"letter-spacing", "2px"}}},
      {"强调", {{"color", "#185abd"}, {"font-style", "italic"}}},
  }};
  return presets;
}

enum class Icon {
  Paste,
  Cut,
  Copy,
  Painter,
  Undo,
  Redo,
  Save,
  SaveAs,
  Grow,
  Shrink,
  Case,
  ClearFormat,
  Effects,
  AlignLeft,
  AlignCenter,
  AlignRight,
  AlignJustify,
  LineSpacing,
  Indent,
  Pilcrow,
  Ltr,
  Rtl,
  Vertical,
  SelectAll,
  Chevron,
  ChevronRight,
  Launcher,
  Close,
  Back,
  NewDoc,
  Open,
  Info,
  ZoomIn,
  ZoomOut,
  Zoom100,
  PageWidth,
  Check,
  Sample,
};

struct TextStyle {
  float size = 12;
  ColorARGB color = kInk;
  int weight = 400;
  bool italic = false;
  std::string_view family = {}; // CSS font-family; used by font previews.
};

class Editor {
public:
  enum class Pending {
    None,
    New,
    Sample,
    Open,
    Close
  };
  enum class PopupKind {
    List,
    Font,
    Color
  };
  // Independently retained regions of the scene.
  enum Layer {
    kChrome, // Title bar, tabs and ribbon; the whole window in backstage view.
    kRuler,
    kPage,
    kPane,
    kStatus,
    kLayerCount
  };

  Document document;
  DocumentView view;
  GLFWwindow* window = nullptr;
  bool dirty = true;
  bool composing = false;
  float scale = 1, width = 1440, height = 980, zoom = 1;
  float scroll = 0, pan = 0, pane_scroll = 0;
  float mouse_x = 0, mouse_y = 0;
  int tab = 0; // 0 开始, 1 字体, 2 段落, 3 文字效果 (catalogue tabs), 4 视图.
  bool backstage = false;
  int backstage_page = 0; // 0 信息, 1 新建.
  bool pane_visible = true, ruler_visible = true, marks_visible = false;
  std::array<bool, 4> pane_expanded{true, false, false, false};
  bool dragging = false, scrollbar_drag = false, zoom_drag = false, caret_on = true;
  float scrollbar_grab = 0;
  TextAffinity affinity = TextAffinity::kDownstream;
  std::string path, status = "就绪";
  double last_input = 0, hover_since = 0, last_click = -1;
  int click_count = 0;
  float click_x = 0, click_y = 0;
  std::optional<Properties> painter; // Format painter: a copied character style.
  std::string font_color = "#c00000";
  int popup = -1; // Catalogue index of the open dropdown.
  Rect popup_anchor{};
  float popup_scroll = 0;
  int popup_active = -1;
  bool popup_scrollbar_drag = false;
  float popup_scrollbar_grab = 0;
  std::u16string popup_input; // Font search, custom color or custom CSS value.
  bool popup_input_selected = false;
  bool fonts_loaded = false;
  size_t installed_font_count = 0;
  std::vector<FontChoice> font_choices;
  std::vector<size_t> filtered_fonts;
  std::unordered_map<std::string, std::string> font_names;
  Pending pending = Pending::None;
  std::string pending_path;
  bool confirm = false;
  bool path_dialog = false, path_save = false;
  std::u16string path_input;
  bool path_select_all = false;
  std::vector<Button> buttons;
  struct TextBox {
    std::unique_ptr<InlineFormattingContext> context;
    float width = 0;
  };
  std::unordered_map<std::string, TextBox> labels, previews;
  Bitmap bitmap;
  Bitmap scene_bitmap; // Opaque retained scene, excluding caret and popups.
  std::unique_ptr<RasterCanvas> scene_canvas;
  std::array<std::vector<Button>, kLayerCount> scene_buttons;
  std::vector<Button> dialog_buttons;
  std::vector<IntRect> upload_damage;
  // Effective value and "mixed" flag of every catalogue property, recomputed
  // only when the document, selection or typing style changes.
  struct Formats {
    uint64_t revision = ~uint64_t{0};
    Document::Selection selection{~0u, ~0u};
    uint32_t typing = ~0u;
    std::vector<std::string> values;
    std::vector<char> mixed;
  };
  mutable Formats formats;
  struct WordCount {
    uint64_t revision = ~uint64_t{0};
    Document::Selection selection{~0u, ~0u};
    size_t total = 0, selected = 0;
  };
  WordCount words;
  struct FrameState {
    uint64_t revision = 0;
    Document::Selection selection;
    std::vector<std::string> values;
    std::vector<char> mixed;
    int tab = 0, backstage_page = 0;
    bool backstage = false, pane = false, ruler = false, marks = false, painter = false;
    std::array<bool, 4> expanded{};
    float scale = 0, zoom = 0, scroll = 0, pan = 0, pane_scroll = 0;
    bool modified = false, undo = false, redo = false;
    std::array<Rect, kLayerCount> layers{};
    Rect hover{}, caret{}, overlay{};
    std::string path, status, font_color, overlay_key;
  };
  FrameState frame;
  bool frame_valid = false;
  float desired_x = -1;
#ifdef _WIN32
  WNDPROC previous_proc = nullptr;
#endif

  Editor() {
    document.New(true);
  }
  float ContentWidth() const {
    return width - (pane_visible && !backstage ? kPaneWidth : 0);
  }
  float BodyTop() const {
    return kRibbonBottom + (ruler_visible ? kRulerHeight : 0);
  }
  float PageX() const {
    return std::max(24.0f, (ContentWidth() - kScrollbar - view.Width() / scale) / 2) - pan;
  }
  float PageY() const {
    return BodyTop() + 24 - scroll;
  }
  float ViewportHeight() const {
    return height - kStatusHeight - BodyTop();
  }
  float MaxScroll() const {
    return std::max(0.0f, view.Height() / scale + 48 - ViewportHeight());
  }
  float MaxPan() const {
    return std::max(0.0f, view.Width() / scale + 48 - (ContentWidth() - kScrollbar));
  }
  float ScrollThumb() const {
    const float track = ViewportHeight();
    return std::max(36.0f, track * track / (track + MaxScroll()));
  }
  float Hair() const {
    return std::max(1.0f, std::floor(scale)) / scale; // One device pixel line.
  }
  bool Modal() const {
    return popup >= 0 || confirm || path_dialog;
  }
  std::string DocumentName() const {
    return path.empty() ? "排版实验.json" : Filename(path);
  }
  void Layout(bool force = false) {
    if (force) frame_valid = false;
    view.Update(document, scale, zoom, force);
    scroll = std::clamp(scroll, 0.0f, MaxScroll());
    pan = std::clamp(pan, 0.0f, MaxPan());
  }
  void Touch(bool reveal = true) {
    dirty = caret_on = true;
    last_input = window ? glfwGetTime() : 0;
    desired_x = -1;
    Layout();
    if (reveal) RevealCaret();
  }
  void RevealCaret() {
    const auto rect = view.Caret(document.GetSelection().focus, affinity);
    const float top = PageY() + rect.Y().ToFloat() / scale;
    const float bottom = top + std::max(24.0f * zoom, rect.Height().ToFloat() / scale);
    if (top < BodyTop() + 12) scroll -= BodyTop() + 12 - top;
    if (bottom > height - kStatusHeight - 12) scroll += bottom - (height - kStatusHeight - 12);
    const float left = PageX() + rect.X().ToFloat() / scale;
    if (left < 16) pan -= 16 - left;
    if (left > ContentWidth() - kScrollbar - 10) pan += left - (ContentWidth() - kScrollbar - 10);
    scroll = std::clamp(scroll, 0.0f, MaxScroll());
    pan = std::clamp(pan, 0.0f, MaxPan());
  }
  void Zoom(float next) {
    const float old = zoom;
    zoom = std::clamp(next, 0.5f, 2.0f);
    scroll = (scroll + ViewportHeight() / 2) * zoom / old - ViewportHeight() / 2;
    Layout();
    dirty = true;
  }
  void FitPageWidth() {
    Zoom((ContentWidth() - kScrollbar - 48) / 794);
  }
  void ZoomFromSlider() {
    Zoom(0.5f + std::clamp((mouse_x - (width - 216)) / 118, 0.0f, 1.0f) * 1.5f);
  }

  // Formatting state.
  static std::string Default(std::string_view name) {
    if (name == "font-family") return "Microsoft YaHei";
    if (name == "font-size") return "18px";
    if (name == "font-weight") return "400";
    if (name == "color") return "#243247";
    if (name == "line-height") return "1.6";
    if (name == "tab-size") return "4";
    if (name == "white-space-collapse") return "preserve";
    if (name == "overflow-wrap") return "anywhere";
    if (name == "-webkit-locale") return "zh-CN";
    const auto* spec = FindProperty(name);
    return spec ? spec->choices.front() : "";
  }
  void RefreshFormats() const {
    const auto selection = document.GetSelection();
    if (formats.revision == document.Revision() && formats.selection == selection && formats.typing == document.TypingStyle()) return;
    formats.revision = document.Revision();
    formats.selection = selection;
    formats.typing = document.TypingStyle();
    // The first entry is the reference style; the rest only decide "mixed".
    const auto& tree = document.Tree();
    std::vector<uint32_t> characters{document.TypingStyle()}, paragraphs;
    const uint32_t first = tree.LineBreaksBefore(selection.Start());
    const uint32_t last = selection.Empty() ? first : tree.LineBreaksBefore(selection.End() - 1);
    for (uint32_t i = first; i <= last; ++i) paragraphs.push_back(document.Paragraphs()[i]);
    if (!selection.Empty())
      for (const auto& piece : tree.Slice(selection.Start(), selection.End() - selection.Start())) characters.push_back(piece.style);
    for (auto* ids : {&characters, &paragraphs}) {
      std::sort(ids->begin() + 1, ids->end());
      ids->erase(std::unique(ids->begin() + 1, ids->end()), ids->end());
    }
    const auto catalog = PropertyCatalog();
    formats.values.resize(catalog.size());
    formats.mixed.assign(catalog.size(), 0);
    for (size_t i = 0; i < catalog.size(); ++i) {
      const auto& ids = catalog[i].paragraph ? paragraphs : characters;
      const std::string fallback = Default(catalog[i].name);
      formats.values[i] = PropertyValue(document.GetStyle(ids.front()).properties, catalog[i].name, fallback);
      for (size_t j = 1; j < ids.size() && !formats.mixed[i]; ++j)
        formats.mixed[i] = PropertyValue(document.GetStyle(ids[j]).properties, catalog[i].name, fallback) != formats.values[i];
    }
  }
  const std::string& Value(size_t index) const {
    RefreshFormats();
    return formats.values[index];
  }
  const std::string& Value(std::string_view name) const {
    return Value(IndexOf(name));
  }
  bool Mixed(size_t index) const {
    RefreshFormats();
    return formats.mixed[index] != 0;
  }
  bool Is(std::string_view name, std::string_view value) const {
    const size_t index = IndexOf(name);
    return !Mixed(index) && Value(index) == value;
  }
  bool CharactersMixed() const {
    RefreshFormats();
    for (size_t i = 0; i < formats.mixed.size(); ++i)
      if (formats.mixed[i] && !PropertyCatalog()[i].paragraph) return true;
    return false;
  }
  void RefreshWords() {
    const auto selection = document.GetSelection();
    if (words.revision != document.Revision()) {
      words.total = CountWords(view.Text());
      words.revision = document.Revision();
      words.selection = {~0u, ~0u};
    }
    if (words.selection != selection) {
      words.selection = selection;
      const auto& text = view.Text();
      const uint32_t end = std::min<uint32_t>(selection.End(), static_cast<uint32_t>(text.size()));
      const uint32_t start = std::min(selection.Start(), end);
      words.selected = selection.Empty() ? 0 : CountWords(std::u16string_view(text).substr(start, end - start));
    }
  }

  // Editing commands.
  bool Format(const std::string& name, const std::string& value) {
    std::string error;
    if (!document.Format(name, value, error)) {
      status = error.empty() ? "无效的值：" + value : error;
      dirty = true;
      return false;
    }
    status = std::string(FindProperty(name)->label) + " · " + (value.empty() ? "恢复默认" : value);
    popup = -1;
    Touch(false);
    return true;
  }
  void Toggle(const char* property, const char* on, const char* off) {
    Format(property, Is(property, on) ? off : on);
  }
  bool HasDecoration(std::string_view token) const {
    const size_t index = IndexOf("text-decoration-line");
    if (Mixed(index)) return false;
    const std::string& value = Value(index);
    for (size_t start = 0; start < value.size();) {
      const size_t end = std::min(value.find(' ', start), value.size());
      if (std::string_view(value).substr(start, end - start) == token) return true;
      start = end + 1;
    }
    return false;
  }
  // Underline and strikethrough are independent toggles over one property.
  void ToggleDecoration(std::string_view token) {
    std::string value;
    for (const std::string_view name : {"underline", "overline", "line-through"}) {
      if ((name == token) != HasDecoration(name)) value += (value.empty() ? "" : " ") + std::string(name);
    }
    Format("text-decoration-line", value.empty() ? "none" : value);
  }
  void StepFontSize(int direction) {
    static constexpr float kSizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 48, 56, 64, 72, 96};
    const float current = ParseLength(Value("font-size"), 18, 18, 18);
    float next = current;
    if (direction > 0) {
      next = current + 8;
      for (float size : kSizes)
        if (size > current + 0.01f) {
          next = size;
          break;
        }
    } else {
      next = std::max(1.0f, current - 1);
      for (auto it = std::rbegin(kSizes); it != std::rend(kSizes); ++it)
        if (*it < current - 0.01f) {
          next = *it;
          break;
        }
    }
    Format("font-size", PixelText(std::min(next, 400.0f)));
  }
  void NudgeFontSize(float delta) {
    Format("font-size", PixelText(std::clamp(ParseLength(Value("font-size"), 18, 18, 18) + delta, 1.0f, 400.0f)));
  }
  bool PresetActive(size_t index) const {
    return !CharactersMixed() && document.GetStyle(document.TypingStyle()).properties == StylePresets()[index].properties;
  }
  void ApplyPreset(size_t index) {
    // Like a Word paragraph style: without a selection, the caret's paragraph.
    const auto selection = document.GetSelection();
    if (selection.Empty()) {
      const auto& block = view.BlockAt(selection.focus);
      document.Select(block.start, block.end);
    }
    std::string error;
    if (!document.SetCharacterStyle(StylePresets()[index].properties, error))
      status = error;
    else
      status = std::string("样式 · ") + StylePresets()[index].name;
    if (selection.Empty()) document.Select(selection.anchor, selection.focus);
    Touch(false);
  }
  void ClearFormatting() {
    document.ClearCharacterFormatting();
    status = "已清除文字格式";
    Touch(false);
  }
  void CopyFormat() {
    painter = document.GetStyle(document.TypingStyle()).properties;
    status = "格式刷：选择要应用格式的文字";
    dirty = true;
  }
  void PasteFormat() {
    if (!painter) return;
    std::string error;
    status = document.SetCharacterStyle(*painter, error) ? "已应用复制的格式" : error;
    Touch(false);
  }
  void TogglePainter() {
    if (painter) {
      painter.reset();
      status = "就绪";
      dirty = true;
    } else
      CopyFormat();
  }
  void Insert(std::u16string_view text) {
    std::string error;
    if (!document.ReplaceSelection(text, error))
      status = error;
    else
      status = "编辑中";
    affinity = TextAffinity::kDownstream;
    Touch();
  }
  void SelectAll() {
    document.Select(0, document.Tree().Length());
    Touch(false);
  }
  void Copy(bool cut) {
    if (!window || document.GetSelection().Empty()) return;
    const auto text = document.SelectedText();
    glfwSetClipboardString(window, text.c_str());
    if (cut)
      Insert({});
    else {
      status = "已复制所选文字";
      dirty = true;
    }
  }
  void Paste() {
    if (!window) return;
    const char* clipboard = glfwGetClipboardString(window);
    std::u16string text;
    if (clipboard && DecodeUTF8(clipboard, text)) Insert(text);
  }
  void Undo(bool redo) {
    const bool changed = redo ? document.Redo() : document.Undo();
    if (changed) {
      status = redo ? "已重做" : "已撤销";
      Touch();
    }
  }
  void OpenBackstage() {
    backstage = true;
    backstage_page = 0;
    popup = -1;
    dragging = scrollbar_drag = zoom_drag = false;
    dirty = true;
  }
  void CloseBackstage() {
    backstage = false;
    Layout();
    dirty = true;
  }
  float SectionHeight(int section) const {
    return 32 + (pane_expanded[section] ? SectionSpecs()[section].size() * 34.0f + 6 : 0);
  }
  void OpenPaneSection(int section) {
    pane_visible = true;
    pane_expanded[section] = true;
    pane_scroll = 0;
    for (int i = 0; i < section; ++i) pane_scroll += SectionHeight(i);
    popup = -1;
    Layout();
    dirty = true;
  }

  // Drawing primitives. Coordinates are logical pixels.
  void Fill(RasterCanvas& raster, Rect rect, ColorARGB color, float radius = 0) {
    const auto bounds = ScalarRect::MakeXYWH(rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale);
    PlatformPaint paint(color);
    if (radius > 0) {
      ScalarPath path;
      path.AddRRect(bounds, radius * scale, radius * scale);
      paint.SetAntiAlias(true);
      raster.DrawPath(path, paint);
    } else
      raster.DrawRect(bounds, paint);
  }
  void Outline(RasterCanvas& raster, Rect rect, ColorARGB color) {
    const float hair = Hair();
    Fill(raster, {rect.x, rect.y, rect.width, hair}, color);
    Fill(raster, {rect.x, rect.y + rect.height - hair, rect.width, hair}, color);
    Fill(raster, {rect.x, rect.y + hair, hair, rect.height - 2 * hair}, color);
    Fill(raster, {rect.x + rect.width - hair, rect.y + hair, hair, rect.height - 2 * hair}, color);
  }
  void Clip(RasterCanvas& raster, Rect rect) {
    raster.ClipRect(ScalarRect::MakeXYWH(rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale), false);
  }
  // Cached single-line layouts. Font previews have their own, smaller cache
  // so that scrolling a long font list never evicts the UI labels.
  TextBox& Text(std::string_view text, const TextStyle& style) {
    std::string key(text);
    key += '\x1f' + std::to_string(style.size) + '|' + std::to_string(style.color) + '|' + std::to_string(style.weight) +
           (style.italic ? "|i|" : "|n|");
    key += style.family;
    auto& cache = style.family.empty() ? labels : previews;
    if (auto found = cache.find(key); found != cache.end()) return found->second;
    if (cache.size() > (style.family.empty() ? 1024u : 384u)) cache.clear();
    auto context = std::make_unique<InlineFormattingContext>(Settings(), nullptr);
    auto declaration = DefaultParagraphStyle();
    (void)declaration.Set<css_longhand::FontSize>(CSSLength{style.size, CSSPrimitiveValue::UnitType::kPixels});
    (void)declaration.Set<css_longhand::FontWeight>(CSSNumber{static_cast<double>(style.weight)});
    const ColorARGB color = style.color;
    (void)declaration.Set<css_longhand::Color>(
        StyleColorValue(Color::FromRGBA((color >> 16) & 255, (color >> 8) & 255, color & 255, color >> 24)));
    (void)declaration.Set<css_longhand::LineHeight>(CSSNumber{1.25});
    (void)declaration.Set<css_longhand::TextWrapMode>(CSSValueID::kNowrap);
    if (style.italic || !style.family.empty()) {
      Properties extra;
      if (!style.family.empty()) extra.emplace_back("font-family", std::string(style.family));
      if (style.italic) extra.emplace_back("font-style", "italic");
      StyleDeclaration overrides;
      std::string error;
      if (BuildDeclaration(extra, overrides, error)) declaration.Merge(overrides);
    }
    context->SetInlineStyle(context->RootObject(), declaration);
    context->SetZoomFactors(scale);
    auto options = context->Options();
    options.available_inline_size = LayoutUnit(4000 * scale);
    context->SetOptions(options);
    const auto& node = context->AppendText(context->RootObject(), String::FromUTF8(text));
    context->UpdateLayout();
    // The fragment tree spans the available size; measure the text itself.
    float right = 0;
    for (const auto& rect : context->ObjectRects(node)) right = std::max(right, rect.Right().ToFloat());
    const float measured = right / scale;
    return cache.emplace(std::move(key), TextBox{std::move(context), measured}).first->second;
  }
  float LabelWidth(std::string_view text, const TextStyle& style) {
    return text.empty() ? 0 : Text(text, style).width;
  }
  void Label(RasterCanvas& raster, std::string_view text, float x, float y, const TextStyle& style = {}, float max_width = 600) {
    if (text.empty() || max_width <= 0) return;
    auto& box = Text(text, style);
    const bool clip = box.width > max_width;
    if (clip) {
      raster.Save();
      Clip(raster, {x, y - 4, max_width, style.size * 2 + 12});
    }
    CanvasPaintCanvas canvas(&raster);
    box.context->Paint(&canvas, {LayoutUnit(x * scale), LayoutUnit(y * scale)});
    if (clip) raster.Restore();
  }
  void CenterLabel(RasterCanvas& raster, std::string_view text, Rect rect, const TextStyle& style = {}) {
    const float w = LabelWidth(text, style);
    Label(raster, text, rect.x + std::max(0.0f, (rect.width - w) / 2), rect.y + (rect.height - style.size * 1.25f) / 2, style, rect.width);
  }

  // Vector icons on a 16-unit grid; `size` is the logical icon size.
  void PaintIcon(RasterCanvas& raster, Icon icon, float x, float y, float size, ColorARGB color) {
    const float k = size / 16 * scale, line = size >= 24 ? 1.5f : 1.0f;
    const auto P = [&](float px, float py) { return ScalarPoint{x * scale + px * k, y * scale + py * k}; };
    const auto paint = [&](ColorARGB c, float stroke) {
      PlatformPaint result(c);
      result.SetAntiAlias(true);
      if (stroke > 0) {
        result.SetStyle(PlatformPaint::Style::kStroke);
        result.SetStrokeWidth(stroke * scale);
        result.SetStrokeCap(StrokeCap::kRound);
        result.SetStrokeJoin(StrokeJoin::kRound);
      }
      return result;
    };
    const auto poly = [&](std::initializer_list<ScalarPoint> points, bool closed = false) {
      ScalarPath path;
      bool first = true;
      for (const auto& point : points) {
        if (first)
          path.MoveTo(P(point.x, point.y));
        else
          path.LineTo(P(point.x, point.y));
        first = false;
      }
      if (closed) path.Close();
      return path;
    };
    const auto box = [&](float l, float t, float r, float b, float radius = 0) {
      ScalarPath path;
      const auto a = P(l, t), c = P(r, b);
      if (radius > 0)
        path.AddRRect(ScalarRect::MakeLTRB(a.x, a.y, c.x, c.y), radius * k, radius * k);
      else
        path.AddRect(ScalarRect::MakeLTRB(a.x, a.y, c.x, c.y));
      return path;
    };
    const auto circle = [&](float cx, float cy, float r) {
      ScalarPath path;
      const auto c = P(cx, cy);
      path.AddCircle(c.x, c.y, r * k);
      return path;
    };
    const auto stroke = [&](const ScalarPath& path, ColorARGB c, float w = 0) { raster.DrawPath(path, paint(c, w > 0 ? w : line)); };
    const auto fill = [&](const ScalarPath& path, ColorARGB c) { raster.DrawPath(path, paint(c, 0)); };
    const auto lines = [&](std::initializer_list<std::array<float, 3>> rows, ColorARGB c) {
      for (const auto& row : rows) stroke(poly({{row[1], row[0]}, {row[2], row[0]}}), c);
    };
    const auto pilcrow = [&](float dx, float top, float bottom) {
      ScalarPath bowl;
      bowl.MoveTo(P(dx + 5, top + 5));
      bowl.CubicTo(P(dx + 0.5f, top + 5), P(dx + 0.5f, top), P(dx + 5, top));
      bowl.Close();
      fill(bowl, color);
      stroke(poly({{dx + 5, top}, {dx + 5, bottom}}), color, 1.2f);
      stroke(poly({{dx + 8, top}, {dx + 8, bottom}}), color, 1.2f);
      stroke(poly({{dx + 5, top}, {dx + 9.5f, top}}), color, 1.2f);
    };
    const auto save_shape = [&] {
      stroke(poly({{2.5f, 2.5f}, {11.5f, 2.5f}, {13.5f, 4.5f}, {13.5f, 13.5f}, {2.5f, 13.5f}}, true), color);
      stroke(box(5.5f, 2.5f, 10.5f, 6), color);
      stroke(box(4.5f, 9, 11.5f, 13.5f), color);
    };
    const TextStyle letter{size * 0.8f, color, 600};
    switch (icon) {
    case Icon::Paste:
      fill(box(2.5f, 2.5f, 11.5f, 14.5f, 1), 0xfff1dfba);
      stroke(box(2.5f, 2.5f, 11.5f, 14.5f, 1), color);
      fill(box(5, 1, 9, 4, 0.5f), color);
      fill(box(7.5f, 6.5f, 14.5f, 15), kWhite);
      stroke(box(7.5f, 6.5f, 14.5f, 15), color);
      lines({{9.5f, 9.5f, 12.5f}, {12, 9.5f, 12.5f}}, kBlue);
      break;
    case Icon::Cut:
      stroke(circle(4.5f, 12, 2.2f), color);
      stroke(circle(11.5f, 12, 2.2f), color);
      stroke(poly({{6, 10.3f}, {11.5f, 1.5f}}), color);
      stroke(poly({{10, 10.3f}, {4.5f, 1.5f}}), color);
      break;
    case Icon::Copy:
      stroke(box(2.5f, 1.5f, 10.5f, 11.5f), color);
      fill(box(5.5f, 4.5f, 13.5f, 14.5f), kWhite);
      stroke(box(5.5f, 4.5f, 13.5f, 14.5f), color);
      break;
    case Icon::Painter:
      fill(box(2.5f, 1.5f, 12.5f, 5.5f, 1), 0xffdbe7f7);
      stroke(box(2.5f, 1.5f, 12.5f, 5.5f, 1), color);
      stroke(poly({{12.5f, 3.5f}, {14.5f, 3.5f}, {14.5f, 7.5f}, {8, 7.5f}, {8, 9.5f}}), color);
      fill(box(6.5f, 9.5f, 9.5f, 14.5f, 0.5f), color);
      break;
    case Icon::Undo:
    case Icon::Redo: {
      const bool redo = icon == Icon::Redo;
      const auto X = [&](float value) { return redo ? 16 - value : value; };
      ScalarPath path;
      path.MoveTo(P(X(3.5f), 5.5f));
      path.LineTo(P(X(10), 5.5f));
      path.CubicTo(P(X(12.5f), 5.5f), P(X(14), 7.4f), P(X(14), 9.5f));
      path.CubicTo(P(X(14), 11.6f), P(X(12.5f), 13.5f), P(X(10), 13.5f));
      path.LineTo(P(X(6.5f), 13.5f));
      stroke(path, color, 1.3f);
      stroke(poly({{X(6.5f), 2.5f}, {X(3.5f), 5.5f}, {X(6.5f), 8.5f}}), color, 1.3f);
      break;
    }
    case Icon::Save:
      save_shape();
      break;
    case Icon::SaveAs:
      save_shape();
      stroke(poly({{9, 15}, {15, 9}}), kBlue, 1.8f);
      break;
    case Icon::Grow:
    case Icon::Shrink: {
      const bool grow = icon == Icon::Grow;
      Label(raster, "A", x, y + (grow ? 0 : 2) * size / 16, {size * (grow ? 0.82f : 0.68f), color, 600});
      stroke(grow ? poly({{11, 5}, {13, 3}, {15, 5}}) : poly({{11, 3}, {13, 5}, {15, 3}}), color, 1.1f);
      break;
    }
    case Icon::Case:
      Label(raster, "Aa", x, y + size * 0.04f, {size * 0.72f, color, 400});
      break;
    case Icon::ClearFormat:
      Label(raster, "A", x, y, letter);
      fill(poly({{8.5f, 12.5f}, {12, 9}, {15, 12}, {11.5f, 15.5f}}, true), 0xffeba1b6);
      stroke(poly({{8.5f, 12.5f}, {12, 9}, {15, 12}, {11.5f, 15.5f}}, true), 0xffb2486a);
      break;
    case Icon::Effects:
      Label(raster, "A", x + 0.08f * size, y + 0.08f * size, {size * 0.9f, 0xffbdd3f2, 700});
      Label(raster, "A", x, y, {size * 0.9f, 0xff2e75d6, 700});
      break;
    case Icon::AlignLeft:
      lines({{3.5f, 2, 14}, {6.5f, 2, 10}, {9.5f, 2, 14}, {12.5f, 2, 10}}, color);
      break;
    case Icon::AlignCenter:
      lines({{3.5f, 2, 14}, {6.5f, 4, 12}, {9.5f, 2, 14}, {12.5f, 4, 12}}, color);
      break;
    case Icon::AlignRight:
      lines({{3.5f, 2, 14}, {6.5f, 6, 14}, {9.5f, 2, 14}, {12.5f, 6, 14}}, color);
      break;
    case Icon::AlignJustify:
      lines({{3.5f, 2, 14}, {6.5f, 2, 14}, {9.5f, 2, 14}, {12.5f, 2, 14}}, color);
      break;
    case Icon::LineSpacing:
      lines({{3.5f, 7, 14}, {6.5f, 7, 14}, {9.5f, 7, 14}, {12.5f, 7, 14}}, color);
      stroke(poly({{3.5f, 2}, {3.5f, 14}}), kBlue);
      stroke(poly({{1.5f, 4}, {3.5f, 2}, {5.5f, 4}}), kBlue);
      stroke(poly({{1.5f, 12}, {3.5f, 14}, {5.5f, 12}}), kBlue);
      break;
    case Icon::Indent:
      lines({{3.5f, 7, 14}, {6.5f, 2, 14}, {9.5f, 2, 14}, {12.5f, 2, 10}}, color);
      stroke(poly({{1.5f, 3.5f}, {5, 3.5f}}), kBlue);
      stroke(poly({{3.5f, 1.8f}, {5.2f, 3.5f}, {3.5f, 5.2f}}), kBlue);
      break;
    case Icon::Pilcrow:
      pilcrow(2.5f, 1.5f, 14.5f);
      break;
    case Icon::Ltr:
    case Icon::Rtl: {
      const bool ltr = icon == Icon::Ltr;
      pilcrow(ltr ? 2 : 4.5f, 1.5f, 10);
      stroke(poly({{2, 13}, {14, 13}}), kBlue);
      stroke(ltr ? poly({{12, 11}, {14, 13}, {12, 15}}) : poly({{4, 11}, {2, 13}, {4, 15}}), kBlue);
      break;
    }
    case Icon::Vertical:
      stroke(poly({{12.5f, 2}, {12.5f, 14}}), color);
      stroke(poly({{9.5f, 2}, {9.5f, 11}}), color);
      stroke(poly({{6.5f, 2}, {6.5f, 14}}), color);
      stroke(poly({{2.5f, 2}, {2.5f, 14}}), kBlue);
      stroke(poly({{0.8f, 12}, {2.5f, 14}, {4.2f, 12}}), kBlue);
      break;
    case Icon::SelectAll:
      for (float t = 2.5f; t < 13.5f; t += 3) {
        const float e = std::min(t + 1.6f, 13.5f);
        stroke(poly({{t, 2.5f}, {e, 2.5f}}), color);
        stroke(poly({{t, 13.5f}, {e, 13.5f}}), color);
        stroke(poly({{2.5f, t}, {2.5f, e}}), color);
        stroke(poly({{13.5f, t}, {13.5f, e}}), color);
      }
      fill(poly({{6, 5}, {6, 12}, {7.8f, 10.3f}, {9.2f, 13.3f}, {10.4f, 12.7f}, {9, 9.8f}, {11.5f, 9.8f}}, true), color);
      break;
    case Icon::Chevron:
      stroke(poly({{4.5f, 6.5f}, {8, 10}, {11.5f, 6.5f}}), color, 1.2f);
      break;
    case Icon::ChevronRight:
      stroke(poly({{6.5f, 4.5f}, {10, 8}, {6.5f, 11.5f}}), color, 1.2f);
      break;
    case Icon::Launcher:
      stroke(poly({{2.5f, 9}, {2.5f, 2.5f}, {9, 2.5f}}), color);
      stroke(poly({{6, 6}, {13, 13}}), color);
      stroke(poly({{8, 13}, {13, 13}, {13, 8}}), color);
      break;
    case Icon::Close:
      stroke(poly({{4, 4}, {12, 12}}), color, 1.2f);
      stroke(poly({{12, 4}, {4, 12}}), color, 1.2f);
      break;
    case Icon::Back:
      stroke(circle(8, 8, 7), color, 1.2f);
      stroke(poly({{11.5f, 8}, {4.5f, 8}}), color, 1.2f);
      stroke(poly({{7.5f, 5}, {4.5f, 8}, {7.5f, 11}}), color, 1.2f);
      break;
    case Icon::NewDoc:
    case Icon::Sample:
      stroke(poly({{3.5f, 1.5f}, {9.5f, 1.5f}, {12.5f, 4.5f}, {12.5f, 14.5f}, {3.5f, 14.5f}}, true), color);
      stroke(poly({{9.5f, 1.5f}, {9.5f, 4.5f}, {12.5f, 4.5f}}), color);
      if (icon == Icon::Sample) {
        stroke(poly({{5.5f, 6.5f}, {9.5f, 6.5f}}), kBlue, 1.5f);
        lines({{9, 5.5f, 10.5f}, {11, 5.5f, 10.5f}, {13, 5.5f, 8.5f}}, color);
      }
      break;
    case Icon::Open:
      stroke(poly({{1.5f, 3.5f}, {6, 3.5f}, {7.5f, 5.5f}, {14.5f, 5.5f}, {14.5f, 13.5f}, {1.5f, 13.5f}}, true), color);
      break;
    case Icon::Info:
      stroke(circle(8, 8, 6.5f), color);
      fill(circle(8, 4.9f, 0.9f), color);
      stroke(poly({{8, 7.3f}, {8, 11.6f}}), color, 1.3f);
      break;
    case Icon::ZoomIn:
    case Icon::ZoomOut:
      stroke(circle(6.5f, 6.5f, 4.5f), color);
      stroke(poly({{9.8f, 9.8f}, {14, 14}}), color, 1.8f);
      stroke(poly({{4.5f, 6.5f}, {8.5f, 6.5f}}), kBlue);
      if (icon == Icon::ZoomIn) stroke(poly({{6.5f, 4.5f}, {6.5f, 8.5f}}), kBlue);
      break;
    case Icon::Zoom100:
      stroke(box(1.5f, 3.5f, 14.5f, 12.5f), color);
      CenterLabel(raster, "100", {x, y + size * 0.25f, size, size * 0.5f}, {size * 0.3f, kBlue, 600});
      break;
    case Icon::PageWidth:
      stroke(box(4.5f, 1.5f, 11.5f, 14.5f), color);
      stroke(poly({{1, 8}, {15, 8}}), kBlue);
      stroke(poly({{2.8f, 6.2f}, {1, 8}, {2.8f, 9.8f}}), kBlue);
      stroke(poly({{13.2f, 6.2f}, {15, 8}, {13.2f, 9.8f}}), kBlue);
      break;
    case Icon::Check:
      stroke(poly({{3, 8}, {6.5f, 11.5f}, {13, 4.5f}}), color, 1.6f);
      break;
    }
  }

  // Controls. Each paints itself and registers its hit area in `buttons`.
  bool Hot(Rect rect) const {
    return !Modal() && rect.Contains(mouse_x, mouse_y);
  }
  void Hit(Rect rect, std::function<void()> action, std::string hint = {}) {
    if (action || !hint.empty()) buttons.push_back({rect, std::move(action), std::move(hint)});
  }
  void Face(RasterCanvas& raster, Rect rect, bool checked, bool enabled, bool dark = false) {
    const bool hot = enabled && Hot(rect);
    if (dark) {
      if (checked || hot) Fill(raster, rect, checked ? kBlueActive : kBlueHover);
    } else if (checked)
      Fill(raster, rect, hot ? kCheckedHover : kChecked, 2);
    else if (hot)
      Fill(raster, rect, kHover, 2);
  }
  void IconButton(RasterCanvas& raster, Rect rect, Icon icon, std::string_view label, std::function<void()> action,
                  std::string hint, bool checked = false, bool enabled = true) {
    Face(raster, rect, checked, enabled);
    PaintIcon(raster, icon, rect.x + 5, rect.y + (rect.height - 16) / 2, 16, enabled ? kIcon : kDisabled);
    if (!label.empty()) Label(raster, label, rect.x + 26, rect.y + (rect.height - 15) / 2, {12, enabled ? kInk : kDisabled}, rect.width - 28);
    Hit(rect, enabled ? std::move(action) : nullptr, std::move(hint));
  }
  void GlyphButton(RasterCanvas& raster, Rect rect, const std::function<void(Rect)>& draw, std::function<void()> action,
                   std::string hint, bool checked = false) {
    Face(raster, rect, checked, true);
    draw(rect);
    Hit(rect, std::move(action), std::move(hint));
  }
  void LargeButton(RasterCanvas& raster, Rect rect, Icon icon, std::string_view label, std::function<void()> action,
                   std::string hint, bool enabled = true) {
    Face(raster, rect, false, enabled);
    PaintIcon(raster, icon, rect.x + (rect.width - 32) / 2, rect.y + 6, 32, enabled ? kIcon : kDisabled);
    CenterLabel(raster, label, {rect.x, rect.y + 44, rect.width, 18}, {12, enabled ? kInk : kDisabled});
    Hit(rect, enabled ? std::move(action) : nullptr, std::move(hint));
  }
  // A command with an attached dropdown arrow, e.g. underline or font color.
  void SplitButton(RasterCanvas& raster, Rect rect, const std::function<void(Rect)>& draw, std::function<void()> action,
                   std::function<void()> menu, std::string hint, bool checked = false) {
    const Rect main{rect.x, rect.y, rect.width - 13, rect.height}, arrow{rect.x + rect.width - 13, rect.y, 13, rect.height};
    if (checked) Fill(raster, main, Hot(main) ? kCheckedHover : kChecked, 2);
    if (Hot(rect)) {
      if (!checked || Hot(arrow)) Fill(raster, Hot(main) ? main : arrow, kHover, 2);
      Outline(raster, rect, kBorder);
    }
    draw(main);
    PaintIcon(raster, Icon::Chevron, arrow.x + 1, rect.y + (rect.height - 11) / 2, 11, kIcon);
    Hit(main, std::move(action), hint);
    Hit(arrow, std::move(menu), hint.substr(0, hint.find('\n')) + "\n更多选项");
  }
  // An icon with a dropdown arrow that opens a property menu.
  void MenuButton(RasterCanvas& raster, Rect rect, Icon icon, const char* property, std::string hint) {
    const size_t index = IndexOf(property);
    Face(raster, rect, popup == static_cast<int>(index), true);
    PaintIcon(raster, icon, rect.x + 4, rect.y + (rect.height - 16) / 2, 16, kIcon);
    PaintIcon(raster, Icon::Chevron, rect.x + rect.width - 13, rect.y + (rect.height - 11) / 2, 11, kIcon);
    Hit(rect, [this, index, rect] { OpenPopup(index, rect); }, std::move(hint));
  }
  void ComboBox(RasterCanvas& raster, Rect rect, std::string_view text, std::function<void()> action, std::string hint, bool open) {
    const bool hot = Hot(rect);
    Fill(raster, rect, kWhite);
    if (hot || open) Fill(raster, {rect.x + rect.width - 18, rect.y, 18, rect.height}, open ? 0xffcce0f7 : 0xffe5eef9);
    Outline(raster, rect, hot || open ? kBlue : kBorder);
    Label(raster, text, rect.x + 6, rect.y + (rect.height - 15) / 2, {12, kInk}, rect.width - 26);
    PaintIcon(raster, Icon::Chevron, rect.x + rect.width - 17, rect.y + (rect.height - 16) / 2, 16, kIcon);
    Hit(rect, std::move(action), std::move(hint));
  }
  const std::string& DisplayFontName(const std::string& value) {
    if (auto found = font_names.find(value); found != font_names.end()) return found->second;
    LoadFontChoices();
    const auto name = FontNameLabel(value), folded = FoldFontName(name);
    std::string display = name;
    for (const auto& choice : font_choices)
      if (choice.Matches(value, folded)) {
        display = choice.name;
        break;
      }
    return font_names.emplace(value, std::move(display)).first->second;
  }
  void PropertyCombo(RasterCanvas& raster, const PropertySpec& spec, Rect rect) {
    const size_t index = static_cast<size_t>(&spec - PropertyCatalog().data());
    const std::string& current = Value(index);
    const std::string display = std::string_view(spec.name) == "font-family" ? DisplayFontName(current) : current;
    const bool mixed = Mixed(index);
    // Like Word, a mixed selection shows an empty field.
    ComboBox(raster, rect, mixed ? std::string() : display, [this, index, rect] { OpenPopup(index, rect); },
             std::string(spec.label) + "\n" + spec.name + ": " + (mixed ? "（多种值）" : display), popup == static_cast<int>(index));
  }
  void CheckBox(RasterCanvas& raster, Rect rect, std::string_view label, bool checked, std::function<void()> action) {
    Face(raster, rect, false, true);
    const Rect box{rect.x + 5, rect.y + (rect.height - 14) / 2, 14, 14};
    Fill(raster, box, checked ? kBlue : kWhite);
    if (checked)
      PaintIcon(raster, Icon::Check, box.x, box.y, 14, kWhite);
    else
      Outline(raster, box, 0xff767676);
    Label(raster, label, box.x + 21, rect.y + (rect.height - 15) / 2, {12, kInk}, rect.width - 30);
    Hit(rect, std::move(action));
  }
  // Ribbon group caption, separator and optional dialog launcher.
  void GroupEnd(RasterCanvas& raster, float x0, float x1, std::string_view label, int section) {
    CenterLabel(raster, label, {x0, 149, x1 - x0, 18}, {11, kMuted});
    Fill(raster, {x1 - Hair(), 78, Hair(), 84}, kLine);
    if (section < 0) return;
    const Rect rect{x1 - 19, 151, 15, 15};
    Face(raster, rect, false, true);
    PaintIcon(raster, Icon::Launcher, rect.x + 2, rect.y + 2, 11, kIcon);
    Hit(rect, [this, section] { OpenPaneSection(section); }, std::string(label) + "设置\n在“格式”窗格中显示全部属性");
  }

  // Dropdowns: a filtered font list, a color grid, or a property's choices.
  PopupKind Kind() const {
    if (popup < 0) return PopupKind::List;
    const std::string_view name = PropertyCatalog()[popup].name;
    if (name == "font-family") return PopupKind::Font;
    if (name.ends_with("color")) return PopupKind::Color;
    return PopupKind::List;
  }
  size_t PopupChoices() const {
    return Kind() == PopupKind::Font ? filtered_fonts.size() : PropertyCatalog()[popup].choices.size();
  }
  // Index PopupChoices() is the "restore default" row, whose value is empty.
  std::string PopupValue(size_t index) const {
    if (index >= PopupChoices()) return {};
    return Kind() == PopupKind::Font ? font_choices[filtered_fonts[index]].value : PropertyCatalog()[popup].choices[index];
  }
  bool FontChoiceMatches(size_t index, const std::string& current, const std::string& folded) const {
    return font_choices[filtered_fonts[index]].Matches(current, folded);
  }
  void LoadFontChoices() {
    if (fonts_loaded) return;
    font_choices = InstalledFonts();
    installed_font_count = font_choices.size();
    for (const auto& generic : FindProperty("font-family")->choices) {
      const auto folded = FoldFontName(generic);
      font_choices.push_back({generic, generic, folded, folded, {folded}});
    }
    fonts_loaded = true;
  }
  float RowHeight() const {
    return Kind() == PopupKind::Font ? 30.0f : 26.0f;
  }
  Rect PopupBox() const {
    float pw = 234, ph = 254;
    if (Kind() != PopupKind::Color) {
      const float row = RowHeight();
      const int rows = std::clamp(static_cast<int>((height - kStatusHeight - 84 - 44) / row), 1, Kind() == PopupKind::Font ? 14 : 12);
      pw = Kind() == PopupKind::Font ? 320.0f : std::max(220.0f, popup_anchor.width);
      ph = 44 + std::min(PopupChoices() + 1, static_cast<size_t>(rows)) * row + 6;
    }
    return {std::clamp(popup_anchor.x, 8.0f, width - pw - 8),
            std::clamp(popup_anchor.y + popup_anchor.height + 2, 72.0f, std::max(72.0f, height - kStatusHeight - ph - 5)), pw, ph};
  }
  Rect PopupList() const {
    const auto box = PopupBox();
    return {box.x + 4, box.y + 44, box.width - 16, box.height - 50};
  }
  Rect PopupInputBox() const {
    const auto box = PopupBox();
    if (Kind() == PopupKind::Color) return {box.x + 8, box.y + 218, box.width - 16, 28};
    return {box.x + 8, box.y + 8, box.width - 16, 28};
  }
  float PopupMaxScroll() const {
    return std::max(0.0f, (PopupChoices() + 1) * RowHeight() - PopupList().height);
  }
  Rect PopupScrollThumb() const {
    if (Kind() == PopupKind::Color) return {};
    const auto list = PopupList();
    const float maximum = PopupMaxScroll();
    if (maximum <= 0) return {};
    const float thumb = std::max(24.0f, list.height * list.height / (list.height + maximum));
    return {list.x + list.width + 3, list.y + popup_scroll / maximum * (list.height - thumb), 5, thumb};
  }
  int PopupHit() const {
    if (Kind() == PopupKind::Color) return -1;
    const auto list = PopupList();
    if (!list.Contains(mouse_x, mouse_y)) return -1;
    const float row = RowHeight(), offset = mouse_y - list.y + popup_scroll;
    const int index = static_cast<int>(offset / row);
    return offset - index * row < row - 2 && index <= static_cast<int>(PopupChoices()) ? index : -1;
  }
  static constexpr int kColorAuto = 100;
  Rect ColorAutoRow() const {
    const auto box = PopupBox();
    return {box.x + 6, box.y + 6, box.width - 12, 26};
  }
  Rect ColorCell(int index) const {
    const auto box = PopupBox();
    const int column = index % 10, row = index / 10;
    const float y = index >= 60 ? 192 : row == 0 ? 56 : 78 + (row - 1) * 18.0f;
    return {box.x + 10 + column * 22.0f, box.y + y, 16, 16};
  }
  int ColorHit() const {
    if (Kind() != PopupKind::Color) return -1;
    if (ColorAutoRow().Contains(mouse_x, mouse_y)) return kColorAuto;
    for (int i = 0; i < 70; ++i) {
      const auto cell = ColorCell(i);
      if (Rect{cell.x - 3, cell.y - 1, cell.width + 6, cell.height + 2}.Contains(mouse_x, mouse_y)) return i;
    }
    return -1;
  }
  void FilterFonts() {
    const auto query = FoldFontName(EncodeUTF8(popup_input));
    filtered_fonts.clear();
    for (size_t i = 0; i < font_choices.size(); ++i)
      if (query.empty() || font_choices[i].search.find(query) != std::string::npos) filtered_fonts.push_back(i);
    popup_scroll = 0;
    popup_active = filtered_fonts.empty() ? -1 : 0;
    dirty = true;
  }
  void AppendPopupInput(std::u16string_view text) {
    if (popup_input_selected) popup_input.clear();
    popup_input_selected = false;
    size_t count = std::min(text.size(), 1024 - popup_input.size());
    if (count < text.size() && count && text[count - 1] >= 0xd800 && text[count - 1] <= 0xdbff) --count;
    popup_input.append(text.substr(0, count));
    if (Kind() == PopupKind::Font) FilterFonts();
    dirty = true;
  }
  void RevealPopupChoice() {
    const auto list = PopupList();
    const float row = RowHeight();
    if (popup_active >= 0) {
      popup_scroll = std::min(popup_scroll, popup_active * row);
      popup_scroll = std::max(popup_scroll, (popup_active + 1) * row - list.height);
    }
    popup_scroll = std::clamp(popup_scroll, 0.0f, PopupMaxScroll());
    dirty = true;
  }
  void OpenPopup(size_t index, Rect anchor) {
    popup = static_cast<int>(index);
    popup_anchor = anchor;
    popup_scroll = 0;
    popup_active = -1;
    popup_scrollbar_drag = dragging = scrollbar_drag = false;
    popup_input.clear();
    popup_input_selected = false;
    if (Kind() == PopupKind::Font) {
      LoadFontChoices();
      FilterFonts();
    }
    if (Kind() != PopupKind::Color) {
      const auto& current = Value(index);
      const auto current_name = FoldFontName(FontNameLabel(current));
      popup_active = -1;
      for (size_t i = 0; i < PopupChoices(); ++i) {
        if (PopupValue(i) == current || (Kind() == PopupKind::Font && FontChoiceMatches(i, current, current_name))) {
          popup_active = static_cast<int>(i);
          break;
        }
      }
    }
    RevealPopupChoice();
    UpdateIme();
  }
  void ApplyPopupValue(const std::string& value) {
    const bool color = std::string_view(PropertyCatalog()[popup].name) == "color";
    if (Format(PropertyCatalog()[popup].name, value) && color && !value.empty()) font_color = value;
  }
  void ApplyPopupInput() {
    std::string text = EncodeUTF8(popup_input);
    while (!text.empty() && text.back() == ' ') text.pop_back();
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    const int count = static_cast<int>(PopupChoices());
    if (Kind() == PopupKind::Font) {
      if (popup_active >= 0 && popup_active <= count)
        ApplyPopupValue(PopupValue(popup_active));
      else if (!text.empty())
        ApplyPopupValue(QuoteFontName(text)); // A family that is not enumerated.
    } else if (!text.empty())
      ApplyPopupValue(Kind() == PopupKind::Color && text.front() != '#' ? "#" + text : text);
    else if (popup_active >= 0 && Kind() == PopupKind::List)
      ApplyPopupValue(PopupValue(popup_active));
  }

  // Scene regions.
  void TitleBar(RasterCanvas& raster) {
    Fill(raster, {0, 0, width, kTitleHeight}, kBlue);
    Fill(raster, {12, 9, 22, 22}, kWhite, 3);
    CenterLabel(raster, "W", {12, 9, 22, 22}, {14, kBlue, 700});
    const auto quick = [&](float x, Icon icon, std::function<void()> action, std::string hint, bool enabled) {
      const Rect rect{x, 6, 30, 28};
      Face(raster, rect, false, enabled, true);
      PaintIcon(raster, icon, rect.x + 7, rect.y + 6, 16, enabled ? kWhite : 0xff86a8de);
      Hit(rect, enabled ? std::move(action) : nullptr, std::move(hint));
    };
    quick(46, Icon::Save, [this] { Save(false); }, "保存 (Ctrl+S)", true);
    quick(78, Icon::Undo, [this] { Undo(false); }, "撤销 (Ctrl+Z)", document.CanUndo());
    quick(110, Icon::Redo, [this] { Undo(true); }, "重做 (Ctrl+Y)", document.CanRedo());
    Fill(raster, {148, 12, Hair(), 16}, 0xff5b88d0);
    const std::string title = DocumentName() + (document.Modified() ? " *" : "") + "  -  bkfont 文档";
    const float w = LabelWidth(title, {12, kWhite});
    Label(raster, title, std::max(160.0f, (width - w) / 2), 12, {12, kWhite}, width - 320);
  }
  void TabStrip(RasterCanvas& raster) {
    Fill(raster, {0, kTitleHeight, width, kTabBottom - kTitleHeight}, kBlue);
    static constexpr const char* kTabs[] = {"文件", "开始", "字体", "段落", "文字效果", "视图"};
    float x = 6;
    for (int i = 0; i < 6; ++i) {
      const float w = LabelWidth(kTabs[i], {13, kWhite}) + 30;
      const Rect rect{x, kTitleHeight + 3, w, kTabBottom - kTitleHeight - 3};
      const bool active = i > 0 && tab == i - 1;
      if (active)
        Fill(raster, rect, kRibbon);
      else if (Hot(rect))
        Fill(raster, rect, i == 0 ? kBlueActive : kBlueHover);
      CenterLabel(raster, kTabs[i], rect, {13, active ? kBlue : kWhite, active ? 600 : 400});
      if (i == 0)
        Hit(rect, [this] { OpenBackstage(); }, "文件\n新建、打开、保存与文档信息");
      else
        Hit(rect, [this, i] { tab = i - 1; popup = -1; dirty = true; });
      x += w + 2;
    }
    Label(raster, std::to_string(PropertyCatalog().size()) + " 项 CSS 文字属性", width - 160, kTitleHeight + 8, {12, 0xffc9d9f2}, 150);
  }
  void Ribbon(RasterCanvas& raster) {
    Fill(raster, {0, kTabBottom, width, kRibbonBottom - kTabBottom}, kRibbon);
    Fill(raster, {0, kRibbonBottom - Hair(), width, Hair()}, kLine);
    if (tab == 0)
      HomeTab(raster);
    else if (tab == 4)
      ViewTab(raster);
    else
      CatalogTab(raster);
  }
  void HomeTab(RasterCanvas& raster) {
    const bool selected = !document.GetSelection().Empty();
    LargeButton(raster, {8, 75, 48, 72}, Icon::Paste, "粘贴", [this] { Paste(); }, "粘贴 (Ctrl+V)\n插入剪贴板中的文字");
    IconButton(raster, {58, 75, 74, 23}, Icon::Cut, "剪切", [this] { Copy(true); }, "剪切 (Ctrl+X)", false, selected);
    IconButton(raster, {58, 99, 74, 23}, Icon::Copy, "复制", [this] { Copy(false); }, "复制 (Ctrl+C)", false, selected);
    IconButton(raster, {58, 123, 74, 23}, Icon::Painter, "格式刷", [this] { TogglePainter(); },
               "格式刷 (Ctrl+Shift+C / V)\n复制当前文字格式，再拖选要应用的文字", painter.has_value());
    GroupEnd(raster, 4, 138, "剪贴板", -1);

    // 字体
    const float f = 144, r1 = 79, r2 = 114, h = 25;
    PropertyCombo(raster, *FindProperty("font-family"), {f, r1, 150, h});
    PropertyCombo(raster, *FindProperty("font-size"), {f + 154, r1, 60, h});
    IconButton(raster, {f + 218, r1, 26, h}, Icon::Grow, {}, [this] { StepFontSize(1); }, "增大字号 (Ctrl+Shift+>)");
    IconButton(raster, {f + 246, r1, 26, h}, Icon::Shrink, {}, [this] { StepFontSize(-1); }, "减小字号 (Ctrl+Shift+<)");
    MenuButton(raster, {f + 276, r1, 36, h}, Icon::Case, "text-transform", "更改大小写\n全部大写、小写或首字母大写");
    IconButton(raster, {f + 316, r1, 26, h}, Icon::ClearFormat, {}, [this] { ClearFormatting(); }, "清除所有格式\n删除所选文字的字符格式");
    const auto glyph = [&](std::string_view text, TextStyle style) {
      return [this, &raster, text, style](Rect rect) { CenterLabel(raster, text, rect, style); };
    };
    GlyphButton(raster, {f, r2, 26, h}, glyph("B", {15, kIcon, 700}), [this] { Toggle("font-weight", "700", "400"); }, "加粗 (Ctrl+B)", Is("font-weight", "700"));
    GlyphButton(raster, {f + 28, r2, 26, h}, glyph("I", {15, kIcon, 400, true}), [this] { Toggle("font-style", "italic", "normal"); }, "倾斜 (Ctrl+I)", Is("font-style", "italic"));
    const Rect underline{f + 56, r2, 38, h}, font_color_button{f + 188, r2, 38, h};
    SplitButton(raster, underline, [&](Rect rect) {
        CenterLabel(raster, "U", {rect.x, rect.y - 1, rect.width, rect.height}, {14, kIcon});
        Fill(raster, {rect.x + rect.width / 2 - 5, rect.y + 19, 10, 1}, kIcon); },
        [this] { ToggleDecoration("underline"); },
        [this, underline] { OpenPopup(IndexOf("text-decoration-style"), underline); }, "下划线 (Ctrl+U)", HasDecoration("underline"));
    GlyphButton(raster, {f + 96, r2, 30, h}, [&](Rect rect) {
        CenterLabel(raster, "abc", rect, {12, kIcon});
        Fill(raster, {rect.x + 5, rect.y + rect.height / 2 + 1, rect.width - 10, 1}, kIcon); },
        [this] { ToggleDecoration("line-through"); }, "删除线", HasDecoration("line-through"));
    for (int i = 0; i < 2; ++i) {
      const bool sub = i == 0;
      GlyphButton(raster, {f + 128 + i * 28.0f, r2, 26, h}, [&, sub](Rect rect) {
          Label(raster, "x", rect.x + 7, rect.y + 3, {14, kIcon});
          Label(raster, "2", rect.x + 15, rect.y + (sub ? 10 : 1), {9, kBlue, 600}); },
          [this, sub] { Toggle("vertical-align", sub ? "sub" : "super", "baseline"); },
          sub ? "下标" : "上标 (Ctrl+Shift+=)", Is("vertical-align", sub ? "sub" : "super"));
    }
    SplitButton(raster, font_color_button, [&](Rect rect) {
        CenterLabel(raster, "A", {rect.x, rect.y - 3, rect.width, rect.height}, {14, kIcon, 600});
        Fill(raster, {rect.x + 5, rect.y + 18, rect.width - 10, 4}, ParseColor(font_color).value_or(0xffc00000)); },
        [this] { Format("color", font_color); }, [this, font_color_button] { OpenPopup(IndexOf("color"), font_color_button); },
        "字体颜色\n应用 " + font_color);
    MenuButton(raster, {f + 230, r2, 36, h}, Icon::Effects, "text-shadow", "文本效果\n为文字添加阴影");
    GroupEnd(raster, 138, f + 352, "字体", 0);

    // 段落
    const float p = f + 358;
    IconButton(raster, {p, r1, 26, h}, Icon::Pilcrow, {}, [this] { marks_visible = !marks_visible; dirty = true; },
               "显示/隐藏编辑标记 (Ctrl+Shift+8)", marks_visible);
    MenuButton(raster, {p + 28, r1, 36, h}, Icon::LineSpacing, "line-height", "行距\n设置所选段落的行间距");
    IconButton(raster, {p + 66, r1, 26, h}, Icon::Indent, {}, [this] { Toggle("text-indent", "2em", "0px"); }, "首行缩进 2 字符", Is("text-indent", "2em"));
    MenuButton(raster, {p + 94, r1, 36, h}, Icon::Vertical, "writing-mode", "文字方向\n横排或竖排");
    const char* align[] = {"left", "center", "right", "justify"};
    const char* align_hint[] = {"左对齐 (Ctrl+L)", "居中 (Ctrl+E)", "右对齐 (Ctrl+R)", "两端对齐 (Ctrl+J)"};
    const Icon align_icon[] = {Icon::AlignLeft, Icon::AlignCenter, Icon::AlignRight, Icon::AlignJustify};
    for (int i = 0; i < 4; ++i) {
      const bool checked = Is("text-align", align[i]) || (i == 0 && Is("text-align", "start"));
      IconButton(raster, {p + i * 28.0f, r2, 26, h}, align_icon[i], {}, [this, value = std::string(align[i])] { Format("text-align", value); },
                 align_hint[i], checked);
    }
    IconButton(raster, {p + 120, r2, 26, h}, Icon::Ltr, {}, [this] { Format("direction", "ltr"); }, "从左向右文字方向", Is("direction", "ltr"));
    IconButton(raster, {p + 148, r2, 26, h}, Icon::Rtl, {}, [this] { Format("direction", "rtl"); }, "从右向左文字方向", Is("direction", "rtl"));
    GroupEnd(raster, f + 352, p + 182, "段落", 2);

    // 样式 gallery, as wide as the window allows, then 编辑.
    const float g = p + 188, edit = 92;
    const int tiles = std::clamp(static_cast<int>((width - g - 10 - edit) / 80), 2, static_cast<int>(StylePresets().size()));
    const Rect gallery{g, 76, tiles * 80.0f + 2, 70};
    Fill(raster, gallery, kWhite);
    Outline(raster, gallery, kLine);
    for (int i = 0; i < tiles; ++i) {
      const auto& preset = StylePresets()[i];
      const Rect tile{g + 1 + i * 80.0f, 77, 80, 68};
      const bool active = PresetActive(i);
      if (active || Hot(tile)) Fill(raster, tile, active ? 0xffdce8f7 : 0xffececec);
      if (active) Outline(raster, tile, 0xff8fb1e0);
      TextStyle preview{14, kBodyInk};
      const float px = ParseLength(PropertyValue(preset.properties, "font-size", "18px"), 18, 18, 18);
      preview.size = std::clamp(8 + px / 3, 11.0f, 20.0f);
      preview.color = ParseColor(PropertyValue(preset.properties, "color")).value_or(kBodyInk);
      preview.weight = std::atoi(PropertyValue(preset.properties, "font-weight", "400").c_str());
      preview.italic = PropertyValue(preset.properties, "font-style") == "italic";
      CenterLabel(raster, "AaBb", {tile.x, tile.y + 4, tile.width, 38}, preview);
      CenterLabel(raster, preset.name, {tile.x, tile.y + 46, tile.width, 16}, {11, kInk});
      std::string detail;
      for (const auto& [name, value] : preset.properties) detail += (detail.empty() ? "" : "; ") + name + ": " + value;
      Hit(tile, [this, i] { ApplyPreset(i); }, std::string("样式：") + preset.name + "\n" + (detail.empty() ? "默认正文格式" : detail));
    }
    const float e = gallery.x + gallery.width + 8;
    GroupEnd(raster, p + 182, e, "样式", -1);
    IconButton(raster, {e + 6, 75, 80, 23}, Icon::SelectAll, "全选", [this] { SelectAll(); }, "全选 (Ctrl+A)");
    IconButton(raster, {e + 6, 99, 80, 23}, Icon::Undo, "撤销", [this] { Undo(false); }, "撤销 (Ctrl+Z)", false, document.CanUndo());
    IconButton(raster, {e + 6, 123, 80, 23}, Icon::Redo, "重做", [this] { Undo(true); }, "重做 (Ctrl+Y)", false, document.CanRedo());
    GroupEnd(raster, e, e + edit, "编辑", -1);
  }
  void ViewTab(RasterCanvas& raster) {
    CheckBox(raster, {10, 76, 116, 22}, "标尺", ruler_visible, [this] { ruler_visible = !ruler_visible; Layout(); dirty = true; });
    CheckBox(raster, {10, 100, 116, 22}, "编辑标记", marks_visible, [this] { marks_visible = !marks_visible; dirty = true; });
    CheckBox(raster, {10, 124, 116, 22}, "格式窗格", pane_visible, [this] { pane_visible = !pane_visible; Layout(); dirty = true; });
    GroupEnd(raster, 4, 134, "显示", -1);
    LargeButton(raster, {140, 75, 54, 72}, Icon::ZoomIn, "放大", [this] { Zoom(zoom + 0.1f); }, "放大 (Ctrl+=)", zoom < 2);
    LargeButton(raster, {196, 75, 54, 72}, Icon::ZoomOut, "缩小", [this] { Zoom(zoom - 0.1f); }, "缩小 (Ctrl+-)", zoom > 0.5f);
    LargeButton(raster, {252, 75, 54, 72}, Icon::Zoom100, "100%", [this] { Zoom(1); }, "缩放到 100% (Ctrl+0)");
    LargeButton(raster, {308, 75, 54, 72}, Icon::PageWidth, "页宽", [this] { FitPageWidth(); }, "页宽\n缩放文档，使页面宽度与窗口一致");
    GroupEnd(raster, 134, 368, "缩放", -1);
    LargeButton(raster, {374, 75, 66, 72}, Icon::Sample, "示例文档", [this] { Request(Pending::Sample); }, "示例文档\n载入多语言排版示例");
    GroupEnd(raster, 368, 446, "文档", -1);
  }
  void CatalogTab(RasterCanvas& raster) {
    constexpr float kField = 104, kRows[] = {76, 100, 124};
    const TextStyle caption{12, kMuted};
    float x = 4;
    bool collapse = false;
    for (const auto& group : RibbonGroups()) {
      if (group.tab != tab) continue;
      const size_t count = group.properties.size(), columns = (count + 2) / 3;
      std::vector<float> label_width(columns, 0);
      for (size_t i = 0; i < count; ++i)
        label_width[i / 3] = std::max(label_width[i / 3], LabelWidth(FindProperty(group.properties[i])->label, caption));
      float full = 10;
      for (float w : label_width) full += w + 6 + kField + 10;
      // Like Word, groups that no longer fit collapse into a single button.
      collapse = collapse || x + full > width - 4;
      if (collapse) {
        const float w = std::max(64.0f, LabelWidth(group.label, caption) + 16);
        LargeButton(raster, {x + 6, 75, w, 72}, Icon::Launcher, group.label, [this] { OpenPaneSection(tab); },
                    std::string(group.label) + "\n空间不足，在“格式”窗格中设置");
        Fill(raster, {x + w + 12 - Hair(), 78, Hair(), 84}, kLine);
        x += w + 12;
        continue;
      }
      float column_x = x + 8;
      for (size_t c = 0; c < columns; ++c) {
        for (size_t r = 0; r < 3 && c * 3 + r < count; ++r) {
          const auto& spec = *FindProperty(group.properties[c * 3 + r]);
          Label(raster, spec.label, column_x, kRows[r] + 3, caption, label_width[c] + 2);
          PropertyCombo(raster, spec, {column_x + label_width[c] + 6, kRows[r], kField, 22});
        }
        column_x += label_width[c] + 6 + kField + 10;
      }
      GroupEnd(raster, x, x + full, group.label, tab);
      x += full;
    }
  }

  void Chrome(RasterCanvas& raster) {
    if (backstage) {
      Backstage(raster);
      return;
    }
    TitleBar(raster);
    TabStrip(raster);
    Ribbon(raster);
  }

  void Ruler(RasterCanvas& raster) {
    const Rect area{0, kRibbonBottom, ContentWidth(), kRulerHeight};
    Fill(raster, area, kCanvas);
    raster.Save();
    Clip(raster, {0, area.y, ContentWidth() - kScrollbar, area.height});
    const float page = view.Width() / scale, margin = kPageMargin * zoom, left = PageX(), top = area.y + 5, bar = 15;
    Fill(raster, {left, top, page, bar}, 0xffcdcdcd);
    Fill(raster, {left + margin, top, page - 2 * margin, bar}, kWhite);
    // One unit is one 字符 at the 18px body size, numbered every two.
    const float unit = 18 * zoom;
    for (int i = -static_cast<int>(margin / unit); left + margin + i * unit <= left + page; ++i) {
      if (i == 0) continue;
      const float x = left + margin + i * unit;
      if (i % 2 == 0)
        CenterLabel(raster, std::to_string(std::abs(i)), {x - 12, top + 1, 24, 13}, {9, 0xff505050});
      else
        Fill(raster, {x, top + 6, Hair(), 3}, 0xff707070);
    }
    // Indent markers: first line (top) and left indent (bottom).
    const float first = left + margin + ParseLength(Value("text-indent"), 18, page - 2 * kPageMargin, 0) * zoom;
    const auto marker = [&](float x, bool down) {
      ScalarPath path;
      const float y0 = down ? top - 1 : top + bar + 1, y1 = down ? top + 5 : top + bar - 5;
      path.MoveTo({(x - 4.5f) * scale, y0 * scale});
      path.LineTo({(x + 4.5f) * scale, y0 * scale});
      path.LineTo({x * scale, y1 * scale});
      path.Close();
      PlatformPaint paint(0xfff8f8f8);
      paint.SetAntiAlias(true);
      raster.DrawPath(path, paint);
      paint.SetColor(0xff7a7a7a);
      paint.SetStyle(PlatformPaint::Style::kStroke);
      paint.SetStrokeWidth(scale);
      raster.DrawPath(path, paint);
    };
    marker(first, true);
    marker(left + margin, false);
    Fill(raster, {left + margin - 4.5f, top + bar + 1, 9, 4}, 0xfff8f8f8);
    Outline(raster, {left + margin - 4.5f, top + bar + 1, 9, 4}, 0xff7a7a7a);
    raster.Restore();
  }

  void Page(RasterCanvas& raster) {
    const float top = BodyTop(), bottom = height - kStatusHeight, scrollbar = ContentWidth() - kScrollbar;
    Fill(raster, {0, top, ContentWidth(), bottom - top}, kCanvas);
    raster.Save();
    Clip(raster, {0, top, scrollbar, bottom - top});
    const Rect paper{PageX(), PageY(), view.Width() / scale, view.Height() / scale};
    // Opaque layers only, so partial repaints composite identically.
    Fill(raster, {paper.x + 1, paper.y + 2, paper.width + 2, paper.height + 1}, 0xffc6c6c6);
    Fill(raster, {paper.x - 1, paper.y - 1, paper.width + 2, paper.height + 2}, 0xffd2d2d2);
    Fill(raster, paper, kWhite);
    // Word's text-boundary crop marks at the corners of the type area.
    const float m = kPageMargin * zoom, mark = 12 * zoom;
    for (int corner = 0; corner < 4; ++corner) {
      const float cx = corner & 1 ? paper.x + paper.width - m : paper.x + m;
      const float cy = corner & 2 ? paper.y + paper.height - m : paper.y + m;
      Fill(raster, {corner & 1 ? cx : cx - mark, cy, mark, Hair()}, 0xffb8b8b8);
      Fill(raster, {cx, corner & 2 ? cy : cy - mark, Hair(), mark}, 0xffb8b8b8);
    }
    view.Paint(raster, paper.x * scale, paper.y * scale, top * scale, bottom * scale, document.GetSelection(), marks_visible);
    raster.Restore();
    const float track = bottom - top, maximum = MaxScroll();
    Fill(raster, {scrollbar, top, kScrollbar, track}, 0xfff0f0f0);
    if (maximum > 0) {
      const float thumb = ScrollThumb();
      Fill(raster, {scrollbar + 3, top + scroll / maximum * (track - thumb), kScrollbar - 6, thumb}, 0xffc2c2c2, 2);
    }
  }

  void Pane(RasterCanvas& raster) {
    const float x = ContentWidth(), top = kRibbonBottom, bottom = height - kStatusHeight;
    Fill(raster, {x, top, kPaneWidth, bottom - top}, kWhite);
    Fill(raster, {x, top, Hair(), bottom - top}, kLine);
    Label(raster, "格式", x + 18, top + 14, {16, kInk, 600}, 200);
    const Rect close{x + kPaneWidth - 38, top + 12, 26, 26};
    Face(raster, close, false, true);
    PaintIcon(raster, Icon::Close, close.x + 5, close.y + 5, 16, kIcon);
    Hit(close, [this] { pane_visible = false; Layout(); dirty = true; }, "关闭窗格");
    const auto selection = document.GetSelection();
    Label(raster, selection.Empty() ? "插入点 · 文字属性作用于后续输入" : "已选择 " + std::to_string(selection.End() - selection.Start()) + " 个字符",
          x + 18, top + 44, {12, kMuted}, kPaneWidth - 36);
    const float list_top = top + 72, list_bottom = bottom - 4;
    float content = 0;
    for (int s = 0; s < 4; ++s) content += SectionHeight(s);
    const float max_scroll = std::max(0.0f, content - (list_bottom - list_top));
    pane_scroll = std::clamp(pane_scroll, 0.0f, max_scroll);
    // Clip hit areas as well as painting for partially visible rows.
    const auto clip_hits = [&](size_t first) {
      for (size_t i = first; i < buttons.size(); ++i) {
        auto& rect = buttons[i].rect;
        const float t = std::max(rect.y, list_top), b = std::min(rect.y + rect.height, list_bottom);
        rect.y = t;
        rect.height = std::max(0.0f, b - t);
      }
    };
    raster.Save();
    Clip(raster, {x + 1, list_top, kPaneWidth - 2, list_bottom - list_top});
    float y = list_top - pane_scroll;
    for (int s = 0; s < 4; ++s) {
      const auto& specs = SectionSpecs()[s];
      if (y + 30 > list_top && y < list_bottom) {
        const size_t first = buttons.size();
        const Rect header{x + 8, y, kPaneWidth - 24, 30};
        Face(raster, header, false, true);
        PaintIcon(raster, pane_expanded[s] ? Icon::Chevron : Icon::ChevronRight, header.x + 4, header.y + 7, 16, kIcon);
        Label(raster, kSectionNames[s], header.x + 26, header.y + 7, {13, kInk, 600}, 190);
        const std::string count = std::to_string(specs.size()) + " 项";
        Label(raster, count, header.x + header.width - 8 - LabelWidth(count, {11, kMuted}), header.y + 8, {11, kMuted});
        Hit(header, [this, s] { pane_expanded[s] = !pane_expanded[s]; dirty = true; });
        clip_hits(first);
      }
      y += 32;
      if (!pane_expanded[s]) continue;
      for (size_t index : specs) {
        if (y + 34 > list_top && y < list_bottom) {
          const auto& spec = PropertyCatalog()[index];
          Label(raster, spec.label, x + 32, y + 9, {12, kMuted}, 100);
          const size_t first = buttons.size();
          PropertyCombo(raster, spec, {x + 136, y + 4, kPaneWidth - 158, 26});
          clip_hits(first);
        }
        y += 34;
      }
      y += 6;
    }
    raster.Restore();
    if (max_scroll > 0) {
      const float track = list_bottom - list_top, thumb = std::max(24.0f, track * track / (track + max_scroll));
      Fill(raster, {x + kPaneWidth - 8, list_top + pane_scroll / max_scroll * (track - thumb), 4, thumb}, 0xffc2c2c2, 2);
    }
  }

  void StatusBar(RasterCanvas& raster) {
    const float y = height - kStatusHeight;
    Fill(raster, {0, y, width, kStatusHeight}, kBlue);
    RefreshWords();
    const auto selection = document.GetSelection();
    const TextStyle style{12, kWhite};
    float x = 14;
    const auto item = [&](const std::string& text, float max_width = 400) {
      Label(raster, text, x, y + 5, style, max_width);
      x += std::min(LabelWidth(text, style), max_width) + 26;
    };
    item("第 " + std::to_string(document.Tree().LineBreaksBefore(selection.focus) + 1) + " 段，共 " +
         std::to_string(document.Paragraphs().size()) + " 段");
    item(selection.Empty() ? std::to_string(words.total) + " 个字" : std::to_string(words.selected) + "/" + std::to_string(words.total) + " 个字");
    item(LanguageName(Value("-webkit-locale")));
    item(status, std::max(0.0f, width - 290 - x));
    const auto small = [&](Rect rect, std::string_view text, float size, std::function<void()> action, std::string hint) {
      Face(raster, rect, false, true, true);
      CenterLabel(raster, text, rect, {size, kWhite});
      Hit(rect, std::move(action), std::move(hint));
    };
    small({width - 242, y + 3, 22, 20}, "−", 15, [this] { Zoom(zoom - 0.1f); }, "缩小");
    const float track = width - 216;
    Fill(raster, {track, y + 12, 118, 2}, 0xff8fb0e3);
    Fill(raster, {track + 118 / 3.0f, y + 8, 1, 10}, 0xffc9d9f2); // 100%
    Fill(raster, {track + (zoom - 0.5f) / 1.5f * 118 - 3, y + 6, 6, 14}, kWhite, 1);
    Hit({track - 4, y, 126, kStatusHeight}, [this] { zoom_drag = true; ZoomFromSlider(); }, "缩放\n50% — 200%");
    small({width - 94, y + 3, 22, 20}, "+", 15, [this] { Zoom(zoom + 0.1f); }, "放大");
    small({width - 68, y + 2, 58, 22}, std::to_string(static_cast<int>(std::round(zoom * 100))) + "%", 12, [this] { Zoom(1); }, "缩放到 100%");
  }

  void Backstage(RasterCanvas& raster) {
    TitleBar(raster);
    Fill(raster, {0, kTitleHeight, kBackstageSide, height - kTitleHeight}, kBlue);
    Fill(raster, {kBackstageSide, kTitleHeight, width - kBackstageSide, height - kTitleHeight}, kWhite);
    const Rect back{14, kTitleHeight + 16, 40, 40};
    Face(raster, back, false, true, true);
    PaintIcon(raster, Icon::Back, back.x + 6, back.y + 6, 28, kWhite);
    Hit(back, [this] { CloseBackstage(); }, "返回文档 (Esc)");
    float y = kTitleHeight + 80;
    const auto item = [&](const char* label, Icon icon, int page, std::function<void()> action) {
      const Rect rect{0, y, kBackstageSide, 42};
      Face(raster, rect, page >= 0 && backstage_page == page, true, true);
      PaintIcon(raster, icon, 26, y + 13, 16, kWhite);
      Label(raster, label, 58, y + 11, {15, kWhite}, kBackstageSide - 66);
      if (page >= 0)
        Hit(rect, [this, page] { backstage_page = page; dirty = true; });
      else
        Hit(rect, std::move(action));
      y += 42;
    };
    item("信息", Icon::Info, 0, {});
    item("新建", Icon::NewDoc, 1, {});
    item("打开", Icon::Open, -1, [this] { CloseBackstage(); ChooseOpen(); });
    item("保存", Icon::Save, -1, [this] { if (Save(false)) CloseBackstage(); });
    item("另存为", Icon::SaveAs, -1, [this] { if (Save(true)) CloseBackstage(); });
    Fill(raster, {20, y + 8, kBackstageSide - 40, Hair()}, 0xff5b88d0);
    y += 17;
    item("关闭", Icon::Close, -1, [this] { Request(Pending::Close); });
    const float x = kBackstageSide + 44, top = kTitleHeight + 28;
    const auto tile = [&](Rect rect, Icon icon, const char* label, std::function<void()> action, std::string hint) {
      Face(raster, rect, false, true);
      Outline(raster, rect, kLine);
      PaintIcon(raster, icon, rect.x + (rect.width - 32) / 2, rect.y + 20, 32, kIcon);
      CenterLabel(raster, label, {rect.x, rect.y + 64, rect.width, 20}, {13, kInk});
      Hit(rect, std::move(action), std::move(hint));
    };
    if (backstage_page == 0) {
      Label(raster, "信息", x, top, {32, kInk, 300}, 400);
      Label(raster, DocumentName(), x, top + 64, {18, kInk}, width - x - 40);
      Label(raster, path.empty() ? "尚未保存到磁盘" : path, x, top + 92, {12, kMuted}, width - x - 40);
      Fill(raster, {x, top + 124, width - x - 40, Hair()}, kLine);
      tile({x, top + 148, 120, 100}, Icon::Save, "保存", [this] { if (Save(false)) CloseBackstage(); }, "保存 (Ctrl+S)");
      tile({x + 132, top + 148, 120, 100}, Icon::SaveAs, "另存为", [this] { if (Save(true)) CloseBackstage(); }, "另存为 (Ctrl+Shift+S)");
      RefreshWords();
      const auto stats = document.Tree().Stats();
      const float px = x + 320;
      Label(raster, "属性", px, top + 148, {16, kInk, 600});
      const std::pair<const char*, std::string> rows[] = {
          {"段落", std::to_string(document.Paragraphs().size())},
          {"字数", std::to_string(words.total)},
          {"字符 (UTF-16)", std::to_string(view.Text().size())},
          {"片段节点", std::to_string(stats.live_nodes)},
          {"空闲节点", std::to_string(stats.free_nodes)},
          {"缓冲区单元", std::to_string(stats.buffer_units)},
          {"修订版本", std::to_string(document.Revision())},
          {"状态", document.Modified() ? "有未保存的更改" : "已保存"},
      };
      float row = top + 184;
      for (const auto& [name, value] : rows) {
        Label(raster, name, px, row, {13, kMuted}, 120);
        Label(raster, value, px + 130, row, {13, kInk}, std::max(0.0f, width - px - 170));
        row += 28;
      }
    } else {
      Label(raster, "新建", x, top, {32, kInk, 300}, 400);
      const auto thumbnail = [&](Rect rect, bool sample, const char* label, Pending action, std::string hint) {
        Face(raster, rect, false, true);
        const Rect page{rect.x + 12, rect.y + 12, rect.width - 24, 196};
        Fill(raster, page, kWhite);
        Outline(raster, page, kBorder);
        if (sample) {
          Fill(raster, {page.x + 16, page.y + 22, 92, 8}, kBlue);
          Fill(raster, {page.x + 16, page.y + 38, 70, 4}, 0xffb8b8b8);
          for (int i = 0; i < 9; ++i) Fill(raster, {page.x + 16 + (i == 2 ? 10.0f : 0), page.y + 58 + i * 12.0f, i % 4 == 3 ? 72.0f : 118.0f - (i == 2 ? 10 : 0), 3}, 0xffd0d0d0);
        }
        CenterLabel(raster, label, {rect.x, rect.y + 214, rect.width, 22}, {13, kInk});
        Hit(rect, [this, action] { Request(action); }, std::move(hint));
      };
      thumbnail({x, top + 70, 176, 246}, false, "空白文档", Pending::New, "空白文档 (Ctrl+N)");
      thumbnail({x + 196, top + 70, 176, 246}, true, "多语言排版示例", Pending::Sample, "示例文档\n中文、日文竖排、阿拉伯文与 OpenType 特性");
    }
  }

  void Popup(RasterCanvas& raster) {
    if (popup < 0) return;
    const auto box = PopupBox();
    Fill(raster, {box.x + 1, box.y + 3, box.width + 2, box.height + 1}, 0x26000000, 4);
    Fill(raster, {box.x - 1, box.y - 1, box.width + 2, box.height + 2}, 0xffc6c6c6, 3);
    Fill(raster, box, kWhite, 2);
    const auto input = PopupInputBox();
    Fill(raster, input, popup_input_selected ? 0xffcce0f7 : kWhite);
    Outline(raster, input, kBlue);
    const PopupKind kind = Kind();
    const std::string placeholder = kind == PopupKind::Font    ? "搜索 " + std::to_string(installed_font_count) + " 个字体…  ↑↓ 选择"
                                    : kind == PopupKind::Color ? "其他颜色，如 #2b579a"
                                                               : "输入 CSS 值，Enter 应用";
    Label(raster, popup_input.empty() ? placeholder : EncodeUTF8(popup_input) + "|", input.x + 8, input.y + 6,
          {12, popup_input.empty() ? kMuted : kInk}, input.width - 16);
    const size_t index = static_cast<size_t>(popup);
    const auto& current = Value(index);
    const bool mixed = Mixed(index);
    if (kind == PopupKind::Color) {
      const int hovered = ColorHit();
      const auto automatic = ColorAutoRow();
      if (hovered == kColorAuto) Fill(raster, automatic, kMenuHover);
      const Rect swatch{automatic.x + 6, automatic.y + 5, 16, 16};
      Fill(raster, swatch, ParseColor(Default(PropertyCatalog()[index].name)).value_or(kBodyInk));
      Label(raster, "自动（恢复默认）", automatic.x + 30, automatic.y + 5, {12, kInk});
      Label(raster, "主题颜色", box.x + 10, box.y + 38, {11, kMuted, 600});
      Label(raster, "标准色", box.x + 10, box.y + 174, {11, kMuted, 600});
      for (int i = 0; i < 70; ++i) {
        const auto cell = ColorCell(i);
        const ColorARGB color = PaletteColor(i);
        Fill(raster, cell, color);
        if (i < 10 || i >= 60 || i % 10 < 3) Outline(raster, cell, 0xffd4d4d4);
        if (!mixed && ColorText(color) == current) Outline(raster, {cell.x - 2, cell.y - 2, cell.width + 4, cell.height + 4}, kInk);
        if (i == hovered) {
          Outline(raster, {cell.x - 1, cell.y - 1, cell.width + 2, cell.height + 2}, 0xfff29436);
          Outline(raster, {cell.x, cell.y, cell.width, cell.height}, kWhite);
        }
      }
      return;
    }
    const auto list = PopupList();
    const float row = RowHeight();
    const bool font = kind == PopupKind::Font;
    const auto current_name = font ? FoldFontName(FontNameLabel(current)) : std::string();
    const int hovered = PopupHit();
    const size_t count = PopupChoices();
    const size_t first = static_cast<size_t>(popup_scroll / row);
    const size_t last = std::min(count + 1, static_cast<size_t>(std::ceil((popup_scroll + list.height) / row)));
    raster.Save();
    Clip(raster, list);
    // Only visible rows create labels, regardless of the number of fonts.
    for (size_t i = first; i < last; ++i) {
      const bool clear = i == count;
      const std::string value = PopupValue(i);
      const Rect rect{list.x, list.y + i * row - popup_scroll, list.width, row - 2};
      const bool active = !clear && !mixed && (current == value || (font && FontChoiceMatches(i, current, current_name)));
      if (static_cast<int>(i) == hovered || static_cast<int>(i) == popup_active) Fill(raster, rect, kMenuHover, 2);
      if (active) PaintIcon(raster, Icon::Check, rect.x + 6, rect.y + (rect.height - 14) / 2, 14, kBlue);
      if (clear)
        Label(raster, "恢复默认（清除覆盖）", rect.x + 28, rect.y + (rect.height - 15) / 2, {12, kMuted}, rect.width - 34);
      else if (font) {
        // Each family previews itself, as in Word's font list.
        const auto& choice = font_choices[filtered_fonts[i]];
        Label(raster, choice.name, rect.x + 28, rect.y + (rect.height - 17.5f) / 2, {14, kInk, 400, false, choice.value}, rect.width - 34);
      } else
        Label(raster, value, rect.x + 28, rect.y + (rect.height - 15) / 2, {12, active ? kBlue : kInk}, rect.width - 34);
    }
    raster.Restore();
    if (const auto thumb = PopupScrollThumb(); !thumb.Empty()) Fill(raster, thumb, 0xffc2c2c2, 2);
  }

  int DialogHover() const {
    for (size_t i = 0; i < dialog_buttons.size(); ++i)
      if (dialog_buttons[i].rect.Contains(mouse_x, mouse_y)) return static_cast<int>(i);
    return -1;
  }
  void Dialog(RasterCanvas& raster) {
    if (!confirm && !path_dialog) return;
    buttons.clear();
    Fill(raster, {0, 0, width, height}, 0x55000000);
    const float w = 470, h = path_dialog ? 196.0f : 176.0f, x = (width - w) / 2, y = (height - h) / 2;
    Fill(raster, {x - 1, y - 1, w + 2, h + 2}, kBlue);
    Fill(raster, {x, y, w, h}, kWhite);
    Label(raster, "bkfont 文档", x + 16, y + 10, {12, kMuted});
    const auto button = [&](float bx, const char* label, std::function<void()> action, bool primary) {
      const Rect rect{bx, y + h - 46, 96, 30};
      Fill(raster, rect, rect.Contains(mouse_x, mouse_y) ? 0xffd5e4f7 : 0xffe9e9e9);
      Outline(raster, rect, primary ? kBlue : 0xffadadad);
      if (primary) Outline(raster, {rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2}, kBlue);
      CenterLabel(raster, label, rect, {12, kInk});
      buttons.push_back({rect, std::move(action), {}});
    };
    if (path_dialog) {
      Label(raster, path_save ? "保存为 JSON 文档" : "打开 JSON 文档", x + 16, y + 38, {16, kInk, 600}, w - 32);
      const Rect field{x + 16, y + 76, w - 32, 32};
      Fill(raster, field, path_select_all ? 0xffcce0f7 : kWhite);
      Outline(raster, field, kBlue);
      Label(raster, EncodeUTF8(path_input) + "|", field.x + 8, field.y + 8, {13, kInk}, field.width - 16);
      Label(raster, "文件路径 · Enter 确定 · Esc 取消", x + 16, y + 116, {12, kMuted}, w - 32);
      button(x + w - 220, "确定", [this] { FinishPathDialog(); }, true);
      button(x + w - 114, "取消", [this] { path_dialog = false; dirty = true; }, false);
    } else {
      Label(raster, "是否将更改保存到“" + DocumentName() + "”中？", x + 16, y + 44, {15, kInk, 600}, w - 32);
      Label(raster, "如果不保存，所做的更改将丢失。", x + 16, y + 76, {12, kMuted}, w - 32);
      button(x + w - 326, "保存(S)", [this] { if (Save(false)) { confirm = false; ExecutePending(); } }, true);
      button(x + w - 220, "不保存(N)", [this] { confirm = false; ExecutePending(); }, false);
      button(x + w - 114, "取消", [this] { confirm = false; pending = Pending::None; dirty = true; }, false);
    }
  }

  const Button* Hovered() const {
    if (Modal()) return nullptr;
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
      if (it->rect.Contains(mouse_x, mouse_y)) return &*it;
    return nullptr;
  }
  IntRect Pixels(Rect rect) const {
    return IntRect::MakeLTRB(std::clamp(static_cast<int>(std::floor(rect.x * scale)), 0, bitmap.GetPixmap().Width()),
                             std::clamp(static_cast<int>(std::floor(rect.y * scale)), 0, bitmap.GetPixmap().Height()),
                             std::clamp(static_cast<int>(std::ceil((rect.x + rect.width) * scale)), 0, bitmap.GetPixmap().Width()),
                             std::clamp(static_cast<int>(std::ceil((rect.y + rect.height) * scale)), 0, bitmap.GetPixmap().Height()));
  }
  static Pixmap Region(const Pixmap& pixels, IntRect area) {
    return {pixels.GetColorType(), area.Width(), area.Height(), pixels.WritableAddr8(area.left, area.top), pixels.RowBytes()};
  }
  void Damage(Rect rect) {
    if (rect.Empty()) return;
    IntRect area = Pixels(rect);
    if (area.IsEmpty()) return;
    for (size_t i = 0; i < upload_damage.size();) {
      const auto& other = upload_damage[i];
      if (area.left < other.right && other.left < area.right && area.top < other.bottom && other.top < area.bottom) {
        area = IntRect::MakeLTRB(std::min(area.left, other.left), std::min(area.top, other.top),
                                 std::max(area.right, other.right), std::max(area.bottom, other.bottom));
        upload_damage.erase(upload_damage.begin() + i);
        i = 0;
      } else
        ++i;
    }
    upload_damage.push_back(area);
  }
  Rect CaretBounds() {
    if (backstage || !caret_on || !document.GetSelection().Empty() || Modal()) return {};
    const auto caret = view.Caret(document.GetSelection().focus, affinity);
    const float left = PageX() + caret.X().ToFloat() / scale, top = PageY() + caret.Y().ToFloat() / scale;
    const float x = std::max(0.0f, left), y = std::max(BodyTop(), top);
    const float right = std::min(ContentWidth() - kScrollbar, left + std::max(1.3f, caret.Width().ToFloat() / scale));
    const float bottom = std::min(height - kStatusHeight, top + std::max(1.3f, caret.Height().ToFloat() / scale));
    return right > x && bottom > y ? Rect{x, y, right - x, bottom - y} : Rect{};
  }
  // Word-style ScreenTips appear after the pointer rests on a control.
  bool TooltipVisible(const Button* hover) const {
    return window && hover && !hover->hint.empty() && !dragging && !zoom_drag && glfwGetTime() - hover_since >= 0.5;
  }
  Rect OverlayBounds(std::string& key) {
    if (confirm || path_dialog) {
      key = "dialog:" + std::to_string(confirm) + std::to_string(path_dialog) + std::to_string(path_save) +
            std::to_string(path_select_all) + ':' + std::to_string(DialogHover()) + ':' + EncodeUTF8(path_input);
      return {0, 0, width, height};
    }
    if (popup >= 0) {
      const auto box = PopupBox();
      key = "popup:" + std::to_string(popup) + ':' + Value(popup) + ':' + std::to_string(Mixed(popup)) + ':' + std::to_string(PopupHit()) +
            ':' + std::to_string(ColorHit()) + ':' + std::to_string(popup_scroll) + ':' + std::to_string(popup_active) + ':' +
            std::to_string(popup_input_selected) + ':' + EncodeUTF8(popup_input);
      return {box.x - 1, box.y - 1, box.width + 4, box.height + 5};
    }
    if (const auto* hover = Hovered(); TooltipVisible(hover)) {
      const size_t newline = hover->hint.find('\n');
      const std::string_view title = std::string_view(hover->hint).substr(0, newline);
      const std::string_view detail = newline == std::string::npos ? std::string_view() : std::string_view(hover->hint).substr(newline + 1);
      const float tw = std::min(420.0f, std::max(LabelWidth(title, {12, kInk, 600}), LabelWidth(detail, {12, kMuted})) + 24);
      const float th = detail.empty() ? 30.0f : 52.0f;
      float y = hover->rect.y + hover->rect.height + 6;
      if (y + th > height - 4) y = hover->rect.y - th - 6;
      key = hover->hint;
      return {std::clamp(hover->rect.x, 4.0f, width - tw - 8), y, tw + 3, th + 3};
    }
    key.clear();
    return {};
  }
  void Tooltip(RasterCanvas& raster, Rect bounds, const std::string& hint) {
    const Rect box{bounds.x, bounds.y, bounds.width - 3, bounds.height - 3};
    Fill(raster, {box.x + 2, box.y + 2, box.width + 1, box.height + 1}, 0x22000000, 2);
    Fill(raster, box, 0xffbdbdbd);
    Fill(raster, {box.x + 1, box.y + 1, box.width - 2, box.height - 2}, kWhite);
    const size_t newline = hint.find('\n');
    Label(raster, std::string_view(hint).substr(0, newline), box.x + 11, box.y + 7, {12, kInk, 600}, box.width - 20);
    if (newline != std::string::npos) Label(raster, std::string_view(hint).substr(newline + 1), box.x + 11, box.y + 28, {12, kMuted}, box.width - 20);
  }
  Rect LayerRect(int layer) const {
    if (backstage) return layer == kChrome ? Rect{0, 0, width, height} : Rect{};
    switch (layer) {
    case kChrome:
      return {0, 0, width, kRibbonBottom};
    case kRuler:
      return ruler_visible ? Rect{0, kRibbonBottom, ContentWidth(), kRulerHeight} : Rect{};
    case kPage:
      return {0, BodyTop(), ContentWidth(), height - kStatusHeight - BodyTop()};
    case kPane:
      return pane_visible ? Rect{ContentWidth(), kRibbonBottom, kPaneWidth, height - kStatusHeight - kRibbonBottom} : Rect{};
    default:
      return {0, height - kStatusHeight, width, kStatusHeight};
    }
  }
  void PaintLayer(RasterCanvas& raster, int layer) {
    switch (layer) {
    case kChrome:
      Chrome(raster);
      break;
    case kRuler:
      Ruler(raster);
      break;
    case kPage:
      Page(raster);
      break;
    case kPane:
      Pane(raster);
      break;
    default:
      StatusBar(raster);
      break;
    }
  }
  void Render(int pixel_width, int pixel_height, float device_scale) {
    if (device_scale != scale) {
      labels.clear();
      previews.clear();
    }
    scale = device_scale;
    width = pixel_width / scale;
    height = pixel_height / scale;
    if (popup >= 0) popup_scroll = std::clamp(popup_scroll, 0.0f, PopupMaxScroll());
    if (bitmap.GetPixmap().Width() != pixel_width || bitmap.GetPixmap().Height() != pixel_height) {
      scene_canvas.reset();
      bitmap = Bitmap(ColorType::kN32, pixel_width, pixel_height);
      scene_bitmap = Bitmap(ColorType::kN32, pixel_width, pixel_height);
      scene_canvas = std::make_unique<RasterCanvas>(scene_bitmap.GetPixmap(), SurfaceProps(), kCanvas);
      frame_valid = false;
    }
    Layout();
    RefreshFormats();
    upload_damage.clear();
    FrameState next;
    next.revision = document.Revision();
    next.selection = document.GetSelection();
    next.values = formats.values;
    next.mixed = formats.mixed;
    next.tab = tab;
    next.backstage = backstage;
    next.backstage_page = backstage_page;
    next.pane = pane_visible;
    next.ruler = ruler_visible;
    next.marks = marks_visible;
    next.painter = painter.has_value();
    next.expanded = pane_expanded;
    next.scale = scale;
    next.zoom = zoom;
    next.scroll = scroll;
    next.pan = pan;
    next.pane_scroll = pane_scroll;
    next.modified = document.Modified();
    next.undo = document.CanUndo();
    next.redo = document.CanRedo();
    next.path = path;
    next.status = status;
    next.font_color = font_color;
    for (int i = 0; i < kLayerCount; ++i) next.layers[i] = LayerRect(i);
    next.caret = CaretBounds();
    if (const auto* hover = Hovered()) next.hover = hover->rect;
    // Each layer repaints only when state it displays has changed. Moving the
    // caret through uniformly formatted text repaints nothing but the caret.
    const bool all = !frame_valid || frame.scale != scale || frame.backstage != backstage;
    const bool revised = frame.revision != next.revision;
    const bool formatted = frame.values != next.values || frame.mixed != next.mixed;
    const bool moved = frame.selection != next.selection;
    const bool emptiness = frame.selection.Empty() != next.selection.Empty();
    const bool highlighted = moved && (!frame.selection.Empty() || !next.selection.Empty());
    const bool view_changed = frame.zoom != zoom || frame.scroll != scroll || frame.pan != pan;
    std::array<bool, kLayerCount> need{};
    need[kChrome] = formatted || emptiness || frame.tab != tab || frame.backstage_page != backstage_page || frame.pane != pane_visible ||
                    frame.ruler != ruler_visible || frame.marks != marks_visible || frame.painter != next.painter ||
                    frame.path != path || frame.modified != next.modified || frame.undo != next.undo || frame.redo != next.redo ||
                    frame.font_color != font_color || frame.zoom != zoom || (backstage && revised);
    need[kRuler] = formatted || frame.zoom != zoom || frame.pan != pan;
    need[kPage] = revised || highlighted || view_changed || frame.marks != marks_visible;
    need[kPane] = formatted || emptiness || (highlighted && !next.selection.Empty()) || frame.expanded != pane_expanded ||
                  frame.pane_scroll != pane_scroll;
    need[kStatus] = revised || moved || formatted || frame.zoom != zoom || frame.status != status;
    std::array<Rect, kLayerCount> repaint{};
    for (int i = 0; i < kLayerCount; ++i) {
      if (next.layers[i].Empty())
        scene_buttons[i].clear();
      else if (all || need[i] || next.layers[i] != frame.layers[i])
        repaint[i] = next.layers[i];
    }
    if (frame.hover != next.hover) {
      for (Rect hover : {frame.hover, next.hover})
        for (int i = 0; i < kLayerCount; ++i)
          if (!hover.Empty() && !next.layers[i].Empty()) repaint[i] = Union(repaint[i], Intersect(hover, next.layers[i]));
    }
    for (int i = 0; i < kLayerCount; ++i) {
      const IntRect area = Pixels(repaint[i]);
      if (repaint[i].Empty() || area.IsEmpty()) continue;
      buttons.clear();
      // Retain float storage: allocating/widening the framebuffer for
      // every interaction otherwise dominates high-DPI rendering.
      auto& raster = *scene_canvas;
      raster.Save();
      raster.ClipRect(ScalarRect::MakeLTRB(static_cast<float>(area.left), static_cast<float>(area.top),
                                           static_cast<float>(area.right), static_cast<float>(area.bottom)),
                      false);
      raster.Clear(kCanvas);
      PaintLayer(raster, i);
      raster.Restore();
      raster.Flush(area);
      scene_buttons[i] = std::move(buttons);
      Damage(repaint[i]);
    }
    buttons.clear();
    for (const auto& group : scene_buttons) buttons.insert(buttons.end(), group.begin(), group.end());
    next.hover = {};
    if (const auto* hover = Hovered()) next.hover = hover->rect;
    next.pane_scroll = pane_scroll; // The pane clamps it to its content.
    next.overlay = OverlayBounds(next.overlay_key);
    if (frame.caret != next.caret) {
      Damage(frame.caret);
      Damage(next.caret);
    }
    if (frame.overlay != next.overlay || frame.overlay_key != next.overlay_key) {
      Damage(frame.overlay);
      Damage(next.overlay);
    }
    const auto affected = [&](Rect rect) {
      if (rect.Empty()) return false;
      const auto other = Pixels(rect);
      return std::any_of(upload_damage.begin(), upload_damage.end(), [&](const IntRect& area) {
        return area.left < other.right && other.left < area.right && area.top < other.bottom && other.top < area.bottom;
      });
    };
    // Paint translucent overlays once, with a stable device-space origin.
    // Splitting a rounded path across damage rectangles changes its floating
    // point flattening and can also composite shared edge pixels twice.
    const bool paint_overlay = affected(next.overlay);
    if (paint_overlay) Damage(next.overlay);
    const bool paint_caret = affected(next.caret);
    if (paint_caret) Damage(next.caret);
    for (const IntRect area : upload_damage) {
      const auto& src = scene_bitmap.GetPixmap();
      const auto& dst = bitmap.GetPixmap();
      for (int y = area.top; y < area.bottom; ++y)
        std::memcpy(dst.WritableAddr8(area.left, y), src.WritableAddr8(area.left, y), static_cast<size_t>(area.Width()) * 4);
    }
    if (paint_caret) {
      const auto caret_area = Pixels(next.caret);
      const auto& dst = bitmap.GetPixmap();
      RasterCanvas raster(Region(dst, caret_area), SurfaceProps(), std::nullopt, caret_area.left, caret_area.top);
      Fill(raster, next.caret, 0xff000000);
    }
    if (paint_overlay) {
      const auto overlay_area = Pixels(next.overlay);
      const auto& dst = bitmap.GetPixmap();
      RasterCanvas raster(Region(dst, overlay_area), SurfaceProps(), std::nullopt, overlay_area.left, overlay_area.top);
      if (!Modal()) Tooltip(raster, next.overlay, next.overlay_key);
      const auto scene = buttons;
      Popup(raster);
      Dialog(raster);
      if (confirm || path_dialog)
        dialog_buttons = buttons;
      else
        buttons = scene;
    }
    if (confirm || path_dialog) buttons = dialog_buttons;
    frame = std::move(next);
    frame_valid = true;
  }
  // True when a tooltip has become due (or expired) since the last frame.
  bool OverlayChanged() {
    std::string key;
    OverlayBounds(key);
    return key != frame.overlay_key;
  }

  // Platform/file operations and event handling are below the drawing code.
  void Request(Pending action, std::string file = {});
  void ExecutePending();
  std::string ChoosePath(bool save);
  void ChooseOpen();
  bool Save(bool save_as);
  void FinishPathDialog();
  void Key(int key, int modifiers);
  void Character(uint32_t codepoint);
  void Mouse(bool down, int modifiers);
  void Motion(float x, float y);
  void Wheel(double dx, double dy, bool control, bool shift);
  void UpdateIme();
};

void Editor::Request(Pending action, std::string file) {
  popup = -1;
  backstage = false;
  pending = action;
  pending_path = std::move(file);
  if (document.Modified())
    confirm = true;
  else
    ExecutePending();
  dirty = true;
}

void Editor::ExecutePending() {
  const Pending action = pending;
  pending = Pending::None;
  confirm = false;
  if (action == Pending::Close) {
    if (window) glfwSetWindowShouldClose(window, GLFW_TRUE);
    return;
  }
  if (action == Pending::New || action == Pending::Sample) {
    document.New(action == Pending::Sample);
    path.clear();
    status = action == Pending::Sample ? "已载入示例文档" : "新文档";
  } else if (action == Pending::Open) {
    std::string error;
    if (!document.OpenFile(pending_path, error)) {
      status = error;
      dirty = true;
      return;
    }
    path = pending_path;
    status = "已打开 " + Filename(path);
  } else
    return;
  scroll = pan = 0;
  painter.reset();
  affinity = TextAffinity::kDownstream;
  Layout(true);
  Touch();
}

std::string Editor::ChoosePath(bool save) {
#ifdef _WIN32
  if (!window) return {};
  wchar_t filename[32768]{};
  std::u16string initial;
  DecodeUTF8(path.empty() ? "document.json" : path, initial);
  std::copy_n(initial.c_str(), std::min(initial.size(), std::size(filename) - 1), filename);
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = glfwGetWin32Window(window);
  dialog.lpstrFilter = L"bkfont JSON document (*.json)\0*.json\0All files (*.*)\0*.*\0\0";
  dialog.lpstrFile = filename;
  dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
  dialog.lpstrDefExt = L"json";
  dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))
    return EncodeUTF8(std::u16string_view(reinterpret_cast<const char16_t*>(filename)));
  if (CommDlgExtendedError()) status = "无法打开文件对话框";
  dirty = true;
  return {};
#else
  path_dialog = true;
  path_save = save;
  DecodeUTF8(path.empty() ? "document.json" : path, path_input);
  path_select_all = true;
  dirty = true;
  return {};
#endif
}
void Editor::ChooseOpen() {
  const auto file = ChoosePath(false);
  if (!file.empty()) Request(Pending::Open, file);
}
bool Editor::Save(bool save_as) {
  const auto file = save_as || path.empty() ? ChoosePath(true) : path;
  if (file.empty()) return false;
  std::string error;
  const bool ok = document.SaveFile(file, error);
  if (ok) {
    path = file;
    status = "已保存 " + Filename(file);
  } else
    status = error;
  dirty = true;
  return ok;
}
void Editor::FinishPathDialog() {
  const auto file = EncodeUTF8(path_input);
  if (file.empty()) return;
  if (path_save) {
    std::string error;
    if (!document.SaveFile(file, error)) {
      status = error;
      dirty = true;
      return;
    }
    path = file;
    status = "已保存 " + Filename(file);
    path_dialog = false;
    if (confirm) ExecutePending();
  } else {
    path_dialog = false;
    Request(Pending::Open, file);
  }
  dirty = true;
}

void Editor::Character(uint32_t codepoint) {
  if ((confirm && !path_dialog) || (backstage && !path_dialog)) return;
  if (codepoint < 0x20 || codepoint == 0x7f || codepoint > 0x10ffff ||
      (codepoint >= 0xd800 && codepoint <= 0xdfff)) return;
  std::u16string text;
  if (codepoint <= 0xffff)
    text.push_back(static_cast<char16_t>(codepoint));
  else {
    codepoint -= 0x10000;
    text.push_back(static_cast<char16_t>(0xd800 + (codepoint >> 10)));
    text.push_back(static_cast<char16_t>(0xdc00 + (codepoint & 1023)));
  }
  if (path_dialog) {
    if (path_select_all) path_input.clear();
    path_select_all = false;
    path_input += text;
    dirty = true;
  } else if (popup >= 0)
    AppendPopupInput(text); // Dropdowns are editable combo boxes.
  else
    Insert(text);
}

void Editor::Key(int key, int modifiers) {
  const bool control = (modifiers & GLFW_MOD_CONTROL) != 0;
  const bool shift = (modifiers & GLFW_MOD_SHIFT) != 0;
  if (composing) return;
  if (path_dialog) {
    if (key == GLFW_KEY_ESCAPE)
      path_dialog = false;
    else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)
      FinishPathDialog();
    else if (control && key == GLFW_KEY_A)
      path_select_all = true;
    else if (control && key == GLFW_KEY_V && window) {
      const char* content = glfwGetClipboardString(window);
      std::u16string value;
      if (content && DecodeUTF8(content, value)) {
        if (path_select_all) path_input.clear();
        path_input += value;
        path_select_all = false;
      }
    } else if (key == GLFW_KEY_BACKSPACE) {
      if (path_select_all)
        path_input.clear();
      else if (!path_input.empty()) {
        const char16_t last = path_input.back();
        path_input.pop_back();
        if (last >= 0xdc00 && last <= 0xdfff && !path_input.empty()) path_input.pop_back();
      }
      path_select_all = false;
    }
    dirty = true;
    return;
  }
  if (confirm) {
    if (key == GLFW_KEY_ESCAPE) {
      confirm = false;
      pending = Pending::None;
      dirty = true;
    } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_S) {
      if (Save(false)) ExecutePending();
    } else if (key == GLFW_KEY_N)
      ExecutePending();
    return;
  }
  if (backstage) {
    if (key == GLFW_KEY_ESCAPE)
      CloseBackstage();
    else if (control && key == GLFW_KEY_N)
      Request(Pending::New);
    else if (control && key == GLFW_KEY_O) {
      CloseBackstage();
      ChooseOpen();
    } else if (control && key == GLFW_KEY_S && Save(shift))
      CloseBackstage();
    return;
  }
  if (key == GLFW_KEY_ESCAPE) {
    if (popup < 0 && painter) {
      painter.reset();
      status = "就绪";
    }
    popup = -1;
    popup_scrollbar_drag = false;
    dragging = false;
    dirty = true;
    return;
  }
  if (popup >= 0) {
    if (control && key == GLFW_KEY_A) {
      popup_input_selected = !popup_input.empty();
      dirty = true;
      return;
    }
    if (control && key == GLFW_KEY_V && window) {
      std::u16string text;
      const char* content = glfwGetClipboardString(window);
      if (content && DecodeUTF8(content, text)) AppendPopupInput(text);
      return;
    }
    if (key == GLFW_KEY_BACKSPACE || key == GLFW_KEY_DELETE) {
      if (popup_input_selected || control)
        popup_input.clear();
      else if (key == GLFW_KEY_BACKSPACE && !popup_input.empty()) {
        const char16_t last = popup_input.back();
        popup_input.pop_back();
        if (last >= 0xdc00 && last <= 0xdfff && !popup_input.empty()) popup_input.pop_back();
      }
      popup_input_selected = false;
      if (Kind() == PopupKind::Font) FilterFonts();
      dirty = true;
      return;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
      ApplyPopupInput();
      return;
    }
    if (Kind() == PopupKind::Color) return;
    const int last = static_cast<int>(PopupChoices());
    const int page = std::max(1, static_cast<int>(PopupList().height / RowHeight()));
    if (key == GLFW_KEY_UP)
      popup_active = popup_active < 0 ? last : std::max(0, popup_active - 1);
    else if (key == GLFW_KEY_DOWN)
      popup_active = std::min(last, popup_active + 1);
    else if (key == GLFW_KEY_HOME && popup_input.empty())
      popup_active = 0;
    else if (key == GLFW_KEY_END && popup_input.empty())
      popup_active = last;
    else if (key == GLFW_KEY_PAGE_UP)
      popup_active = std::max(0, popup_active - page);
    else if (key == GLFW_KEY_PAGE_DOWN)
      popup_active = std::min(last, popup_active + page);
    else
      return;
    RevealPopupChoice();
    return;
  }
  if (control) {
    switch (key) {
    case GLFW_KEY_N:
      Request(Pending::New);
      return;
    case GLFW_KEY_O:
      ChooseOpen();
      return;
    case GLFW_KEY_S:
      Save(shift);
      return;
    case GLFW_KEY_A:
      SelectAll();
      return;
    case GLFW_KEY_C:
      if (shift)
        CopyFormat();
      else
        Copy(false);
      return;
    case GLFW_KEY_X:
      Copy(true);
      return;
    case GLFW_KEY_V:
      if (shift)
        PasteFormat();
      else
        Paste();
      return;
    case GLFW_KEY_Z:
      Undo(shift);
      return;
    case GLFW_KEY_Y:
      Undo(true);
      return;
    case GLFW_KEY_B:
      Toggle("font-weight", "700", "400");
      return;
    case GLFW_KEY_I:
      Toggle("font-style", "italic", "normal");
      return;
    case GLFW_KEY_U:
      ToggleDecoration("underline");
      return;
    case GLFW_KEY_L:
      Format("text-align", "left");
      return;
    case GLFW_KEY_E:
      Format("text-align", "center");
      return;
    case GLFW_KEY_R:
      Format("text-align", "right");
      return;
    case GLFW_KEY_J:
      Format("text-align", "justify");
      return;
    case GLFW_KEY_D:
      OpenPaneSection(0);
      return;
    case GLFW_KEY_8:
      if (shift) {
        marks_visible = !marks_visible;
        dirty = true;
      }
      return;
    case GLFW_KEY_PERIOD:
    case GLFW_KEY_COMMA:
      if (shift) StepFontSize(key == GLFW_KEY_PERIOD ? 1 : -1);
      return;
    case GLFW_KEY_RIGHT_BRACKET:
    case GLFW_KEY_LEFT_BRACKET:
      NudgeFontSize(key == GLFW_KEY_RIGHT_BRACKET ? 1 : -1);
      return;
    case GLFW_KEY_EQUAL:
      if (shift)
        Toggle("vertical-align", "super", "baseline");
      else
        Zoom(zoom + 0.1f);
      return;
    case GLFW_KEY_MINUS:
      Zoom(zoom - 0.1f);
      return;
    case GLFW_KEY_0:
      Zoom(1);
      return;
    default:
      break;
    }
  }
  if (modifiers & GLFW_MOD_ALT) return;
  auto selection = document.GetSelection();
  const auto length = document.Tree().Length();
  switch (key) {
  case GLFW_KEY_BACKSPACE:
  case GLFW_KEY_DELETE: {
    if (selection.Empty()) {
      const auto other = view.Move(selection.focus, key == GLFW_KEY_BACKSPACE ? -1 : 1, control);
      document.Select(selection.focus, other);
    }
    Insert({});
    return;
  }
  case GLFW_KEY_ENTER:
  case GLFW_KEY_KP_ENTER:
    Insert(u"\n");
    return;
  case GLFW_KEY_TAB:
    Insert(u"\t");
    return;
  default:
    break;
  }
  uint32_t position = selection.focus;
  bool moved = true, vertical = false;
  switch (key) {
  case GLFW_KEY_LEFT:
  case GLFW_KEY_RIGHT: {
    const bool right = key == GLFW_KEY_RIGHT;
    if (!shift && !selection.Empty())
      position = right ? selection.End() : selection.Start();
    else
      position = view.Move(position, right ? 1 : -1, control);
    affinity = right ? TextAffinity::kDownstream : TextAffinity::kUpstream;
    break;
  }
  case GLFW_KEY_HOME:
  case GLFW_KEY_END: {
    const bool end = key == GLFW_KEY_END;
    if (control)
      position = end ? length : 0;
    else {
      const auto& block = view.BlockAt(position);
      const auto rect = view.Caret(position, affinity);
      position = block.vertical ? view.Hit(rect.X().ToFloat() + rect.Width().ToFloat() / 2,
                                           block.y + (end ? block.height + 100 : -100), &affinity)
                                : view.Hit(end ? 100000 : -100000, rect.Y().ToFloat() + rect.Height().ToFloat() / 2, &affinity);
    }
    break;
  }
  case GLFW_KEY_UP:
  case GLFW_KEY_DOWN:
  case GLFW_KEY_PAGE_UP:
  case GLFW_KEY_PAGE_DOWN: {
    const bool down = key == GLFW_KEY_DOWN || key == GLFW_KEY_PAGE_DOWN;
    const auto rect = view.Caret(position, affinity);
    const auto& block = view.BlockAt(position);
    if (desired_x < 0) desired_x = rect.X().ToFloat();
    float y = down ? rect.Bottom().ToFloat() + 3 * scale : rect.Y().ToFloat() - 3 * scale;
    if (key == GLFW_KEY_PAGE_UP || key == GLFW_KEY_PAGE_DOWN)
      y = rect.Y().ToFloat() + (down ? 1 : -1) * ViewportHeight() * scale * 0.85f;
    position = view.Hit(desired_x, y, &affinity);
    // A gap between paragraphs can map back to the same line. Jump across
    // that gap so Up/Down still makes progress at a paragraph boundary.
    if (position == selection.focus) {
      if (down && block.end < length)
        position = block.end + 1;
      else if (!down && block.start)
        position = block.start - 1;
    }
    vertical = true;
    break;
  }
  default:
    moved = false;
  }
  if (moved) {
    const float preferred = desired_x;
    document.Select(shift ? selection.anchor : position, position);
    Touch();
    if (vertical) desired_x = preferred;
  }
}

void Editor::Mouse(bool down, int modifiers) {
  if (!down) {
    // The format painter applies once the drag that selects its target ends.
    if (dragging && painter && !document.GetSelection().Empty()) {
      PasteFormat();
      painter.reset();
    }
    dragging = scrollbar_drag = popup_scrollbar_drag = zoom_drag = false;
    dirty = true;
    return;
  }
  hover_since = window ? glfwGetTime() : 0; // A click dismisses the ScreenTip.
  dirty = true;
  if (popup >= 0) {
    if (PopupInputBox().Contains(mouse_x, mouse_y)) {
      popup_input_selected = !popup_input.empty();
      UpdateIme();
      return;
    }
    if (Kind() == PopupKind::Color) {
      if (const int hit = ColorHit(); hit >= 0) {
        ApplyPopupValue(hit == kColorAuto ? std::string() : ColorText(PaletteColor(hit)));
        return;
      }
    } else {
      const auto list = PopupList(), thumb = PopupScrollThumb();
      if (!thumb.Empty() && Rect{list.x + list.width, list.y, 12, list.height}.Contains(mouse_x, mouse_y)) {
        popup_scrollbar_grab = thumb.Contains(mouse_x, mouse_y) ? mouse_y - thumb.y : thumb.height / 2;
        popup_scrollbar_drag = true;
        Motion(mouse_x, mouse_y);
        return;
      }
      if (const int index = PopupHit(); index >= 0) {
        ApplyPopupValue(PopupValue(index));
        return;
      }
    }
    if (!PopupBox().Contains(mouse_x, mouse_y)) popup = -1;
    return;
  }
  for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
    if (it->rect.Contains(mouse_x, mouse_y)) {
      const auto action = it->action;
      if (action) action();
      return;
    }
  if (confirm || path_dialog || backstage) return;
  if (mouse_x >= ContentWidth() - kScrollbar && mouse_x < ContentWidth() && mouse_y >= BodyTop() && mouse_y < height - kStatusHeight &&
      MaxScroll() > 0) {
    const float thumb = ScrollThumb();
    const float top = BodyTop() + scroll / MaxScroll() * (ViewportHeight() - thumb);
    scrollbar_grab = mouse_y >= top && mouse_y <= top + thumb ? mouse_y - top : thumb / 2;
    scrollbar_drag = true;
    Motion(mouse_x, mouse_y);
    return;
  }
  if (mouse_x < ContentWidth() - kScrollbar && mouse_y >= BodyTop() && mouse_y < height - kStatusHeight) {
    const uint32_t position = view.Hit((mouse_x - PageX()) * scale, (mouse_y - PageY()) * scale, &affinity);
    // Double-click selects a word, triple-click a paragraph.
    const double now = window ? glfwGetTime() : 0;
    const bool repeat = now - last_click < 0.45 && std::abs(mouse_x - click_x) < 4 && std::abs(mouse_y - click_y) < 4;
    click_count = repeat ? click_count % 3 + 1 : 1;
    last_click = now;
    click_x = mouse_x;
    click_y = mouse_y;
    if (click_count == 2) {
      const auto [start, end] = view.WordAt(position);
      document.Select(start, end);
    } else if (click_count == 3) {
      const auto& block = view.BlockAt(position);
      document.Select(block.start, block.end);
    } else
      document.Select(modifiers & GLFW_MOD_SHIFT ? document.GetSelection().anchor : position, position);
    dragging = true;
    Touch(false);
  }
}
void Editor::Motion(float x, float y) {
  const auto* before = Hovered();
  const Rect old_hover = before ? before->rect : Rect{};
  std::string old_overlay_key;
  const Rect old_overlay = OverlayBounds(old_overlay_key);
  mouse_x = x;
  mouse_y = y;
  if (popup >= 0) {
    if (popup_scrollbar_drag) {
      const auto list = PopupList(), thumb = PopupScrollThumb();
      if (list.height > thumb.height)
        popup_scroll = std::clamp((y - list.y - popup_scrollbar_grab) / (list.height - thumb.height) * PopupMaxScroll(), 0.0f, PopupMaxScroll());
    } else if (const int hovered = PopupHit(); hovered >= 0)
      popup_active = hovered;
  } else if (zoom_drag)
    ZoomFromSlider();
  else if (scrollbar_drag) {
    const float thumb = ScrollThumb();
    scroll = std::clamp((y - BodyTop() - scrollbar_grab) / (ViewportHeight() - thumb) * MaxScroll(), 0.0f, MaxScroll());
  } else if (dragging && click_count == 1) {
    if (y < BodyTop() + 10) scroll -= 18;
    if (y > height - kStatusHeight - 10) scroll += 18;
    scroll = std::clamp(scroll, 0.0f, MaxScroll());
    const uint32_t position = view.Hit((x - PageX()) * scale, (y - PageY()) * scale, &affinity);
    document.Select(document.GetSelection().anchor, position);
    caret_on = true;
  }
  const auto* after = Hovered();
  const Rect new_hover = after ? after->rect : Rect{};
  if (old_hover != new_hover) hover_since = window ? glfwGetTime() : 0;
  std::string new_overlay_key;
  const Rect new_overlay = OverlayBounds(new_overlay_key);
  if (dragging || scrollbar_drag || zoom_drag || old_hover != new_hover || old_overlay != new_overlay || old_overlay_key != new_overlay_key)
    dirty = true;
}
void Editor::Wheel(double dx, double dy, bool control, bool shift) {
  if (confirm || path_dialog) return;
  if (popup >= 0 && !control) {
    if (Kind() != PopupKind::Color) {
      popup_scroll = std::clamp(popup_scroll - static_cast<float>(dy) * 90, 0.0f, PopupMaxScroll());
      popup_active = -1;
      dirty = true;
    }
    return;
  }
  popup = -1;
  popup_scrollbar_drag = false;
  if (backstage) return;
  if (control)
    Zoom(zoom + static_cast<float>(dy) * 0.05f);
  else if (pane_visible && mouse_x >= ContentWidth() && mouse_y >= kRibbonBottom)
    pane_scroll -= static_cast<float>(dy) * 48;
  else if (mouse_y < kRibbonBottom)
    return;
  else if (shift || dx)
    pan = std::clamp(pan - static_cast<float>(shift ? dy : dx) * 48, 0.0f, MaxPan());
  else
    scroll = std::clamp(scroll - static_cast<float>(dy) * 56, 0.0f, MaxScroll());
  dirty = true;
}

void Editor::UpdateIme() {
#ifdef _WIN32
  if (!window) return;
  const HWND handle = glfwGetWin32Window(window);
  const HIMC context = ImmGetContext(handle);
  if (!context) return;
  const auto rect = view.Caret(document.GetSelection().focus, affinity);
  int window_width, window_height;
  glfwGetWindowSize(window, &window_width, &window_height);
  const float to_window = window_width / width;
  const auto input = popup >= 0 ? PopupInputBox() : Rect{};
  const LONG x = static_cast<LONG>((popup >= 0 ? input.x + 10 : PageX() + rect.X().ToFloat() / scale) * to_window);
  const LONG y = static_cast<LONG>((popup >= 0 ? input.y + input.height : PageY() + rect.Bottom().ToFloat() / scale) * to_window);
  COMPOSITIONFORM composition{};
  composition.dwStyle = CFS_POINT;
  composition.ptCurrentPos = {x, y};
  ImmSetCompositionWindow(context, &composition);
  CANDIDATEFORM candidate{};
  candidate.dwStyle = CFS_CANDIDATEPOS;
  candidate.ptCurrentPos = {x, y + 4};
  ImmSetCandidateWindow(context, &candidate);
  ImmReleaseContext(handle, context);
#endif
}

Editor& Get(GLFWwindow* window) {
  return *static_cast<Editor*>(glfwGetWindowUserPointer(window));
}
#ifdef _WIN32
LRESULT CALLBACK ImeWindowProc(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* editor = reinterpret_cast<Editor*>(GetPropW(handle, L"bkfont.RichTextEditor"));
  if (!editor) return DefWindowProcW(handle, message, wparam, lparam);
  if (message == WM_IME_STARTCOMPOSITION) {
    editor->composing = true;
    editor->UpdateIme();
  }
  if (message == WM_IME_ENDCOMPOSITION) editor->composing = false;
  if (message == WM_KILLFOCUS) editor->composing = false;
  return CallWindowProcW(editor->previous_proc, handle, message, wparam, lparam);
}
#endif

bool WriteBitmap(const Pixmap& pixels, const std::string& path) {
#ifdef _WIN32
  std::u16string utf16;
  if (!DecodeUTF8(path, utf16)) return false;
  FILE* file = _wfopen(reinterpret_cast<const wchar_t*>(utf16.c_str()), L"wb");
#else
  FILE* file = std::fopen(path.c_str(), "wb");
#endif
  if (!file) return false;
  const uint32_t size = 54 + pixels.Width() * pixels.Height() * 4;
  const uint32_t header[] = {40, static_cast<uint32_t>(pixels.Width()), static_cast<uint32_t>(-pixels.Height()),
                             0x00200001, 0, size - 54, 0, 0, 0, 0};
  const uint32_t offset = 54, reserved = 0;
  std::fwrite("BM", 1, 2, file);
  std::fwrite(&size, 4, 1, file);
  std::fwrite(&reserved, 4, 1, file);
  std::fwrite(&offset, 4, 1, file);
  std::fwrite(header, 4, 10, file);
  for (int y = 0; y < pixels.Height(); ++y) std::fwrite(pixels.WritableAddr8(0, y), 4, pixels.Width(), file);
  const bool written = std::ferror(file) == 0;
  return std::fclose(file) == 0 && written;
}

void Present(const Pixmap& pixels, GLuint texture, std::span<const IntRect> damage = {}, bool allocate = true) {
  glViewport(0, 0, pixels.Width(), pixels.Height());
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  if (allocate) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels.Width(), pixels.Height(), 0, 0x80e1, GL_UNSIGNED_BYTE, pixels.Addr());
  } else if (!damage.empty()) {
    // Keep storage until resize. Upload rows directly from the retained image.
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pixels.Width());
    for (const auto& area : damage)
      glTexSubImage2D(GL_TEXTURE_2D, 0, area.left, area.top, area.Width(), area.Height(), 0x80e1, GL_UNSIGNED_BYTE,
                      pixels.WritableAddr8(area.left, area.top));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  }
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glBegin(GL_TRIANGLE_STRIP);
  glTexCoord2f(0, 1);
  glVertex2f(-1, -1);
  glTexCoord2f(1, 1);
  glVertex2f(1, -1);
  glTexCoord2f(0, 0);
  glVertex2f(-1, 1);
  glTexCoord2f(1, 0);
  glVertex2f(1, 1);
  glEnd();
}

} // namespace
} // namespace rich_text

int RunEditor(int argc, char** argv) {
  using namespace rich_text;
  std::string file, snapshot, sample_path, popup_property;
  int selected_tab = 0;
  bool backstage = false, marks = false;
  float snapshot_scale = 1;
  const auto usage = [] {
    std::puts("bkfont_rich_text_example [--file document.json] [--snapshot view.bmp]\n"
              "  [--tab 0|1|2|3|4] [--backstage] [--popup css-property] [--marks]\n"
              "  [--scale 1|1.5|2] [--write-sample sample.json]\n"
              "Ctrl+N/O/S, Ctrl+Shift+S, Ctrl+Z/Y, Ctrl+A/C/X/V, Ctrl+B/I/U, Ctrl+L/E/R/J.\n"
              "Ctrl+Shift+C/V: copy/paste formatting. Ctrl+Shift+</>: font size.\n"
              "Ctrl+wheel: zoom. Shift+wheel: pan. 格式 pane: CSS properties.");
  };
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--file" && i + 1 < argc)
      file = argv[++i];
    else if (arg == "--snapshot" && i + 1 < argc)
      snapshot = argv[++i];
    else if (arg == "--write-sample" && i + 1 < argc)
      sample_path = argv[++i];
    else if (arg == "--popup" && i + 1 < argc) {
      popup_property = argv[++i];
      if (!FindProperty(popup_property)) {
        usage();
        return 2;
      }
    } else if (arg == "--backstage")
      backstage = true;
    else if (arg == "--marks")
      marks = true;
    else if (arg == "--tab" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value.size() != 1 || value[0] < '0' || value[0] > '4') {
        usage();
        return 2;
      }
      selected_tab = value[0] - '0';
    } else if (arg == "--scale" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value == "1")
        snapshot_scale = 1;
      else if (value == "1.5")
        snapshot_scale = 1.5f;
      else if (value == "2")
        snapshot_scale = 2;
      else {
        usage();
        return 2;
      }
    } else if (arg == "--help") {
      usage();
      return 0;
    } else {
      usage();
      return 2;
    }
  }
  bkfont::InitializeFonts();
  Editor editor;
  editor.tab = selected_tab;
  editor.backstage = backstage;
  editor.marks_visible = marks;
  if (!file.empty()) {
    std::string error;
    if (!editor.document.OpenFile(file, error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    editor.path = file;
  }
  if (!sample_path.empty()) {
    std::string error;
    if (!editor.document.SaveFile(sample_path, error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    return 0;
  }
  if (!snapshot.empty()) {
    const int pixel_width = static_cast<int>(1440 * snapshot_scale), pixel_height = static_cast<int>(980 * snapshot_scale);
    if (!popup_property.empty()) {
      // Lay out once so the dropdown can anchor to its ribbon control.
      editor.Render(pixel_width, pixel_height, snapshot_scale);
      editor.OpenPopup(IndexOf(popup_property), {150, 79, 150, 25});
    }
    editor.Render(pixel_width, pixel_height, snapshot_scale);
    return WriteBitmap(editor.bitmap.GetPixmap(), snapshot) ? 0 : 1;
  }
  glfwSetErrorCallback([](int code, const char* error) { std::fprintf(stderr, "GLFW %d: %s\n", code, error); });
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  editor.window = glfwCreateWindow(1440, 980, "bkfont 文档", nullptr, nullptr);
  if (!editor.window) {
    glfwTerminate();
    return 1;
  }
  GLFWwindow* window = editor.window;
  glfwSetWindowSizeLimits(window, 1120, 760, GLFW_DONT_CARE, GLFW_DONT_CARE);
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  glfwSetWindowUserPointer(window, &editor);
  glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int modifiers) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) Get(w).Key(key, modifiers);
  });
  glfwSetCharCallback(window, [](GLFWwindow* w, unsigned codepoint) { Get(w).Character(codepoint); });
  glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int modifiers) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) Get(w).Mouse(action == GLFW_PRESS, modifiers);
  });
  glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
    int ww, wh;
    glfwGetWindowSize(w, &ww, &wh);
    if (ww && wh) Get(w).Motion(static_cast<float>(x) * Get(w).width / ww, static_cast<float>(y) * Get(w).height / wh);
  });
  glfwSetScrollCallback(window, [](GLFWwindow* w, double x, double y) {
    const bool control = glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    const bool shift = glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    Get(w).Wheel(x, y, control, shift);
  });
  glfwSetWindowCloseCallback(window, [](GLFWwindow* w) { glfwSetWindowShouldClose(w, GLFW_FALSE); Get(w).Request(Editor::Pending::Close); });
  glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) { if (count) Get(w).Request(Editor::Pending::Open, paths[0]); });
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) { Get(w).dirty = true; });
  glfwSetWindowContentScaleCallback(window, [](GLFWwindow* w, float, float) { Get(w).dirty = true; });
  glfwSetWindowRefreshCallback(window, [](GLFWwindow* w) { Get(w).dirty = true; });
  glfwSetWindowFocusCallback(window, [](GLFWwindow* w, int focused) {
    if (!focused) Get(w).dragging = Get(w).scrollbar_drag = Get(w).popup_scrollbar_drag = Get(w).zoom_drag = false;
    Get(w).dirty = true;
  });
  editor.Layout();
#ifdef _WIN32
  const HWND handle = glfwGetWin32Window(window);
  SetPropW(handle, L"bkfont.RichTextEditor", &editor);
  editor.previous_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ImeWindowProc)));
#endif
  GLuint texture;
  glGenTextures(1, &texture);
  int texture_width = 0, texture_height = 0;
  std::string last_title;
  while (!glfwWindowShouldClose(window)) {
    const bool blink = static_cast<int>((glfwGetTime() - editor.last_input) * 2) % 2 == 0;
    if (editor.caret_on != blink && editor.document.GetSelection().Empty()) {
      editor.caret_on = blink;
      editor.dirty = true;
    }
    if (!editor.dirty && editor.OverlayChanged()) editor.dirty = true; // ScreenTip delay.
    if (editor.dirty) {
      editor.dirty = false;
      int pixel_width, pixel_height;
      float sx, sy;
      glfwGetFramebufferSize(window, &pixel_width, &pixel_height);
      glfwGetWindowContentScale(window, &sx, &sy);
      if (pixel_width > 0 && pixel_height > 0) {
        editor.Render(pixel_width, pixel_height, sx > 0 ? sx : 1);
        Present(editor.bitmap.GetPixmap(), texture, editor.upload_damage, texture_width != pixel_width || texture_height != pixel_height);
        texture_width = pixel_width;
        texture_height = pixel_height;
        glfwSwapBuffers(window);
        editor.UpdateIme();
        const std::string title = (editor.document.Modified() ? "* " : "") + editor.DocumentName() + " - bkfont 文档";
        if (title != last_title) {
          glfwSetWindowTitle(window, title.c_str());
          last_title = title;
        }
      }
    }
    glfwWaitEventsTimeout(0.1);
  }
#ifdef _WIN32
  SetWindowLongPtrW(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editor.previous_proc));
  RemovePropW(handle, L"bkfont.RichTextEditor");
#endif
  glDeleteTextures(1, &texture);
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** wide_argv) {
  std::vector<std::string> arguments;
  arguments.reserve(argc);
  for (int i = 0; i < argc; ++i)
    arguments.push_back(rich_text::EncodeUTF8(std::u16string_view(reinterpret_cast<const char16_t*>(wide_argv[i]))));
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  return RunEditor(argc, argv.data());
}
#else
int main(int argc, char** argv) {
  return RunEditor(argc, argv);
}
#endif
