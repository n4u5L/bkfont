#include "document.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#endif

#include "json.h"

namespace rich_text {
namespace {

constexpr size_t kMaxFileBytes = 16 * 1024 * 1024;
constexpr size_t kMaxTextUnits = 2 * 1024 * 1024;
constexpr size_t kHistoryLimit = 256;

bool IndexValue(const Json* value, size_t limit, uint32_t& index) {
  if (!value || value->type != Json::Type::Number || value->number < 0 ||
      value->number >= static_cast<double>(limit) || value->number != std::floor(value->number)) return false;
  index = static_cast<uint32_t>(value->number);
  return true;
}
FILE* Open(const std::string& path, bool write) {
#ifdef _WIN32
  std::u16string utf16;
  if (!DecodeUTF8(path, utf16)) return nullptr;
  return _wfopen(reinterpret_cast<const wchar_t*>(utf16.c_str()), write ? L"wb" : L"rb");
#else
  return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}
std::filesystem::path FilePath(const std::string& path) {
#ifdef _WIN32
  std::u16string utf16;
  DecodeUTF8(path, utf16);
  return std::filesystem::path(utf16);
#else
  return std::filesystem::path(path);
#endif
}

} // namespace

Document::Document() {
  styles_.push_back({});
}

uint32_t Document::Intern(Properties properties) {
  std::sort(properties.begin(), properties.end());
  for (uint32_t i = 0; i < styles_.size(); ++i)
    if (styles_[i].properties == properties) return i;
  bkfont::StyleDeclaration declaration;
  std::string error;
  const bool valid = BuildDeclaration(properties, declaration, error);
  assert(valid);
  if (!valid) return 0;
  styles_.push_back({std::move(properties), std::move(declaration)});
  return static_cast<uint32_t>(styles_.size() - 1);
}

void Document::New(bool sample) {
  *this = Document();
  if (!sample) return;
  std::u16string text;
  std::vector<PieceTree::Piece> pieces;
  paragraphs_.clear();
  const auto paragraph = [&](std::u16string_view content, Properties character, Properties block) {
    const uint32_t character_id = Intern(std::move(character));
    paragraphs_.push_back(Intern(std::move(block)));
    const Offset start = static_cast<Offset>(text.size());
    text.append(content);
    text.push_back(u'\n');
    pieces.push_back({0, start, static_cast<Offset>(content.size() + 1), character_id});
  };
  paragraph(u"把想法，写成作品。", {{"font-size", "40px"}, {"font-weight", "700"}, {"color", "#185abd"}},
            {{"line-height", "1.25"}});
  paragraph(u"一份可以编辑的排版实验 · bkfont", {{"font-size", "14px"}, {"color", "#808080"}, {"letter-spacing", "2px"}}, {});
  paragraph(u"从这里开始", {{"font-size", "24px"}, {"font-weight", "600"}}, {});
  paragraph(u"点击文字放置光标，拖动选择一段内容，再使用上方功能区更改格式。支持中文输入、复制粘贴、撤销重做，以及 JSON 文件的打开和保存。", {},
            {{"text-indent", "2em"}});
  paragraph(u"让文字拥有自己的节奏", {{"font-size", "24px"}, {"font-weight", "600"}}, {});
  paragraph(u"这条蓝色波浪下划线来自 CSS 的文字装饰。", {{"text-decoration-line", "underline"}, {"text-decoration-style", "wavy"}, {"text-decoration-color", "#185abd"}, {"text-underline-offset", "4px"}}, {});
  paragraph(u"着重号强调细节，阴影增加层次。", {{"text-emphasis-style", "filled dot"}, {"text-emphasis-color", "#c43e1c"}, {"text-shadow", "1px 2px 2px #7687a6"}}, {{"line-height", "2"}});
  paragraph(u"Typography: office affinity • 0123456789 • 1/2 3/4", {{"font-family", "Georgia"}, {"font-size", "22px"}, {"font-variant-numeric", "tabular-nums"}, {"font-variant-ligatures", "common-ligatures"}}, {{"-webkit-locale", "en-US"}});
  paragraph(u"同一张纸，多种语言", {{"font-size", "24px"}, {"font-weight", "600"}}, {});
  paragraph(u"中文排版 · 日本語の組版 · 한글 조판 · Hello, world! 🌏", {}, {{"text-autospace", "normal"}});
  paragraph(u"مرحبا بالعالم — يمكن تحرير النص العربي هنا", {{"font-size", "22px"}}, {{"direction", "rtl"}, {"-webkit-locale", "ar"}});
  paragraph(u"春はあけぼの。やうやう白くなりゆく山ぎは。", {{"font-size", "22px"}},
            {{"writing-mode", "vertical-rl"}, {"text-orientation", "mixed"}, {"-webkit-locale", "ja"}});
  paragraph(u"试一试：选中文字，在“字体”“段落”“文字效果”中探索更多排版属性。", {{"font-size", "14px"}, {"color", "#808080"}}, {});
  text.pop_back();
  --pieces.back().length;
  tree_.Reset(std::move(text), pieces);
  Select(0, 0);
}

void Document::Select(Offset anchor, Offset focus) {
  selection_ = {std::min(anchor, tree_.Length()), std::min(focus, tree_.Length())};
  Offset position = selection_.Empty() && selection_.focus ? selection_.focus - 1 : selection_.Start();
  if (selection_.Empty() && selection_.focus && selection_.focus < tree_.Length()) {
    const auto preceding = tree_.Slice(selection_.focus - 1, 1);
    if (tree_.View(preceding.front()).front() == u'\n') position = selection_.focus;
  }
  typing_style_ = tree_.StyleAt(position);
}

Document::Offset Document::PiecesLength(const std::vector<PieceTree::Piece>& pieces) {
  Offset result = 0;
  for (const auto& piece : pieces) result += piece.length;
  return result;
}
Document::Change Document::BeginChange(Offset position, Offset length) const {
  Change change;
  change.position = position;
  change.before = tree_.Slice(position, length);
  change.paragraphs_before = paragraphs_;
  change.selection_before = selection_;
  change.revision_before = revision_;
  return change;
}
void Document::Commit(Change change) {
  change.paragraphs_after = paragraphs_;
  change.selection_after = selection_;
  change.revision_after = revision_ = next_revision_++;
  history_.resize(history_cursor_);
  if (history_.size() == kHistoryLimit) history_.erase(history_.begin());
  history_.push_back(std::move(change));
  history_cursor_ = history_.size();
}

bool Document::ReplaceSelection(std::u16string_view input, std::string& error) {
  const std::u16string text = NormalizeNewlines(input);
  if (text.empty() && selection_.Empty()) return true;
  const Offset start = selection_.Start(), removed = selection_.End() - start;
  if (text.size() + tree_.Length() - removed > kMaxTextUnits ||
      tree_.Stats().buffer_units + text.size() > std::numeric_limits<Offset>::max()) {
    error = "Document exceeds the example's text capacity";
    return false;
  }
  const Offset first_paragraph = tree_.LineBreaksBefore(start);
  const Offset removed_paragraphs = tree_.LineBreaksBefore(start + removed) - first_paragraph;
  const size_t inserted_paragraphs = std::count(text.begin(), text.end(), u'\n');
  Change change = BeginChange(start, removed);
  if (!text.empty()) change.after.push_back(tree_.Append(text, typing_style_));
  tree_.Replace(start, removed, change.after);
  const uint32_t paragraph_style = paragraphs_[first_paragraph];
  paragraphs_.erase(paragraphs_.begin() + first_paragraph + 1,
                    paragraphs_.begin() + first_paragraph + 1 + removed_paragraphs);
  paragraphs_.insert(paragraphs_.begin() + first_paragraph + 1, inserted_paragraphs, paragraph_style);
  selection_ = {start + static_cast<Offset>(text.size()), start + static_cast<Offset>(text.size())};
  Commit(std::move(change));
  error.clear();
  return true;
}

bool Document::Format(std::string_view name, std::string_view value, std::string& error) {
  const auto* spec = FindProperty(name);
  bkfont::StyleDeclaration validation;
  if (!spec || (!value.empty() && !BuildDeclaration({{std::string(name), std::string(value)}}, validation, error))) return false;
  const auto changed_style = [&](uint32_t old) {
    Properties properties = styles_[old].properties;
    SetProperty(properties, name, value);
    return Intern(std::move(properties));
  };
  if (!spec->paragraph && selection_.Empty()) {
    typing_style_ = changed_style(typing_style_);
    return true;
  }
  const Offset start = selection_.Start(), length = selection_.End() - start;
  Change change = BeginChange(start, spec->paragraph ? 0 : length);
  if (spec->paragraph) {
    const Offset first = tree_.LineBreaksBefore(start);
    const Offset last = tree_.LineBreaksBefore(length ? selection_.End() - 1 : start);
    for (Offset i = first; i <= last; ++i) paragraphs_[i] = changed_style(paragraphs_[i]);
  } else {
    change.after = change.before;
    for (auto& piece : change.after) piece.style = changed_style(piece.style);
    tree_.Replace(start, length, change.after);
    typing_style_ = tree_.StyleAt(start);
  }
  if (change.before != change.after || change.paragraphs_before != paragraphs_) Commit(std::move(change));
  error.clear();
  return true;
}

void Document::ClearCharacterFormatting() {
  std::string error;
  (void)SetCharacterStyle({}, error);
}

bool Document::SetCharacterStyle(Properties properties, std::string& error) {
  bkfont::StyleDeclaration declaration;
  if (!BuildDeclaration(properties, declaration, error)) return false;
  for (const auto& [name, value] : properties) {
    if (FindProperty(name)->paragraph) {
      error = "A character style cannot contain paragraph properties";
      return false;
    }
  }
  typing_style_ = Intern(std::move(properties));
  if (selection_.Empty()) return true;
  Change change = BeginChange(selection_.Start(), selection_.End() - selection_.Start());
  change.after = change.before;
  for (auto& piece : change.after) piece.style = typing_style_;
  tree_.Replace(change.position, PiecesLength(change.before), change.after);
  if (change.before != change.after) Commit(std::move(change));
  return true;
}

void Document::Restore(const Change& change, bool forward) {
  tree_.Replace(change.position, PiecesLength(forward ? change.before : change.after), forward ? change.after : change.before);
  paragraphs_ = forward ? change.paragraphs_after : change.paragraphs_before;
  const Selection selection = forward ? change.selection_after : change.selection_before;
  Select(selection.anchor, selection.focus);
  revision_ = forward ? change.revision_after : change.revision_before;
}
bool Document::Undo() {
  if (!CanUndo()) return false;
  Restore(history_[--history_cursor_], false);
  return true;
}
bool Document::Redo() {
  if (!CanRedo()) return false;
  Restore(history_[history_cursor_++], true);
  return true;
}

std::string Document::SelectedText() const {
  std::u16string text;
  for (const auto& piece : tree_.Slice(selection_.Start(), selection_.End() - selection_.Start())) text.append(tree_.View(piece));
  return EncodeUTF8(text);
}

std::string Document::Serialize() const {
  // Only persist referenced styles, in first-use order, not undo history or
  // runtime node indices. Loading reconstructs a compact original buffer.
  std::vector<uint32_t> ids{0};
  const auto remap = [&](uint32_t id) {
    const auto found = std::find(ids.begin(), ids.end(), id);
    if (found != ids.end()) return static_cast<uint32_t>(found - ids.begin());
    ids.push_back(id);
    return static_cast<uint32_t>(ids.size() - 1);
  };
  std::string runs;
  const auto pieces = tree_.Slice(0, tree_.Length());
  for (size_t i = 0; i < pieces.size();) {
    const uint32_t style = pieces[i].style;
    std::u16string text;
    do {
      text.append(tree_.View(pieces[i++]));
    } while (i < pieces.size() && pieces[i].style == style);
    if (!runs.empty()) runs += ",\n";
    runs += "    {\"style\": " + std::to_string(remap(style)) + ", \"text\": " + QuoteJson(EncodeUTF8(text)) + "}";
  }
  std::string paragraphs;
  for (uint32_t id : paragraphs_) {
    if (!paragraphs.empty()) paragraphs += ", ";
    paragraphs += std::to_string(remap(id));
  }
  std::string output = "{\n  \"format\": \"bkfont-rich-text\",\n  \"version\": 1,\n  \"styles\": [\n";
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i) output += ",\n";
    output += "    {";
    bool first = true;
    for (const auto& [name, value] : styles_[ids[i]].properties) {
      if (!first) output += ", ";
      first = false;
      output += QuoteJson(name) + ": " + QuoteJson(value);
    }
    output += "}";
  }
  return output + "\n  ],\n  \"paragraphs\": [" + paragraphs + "],\n  \"runs\": [\n" + runs + "\n  ]\n}\n";
}

bool Document::Load(std::string_view source, std::string& error) {
  if (source.size() > kMaxFileBytes) {
    error = "JSON file exceeds 16 MiB";
    return false;
  }
  Json json;
  if (!ParseJson(source, json, error)) return false;
  const auto *format = json.Find("format"), *version = json.Find("version"), *styles = json.Find("styles"),
             *paragraphs = json.Find("paragraphs"), *runs = json.Find("runs");
  const auto fail = [&](const char* message) { error = message; return false; };
  if (!format || format->type != Json::Type::String || format->string != "bkfont-rich-text" ||
      !version || version->type != Json::Type::Number || version->number != 1)
    return fail("Expected bkfont-rich-text JSON version 1");
  if (!styles || styles->type != Json::Type::Array || styles->array.empty() || styles->array.size() > 4096 ||
      !paragraphs || paragraphs->type != Json::Type::Array || !runs || runs->type != Json::Type::Array ||
      runs->array.size() > 100000) return fail("Invalid style, paragraph or run table");
  Document loaded;
  std::vector<uint32_t> style_ids;
  for (const Json& style : styles->array) {
    if (style.type != Json::Type::Object || style.object.size() > PropertyCatalog().size()) return fail("Invalid style object");
    Properties properties;
    for (const auto& [name, value] : style.object) {
      if (value.type != Json::Type::String) return fail("CSS values must be strings");
      properties.emplace_back(name, value.string);
    }
    bkfont::StyleDeclaration validation;
    if (!BuildDeclaration(properties, validation, error)) return false;
    style_ids.push_back(loaded.Intern(std::move(properties)));
  }
  const auto valid_scope = [&](uint32_t style, bool paragraph) {
    for (const auto& [name, value] : loaded.styles_[style].properties)
      if (FindProperty(name)->paragraph != paragraph) return false;
    return true;
  };
  loaded.paragraphs_.clear();
  for (const Json& paragraph : paragraphs->array) {
    uint32_t id;
    if (!IndexValue(&paragraph, style_ids.size(), id) || !valid_scope(style_ids[id], true)) return fail("Invalid paragraph style index or scope");
    loaded.paragraphs_.push_back(style_ids[id]);
  }
  std::u16string original;
  std::vector<PieceTree::Piece> pieces;
  for (const Json& run : runs->array) {
    uint32_t id;
    const auto* text = run.Find("text");
    if (!text || text->type != Json::Type::String || !IndexValue(run.Find("style"), style_ids.size(), id) ||
        !valid_scope(style_ids[id], false)) return fail("Invalid text run or character style index");
    std::u16string decoded;
    if (!DecodeUTF8(text->string, decoded) || decoded.find(u'\r') != std::u16string::npos) return fail("Text runs require Unicode with LF newlines");
    if (original.size() + decoded.size() > kMaxTextUnits) return fail("Document exceeds 2 Mi UTF-16 units");
    if (!decoded.empty()) pieces.push_back({0, static_cast<Offset>(original.size()), static_cast<Offset>(decoded.size()), style_ids[id]});
    original += decoded;
  }
  loaded.tree_.Reset(std::move(original), pieces);
  if (loaded.paragraphs_.size() != loaded.tree_.LineBreaks() + 1) return fail("Paragraph count does not match text");
  loaded.Select(0, 0);
  *this = std::move(loaded);
  error.clear();
  return true;
}

bool Document::OpenFile(const std::string& path, std::string& error) {
  FILE* file = Open(path, false);
  if (!file) {
    error = "Cannot open file: " + path;
    return false;
  }
  std::string source;
  char buffer[8192];
  size_t count;
  while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0 && source.size() <= kMaxFileBytes) source.append(buffer, count);
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
  if (failed) {
    error = "Cannot read file: " + path;
    return false;
  }
  return Load(source, error);
}

bool Document::SaveFile(const std::string& path, std::string& error) {
  const std::string source = Serialize();
  if (source.size() > kMaxFileBytes) {
    error = "JSON file exceeds 16 MiB";
    return false;
  }
  // Use a sibling temporary file. A failed write leaves the original intact.
  const std::string temporary = path + ".bkfont-tmp";
  FILE* file = Open(temporary, true);
  if (!file) {
    error = "Cannot create file: " + temporary;
    return false;
  }
  const bool written = std::fwrite(source.data(), 1, source.size(), file) == source.size();
  const bool closed = std::fclose(file) == 0;
  if (!written || !closed) {
    error = "Cannot finish writing file: " + temporary;
    return false;
  }
#ifdef _WIN32
  // C rename cannot replace a Windows file; use the OS atomic replacement.
  std::u16string from, to;
  DecodeUTF8(temporary, from);
  DecodeUTF8(path, to);
  if (!MoveFileExW(reinterpret_cast<const wchar_t*>(from.c_str()), reinterpret_cast<const wchar_t*>(to.c_str()),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = "Cannot replace destination file: " + path;
    return false;
  }
#else
  std::error_code code;
  std::filesystem::rename(FilePath(temporary), FilePath(path), code);
  if (code) {
    error = "Cannot replace destination file: " + code.message();
    return false;
  }
#endif
  MarkSaved();
  error.clear();
  return true;
}

bool Document::Validate() const {
  if (!tree_.Validate() || paragraphs_.size() != tree_.LineBreaks() + 1 ||
      selection_.anchor > tree_.Length() || selection_.focus > tree_.Length() || typing_style_ >= styles_.size()) return false;
  for (uint32_t id : paragraphs_)
    if (id >= styles_.size()) return false;
  for (const auto& piece : tree_.Slice(0, tree_.Length()))
    if (piece.style >= styles_.size()) return false;
  return true;
}

} // namespace rich_text
