// Word-inspired rich text example. Body and UI text are rendered by bkfont.
// Build: cmake --build build --config Release --target bkfont_rich_text_example
// Run:   bkfont_rich_text_example [--file document.json] [--snapshot view.bmp]
//        bkfont_rich_text_example --write-sample sample.json
// Ctrl+N/O/S, Ctrl+Shift+S, Ctrl+Z/Y, Ctrl+A/C/X/V, Ctrl+B/I/U, Ctrl+wheel.
// Shift+wheel pans horizontally. The right pane exposes the CSS catalogue.
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <commdlg.h>
#include <imm.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

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
constexpr float kBodyTop = 196, kStatusHeight = 28, kSidebarWidth = 300;
constexpr ColorARGB kBlue = 0xff185abd, kInk = 0xff243247, kMuted = 0xff66758a;

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
struct Button {
  Rect rect;
  std::function<void()> action;
  std::string hint;
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

class Editor {
public:
  Document document;
  DocumentView view;
  GLFWwindow* window = nullptr;
  bool dirty = true;
  bool composing = false;
  float scale = 1, width = 1440, height = 980, zoom = 1;
  float scroll = 0, pan = 0, inspector_scroll = 0;
  float mouse_x = 0, mouse_y = 0;
  int tab = 0;
  bool dragging = false, scrollbar_drag = false, caret_on = true;
  float scrollbar_grab = 0;
  TextAffinity affinity = TextAffinity::kDownstream;
  std::string path, status = "就绪";
  double last_input = 0;
  int popup = -1;
  Rect popup_anchor{};
  float popup_scroll = 0;
  int popup_active = -1;
  bool popup_scrollbar_drag = false;
  float popup_scrollbar_grab = 0;
  bool fonts_loaded = false, font_query_selected = false;
  size_t installed_font_count = 0;
  std::vector<FontChoice> font_choices;
  std::vector<size_t> filtered_fonts;
  std::u16string font_query;
  enum class Pending {
    None,
    New,
    Sample,
    Open,
    Close
  };
  Pending pending = Pending::None;
  std::string pending_path;
  bool confirm = false;
  bool path_dialog = false, path_save = false;
  std::u16string path_input;
  bool path_select_all = false;
  std::vector<Button> buttons;
  std::map<std::string, std::unique_ptr<InlineFormattingContext>> labels;
  Bitmap bitmap;
  Bitmap scene_bitmap; // Opaque retained scene, excluding caret and popups.
  std::unique_ptr<RasterCanvas> scene_canvas;
  std::array<std::vector<Button>, 4> scene_buttons;
  std::vector<Button> dialog_buttons;
  std::vector<IntRect> upload_damage;
  struct FrameState {
    uint64_t revision = 0;
    Document::Selection selection;
    uint32_t typing_style = 0;
    uint32_t paragraph_style = 0;
    int tab = 0;
    float scale = 0, width = 0, height = 0, zoom = 0, scroll = 0, pan = 0, inspector_scroll = 0;
    bool modified = false, undo = false, redo = false;
    Rect hover{}, caret{}, overlay{};
    std::string path, status, overlay_key;
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
    return width - kSidebarWidth;
  }
  float PageX() const {
    return std::max(24.0f, (ContentWidth() - view.Width() / scale) / 2) - pan;
  }
  float PageY() const {
    return kBodyTop + 28 - scroll;
  }
  float ViewportHeight() const {
    return height - kStatusHeight - kBodyTop;
  }
  float MaxScroll() const {
    return std::max(0.0f, view.Height() / scale + 56 - ViewportHeight());
  }
  float MaxPan() const {
    return std::max(0.0f, view.Width() / scale + 48 - ContentWidth());
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
    if (top < kBodyTop + 12) scroll -= kBodyTop + 12 - top;
    if (bottom > height - kStatusHeight - 12) scroll += bottom - (height - kStatusHeight - 12);
    const float left = PageX() + rect.X().ToFloat() / scale;
    if (left < 16) pan -= 16 - left;
    if (left > ContentWidth() - 24) pan += left - ContentWidth() + 24;
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
  std::string Current(const PropertySpec& spec) const {
    const uint32_t id = spec.paragraph ? document.Paragraphs()[document.Tree().LineBreaksBefore(document.GetSelection().Start())] : document.TypingStyle();
    return PropertyValue(document.GetStyle(id).properties, spec.name, Default(spec.name));
  }
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
  bool Mixed(const PropertySpec& spec) const {
    const auto selection = document.GetSelection();
    if (selection.Empty()) return false;
    const auto value = Current(spec);
    if (spec.paragraph) {
      const uint32_t first = document.Tree().LineBreaksBefore(selection.Start());
      const uint32_t last = document.Tree().LineBreaksBefore(selection.End() - 1);
      for (uint32_t i = first; i <= last; ++i)
        if (PropertyValue(document.GetStyle(document.Paragraphs()[i]).properties, spec.name, Default(spec.name)) != value) return true;
    } else {
      for (const auto& piece : document.Tree().Slice(selection.Start(), selection.End() - selection.Start()))
        if (PropertyValue(document.GetStyle(piece.style).properties, spec.name, Default(spec.name)) != value) return true;
    }
    return false;
  }
  void Format(const std::string& name, const std::string& value) {
    std::string error;
    if (!document.Format(name, value, error))
      status = error;
    else {
      const auto* spec = FindProperty(name);
      status = std::string(spec->label) + " · " + (value.empty() ? "恢复默认" : value);
    }
    popup = -1;
    Touch(false);
  }
  void Toggle(const char* property, const char* on, const char* off) {
    const auto* spec = FindProperty(property);
    Format(property, !Mixed(*spec) && Current(*spec) == on ? off : on);
  }
  void Preset(int kind) {
    if (document.GetSelection().Empty()) {
      const auto& block = view.BlockAt(document.GetSelection().focus);
      document.Select(block.start, block.end);
    }
    std::string error;
    if (!document.SetCharacterStyle({{"font-size", kind == 1 ? "40px" : kind == 2 ? "24px"
                                                                                  : "18px"},
                                     {"font-weight", kind ? "700" : "400"},
                                     {"color", kind == 1 ? "#185abd" : "#243247"}},
                                    error))
      status = error;
    else
      status = "已应用样式";
    Touch(false);
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
  void Clip(RasterCanvas& raster, Rect rect) {
    raster.ClipRect(ScalarRect::MakeXYWH(rect.x * scale, rect.y * scale, rect.width * scale, rect.height * scale), false);
  }
  void Label(RasterCanvas& raster, const std::string& text, float x, float y, float size = 13,
             ColorARGB color = kInk, float max_width = 600, bool bold = false) {
    if (text.empty() || max_width <= 0) return;
    const std::string key = text + "|" + std::to_string(size) + "|" + std::to_string(color) + "|" + std::to_string(bold);
    auto found = labels.find(key);
    if (found == labels.end()) {
      if (labels.size() > 768) labels.clear();
      auto context = std::make_unique<InlineFormattingContext>(Settings(), nullptr);
      auto declaration = DefaultParagraphStyle();
      (void)declaration.SetLength(CSSPropertyID::kFontSize, {size, CSSPrimitiveValue::UnitType::kPixels});
      (void)declaration.SetNumber(CSSPropertyID::kFontWeight, bold ? 600 : 400);
      (void)declaration.SetColor(StyleColorValue(Color::FromRGBA((color >> 16) & 255, (color >> 8) & 255, color & 255, color >> 24)));
      (void)declaration.SetLineHeight(CSSLineHeight::Number(1.25));
      (void)declaration.SetKeyword(CSSPropertyID::kTextWrapMode, CSSValueID::kNowrap);
      context->SetInlineStyle(context->RootObject(), declaration);
      context->SetZoomFactors(scale);
      auto options = context->Options();
      options.available_inline_size = LayoutUnit(4000 * scale);
      context->SetOptions(options);
      context->AppendText(context->RootObject(), String::FromUTF8(text));
      context->UpdateLayout();
      found = labels.emplace(key, std::move(context)).first;
    }
    raster.Save();
    Clip(raster, {x, y - 4, max_width, size * 2 + 12});
    CanvasPaintCanvas canvas(&raster);
    found->second->Paint(&canvas, {LayoutUnit(x * scale), LayoutUnit(y * scale)});
    raster.Restore();
  }
  void AddButton(RasterCanvas& raster, Rect rect, const std::string& label, std::function<void()> action,
                 bool active = false, bool enabled = true, std::string hint = {}, float font = 13) {
    const bool hovered = rect.Contains(mouse_x, mouse_y) && popup < 0 && !confirm && !path_dialog;
    Fill(raster, rect, active ? 0xffdceafd : hovered && enabled ? 0xffedf2f9
                                                                : 0xfffafbfd,
         4);
    if (active) Fill(raster, {rect.x, rect.y + rect.height - 2, rect.width, 2}, kBlue);
    Label(raster, label, rect.x + 10, rect.y + (rect.height - font * 1.25f) / 2, font,
          !enabled ? 0xffa1aab8 : active ? kBlue
                                         : kInk,
          rect.width - 16, active);
    if (enabled) buttons.push_back({rect, std::move(action), std::move(hint)});
  }
  bool FontPopup() const {
    return popup >= 0 && std::string_view(PropertyCatalog()[popup].name) == "font-family";
  }
  size_t PopupChoices() const {
    return FontPopup() ? filtered_fonts.size() : PropertyCatalog()[popup].choices.size();
  }
  std::string PopupValue(size_t index) const {
    if (index >= PopupChoices()) return {};
    return FontPopup() ? font_choices[filtered_fonts[index]].value : PropertyCatalog()[popup].choices[index];
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
  std::string DisplayFontName(const std::string& value) {
    LoadFontChoices();
    const auto name = FontNameLabel(value), folded = FoldFontName(name);
    for (const auto& choice : font_choices)
      if (choice.Matches(value, folded)) return choice.name;
    return name;
  }
  Rect PopupBox() const {
    const float header = FontPopup() ? 78.0f : 35.0f;
    const int rows = std::clamp(static_cast<int>((height - kStatusHeight - 84 - header) / 29), 1, 12);
    const float pw = FontPopup() ? 360.0f : 268.0f;
    const float ph = header + std::min(PopupChoices() + 1, static_cast<size_t>(rows)) * 29 + 6;
    return {std::clamp(popup_anchor.x, 8.0f, width - pw - 8),
            std::clamp(popup_anchor.y + popup_anchor.height + 3, 72.0f, std::max(72.0f, height - kStatusHeight - ph - 5)), pw, ph};
  }
  Rect PopupList() const {
    const auto box = PopupBox();
    const float header = FontPopup() ? 78.0f : 35.0f;
    return {box.x + 5, box.y + header, box.width - 18, box.height - header - 6};
  }
  Rect FontSearchBox() const {
    const auto box = PopupBox();
    return {box.x + 10, box.y + 34, box.width - 20, 32};
  }
  float PopupMaxScroll() const {
    return std::max(0.0f, (PopupChoices() + 1) * 29 - PopupList().height);
  }
  Rect PopupScrollThumb() const {
    const auto list = PopupList();
    const float maximum = PopupMaxScroll();
    if (maximum <= 0) return {};
    const float thumb = std::max(24.0f, list.height * list.height / (list.height + maximum));
    return {list.x + list.width + 3, list.y + popup_scroll / maximum * (list.height - thumb), 5, thumb};
  }
  int PopupHit() const {
    const auto list = PopupList();
    if (!list.Contains(mouse_x, mouse_y)) return -1;
    const float row = mouse_y - list.y + popup_scroll;
    const int index = static_cast<int>(row / 29);
    return row - index * 29 < 27 && index <= static_cast<int>(PopupChoices()) ? index : -1;
  }
  void FilterFonts() {
    const auto query = FoldFontName(EncodeUTF8(font_query));
    filtered_fonts.clear();
    for (size_t i = 0; i < font_choices.size(); ++i)
      if (query.empty() || font_choices[i].search.find(query) != std::string::npos) filtered_fonts.push_back(i);
    popup_scroll = 0;
    popup_active = filtered_fonts.empty() ? -1 : 0;
    dirty = true;
  }
  void AppendFontQuery(std::u16string_view text) {
    if (font_query_selected) font_query.clear();
    font_query_selected = false;
    size_t count = std::min(text.size(), 1024 - font_query.size());
    if (count < text.size() && count && text[count - 1] >= 0xd800 && text[count - 1] <= 0xdbff) --count;
    font_query.append(text.substr(0, count));
    FilterFonts();
  }
  void RevealPopupChoice() {
    const auto list = PopupList();
    if (popup_active >= 0) {
      popup_scroll = std::min(popup_scroll, popup_active * 29.0f);
      popup_scroll = std::max(popup_scroll, (popup_active + 1) * 29.0f - list.height);
    }
    popup_scroll = std::clamp(popup_scroll, 0.0f, PopupMaxScroll());
    dirty = true;
  }
  void OpenPopup(int index, Rect anchor) {
    popup = index;
    popup_anchor = anchor;
    popup_scroll = 0;
    popup_active = -1;
    popup_scrollbar_drag = dragging = scrollbar_drag = false;
    font_query.clear();
    font_query_selected = false;
    if (FontPopup()) {
      LoadFontChoices();
      FilterFonts();
    }
    const auto current = Current(PropertyCatalog()[popup]);
    const auto current_name = FoldFontName(FontNameLabel(current));
    for (size_t i = 0; i < PopupChoices(); ++i) {
      if (PopupValue(i) == current || (FontPopup() && FontChoiceMatches(i, current, current_name))) {
        popup_active = static_cast<int>(i);
        break;
      }
    }
    RevealPopupChoice();
    UpdateIme();
  }
  void PropertyButton(RasterCanvas& raster, const PropertySpec& spec, Rect rect, bool show_label = true) {
    const int index = static_cast<int>(&spec - PropertyCatalog().data());
    const std::string current = Current(spec);
    const std::string display = std::string_view(spec.name) == "font-family" ? DisplayFontName(current) : current;
    std::string value = Mixed(spec) ? "混合格式" : display;
    if (show_label) value = std::string(spec.label) + "  " + value;
    AddButton(raster, rect, value, [this, index, rect] { OpenPopup(index, rect); }, false, true, std::string(spec.name) + ": " + display);
    Label(raster, "⌄", rect.x + rect.width - 19, rect.y + 6, 13, kMuted, 15);
  }
  void Separator(RasterCanvas& raster, float x) {
    Fill(raster, {x, 81, 1, 75}, 0xffdde3ec);
  }

  void Ribbon(RasterCanvas& raster) {
    Fill(raster, {0, 0, width, 36}, kBlue);
    Label(raster, "W", 18, 5, 21, 0xffffffff, 30, true);
    Label(raster, "bkfont  /  " + (path.empty() ? "排版实验.json" : Filename(path)) + (document.Modified() ? "  •" : ""),
          58, 9, 13, 0xffffffff, width - 520);
    const auto top_button = [&](float x, const std::string& text, std::function<void()> action) {
      Rect rect{x, 3, 58, 29};
      if (rect.Contains(mouse_x, mouse_y)) Fill(raster, rect, 0xff124b9f, 3);
      Label(raster, text, x + 11, 9, 12, 0xffffffff, 48);
      buttons.push_back({rect, std::move(action), {}});
    };
    top_button(width - 320, "新建", [this] { Request(Pending::New); });
    top_button(width - 254, "打开", [this] { ChooseOpen(); });
    top_button(width - 188, "保存", [this] { Save(false); });
    top_button(width - 122, "另存为", [this] { Save(true); });
    Label(raster, "✦", width - 37, 8, 16, 0xffc7ddff, 28);
    Fill(raster, {0, 36, width, 35}, 0xfff7f9fc);
    const char* tabs[] = {"开始", "字体", "段落", "文字效果"};
    for (int i = 0; i < 4; ++i) {
      const Rect rect{20.0f + i * 100, 38, 90, 32};
      Label(raster, tabs[i], rect.x + 18, 44, 14, tab == i ? kBlue : kMuted, 78, tab == i);
      if (tab == i) Fill(raster, {rect.x + 12, 68, 66, 3}, kBlue);
      buttons.push_back({rect, [this, i] { tab = i; inspector_scroll = 0; popup = -1; dirty = true; }, {}});
    }
    Label(raster, "64 项文字 CSS  ·  选择文字后设置格式", width - 328, 45, 12, kMuted, 314);
    Fill(raster, {0, 71, width, 99}, 0xffffffff);
    Fill(raster, {0, 169, width, 1}, 0xffd8dee9);
    if (tab == 0) {
      AddButton(raster, {16, 82, 62, 55}, "粘贴", [this] { Paste(); }, false, true, "Ctrl+V");
      AddButton(raster, {84, 82, 59, 25}, "剪切", [this] { Copy(true); }, false, !document.GetSelection().Empty(), "Ctrl+X");
      AddButton(raster, {84, 112, 59, 25}, "复制", [this] { Copy(false); }, false, !document.GetSelection().Empty(), "Ctrl+C");
      Label(raster, "剪贴板", 53, 146, 11, kMuted);
      Separator(raster, 155);
      PropertyButton(raster, *FindProperty("font-family"), {167, 81, 164, 27}, false);
      PropertyButton(raster, *FindProperty("font-size"), {337, 81, 80, 27}, false);
      const char* labels[] = {"B", "I", "U", "S"};
      const char* properties[] = {"font-weight", "font-style", "text-decoration-line", "text-decoration-line"};
      const char* on[] = {"700", "italic", "underline", "line-through"};
      const char* off[] = {"400", "normal", "none", "none"};
      for (int i = 0; i < 4; ++i) {
        const auto* spec = FindProperty(properties[i]);
        AddButton(raster, {167.0f + i * 38, 114, 32, 25}, labels[i], [this, p = std::string(properties[i]), a = std::string(on[i]), b = std::string(off[i])] { Toggle(p.c_str(), a.c_str(), b.c_str()); }, Current(*spec) == on[i] && !Mixed(*spec), true, properties[i], 15);
      }
      PropertyButton(raster, *FindProperty("color"), {323, 114, 94, 25}, false);
      Label(raster, "字体", 277, 146, 11, kMuted);
      Separator(raster, 430);
      const char* align[] = {"left", "center", "right", "justify"};
      const char* align_label[] = {"左", "中", "右", "齐"};
      for (int i = 0; i < 4; ++i)
        AddButton(raster, {442.0f + i * 40, 81, 35, 27}, align_label[i], [this, value = std::string(align[i])] { Format("text-align", value); }, Current(*FindProperty("text-align")) == align[i], true, std::string("text-align: ") + align[i]);
      PropertyButton(raster, *FindProperty("line-height"), {442, 114, 155, 25});
      Label(raster, "段落", 504, 146, 11, kMuted);
      Separator(raster, 610);
      AddButton(raster, {623, 81, 91, 57}, "正文", [this] { Preset(0); }, false, true, "将所选内容设为正文", 16);
      AddButton(raster, {721, 81, 102, 57}, "大标题", [this] { Preset(1); }, false, true, "标题 · 40px", 20);
      AddButton(raster, {830, 81, 98, 57}, "小标题", [this] { Preset(2); }, false, true, "标题 · 24px", 17);
      Label(raster, "样式", 758, 146, 11, kMuted);
      Separator(raster, 940);
      AddButton(raster, {954, 81, 69, 27}, "撤销", [this] { Undo(false); }, false, document.CanUndo(), "Ctrl+Z");
      AddButton(raster, {1029, 81, 69, 27}, "重做", [this] { Undo(true); }, false, document.CanRedo(), "Ctrl+Y");
      AddButton(raster, {954, 114, 144, 25}, "清除文字格式", [this] { document.ClearCharacterFormatting(); Touch(false); });
      Label(raster, "编辑", 1013, 146, 11, kMuted);
      if (width > 1250) {
        Separator(raster, 1111);
        AddButton(raster, {1125, 81, 120, 57}, "示例文档", [this] { Request(Pending::Sample); });
        Label(raster, "多语言排版", 1138, 146, 11, kMuted);
      }
    } else {
      std::vector<const PropertySpec*> specs;
      for (const auto& spec : PropertyCatalog())
        if (spec.tab == tab) specs.push_back(&spec);
      const int columns = std::max(4, static_cast<int>((width - 36) / 178));
      const float field_width = (width - 36) / columns - 8;
      for (int i = 0; i < std::min(static_cast<int>(specs.size()), columns * 2); ++i)
        PropertyButton(raster, *specs[i], {18 + (i % columns) * (field_width + 8), 80.0f + (i / columns) * 33, field_width, 28});
      Label(raster, "更多选项见右侧属性面板  ·  段落属性作用于当前段落，文字属性作用于选区或后续输入", 25, 148, 11, kMuted, width - 50);
    }
  }

  void Inspector(RasterCanvas& raster) {
    const float x = ContentWidth();
    Fill(raster, {x, 170, kSidebarWidth, height - 170 - kStatusHeight}, 0xfffbfcfe);
    Fill(raster, {x, 170, 1, height - 170}, 0xffdce2ec);
    Label(raster, "格式设置", x + 20, 189, 17, kInk, 220, true);
    const auto selection = document.GetSelection();
    Label(raster, selection.Empty() ? "文字：后续输入  /  段落：当前段" : "正在设置选区格式", x + 20, 219, 12, kMuted, 270);
    const char* captions[] = {"常用文字属性", "字体与 OpenType", "段落与文字方向", "装饰与文字效果"};
    Label(raster, captions[tab], x + 20, 251, 12, kBlue, 260, true);
    const float top = 280, bottom = height - kStatusHeight - 92;
    std::vector<const PropertySpec*> specs;
    for (const auto& spec : PropertyCatalog())
      if (spec.tab == tab) specs.push_back(&spec);
    const float max_scroll = std::max(0.0f, static_cast<float>(specs.size()) * 72 - (bottom - top));
    inspector_scroll = std::clamp(inspector_scroll, 0.0f, max_scroll);
    raster.Save();
    Clip(raster, {x + 1, top, kSidebarWidth - 2, bottom - top});
    for (size_t i = 0; i < specs.size(); ++i) {
      const float y = top + i * 72 - inspector_scroll;
      if (y + 68 <= top || y >= bottom) continue;
      Label(raster, specs[i]->label, x + 20, y + 2, 12, kMuted, 250);
      const Rect rect{x + 18, y + 24, kSidebarWidth - 44, 33};
      const size_t before = buttons.size();
      PropertyButton(raster, *specs[i], rect, false);
      // Clip hit areas as well as painting for partially visible fields.
      if (buttons.size() > before) {
        const float clipped_y = std::max(rect.y, top);
        buttons.back().rect.y = clipped_y;
        buttons.back().rect.height = std::max(0.0f, std::min(rect.y + rect.height, bottom) - clipped_y);
      }
    }
    raster.Restore();
    if (max_scroll > 0) {
      const float track = bottom - top;
      const float thumb = std::max(24.0f, track * track / (track + max_scroll));
      Fill(raster, {width - 9, top + inspector_scroll / max_scroll * (track - thumb), 4, thumb}, 0xffb9c5d6, 2);
    }
    Fill(raster, {x + 18, bottom + 12, kSidebarWidth - 36, 1}, 0xffe2e7ef);
    Label(raster, "点击数值选择；默认值可清除覆盖。", x + 20, bottom + 24, 11, kMuted, 260);
    Label(raster, "字体特性取决于所选字体的支持。", x + 20, bottom + 44, 11, kMuted, 260);
    Label(raster, "滚轮浏览属性 · Ctrl+滚轮缩放文档", x + 20, bottom + 64, 11, kMuted, 260);
  }

  void Paper(RasterCanvas& raster) {
    Fill(raster, {0, 170, ContentWidth(), 26}, 0xfff2f4f8);
    raster.Save();
    Clip(raster, {0, 170, ContentWidth() - 14, 26});
    Fill(raster, {PageX(), 176, view.Width() / scale, 14}, 0xffffffff);
    for (int i = 0; i <= 20; ++i) {
      const float x = PageX() + (64 + i * 32) * zoom;
      Fill(raster, {x, 179, 1, i % 2 ? 5.0f : 9.0f}, 0xffaab6c6);
      if (i % 2 == 0) Label(raster, std::to_string(i), x + 3, 177, 9, kMuted, 28);
    }
    raster.Restore();
    raster.Save();
    Clip(raster, {0, kBodyTop, ContentWidth() - 13, ViewportHeight()});
    const Rect paper{PageX(), PageY(), view.Width() / scale, view.Height() / scale};
    // The opaque paper occludes the entire middle of its shadow. Retain the
    // exact aliased pixel edges, including at fractional DPI/scroll offsets.
    const float right = std::ceil((paper.x + paper.width) * scale - 0.5f);
    const float bottom = std::ceil((paper.y + paper.height) * scale - 0.5f);
    raster.Save();
    raster.ClipRect(ScalarRect::MakeLTRB(right, 0, width * scale, height * scale), false);
    Fill(raster, {paper.x + 4, paper.y + 4, paper.width, paper.height}, 0xffc8ced8, 1);
    raster.Restore();
    raster.Save();
    raster.ClipRect(ScalarRect::MakeLTRB(0, bottom, right, height * scale), false);
    Fill(raster, {paper.x + 4, paper.y + 4, paper.width, paper.height}, 0xffc8ced8, 1);
    raster.Restore();
    Fill(raster, paper, 0xffffffff);
    Label(raster, "B K F O N T   /   文 档", paper.x + 64 * zoom, paper.y + 23 * zoom, 9 * zoom, 0xffa0abba, paper.width - 128 * zoom);
    view.Paint(raster, paper.x * scale, paper.y * scale, kBodyTop * scale, (height - kStatusHeight) * scale, document.GetSelection());
    raster.Restore();
    const float track = ViewportHeight(), maximum = MaxScroll();
    Fill(raster, {ContentWidth() - 12, kBodyTop, 12, track}, 0xffe5e9f0);
    if (maximum > 0) {
      const float thumb = std::max(36.0f, track * track / (track + maximum));
      Fill(raster, {ContentWidth() - 9, kBodyTop + scroll / maximum * (track - thumb), 6, thumb}, 0xffaebacc, 3);
    }
  }

  void StatusBar(RasterCanvas& raster) {
    const float y = height - kStatusHeight;
    Fill(raster, {0, y, width, kStatusHeight}, 0xfff8fafd);
    Fill(raster, {0, y, width, 1}, 0xffd6dfeb);
    const auto selection = document.GetSelection();
    const auto stats = document.Tree().Stats();
    Label(raster, std::to_string(document.Paragraphs().size()) + " 段  ·  " + std::to_string(view.Text().size()) + " UTF-16" + (selection.Empty() ? "" : "  ·  选中 " + std::to_string(selection.End() - selection.Start())), 16, y + 7, 11, kMuted, 280);
    Label(raster, "片段 " + std::to_string(stats.live_nodes) + "  /  空闲节点 " + std::to_string(stats.free_nodes), 306, y + 7, 11, kMuted, 230);
    Label(raster, status, 555, y + 7, 11, kMuted, width - 880);
    AddButton(raster, {width - 290, y + 3, 36, 23}, "−", [this] { Zoom(zoom - 0.1f); });
    Fill(raster, {width - 242, y + 14, 100, 2}, 0xffbdcbe0);
    Fill(raster, {width - 242, y + 14, (zoom - 0.5f) / 1.5f * 100, 2}, kBlue);
    Fill(raster, {width - 245 + (zoom - 0.5f) / 1.5f * 100, y + 10, 7, 10}, kBlue, 3);
    buttons.push_back({{width - 245, y + 3, 108, 23}, [this] { Zoom(0.5f + std::clamp((mouse_x - (width - 242)) / 100, 0.0f, 1.0f) * 1.5f); }, "50% — 200%"});
    AddButton(raster, {width - 128, y + 3, 36, 23}, "+", [this] { Zoom(zoom + 0.1f); });
    AddButton(raster, {width - 84, y + 3, 72, 23}, std::to_string(static_cast<int>(std::round(zoom * 100))) + "%", [this] { Zoom(1); });
  }

  void Popup(RasterCanvas& raster) {
    if (popup < 0) return;
    const auto& spec = PropertyCatalog()[popup];
    const auto box = PopupBox(), list = PopupList();
    Fill(raster, {box.x + 3, box.y + 4, box.width, box.height}, 0x33000000, 6);
    Fill(raster, {box.x - 1, box.y - 1, box.width + 2, box.height + 2}, 0xffd5deeb, 6);
    Fill(raster, box, 0xffffffff, 5);
    Label(raster, FontPopup() ? "已安装 " + std::to_string(installed_font_count) + " 个字体族  ·  匹配 " + std::to_string(filtered_fonts.size()) : spec.name,
          box.x + 12, box.y + 10, 11, kMuted, box.width - 24);
    if (FontPopup()) {
      const auto search = FontSearchBox();
      Fill(raster, search, font_query_selected ? 0xffdbeafe : 0xfff0f4fa, 4);
      Label(raster, font_query.empty() ? "搜索字体…  ↑↓选择，Enter应用" : EncodeUTF8(font_query) + "|", search.x + 9, search.y + 7, 13,
            font_query.empty() ? kMuted : kInk, search.width - 18);
    }
    const auto current = Current(spec);
    const auto current_name = FontPopup() ? FoldFontName(FontNameLabel(current)) : std::string();
    const bool mixed = Mixed(spec);
    const int hovered = PopupHit();
    const size_t count = PopupChoices();
    const size_t first = static_cast<size_t>(popup_scroll / 29);
    const size_t last = std::min(count + 1, static_cast<size_t>(std::ceil((popup_scroll + list.height) / 29)));
    raster.Save();
    Clip(raster, list);
    // Only visible rows create labels/hit areas, regardless of font count.
    for (size_t i = first; i < last; ++i) {
      const bool clear = i == count;
      const std::string value = PopupValue(i);
      const std::string label = clear ? "恢复默认 / 清除覆盖" : FontPopup() ? font_choices[filtered_fonts[i]].name
                                                                            : value;
      const Rect rect{list.x, list.y + i * 29 - popup_scroll, list.width, 27};
      const bool active = !clear && !mixed && (current == value || (FontPopup() && FontChoiceMatches(i, current, current_name)));
      if (static_cast<int>(i) == hovered || static_cast<int>(i) == popup_active || active)
        Fill(raster, rect, active ? 0xffdfecff : 0xfff1f5fa, 3);
      Label(raster, (active ? "✓  " : "    ") + label, rect.x + 6, rect.y + 5, 12,
            active ? kBlue : clear ? kMuted
                                   : kInk,
            rect.width - 16);
    }
    raster.Restore();
    if (const auto thumb = PopupScrollThumb(); !thumb.Empty()) Fill(raster, thumb, 0xffaebacc, 2);
  }

  void Dialog(RasterCanvas& raster) {
    if (!confirm && !path_dialog) return;
    buttons.clear();
    Fill(raster, {0, 0, width, height}, 0x770f172a);
    const float x = (width - 520) / 2, y = (height - 220) / 2;
    Fill(raster, {x, y, 520, 220}, 0xffffffff, 8);
    Label(raster, path_dialog ? (path_save ? "保存 JSON 文档" : "打开 JSON 文档") : "保存对文档的更改？", x + 26, y + 24, 20, kInk, 460, true);
    if (path_dialog) {
      Fill(raster, {x + 24, y + 75, 472, 39}, path_select_all ? 0xffdbeafe : 0xfff0f4fa, 4);
      Label(raster, EncodeUTF8(path_input) + "|", x + 34, y + 85, 14, kInk, 450);
      Label(raster, "文件路径 · Enter 确定 · Escape 取消", x + 26, y + 127, 12, kMuted, 460);
      AddButton(raster, {x + 303, y + 169, 91, 31}, "确定", [this] { FinishPathDialog(); }, true);
      AddButton(raster, {x + 402, y + 169, 91, 31}, "取消", [this] { path_dialog = false; dirty = true; });
    } else {
      Label(raster, "当前文档有尚未保存的更改。", x + 26, y + 79, 14, kMuted, 460);
      AddButton(raster, {x + 164, y + 158, 100, 34}, "保存", [this] {
        if (Save(false)) { confirm = false; ExecutePending(); } }, true);
      AddButton(raster, {x + 278, y + 158, 100, 34}, "不保存", [this] { confirm = false; ExecutePending(); });
      AddButton(raster, {x + 392, y + 158, 100, 34}, "取消", [this] { confirm = false; pending = Pending::None; dirty = true; });
    }
  }

  const Button* Hovered() const {
    if (popup >= 0 || confirm || path_dialog) return nullptr;
    for (const auto& button : buttons)
      if (button.rect.Contains(mouse_x, mouse_y)) return &button;
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
    if (!caret_on || !document.GetSelection().Empty() || popup >= 0 || confirm || path_dialog) return {};
    const auto caret = view.Caret(document.GetSelection().focus, affinity);
    const float x = std::max(0.0f, PageX() + caret.X().ToFloat() / scale);
    const float y = std::max(kBodyTop, PageY() + caret.Y().ToFloat() / scale);
    const float right = std::min(ContentWidth() - 13, PageX() + caret.X().ToFloat() / scale + std::max(1.3f, caret.Width().ToFloat() / scale));
    const float bottom = std::min(height - kStatusHeight, PageY() + caret.Y().ToFloat() / scale + std::max(1.3f, caret.Height().ToFloat() / scale));
    return {x, y, right - x, bottom - y};
  }
  Rect OverlayBounds(std::string& key) {
    if (confirm || path_dialog) {
      key = "dialog:" + std::to_string(confirm) + std::to_string(path_dialog) + std::to_string(path_save) +
            std::to_string(path_select_all) + EncodeUTF8(path_input);
      return {0, 0, width, height};
    }
    if (popup >= 0) {
      const auto& spec = PropertyCatalog()[popup];
      const auto box = PopupBox();
      key = "popup:" + std::to_string(popup) + ':' + Current(spec) + ':' + std::to_string(Mixed(spec)) + ':' + std::to_string(PopupHit()) +
            ':' + std::to_string(popup_scroll) + ':' + std::to_string(popup_active) + ':' + std::to_string(font_query_selected) + ':' + EncodeUTF8(font_query);
      return {box.x - 1, box.y - 1, box.width + 5, box.height + 6};
    }
    if (const auto* hover = Hovered(); hover && !hover->hint.empty()) {
      const float tw = std::min(410.0f, static_cast<float>(hover->hint.size()) * 7 + 20);
      key = hover->hint;
      return {std::clamp(mouse_x + 12, 4.0f, width - tw - 4), std::min(height - 60, mouse_y + 25), tw, 29};
    }
    key.clear();
    return {};
  }
  void Render(int pixel_width, int pixel_height, float device_scale) {
    if (device_scale != scale) labels.clear();
    scale = device_scale;
    width = pixel_width / scale;
    height = pixel_height / scale;
    if (popup >= 0) popup_scroll = std::clamp(popup_scroll, 0.0f, PopupMaxScroll());
    if (bitmap.GetPixmap().Width() != pixel_width || bitmap.GetPixmap().Height() != pixel_height) {
      scene_canvas.reset();
      bitmap = Bitmap(ColorType::kN32, pixel_width, pixel_height);
      scene_bitmap = Bitmap(ColorType::kN32, pixel_width, pixel_height);
      scene_canvas = std::make_unique<RasterCanvas>(scene_bitmap.GetPixmap(), SurfaceProps(), 0xffe8ecf2);
      frame_valid = false;
    }
    Layout();
    upload_damage.clear();
    FrameState next;
    next.revision = document.Revision();
    next.selection = document.GetSelection();
    next.typing_style = document.TypingStyle();
    next.paragraph_style = document.Paragraphs()[document.Tree().LineBreaksBefore(next.selection.Start())];
    next.tab = tab;
    next.scale = scale;
    next.width = width;
    next.height = height;
    next.zoom = zoom;
    next.scroll = scroll;
    next.pan = pan;
    next.inspector_scroll = inspector_scroll;
    next.modified = document.Modified();
    next.undo = document.CanUndo();
    next.redo = document.CanRedo();
    next.path = path;
    next.status = status;
    next.caret = CaretBounds();
    if (const auto* hover = Hovered()) next.hover = hover->rect;
    const bool all = !frame_valid || frame.scale != scale || frame.width != width || frame.height != height;
    const bool changed = frame.revision != next.revision;
    const bool format = frame.typing_style != next.typing_style || frame.paragraph_style != next.paragraph_style ||
                        frame.selection.Empty() != next.selection.Empty() ||
                        (!next.selection.Empty() && (changed || frame.selection != next.selection));
    const Rect groups[] = {{0, 0, width, 170}, {0, 170, ContentWidth(), height - kStatusHeight - 170}, {ContentWidth(), 170, kSidebarWidth, height - kStatusHeight - 170}, {0, height - kStatusHeight, width, kStatusHeight}};
    std::array<Rect, 4> repaint{};
    if (all || format || frame.tab != tab || frame.path != path || frame.modified != next.modified || frame.undo != next.undo || frame.redo != next.redo) repaint[0] = groups[0];
    const bool selection_changed = frame.selection != next.selection && (!frame.selection.Empty() || !next.selection.Empty());
    if (all || changed || selection_changed || frame.zoom != zoom || frame.scroll != scroll || frame.pan != pan) repaint[1] = groups[1];
    if (all || format || frame.tab != tab || frame.inspector_scroll != inspector_scroll) repaint[2] = groups[2];
    if (all || changed || frame.selection != next.selection || frame.zoom != zoom || frame.status != status) repaint[3] = groups[3];
    if (frame.hover != next.hover) {
      for (Rect hover : {frame.hover, next.hover}) {
        if (hover.Empty()) continue;
        for (int i = 0; i < 4; ++i)
          if (groups[i].Contains(hover.x + 1, hover.y + 1)) repaint[i] = Union(repaint[i], hover);
      }
    }
    for (int i = 0; i < 4; ++i) {
      const IntRect area = Pixels(repaint[i]);
      if (!repaint[i].Empty() && !area.IsEmpty()) {
        buttons.clear();
        {
          // Retain float storage: allocating/widening the framebuffer for
          // every interaction otherwise dominates high-DPI rendering.
          auto& raster = *scene_canvas;
          raster.Save();
          raster.ClipRect(ScalarRect::MakeLTRB(static_cast<float>(area.left), static_cast<float>(area.top),
                                               static_cast<float>(area.right), static_cast<float>(area.bottom)),
                          false);
          raster.Clear(0xffe8ecf2);
          switch (i) {
          case 0:
            Ribbon(raster);
            break;
          case 1:
            Paper(raster);
            break;
          case 2:
            Inspector(raster);
            break;
          case 3:
            StatusBar(raster);
            break;
          }
          raster.Restore();
          raster.Flush(area);
        }
        scene_buttons[i] = std::move(buttons);
        Damage(repaint[i]);
      }
    }
    buttons.clear();
    for (const auto& group : scene_buttons) buttons.insert(buttons.end(), group.begin(), group.end());
    next.hover = {};
    if (const auto* hover = Hovered()) next.hover = hover->rect;
    next.inspector_scroll = inspector_scroll; // Inspector clamps it to its content.
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
      Fill(raster, next.caret, kBlue);
    }
    if (paint_overlay) {
      const auto overlay_area = Pixels(next.overlay);
      const auto& dst = bitmap.GetPixmap();
      RasterCanvas raster(Region(dst, overlay_area), SurfaceProps(), std::nullopt, overlay_area.left, overlay_area.top);
      if (popup < 0 && !confirm && !path_dialog) {
        Fill(raster, next.overlay, 0xff253550, 4);
        Label(raster, next.overlay_key, next.overlay.x + 9, next.overlay.y + 7, 11, 0xffffffff, next.overlay.width - 18);
      }
      Popup(raster);
      Dialog(raster);
      if (confirm || path_dialog) dialog_buttons = buttons;
    }
    if (confirm || path_dialog) buttons = dialog_buttons;
    frame = std::move(next);
    frame_valid = true;
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
  if (confirm && !path_dialog) return;
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
  } else if (FontPopup()) {
    AppendFontQuery(text);
  } else {
    popup = -1;
    Insert(text);
  }
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
    }
    return;
  }
  if (key == GLFW_KEY_ESCAPE) {
    popup = -1;
    popup_scrollbar_drag = false;
    dragging = false;
    dirty = true;
    return;
  }
  if (popup >= 0) {
    if (FontPopup()) {
      if (control && key == GLFW_KEY_A) {
        font_query_selected = true;
        dirty = true;
        return;
      }
      if (control && key == GLFW_KEY_V && window) {
        std::u16string text;
        const char* content = glfwGetClipboardString(window);
        if (content && DecodeUTF8(content, text)) AppendFontQuery(text);
        return;
      }
      if (key == GLFW_KEY_BACKSPACE || key == GLFW_KEY_DELETE) {
        if (font_query_selected || control)
          font_query.clear();
        else if (key == GLFW_KEY_BACKSPACE && !font_query.empty()) {
          const char16_t last = font_query.back();
          font_query.pop_back();
          if (last >= 0xdc00 && last <= 0xdfff && !font_query.empty()) font_query.pop_back();
        }
        font_query_selected = false;
        FilterFonts();
        return;
      }
    }
    const int last = static_cast<int>(PopupChoices());
    const int page = std::max(1, static_cast<int>(PopupList().height / 29));
    if (key == GLFW_KEY_UP)
      popup_active = popup_active < 0 ? last : std::max(0, popup_active - 1);
    else if (key == GLFW_KEY_DOWN)
      popup_active = std::min(last, popup_active + 1);
    else if (key == GLFW_KEY_HOME)
      popup_active = 0;
    else if (key == GLFW_KEY_END)
      popup_active = last;
    else if (key == GLFW_KEY_PAGE_UP)
      popup_active = std::max(0, popup_active - page);
    else if (key == GLFW_KEY_PAGE_DOWN)
      popup_active = std::min(last, popup_active + page);
    else if ((key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) && popup_active >= 0) {
      Format(PropertyCatalog()[popup].name, PopupValue(popup_active));
      return;
    } else
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
      document.Select(0, document.Tree().Length());
      Touch(false);
      return;
    case GLFW_KEY_C:
      Copy(false);
      return;
    case GLFW_KEY_X:
      Copy(true);
      return;
    case GLFW_KEY_V:
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
      Toggle("text-decoration-line", "underline", "none");
      return;
    case GLFW_KEY_EQUAL:
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
    dragging = scrollbar_drag = popup_scrollbar_drag = false;
    return;
  }
  if (popup >= 0) {
    if (FontPopup() && FontSearchBox().Contains(mouse_x, mouse_y)) {
      font_query_selected = !font_query.empty();
      dirty = true;
      UpdateIme();
      return;
    }
    const auto list = PopupList(), thumb = PopupScrollThumb();
    if (!thumb.Empty() && Rect{list.x + list.width, list.y, 12, list.height}.Contains(mouse_x, mouse_y)) {
      popup_scrollbar_grab = thumb.Contains(mouse_x, mouse_y) ? mouse_y - thumb.y : thumb.height / 2;
      popup_scrollbar_drag = true;
      Motion(mouse_x, mouse_y);
      return;
    }
    if (const int index = PopupHit(); index >= 0) {
      Format(PropertyCatalog()[popup].name, PopupValue(index));
      return;
    }
    if (PopupBox().Contains(mouse_x, mouse_y)) return;
    popup = -1;
    dirty = true;
    return;
  }
  for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
    if (it->rect.Contains(mouse_x, mouse_y)) {
      const auto action = it->action;
      action();
      return;
    }
  if (confirm || path_dialog) return;
  if (mouse_x >= ContentWidth() - 12 && mouse_x < ContentWidth() && mouse_y >= kBodyTop && mouse_y < height - kStatusHeight && MaxScroll() > 0) {
    const float track = ViewportHeight(), thumb = std::max(36.0f, track * track / (track + MaxScroll()));
    const float top = kBodyTop + scroll / MaxScroll() * (track - thumb);
    scrollbar_grab = mouse_y >= top && mouse_y <= top + thumb ? mouse_y - top : thumb / 2;
    scrollbar_drag = true;
    Motion(mouse_x, mouse_y);
    return;
  }
  if (mouse_x < ContentWidth() - 12 && mouse_y >= kBodyTop && mouse_y < height - kStatusHeight) {
    const uint32_t position = view.Hit((mouse_x - PageX()) * scale, (mouse_y - PageY()) * scale, &affinity);
    document.Select(modifiers & GLFW_MOD_SHIFT ? document.GetSelection().anchor : position, position);
    dragging = true;
    Touch(false);
  }
}
void Editor::Motion(float x, float y) {
  const Rect old_hover = Hovered() ? Hovered()->rect : Rect{};
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
  } else if (scrollbar_drag) {
    const float track = ViewportHeight(), thumb = std::max(36.0f, track * track / (track + MaxScroll()));
    scroll = std::clamp((y - kBodyTop - scrollbar_grab) / (track - thumb) * MaxScroll(), 0.0f, MaxScroll());
  } else if (dragging) {
    if (y < kBodyTop + 10) scroll -= 18;
    if (y > height - kStatusHeight - 10) scroll += 18;
    scroll = std::clamp(scroll, 0.0f, MaxScroll());
    const uint32_t position = view.Hit((x - PageX()) * scale, (y - PageY()) * scale, &affinity);
    document.Select(document.GetSelection().anchor, position);
    caret_on = true;
  }
  const Rect new_hover = Hovered() ? Hovered()->rect : Rect{};
  std::string new_overlay_key;
  const Rect new_overlay = OverlayBounds(new_overlay_key);
  if (dragging || scrollbar_drag || old_hover != new_hover || old_overlay != new_overlay || old_overlay_key != new_overlay_key) dirty = true;
}
void Editor::Wheel(double dx, double dy, bool control, bool shift) {
  if (confirm || path_dialog) return;
  if (popup >= 0 && !control) {
    popup_scroll = std::clamp(popup_scroll - static_cast<float>(dy) * 87, 0.0f, PopupMaxScroll());
    popup_active = -1;
    dirty = true;
    return;
  }
  popup = -1;
  popup_scrollbar_drag = false;
  if (control)
    Zoom(zoom + static_cast<float>(dy) * 0.05f);
  else if (mouse_x >= ContentWidth())
    inspector_scroll -= static_cast<float>(dy) * 48;
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
  const auto search = FontPopup() ? FontSearchBox() : Rect{};
  const LONG x = static_cast<LONG>((FontPopup() ? search.x + 10 : PageX() + rect.X().ToFloat() / scale) * to_window);
  const LONG y = static_cast<LONG>((FontPopup() ? search.y + search.height : PageY() + rect.Bottom().ToFloat() / scale) * to_window);
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
  std::string file, snapshot, sample_path;
  int selected_tab = 0;
  float snapshot_scale = 1;
  const auto usage = [] {
    std::puts("bkfont_rich_text_example [--file document.json] [--snapshot view.bmp]\n"
              "  [--tab 0|1|2|3] [--scale 1|1.5|2] [--write-sample sample.json]\n"
              "Ctrl+N/O/S, Ctrl+Shift+S, Ctrl+Z/Y, Ctrl+A/C/X/V, Ctrl+B/I/U.\n"
              "Ctrl+wheel: zoom. Shift+wheel: pan. Right pane: CSS properties.");
  };
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--file" && i + 1 < argc)
      file = argv[++i];
    else if (arg == "--snapshot" && i + 1 < argc)
      snapshot = argv[++i];
    else if (arg == "--write-sample" && i + 1 < argc)
      sample_path = argv[++i];
    else if (arg == "--tab" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value.size() != 1 || value[0] < '0' || value[0] > '3') {
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
    editor.Render(static_cast<int>(1440 * snapshot_scale), static_cast<int>(980 * snapshot_scale), snapshot_scale);
    return WriteBitmap(editor.bitmap.GetPixmap(), snapshot) ? 0 : 1;
  }
  glfwSetErrorCallback([](int code, const char* error) { std::fprintf(stderr, "GLFW %d: %s\n", code, error); });
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  editor.window = glfwCreateWindow(1440, 980, "bkfont | Rich text editor", nullptr, nullptr);
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
    if (!focused) Get(w).dragging = Get(w).scrollbar_drag = Get(w).popup_scrollbar_drag = false;
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
        const std::string title = (editor.document.Modified() ? "* " : "") +
                                  (editor.path.empty() ? "Untitled.json" : Filename(editor.path)) + " | bkfont Rich Text";
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
