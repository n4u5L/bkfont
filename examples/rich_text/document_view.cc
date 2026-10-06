#include "document_view.h"

#include <cmath>
#include <limits>

#include "fonts.h"
#include "text/character_break_iterator.h"
#include <unicode/uchar.h>

namespace rich_text {
namespace {

using namespace bkfont;
String ToString(std::u16string_view text) {
  return String(base::span<const UChar>(text.data(), text.size()));
}
PhysicalOffset Offset(float x, float y) {
  return {LayoutUnit(x), LayoutUnit(y)};
}

} // namespace

bkfont::InlinePosition DocumentView::Block::Position(uint32_t offset, bkfont::TextAffinity affinity) const {
  const uint32_t relative = std::clamp(offset, start, end) - start;
  for (size_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes[i];
    if (relative < node.start + node.length || i + 1 == nodes.size() ||
        (relative == node.start + node.length && affinity == TextAffinity::kUpstream))
      return {node.id, std::min(relative - node.start, node.length), affinity};
  }
  return {};
}

void DocumentView::Update(const Document& document, float device_scale, float zoom, bool force) {
  const float scale = device_scale * zoom;
  if (!force && revision_ == document.Revision() && scale_ == scale) return;
  const bool rescale = scale_ != scale || force;
  revision_ = document.Revision();
  scale_ = scale;
  text_ = document.Tree().Text();
  boundaries_.clear();
  boundaries_.push_back(0);
  CharacterBreakIterator breaks(base::span<const UChar>(text_.data(), text_.size()));
  for (int next = breaks.Next(); next != kTextBreakDone; next = breaks.Next()) boundaries_.push_back(next);
  if (boundaries_.back() != text_.size()) boundaries_.push_back(static_cast<uint32_t>(text_.size()));
  page_width_ = 794 * scale;
  const float margin = 64 * scale, content_width = page_width_ - 2 * margin;
  std::vector<Block> next;
  next.reserve(document.Paragraphs().size());
  uint32_t start = 0;
  float y = margin;
  for (size_t i = 0; i < document.Paragraphs().size(); ++i) {
    const size_t newline = text_.find(u'\n', start);
    const uint32_t end = static_cast<uint32_t>(newline == std::u16string::npos ? text_.size() : newline);
    auto pieces = document.Tree().Slice(start, end - start);
    const uint32_t style = document.Paragraphs()[i];
    Block block;
    // A newline shifts paragraph ordinals but does not change the original
    // buffer/style descriptors of the suffix. Match that suffix at its old
    // ordinal instead of destroying/rebuilding every following context.
    const int64_t shift = static_cast<int64_t>(document.Paragraphs().size()) - static_cast<int64_t>(blocks_.size());
    const auto reusable = [&](int64_t old) {
      return !rescale && old >= 0 && old < static_cast<int64_t>(blocks_.size()) && blocks_[old].context &&
             blocks_[old].pieces == pieces && blocks_[old].style == style;
    };
    if (reusable(i))
      block = std::move(blocks_[i]);
    else if (reusable(static_cast<int64_t>(i) - shift))
      block = std::move(blocks_[static_cast<int64_t>(i) - shift]);
    else {
      block.pieces = std::move(pieces);
      block.style = style;
      block.context = std::make_unique<InlineFormattingContext>(Settings(), nullptr);
      auto declaration = DefaultParagraphStyle();
      declaration.Merge(document.GetStyle(style).declaration);
      block.context->SetInlineStyle(block.context->RootObject(), declaration);
      block.context->SetZoomFactors(device_scale, zoom);
      const auto mode = PropertyValue(document.GetStyle(style).properties, "writing-mode", "horizontal-tb");
      block.vertical = mode == "vertical-rl" || mode == "vertical-lr";
      block.rtl = PropertyValue(document.GetStyle(style).properties, "direction", "ltr") == "rtl";
      auto options = block.context->Options();
      options.available_inline_size = LayoutUnit(block.vertical ? 240 * scale : content_width);
      block.context->SetOptions(options);
      uint32_t relative = 0;
      for (const auto& piece : block.pieces) {
        const auto& span = block.context->AppendInline(block.context->RootObject());
        block.context->SetInlineStyle(span, document.GetStyle(piece.style).declaration);
        const auto& node = block.context->AppendText(span, ToString(document.Tree().View(piece)));
        block.nodes.push_back({node.Id(), relative, piece.length});
        relative += piece.length;
      }
      if (block.nodes.empty()) {
        const auto& node = block.context->AppendText(block.context->RootObject(), String(u"\u200b"));
        block.nodes.push_back({node.Id(), 0, 0});
      }
      block.context->UpdateLayout();
      const auto size = block.context->Fragments().SizeInPhysicalCoordinates();
      block.width = std::max(scale, size.width.ToFloat());
      block.height = std::max(28 * scale, size.height.ToFloat());
    }
    block.start = start;
    block.end = end;
    block.x = margin + (block.vertical ? std::max(0.0f, content_width - block.width) : 0);
    block.y = y;
    y += block.height + 14 * scale;
    next.push_back(std::move(block));
    start = end + 1;
  }
  blocks_ = std::move(next);
  page_height_ = std::max(1123 * scale, y + margin);
}

const DocumentView::Block& DocumentView::BlockAt(uint32_t offset) const {
  auto found = std::upper_bound(blocks_.begin(), blocks_.end(), offset,
                                [](uint32_t position, const Block& block) { return position < block.start; });
  return found == blocks_.begin() ? blocks_.front() : *std::prev(found);
}

uint32_t DocumentView::Hit(float x, float y, TextAffinity* affinity) {
  const Block* nearest = &blocks_.front();
  float distance = std::numeric_limits<float>::max();
  for (auto& block : blocks_) {
    const float d = y < block.y ? block.y - y : y > block.y + block.height ? y - block.y - block.height
                                                                           : 0;
    if (d < distance) {
      nearest = &block;
      distance = d;
    }
  }
  const auto position = nearest->context->HitTest(Offset(x - nearest->x, y - nearest->y));
  if (affinity) *affinity = position.affinity;
  for (const auto& node : nearest->nodes)
    if (node.id == position.node) return Snap(nearest->start + node.start + std::min(node.length, position.offset));
  return nearest->start;
}

PhysicalRect DocumentView::Caret(uint32_t offset, TextAffinity affinity) {
  const Block& block = BlockAt(offset);
  auto rect = block.context->CaretRect(block.Position(offset, affinity));
  if (rect.size.height <= LayoutUnit() && rect.size.width <= LayoutUnit())
    rect = PhysicalRect(LayoutUnit(), LayoutUnit(), LayoutUnit(std::max(1.0f, scale_)), LayoutUnit(24 * scale_));
  rect.offset += Offset(block.x, block.y);
  return rect;
}

void DocumentView::Paint(RasterCanvas& raster, float x, float y, float clip_top, float clip_bottom, const Document::Selection& selection,
                         bool marks) {
  CanvasPaintCanvas canvas(&raster);
  PlatformPaint mark_paint(0xff8c9bb0);
  mark_paint.SetAntiAlias(true);
  for (auto& block : blocks_) {
    if (y + block.y + block.height + 32 * scale_ < clip_top || y + block.y - 32 * scale_ > clip_bottom) continue;
    const PhysicalOffset origin = Offset(x + block.x, y + block.y);
    const uint32_t start = std::max(selection.Start(), block.start), end = std::min(selection.End(), block.end);
    if (!selection.Empty() && start < end) {
      const InlineSelection local{block.Position(start), block.Position(end, TextAffinity::kUpstream)};
      for (const auto& rect : block.context->SelectionRectsForPaint(local, origin))
        raster.DrawRect(ScalarRect::MakeXYWH(rect.X().ToFloat(), rect.Y().ToFloat(), rect.Width().ToFloat(), rect.Height().ToFloat()),
                        PlatformPaint(0xffc9ddfa));
    }
    if (!selection.Empty() && selection.Start() <= block.end && selection.End() > block.end) {
      const auto caret = block.context->CaretRect(block.Position(block.end, TextAffinity::kUpstream));
      raster.DrawRect(ScalarRect::MakeXYWH(x + block.x + caret.X().ToFloat(), y + block.y + caret.Y().ToFloat(),
                                           6 * scale_, std::max(24 * scale_, caret.Height().ToFloat())),
                      PlatformPaint(0xffc9ddfa));
    }
    block.context->Paint(&canvas, origin);
    if (marks) {
      // A pilcrow beside the paragraph's end caret, drawn as a path so it
      // never depends on the fonts available for the paragraph.
      const auto caret = block.context->CaretRect(block.Position(block.end, TextAffinity::kUpstream));
      const float line = std::max(18 * scale_, block.vertical ? caret.Width().ToFloat() : caret.Height().ToFloat());
      const float h = std::min(line * 0.62f, 15 * scale_), w = h * 0.62f;
      float left = x + block.x + caret.X().ToFloat(), top = y + block.y + caret.Y().ToFloat();
      if (block.vertical) {
        left += (caret.Width().ToFloat() - w) / 2;
        top += caret.Height().ToFloat() + 2 * scale_;
      } else {
        left += block.rtl ? -w - 2 * scale_ : 2 * scale_;
        top += (caret.Height().ToFloat() - h) / 2;
      }
      const float stem = std::max(1.0f, h * 0.09f), bowl = w * 0.62f;
      ScalarPath path;
      // Same (clockwise) winding as AddRect, so overlaps do not cancel.
      path.MoveTo({left + bowl, top + h * 0.5f});
      path.CubicTo({left - bowl * 0.05f, top + h * 0.5f}, {left - bowl * 0.05f, top}, {left + bowl, top});
      path.Close();
      path.AddRect(ScalarRect::MakeXYWH(left + bowl - stem, top, w - bowl + stem, stem));
      path.AddRect(ScalarRect::MakeXYWH(left + bowl - stem, top, stem, h));
      path.AddRect(ScalarRect::MakeXYWH(left + w - stem, top, stem, h));
      raster.DrawPath(path, mark_paint);
    }
  }
}

std::pair<uint32_t, uint32_t> DocumentView::WordAt(uint32_t offset) const {
  const Block& block = BlockAt(offset);
  if (block.start == block.end) return {block.start, block.end};
  const uint32_t length = block.end - block.start;
  auto* words = WordBreakIterator(base::span<const UChar>(text_.data() + block.start, length));
  if (!words) return {offset, offset};
  // At the paragraph end, select the word before the caret, as Word does.
  const int32_t relative = static_cast<int32_t>(std::min(std::clamp(offset, block.start, block.end) - block.start, length - 1));
  const int32_t start = words->preceding(relative + 1), end = words->following(relative);
  const uint32_t first = block.start + (start == icu::BreakIterator::DONE ? 0 : static_cast<uint32_t>(start));
  const uint32_t last = block.start + (end == icu::BreakIterator::DONE ? length : static_cast<uint32_t>(end));
  return {first, Snap(last)};
}

uint32_t DocumentView::Snap(uint32_t offset) const {
  const auto it = std::lower_bound(boundaries_.begin(), boundaries_.end(), offset);
  return it == boundaries_.end() ? static_cast<uint32_t>(text_.size()) : *it;
}

uint32_t DocumentView::Move(uint32_t offset, int direction, bool word) const {
  if (!word) {
    if (direction > 0) {
      const auto it = std::upper_bound(boundaries_.begin(), boundaries_.end(), offset);
      return it == boundaries_.end() ? static_cast<uint32_t>(text_.size()) : *it;
    }
    const auto it = std::lower_bound(boundaries_.begin(), boundaries_.end(), offset);
    return it == boundaries_.begin() ? 0 : *std::prev(it);
  }
  const auto is_space = [&](uint32_t pos) { return pos < text_.size() && u_isUWhiteSpace(text_[pos]); };
  if (direction > 0) {
    while (offset < text_.size() && !is_space(offset)) offset = Move(offset, 1, false);
    while (offset < text_.size() && is_space(offset)) offset = Move(offset, 1, false);
  } else {
    while (offset && is_space(offset - 1)) offset = Move(offset, -1, false);
    while (offset && !is_space(offset - 1)) offset = Move(offset, -1, false);
  }
  return offset;
}

} // namespace rich_text
