// A multilingual article laid out in vertical-rl writing mode.
// bkfont shapes and rasterizes the text; OpenGL presents the CPU pixels.

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "fonts.h"
#include "font/font_platform_data.h"
#include "font/simple_font_data.h"
#include "font/text_fragment_paint_info.h"
#include "platform/font_manager.h"
#include "shaping/shape_result_spacing.h"
#include "text/bidi_paragraph.h"

namespace {

using namespace bkfont;

// BGRA is core in OpenGL 1.2 but absent from Windows' OpenGL 1.1 headers.
constexpr GLenum kBGRA = 0x80E1;
constexpr float kMargin = 56;
constexpr char kArticleFontFamily[] = "Source Han Serif SC";

// Resolve the bundled Regular face directly, retaining the library's system
// fallback for characters outside its coverage. The file is loaded only once;
// FontCache shares sized font data across article layouts.
class ArticleFontSelector final : public FontSelector {
public:
  explicit ArticleFontSelector(std::shared_ptr<Typeface> typeface)
      : typeface_(std::move(typeface)), family_(kArticleFontFamily) {}

  std::shared_ptr<const FontData> GetFontData(const FontDescription& description,
                                          const FontFamily& family) override {
    if (family.FamilyName() != family_) return nullptr;
    auto platform_data = std::make_shared<FontPlatformData>(
        typeface_, family_.GetString(), description.EffectiveFontSize(), false, false,
        description.TextRendering(), description.ResolveFontFeatures(), description.Orientation());
    return FontCache::Get().FontDataFromFontPlatformData(std::move(platform_data));
  }

  bool IsPlatformFamilyMatchAvailable(const FontDescription& description,
                                      const FontFamily& family) override {
    return family.FamilyName() == family_ ||
           FontCache::Get().IsPlatformFamilyMatchAvailable(description, family.FamilyName());
  }

  // This standalone example has one immutable, synchronously loaded face;
  // there is no document, asynchronous font loading, or usage reporting.
  void WillUseFontData(const FontDescription&, const FontFamily&, const String&) override {}
  void WillUseRange(const FontDescription&, const AtomicString&, const FontDataForRangeSet&) override {}
  unsigned Version() const override { return 1; }
  void ReportSuccessfulFontFamilyMatch(const AtomicString&) override {}
  void ReportFailedFontFamilyMatch(const AtomicString&) override {}
  void ReportSuccessfulLocalFontMatch(const AtomicString&) override {}
  void ReportFailedLocalFontMatch(const AtomicString&) override {}
  void ReportNotDefGlyph() const override {}
  void ReportEmojiSegmentGlyphCoverage(unsigned, unsigned) override {}
  void RegisterForInvalidationCallbacks(FontSelectorClient*) override {}
  void UnregisterForInvalidationCallbacks(FontSelectorClient*) override {}
  void FontCacheInvalidated() override {}
  ExecutionContext* GetExecutionContext() const override { return nullptr; }
  FontFaceCache* GetFontFaceCache() override { return nullptr; }

private:
  const std::shared_ptr<Typeface> typeface_;
  const AtomicString family_;
};

// Original multilingual plot summary, not quotations from the novel.
constexpr const char16_t* kTitle = u"无职转生：异世界的第二次人生";
constexpr const char16_t* kParagraphs[] = {
    u"前世的生命在一场意外中结束，鲁迪乌斯却带着记忆，在剑与魔法的世界重新睁开眼睛。"
    u"ルーデウス・グレイラット。それが、この世界で与えられた新しい名前だった。"
    u"โลกใหม่นี้มีเวทมนตร์ แต่ความกลัวจากชีวิตเดิมยังคงอยู่ในใจของเขา. "
    u"새로운 가족의 품에서 자라면서도, 그는 지난 삶의 후회와 쉽게 작별하지 못한다. "
    u"第二次人生并没有替他抹去过去。A new life begins, but the past is not forgotten. "
    u"新的机会，也意味着从头学习如何面对世界。",

    u"家中的魔法书成了最早的窗户。鲁迪乌斯反复尝试施法，逐渐显露出惊人的天赋。"
    u"家庭教師のロキシーは、魔術だけでなく、家の外へ踏み出すきっかけも与えてくれた。"
    u"เมื่อได้ออกไปเห็นทุ่งกว้าง เขาจึงเริ่มรู้ว่าโลกไม่ได้มีเพียงห้องที่ตนคุ้นเคย. "
    u"마을에서 만난 실피는 소중한 친구가 되고, 함께 보내는 시간은 그의 일상을 조금씩 바꾼다. "
    u"One step at a time. 从走出房门的那一步开始，水球、咒文、田野与朋友，"
    u"让这次重新开始渐渐有了真实的重量。",

    u"后来，他离开故乡，成为艾莉丝的家庭教师。大小姐的脾气让最初的授课屡屡受挫。"
    u"エリスは簡単には心を開かない。それでも、読み書きや魔術を教える日々が二人の距離を縮めていく。"
    u"การสอนไม่ได้ต้องการเพียงความรู้ แต่ยังต้องอาศัยความอดทนและความเข้าใจ. "
    u"루데우스도 모든 답을 아는 것은 아니다. 실패한 뒤 다시 방법을 찾으며 그 역시 배워 간다. "
    u"他以为生活会这样慢慢向前，却不知道，一场突如其来的灾难即将打断所有安排。",

    u"转移灾害降临，熟悉的土地与人群骤然散落。鲁迪乌斯和艾莉丝被抛到遥远的魔大陆。"
    u"見知らぬ大地で出会ったルイジェルドは、二人を守り、故郷へ帰る旅に同行する。"
    u"ทั้งสามต้องเดินทางผ่านเมืองที่ไม่คุ้นเคย รับงานหาเงิน และเรียนรู้ที่จะเชื่อใจกัน. "
    u"낯선 이름과 소문만으로 사람을 판단할 수 없다는 사실도 길 위에서 조금씩 드러난다. "
    u"归途不再只是地图上的一条线：每次决定，都关系着同伴的安全，也暴露出鲁迪乌斯尚未成熟的一面。",

    u"漫长的旅途中，他终于与父亲保罗重逢。然而，父亲仍在寻找失散的家人，重逢也伴随着争执。"
    u"自分が見てきた苦労と、相手が抱えてきた苦しみは同じではない。親子は衝突し、少しずつ歩み寄る。"
    u"การได้พบกันอีกครั้งไม่ได้ทำให้บาดแผลหายไปทันที แต่ทำให้ทั้งคู่มีโอกาสรับฟังกัน. "
    u"집으로 돌아간다는 말의 의미도 달라진다. 돌아갈 장소만큼, 다시 만나야 할 사람들이 중요해진다. "
    u"离散的家人、未完成的约定，以及仍然遥远的故乡，把冒险变成了一段必须承担责任的旅程。",

    u"回乡之后，新的离别又让鲁迪乌斯陷入低谷。为了寻找母亲，他继续旅行，后来进入拉诺亚魔法大学。"
    u"学園で待っていたのは、新しい仲間と、姿や立場を変えた懐かしい縁だった。"
    u"เมื่อได้พบคนสำคัญอีกครั้ง เขาจึงค่อย ๆ เรียนรู้ว่าตนไม่จำเป็นต้องรับทุกอย่างไว้เพียงลำพัง. "
    u"실피와의 재회는 멈춰 있던 일상에 다시 온기를 가져온다. 그러나 가족을 찾는 여정은 아직 끝나지 않았다. "
    u"《无职转生》的这段故事，从重新出生走向重新建立关系。Keep moving forward. "
    u"跌倒后再站起来，是这次人生仍在学习的事；魔法能够改变许多事，而信任与成长仍需要时间。",
};

struct VerticalTextOptions {
  TextSpacingTrim punctuation_spacing = TextSpacingTrim::kSpaceAll;
  bool center_punctuation = false;
  float letter_spacing_em = 0.08f;

  bool operator==(const VerticalTextOptions&) const = default;
};

Font MakeVerticalFont(float size, const VerticalTextOptions& options,
                      const std::shared_ptr<FontSelector>& font_selector) {
  FontDescription description;
  // Keep Han text and punctuation in the bundled CJK face, whose vert/vrt2,
  // VORG and vhea/vmtx tables supply vertical glyphs, origins and advances.
  auto families = SharedFontFamily::Create(AtomicString("serif"), FontFamily::Type::kGenericFamily);
  for (const char* family : {"SimSun", "Microsoft YaHei", "Noto Sans CJK SC", "Noto Sans SC"}) {
    families = SharedFontFamily::Create(AtomicString(family), FontFamily::Type::kFamilyName, std::move(families));
  }
  families = SharedFontFamily::Create(AtomicString(kArticleFontFamily), FontFamily::Type::kFamilyName,
                                      std::move(families));
  description.SetFamily(*families);
  description.SetGenericFamily(FontDescription::kSerifFamily);
  description.SetLocale(LayoutLocale::Get(AtomicString("zh-Hans")));
  description.SetComputedSize(size);
  description.SetSpecifiedSize(size);
  description.SetOrientation(FontOrientation::kVerticalMixed);
  description.SetTextSpacingTrim(options.punctuation_spacing);
  description.SetLetterSpacing(Length(size * options.letter_spacing_em, Length::kFixed));
  return Font(description, font_selector);
}

struct ShapedText {
  String text;
  std::vector<std::unique_ptr<ShapeResultView>> runs;
  float advance = 0;
};

std::unique_ptr<ShapedText> ShapeText(String text, const Font& font, const VerticalTextOptions& options) {
  text.Ensure16Bit();
  auto result = std::make_unique<ShapedText>();
  result->text = std::move(text);

  // Preserve punctuation context across words. PlainTextNode intentionally
  // splits CJK text for the word cache used by Canvas fillText; article
  // shaping should instead pass complete directional runs to HarfBuzz.
  BidiParagraph bidi;
  BidiParagraph::Runs runs;
  if (bidi.SetParagraph(result->text, TextDirection::kLtr)) {
    bidi.GetVisualRuns(result->text, &runs);
  } else {
    runs.emplace_back(0, result->text.length(), 0);
  }
  HarfBuzzShaper shaper(result->text);
  ShapeResultSpacing<String> spacing(result->text);
  spacing.SetSpacing(font.GetFontDescription());
  for (const BidiParagraph::Run& run : runs) {
    auto shape = shaper.Shape(&font, run.Direction(), run.start, run.end);
    if (options.center_punctuation) {
      // Center the final vertical glyphs within their cells before adding
      // tracking. Brackets and sideways Latin punctuation retain font layout.
      shape->ApplyVerticalPunctuationCentering(result->text);
    }
    if (spacing.HasSpacing()) {
      // HarfBuzzShaper does not apply CSS letter-spacing. Apply it to the
      // result used both for column fitting and painting. The upstream
      // spacing stage excludes cursive scripts such as Arabic.
      shape->ApplySpacing(spacing);
    }
    auto view = ShapeResultView::Create(shape.get());
    result->advance += view->Width();
    result->runs.push_back(std::move(view));
  }
  return result;
}

struct Column {
  std::unique_ptr<ShapedText> text;
  float offset;
  float indent;
  bool title;
};

struct ArticleLayout {
  ArticleLayout(float size, float height, VerticalTextOptions text_options,
                const std::shared_ptr<FontSelector>& font_selector)
      : font_size(size), inline_size(height), options(text_options),
        body_font(MakeVerticalFont(size, options, font_selector)),
        title_font(MakeVerticalFont(size * 1.5f, options, font_selector)) {
    AppendParagraph(String(kTitle), true);
    for (const char16_t* paragraph : kParagraphs) {
      AppendParagraph(String(paragraph), false);
    }
  }

  void AppendParagraph(const String& paragraph, bool title) {
    const Font& font = title ? title_font : body_font;
    const float size = title ? font_size * 1.5f : font_size;
    const LayoutLocale* locale = font.GetFontDescription().Locale();
    LazyLineBreakIterator breaks(paragraph, locale);
    breaks.SetStrictness(LineBreakStrictness::kStrict);
    // Even when a word is too long, retain punctuation prohibitions at the
    // column boundaries. BreakCharacter would discard those prohibitions.
    LazyLineBreakIterator emergency_breaks(paragraph, locale, LineBreakType::kBreakAll);
    emergency_breaks.SetStrictness(LineBreakStrictness::kStrict);
    unsigned start = 0;
    while (start < paragraph.length()) {
      const float indent = !title && start == 0 ? size * 2 : 0;
      const float available = std::max(size, inline_size - indent);
      unsigned end = start;
      std::unique_ptr<ShapedText> line;
      float advance = 0;

      const auto fit = [&](const LazyLineBreakIterator& iterator) {
        line.reset();
        for (unsigned next = iterator.NextBreakOpportunity(start + 1);;) {
          auto candidate = ShapeText(paragraph.Substring(start, next - start), font, options);
          const float candidate_advance = candidate->advance;
          if (line && candidate_advance > available) {
            break;
          }
          end = next;
          advance = candidate_advance;
          line = std::move(candidate);
          if (advance > available || end == paragraph.length()) {
            break;
          }
          next = iterator.NextBreakOpportunity(end + 1);
        }
      };

      // Prefer Unicode line-break opportunities so words and CJK punctuation
      // stay together. Only split a word when it cannot fit an entire column.
      fit(breaks);
      if (advance > available) {
        fit(emergency_breaks);
      }
      columns.push_back({std::move(line), width, indent, title});
      width += size * 1.65f;
      start = end;
      while (start < paragraph.length() && LazyLineBreakIterator::IsBreakableSpace(paragraph[start])) {
        ++start;
      }
    }
    width += size * (title ? 0.8f : 0.45f);
  }

  float font_size;
  float inline_size;
  VerticalTextOptions options;
  Font body_font;
  Font title_font;
  std::vector<Column> columns;
  float width = 0;
};

struct WindowState {
  float zoom = 1;
  float scroll = 0;
  float max_scroll = 0;
  float page_width = 0;
  bool dirty = true;
  VerticalTextOptions text_options;
  std::shared_ptr<FontSelector> font_selector;
  std::unique_ptr<ArticleLayout> article;
};

WindowState& State(GLFWwindow* window) {
  return *static_cast<WindowState*>(glfwGetWindowUserPointer(window));
}

void UpdateWindowTitle(GLFWwindow* window) {
  const VerticalTextOptions& options = State(window).text_options;
  char title[256];
  std::snprintf(title, sizeof(title),
      "bkfont | Source Han Serif SC | C: punctuation %s | T: spacing %s | [/]: gap %.2f em | +/-: size | Wheel/Arrows: scroll | R: reset",
      options.center_punctuation ? "centered" : "font default",
      options.punctuation_spacing == TextSpacingTrim::kSpaceAll ? "full" : "trimmed",
      options.letter_spacing_em);
  glfwSetWindowTitle(window, title);
}

void OnKey(GLFWwindow* window, int key, int, int action, int) {
  if (action != GLFW_PRESS && action != GLFW_REPEAT) {
    return;
  }
  WindowState& state = State(window);
  switch (key) {
  case GLFW_KEY_ESCAPE:
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    break;
  case GLFW_KEY_EQUAL:
  case GLFW_KEY_KP_ADD:
    state.zoom = std::min(1.5f, state.zoom + 0.1f);
    break;
  case GLFW_KEY_MINUS:
  case GLFW_KEY_KP_SUBTRACT:
    state.zoom = std::max(0.6f, state.zoom - 0.1f);
    break;
  case GLFW_KEY_LEFT:
  case GLFW_KEY_PAGE_DOWN:
    state.scroll += state.page_width * 0.8f;
    break;
  case GLFW_KEY_RIGHT:
  case GLFW_KEY_PAGE_UP:
    state.scroll -= state.page_width * 0.8f;
    break;
  case GLFW_KEY_HOME:
    state.scroll = 0;
    break;
  case GLFW_KEY_END:
    state.scroll = state.max_scroll;
    break;
  case GLFW_KEY_C:
    if (action != GLFW_PRESS) return;
    state.text_options.center_punctuation = !state.text_options.center_punctuation;
    break;
  case GLFW_KEY_T:
    if (action != GLFW_PRESS) return;
    state.text_options.punctuation_spacing = state.text_options.punctuation_spacing == TextSpacingTrim::kSpaceAll
                                                ? TextSpacingTrim::kNormal : TextSpacingTrim::kSpaceAll;
    break;
  case GLFW_KEY_LEFT_BRACKET:
    state.text_options.letter_spacing_em = std::max(0.0f, state.text_options.letter_spacing_em - 0.02f);
    break;
  case GLFW_KEY_RIGHT_BRACKET:
    state.text_options.letter_spacing_em = std::min(0.3f, state.text_options.letter_spacing_em + 0.02f);
    break;
  case GLFW_KEY_R:
    state.zoom = 1;
    state.scroll = 0;
    state.text_options = VerticalTextOptions();
    break;
  default:
    return;
  }
  state.dirty = true;
  UpdateWindowTitle(window);
}

void DrawColumn(CanvasPaintCanvas* canvas, const Column& column, const Font& font,
                float x, float top, ColorARGB color) {
  PaintCanvasAutoRestore restore(canvas, true);
  // Rotate the inline coordinate system clockwise. DrawTextBlobs then
  // counter-rotates upright CJK runs and keeps Latin runs sideways.
  ScalarMatrix writing_mode;
  writing_mode.SetSinCos(1, 0, x, top);
  canvas->Concat(writing_mode);
  float advance = 0;
  for (const auto& view : column.text->runs) {
    const TextFragmentPaintInfo info{StringView(column.text->text), view->StartIndex(), view->EndIndex(), view.get()};
    font.DrawText(canvas, info, PointF(x + advance, top), kInvalidNodeId, PlatformPaint(color));
    advance += view->Width();
  }
}

void RenderArticle(Bitmap* bitmap, float scale_x, float scale_y, WindowState& state) {
  const float width = bitmap->GetPixmap().Width() / scale_x;
  const float height = bitmap->GetPixmap().Height() / scale_y;
  const float font_size = 22 * state.zoom;
  const float inline_size = std::max(font_size * 4, height - 2 * kMargin);
  if (!state.article || state.article->font_size != font_size || state.article->inline_size != inline_size ||
      state.article->options != state.text_options) {
    state.article = std::make_unique<ArticleLayout>(font_size, inline_size, state.text_options, state.font_selector);
  }
  const ArticleLayout& article = *state.article;
  const float first_x = width - kMargin - font_size * 1.5f;
  state.page_width = std::max(font_size, first_x - kMargin);
  state.max_scroll = std::max(0.0f, article.width - state.page_width);
  state.scroll = std::clamp(state.scroll, 0.0f, state.max_scroll);

  RasterCanvas raster(bitmap->GetPixmap());
  raster.Clear(0xFFF7F4ED);
  raster.Scale(scale_x, scale_y);
  raster.ClipRect({kMargin * 0.5f, kMargin * 0.5f, width - kMargin * 0.5f, height - kMargin * 0.5f}, false);
  CanvasPaintCanvas canvas(&raster);
  for (const Column& column : article.columns) {
    const float x = first_x - column.offset + state.scroll;
    if (x < -font_size * 2 || x > width) {
      continue;
    }
    DrawColumn(&canvas, column, column.title ? article.title_font : article.body_font,
               x, kMargin + column.indent, column.title ? 0xFF27776B : 0xFF253A3C);
  }
  raster.Flush();
}

void Present(const Pixmap& pixels, GLuint texture) {
  glViewport(0, 0, pixels.Width(), pixels.Height());
  glDisable(GL_BLEND);
  glDisable(GL_DITHER);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels.Width(), pixels.Height(), 0,
              kBGRA, GL_UNSIGNED_BYTE, pixels.Addr());
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  // RasterCanvas rows start at the top. Flip the texture vertically here.
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

bool DrawFrame(GLFWwindow* window, GLuint texture) {
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  if (width <= 0 || height <= 0) {
    return true; // A minimized window has no drawable framebuffer.
  }
  GLint max_texture_size = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  if (width > max_texture_size || height > max_texture_size) {
    std::fprintf(stderr, "Framebuffer exceeds the OpenGL texture size limit.\n");
    return false;
  }
  float scale_x = 1;
  float scale_y = 1;
  glfwGetWindowContentScale(window, &scale_x, &scale_y);
  Bitmap bitmap(ColorType::kN32, width, height);
  if (bitmap.IsEmpty()) {
    return false;
  }
  RenderArticle(&bitmap, scale_x, scale_y, State(window));
  Present(bitmap.GetPixmap(), texture);
  glfwSwapBuffers(window);
  return true;
}

} // namespace

int main() {
  glfwSetErrorCallback([](int code, const char* message) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, message);
  });
  if (!glfwInit()) {
    return 1;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_ALPHA_BITS, 8);
  glfwWindowHint(GLFW_DEPTH_BITS, 0);
  glfwWindowHint(GLFW_STENCIL_BITS, 0);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  GLFWwindow* window = glfwCreateWindow(1120, 720,
      "bkfont | Vertical article | +/-: size | Wheel / Left / Right: scroll | R: reset | Esc: close",
      nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  glfwSetWindowSizeLimits(window, 640, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);
  InitializeFonts();
  auto typeface = FontCache::Get().GetFontManager()->MakeFromFile(String::FromUTF8(BKFONT_EXAMPLE_FONT_PATH));
  if (!typeface) {
    std::fprintf(stderr, "Could not load bundled font: %s\n", BKFONT_EXAMPLE_FONT_PATH);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }
  WindowState state;
  state.font_selector = std::make_shared<ArticleFontSelector>(std::move(typeface));
  glfwSetWindowUserPointer(window, &state);
  UpdateWindowTitle(window);
  glfwSetKeyCallback(window, OnKey);
  glfwSetScrollCallback(window, [](GLFWwindow* w, double x, double y) {
    WindowState& state = State(w);
    state.scroll += static_cast<float>(y != 0 ? -y : x) * 80;
    state.dirty = true;
  });
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) { State(w).dirty = true; });
  glfwSetWindowContentScaleCallback(window, [](GLFWwindow* w, float, float) { State(w).dirty = true; });
  glfwSetWindowRefreshCallback(window, [](GLFWwindow* w) { State(w).dirty = true; });

  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  // GL_CLAMP_TO_EDGE is core in OpenGL 1.2.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);

  bool ok = true;
  while (!glfwWindowShouldClose(window)) {
    if (state.dirty) {
      state.dirty = false;
      if (!DrawFrame(window, texture)) {
        ok = false;
        break;
      }
    }
    glfwWaitEvents();
  }

  glDeleteTextures(1, &texture);
  glfwDestroyWindow(window);
  glfwTerminate();
  return ok ? 0 : 1;
}

