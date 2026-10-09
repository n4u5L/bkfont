#include "document_view.h"

#include <cassert>
#include <cmath>
#include <string_view>
#include <vector>

#include "fonts.h"
#include "text/character_break_iterator.h"
#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/utf16.h>

namespace rich_text {
namespace {

using namespace bkit;
String ToString(std::u16string_view text) {
  return String(base::span<const UChar>(text.data(), text.size()));
}
PhysicalOffset Offset(float x, float y) {
  return {LayoutUnit(x), LayoutUnit(y)};
}
// PaintRect() in highlight_painter.cc: fills the pixel-snapped rect, unless
// the rect or its snapped rect is empty.
void FillPixelSnapped(RasterCanvas& raster, const PhysicalRect& rect, const PlatformPaint& paint) {
  if (rect.size.IsEmpty()) return;
  const Rect snapped = ToPixelSnappedRect(rect);
  if (snapped.IsEmpty()) return;
  raster.DrawRect(ScalarRect::MakeXYWH(static_cast<float>(snapped.x()), static_cast<float>(snapped.y()),
                                       static_cast<float>(snapped.width()), static_cast<float>(snapped.height())),
                  paint);
}

// Pieces of one paragraph, joined while they have the same character style.
struct StyleRun {
  uint32_t style;
  std::u16string text;
};

std::vector<StyleRun> StyleRuns(const Document& document, uint32_t start, uint32_t end) {
  const auto& tree = document.Tree();
  std::vector<StyleRun> runs;
  for (const auto& piece : tree.Slice(start, end - start)) {
    if (runs.empty() || runs.back().style != piece.style) runs.push_back(StyleRun{piece.style, {}});
    runs.back().text.append(tree.View(piece));
  }
  return runs;
}

// The offset after the last character of `paragraph`, before its newline.
uint32_t ParagraphEnd(const Document& document, wtf_size_t paragraph) {
  const auto& tree = document.Tree();
  return paragraph + 1 < document.Paragraphs().size() ? tree.LineStart(paragraph + 1) - 1 : tree.Length();
}

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

} // namespace

bkit::InlinePosition DocumentView::Block::Position(uint32_t offset, bkit::TextAffinity affinity) const {
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
  if (!force && version_ == document.EditVersion() && scale_ == scale) return;
  const bool rescale = scale_ != scale || force;
  // The painted highlight may move or vanish with its paragraphs: repaint
  // where it was painted, then treat the page as showing none.
  if (!painted_selection_.Empty()) DamageRange(painted_selection_.Start(), painted_selection_.End());
  painted_selection_ = {};
  std::span<const Document::Edit> edits;
  const bool incremental = !rescale && document.EditsSince(version_, edits);
  version_ = document.EditVersion();
  scale_ = scale;
  page_width_ = 794 * scale;
  wtf_size_t first_dirty = blocks_.size(), dirty_end = 0;
  if (incremental) {
    // Replay the edits on the paragraph list. The first new paragraphs of an
    // edit keep the old ones' layouts, stale, so that Refresh() can edit them
    // in place; the other removed paragraphs leave damage where they were,
    // the other inserted ones start empty and are built below.
    for (const auto& edit : edits) {
      if (edit.paragraph + edit.removed > blocks_.size()) {
        blocks_.clear(); // Out of step with the document; rebuilt below.
        break;
      }
      first_dirty = std::min(first_dirty, static_cast<wtf_size_t>(edit.paragraph));
      // Translate the dirty range through each splice. Later edits can move
      // or remove paragraphs dirtied by an earlier edit in the same update.
      dirty_end = std::max(static_cast<wtf_size_t>(edit.paragraph + edit.inserted),
                           dirty_end > edit.paragraph + edit.removed ? dirty_end - edit.removed + edit.inserted : 0);
      const wtf_size_t kept = std::min(edit.removed, edit.inserted), first = edit.paragraph + kept;
      for (wtf_size_t i = edit.paragraph; i < first; ++i) blocks_[i].stale = true;
      for (wtf_size_t i = first; i < edit.paragraph + edit.removed; ++i) {
        if (blocks_[i].context) DamageRows(blocks_[i].y, blocks_[i].y + blocks_[i].height);
        words_ -= blocks_[i].words;
      }
      blocks_.EraseAt(first, edit.removed - kept);
      if (edit.inserted > kept) {
        const wtf_size_t size = blocks_.size();
        blocks_.Grow(size + edit.inserted - kept);
        std::move_backward(blocks_.begin() + first, blocks_.begin() + size, blocks_.end());
        for (wtf_size_t i = first; i < edit.paragraph + edit.inserted; ++i) blocks_[i] = Block{};
      }
    }
  }
  if (!incremental || blocks_.size() != document.Paragraphs().size()) {
    // Another document, a new scale or a gap in the edit log.
    damage_full_ = true;
    blocks_.clear();
    blocks_.Grow(document.Paragraphs().size());
    first_dirty = 0;
    dirty_end = blocks_.size();
    words_ = 0;
  }
  // As with BlockNode's cached layout results, unchanged paragraphs keep
  // their layout. Start at the first edit and stop once the remaining suffix
  // has both its old text offsets and its old position.
  const float margin = 64 * scale, content_width = page_width_ - 2 * margin, gap = 14 * scale;
  uint32_t start = first_dirty ? blocks_[first_dirty - 1].end + 1 : 0;
  float y = first_dirty ? blocks_[first_dirty - 1].y + (blocks_[first_dirty - 1].height + gap) : margin;
  for (wtf_size_t i = first_dirty; i < blocks_.size(); ++i) {
    Block& block = blocks_[i];
    const bool built = !block.context || block.stale;
    if (i >= dirty_end && !built && block.start == start && block.y == y) break;
    const size_t old_words = block.words;
    if (block.stale) {
      if (block.context) DamageRows(block.y, block.y + block.height); // Where it was painted.
      block.stale = false;
      if (!Refresh(block, document, i, start)) block = Block{};
    }
    if (!block.context) Build(block, document, i, start, device_scale, zoom);
    const float old_x = block.x, old_y = block.y;
    block.start = start;
    block.end = start + static_cast<uint32_t>(block.text.size());
    block.x = margin + (block.vertical ? std::max(0.0f, content_width - block.width) : 0);
    block.y = y;
    if (built)
      DamageRows(block.y, block.y + block.height);
    else if (old_x != block.x || old_y != block.y) {
      DamageRows(old_y, old_y + block.height);
      DamageRows(block.y, block.y + block.height);
    }
    words_ -= old_words;
    words_ += block.words;
    y += block.height + gap;
    start = block.end + 1;
  }
  const float content_end = blocks_.empty() ? margin : blocks_.back().y + (blocks_.back().height + gap);
  const float page_height = std::max(1123 * scale, content_end + margin);
  // The page end, with the crop marks in its bottom margin.
  if (page_height != page_height_)
    DamageRows(std::min(page_height, page_height_) - margin - 12 * scale, std::max(page_height, page_height_));
  page_height_ = page_height;
}

void DocumentView::SetParagraphStyle(Block& block, const Document& document, wtf_size_t paragraph) {
  const uint32_t style = document.Paragraphs()[paragraph];
  block.style = style;
  auto declaration = DefaultParagraphStyle();
  declaration.Merge(document.GetStyle(style).declaration);
  block.context->SetInlineStyle(block.context->RootObject(), declaration);
  const auto mode = PropertyValue(document.GetStyle(style).properties, "writing-mode", "horizontal-tb");
  block.vertical = mode == "vertical-rl" || mode == "vertical-lr";
  block.rtl = PropertyValue(document.GetStyle(style).properties, "direction", "ltr") == "rtl";
  auto options = block.context->Options();
  options.available_inline_size = LayoutUnit(block.vertical ? 240 * scale_ : page_width_ - 128 * scale_);
  block.context->SetOptions(options);
}

void DocumentView::Build(Block& block, const Document& document, wtf_size_t paragraph, uint32_t start, float device_scale,
                         float zoom) {
  const uint32_t end = ParagraphEnd(document, paragraph);
  block.context = std::make_unique<InlineFormattingContext>(Settings(), nullptr);
  block.context->SetZoomFactors(device_scale, zoom);
  SetParagraphStyle(block, document, paragraph);
  block.text.clear();
  block.text.reserve(end - start);
  for (const auto& run : StyleRuns(document, start, end)) {
    const auto& span = block.context->AppendInline(block.context->RootObject());
    block.context->SetInlineStyle(span, document.GetStyle(run.style).declaration);
    const auto& node = block.context->AppendText(span, ToString(run.text));
    block.nodes.push_back(
        NodeMap{node.Id(), static_cast<uint32_t>(block.text.size()), static_cast<uint32_t>(run.text.size()), run.style});
    block.text += run.text;
  }
  if (block.nodes.empty()) {
    const auto& node = block.context->AppendText(block.context->RootObject(), String(u"\u200b"));
    block.nodes.push_back(NodeMap{node.Id(), 0, 0, NodeMap::kNoStyle});
  }
  Measure(block, /*text_changed=*/true);
}

bool DocumentView::Refresh(Block& block, const Document& document, wtf_size_t paragraph, uint32_t start) {
  if (!block.context) return false;
  const std::vector<StyleRun> runs = StyleRuns(document, start, ParagraphEnd(document, paragraph));
  auto& context = *block.context;
  if (block.style != document.Paragraphs()[paragraph]) SetParagraphStyle(block, document, paragraph);
  const bool placeholder = block.nodes.size() == 1 && block.nodes.front().style == NodeMap::kNoStyle;
  if (runs.empty()) {
    if (!placeholder) {
      for (const auto& node : block.nodes) context.Remove(*context.Find(node.id)->Parent());
      block.nodes.clear();
      const auto& node = context.AppendText(context.RootObject(), String(u"\u200b"));
      block.nodes.push_back(NodeMap{node.Id(), 0, 0, NodeMap::kNoStyle});
    }
    const bool text_changed = !block.text.empty();
    block.text.clear();
    Measure(block, text_changed);
    return true;
  }
  if (placeholder) {
    context.Remove(*context.Find(block.nodes.front().id));
    block.nodes.clear();
  }

  // Reconcile style runs with the existing tree. A local format operation
  // splits/merges runs without discarding the paragraph or changing the IDs
  // of its unaffected prefix and suffix. Only the differing middle is edited.
  const auto unchanged = [&](size_t old_index, size_t new_index) {
    const NodeMap& node = block.nodes[static_cast<wtf_size_t>(old_index)];
    return node.style == runs[new_index].style &&
           std::u16string_view(block.text).substr(node.start, node.length) == runs[new_index].text;
  };
  const size_t old_count = block.nodes.size(), new_count = runs.size();
  size_t prefix = 0, suffix = 0;
  while (prefix < std::min(old_count, new_count) && unchanged(prefix, prefix)) ++prefix;
  while (suffix < std::min(old_count, new_count) - prefix &&
         unchanged(old_count - suffix - 1, new_count - suffix - 1)) ++suffix;
  const size_t kept = std::min(old_count - prefix - suffix, new_count - prefix - suffix);
  const InlineObject* before = suffix ? context.Find(block.nodes[static_cast<wtf_size_t>(old_count - suffix)].id)->Parent()
                                     : nullptr;
  Vector<NodeMap> nodes;
  nodes.ReserveInitialCapacity(new_count);
  std::u16string text;
  text.reserve(block.text.size());
  for (size_t i = 0; i < new_count; ++i) {
    const bool in_suffix = i >= new_count - suffix;
    const bool retained = i < prefix + kept || in_suffix;
    NodeMap node;
    if (retained) {
      const size_t old_index = in_suffix ? old_count - (new_count - i) : i;
      node = block.nodes[static_cast<wtf_size_t>(old_index)];
      // The prefix/suffix matched both text and style. In the changed middle,
      // let style invalidation decide whether to repaint, lay out or reshape.
      if (i >= prefix && !in_suffix) {
        const auto& object = *context.Find(node.id);
        if (node.style != runs[i].style)
          context.SetInlineStyle(*object.Parent(), document.GetStyle(runs[i].style).declaration);
        const std::u16string_view old_text = std::u16string_view(block.text).substr(node.start, node.length);
        const std::u16string_view new_text = runs[i].text;
        if (old_text != new_text) {
          size_t common_start = 0, common_end = 0;
          const size_t limit = std::min(old_text.size(), new_text.size());
          while (common_start < limit && old_text[common_start] == new_text[common_start]) ++common_start;
          while (common_end < limit - common_start &&
                 old_text[old_text.size() - 1 - common_end] == new_text[new_text.size() - 1 - common_end]) ++common_end;
          context.ReplaceText(object, static_cast<unsigned>(common_start),
                              static_cast<unsigned>(old_text.size() - common_start - common_end),
                              ToString(new_text.substr(common_start, new_text.size() - common_start - common_end)));
        }
      }
    } else {
      const auto& span = context.AppendInline(context.RootObject());
      context.SetInlineStyle(span, document.GetStyle(runs[i].style).declaration);
      const auto& object = context.AppendText(span, ToString(runs[i].text));
      if (before) {
        [[maybe_unused]] const bool moved = context.Move(span, context.RootObject(), before);
        assert(moved);
      }
      node.id = object.Id();
    }
    node.start = static_cast<uint32_t>(text.size());
    node.length = static_cast<uint32_t>(runs[i].text.size());
    node.style = runs[i].style;
    nodes.push_back(node);
    text += runs[i].text;
  }
  for (size_t i = prefix + kept; i < old_count - suffix; ++i)
    context.Remove(*context.Find(block.nodes[static_cast<wtf_size_t>(i)].id)->Parent());
  block.nodes = std::move(nodes);
  const bool text_changed = text != block.text;
  if (text_changed) block.text = std::move(text);
  Measure(block, text_changed);
  return true;
}

void DocumentView::Measure(Block& block, bool text_changed) {
  block.context->UpdateLayout();
  const auto size = block.context->Fragments().SizeInPhysicalCoordinates();
  block.width = std::max(scale_, size.width.ToFloat());
  block.height = std::max(28 * scale_, size.height.ToFloat());
  if (!text_changed) return;
  // A newline is always a grapheme boundary, so paragraphs break alone.
  block.boundaries.Shrink(0);
  block.boundaries.push_back(0);
  CharacterBreakIterator breaks(base::span<const UChar>(block.text.data(), block.text.size()));
  for (int next = breaks.Next(); next != kTextBreakDone; next = breaks.Next()) block.boundaries.push_back(next);
  if (block.boundaries.back() != block.text.size()) block.boundaries.push_back(static_cast<uint32_t>(block.text.size()));
  block.words = CountWords(block.text);
}

size_t DocumentView::Words(uint32_t start, uint32_t end) const {
  size_t count = 0;
  for (wtf_size_t i = BlockIndex(start); i < blocks_.size() && blocks_[i].start < end; ++i) {
    const Block& block = blocks_[i];
    const uint32_t from = std::max(start, block.start) - block.start, to = std::min(end, block.end) - block.start;
    if (from >= to) continue;
    count += from == 0 && to == block.text.size() ? block.words : CountWords(std::u16string_view(block.text).substr(from, to - from));
  }
  return count;
}

void DocumentView::InvalidateSelection(const Document::Selection& selection) {
  const Document::Selection& painted = painted_selection_;
  example::ForChangedHighlight(painted.Start(), painted.End(), selection.Start(), selection.End(),
                               [this](uint32_t start, uint32_t end) { DamageRange(start, end); });
  painted_selection_ = selection;
}

DocumentView::Damage DocumentView::TakeDamage() {
  Damage damage;
  damage.full = damage_full_;
  damage.rows = damage_.Take();
  damage_full_ = false;
  return damage;
}

void DocumentView::DamageRows(float top, float bottom) {
  // Ink may overflow a paragraph by as much as Paint() allows when culling.
  damage_.Add(top - 32 * scale_, bottom + 32 * scale_);
}

// The paragraphs meeting offsets [start, end]. A paragraph's end offset is
// its newline, where the selection paints a line-end mark.
void DocumentView::DamageRange(uint32_t start, uint32_t end) {
  for (wtf_size_t i = BlockIndex(start); i < blocks_.size() && blocks_[i].start <= end; ++i)
    DamageRows(blocks_[i].y, blocks_[i].y + blocks_[i].height);
}

wtf_size_t DocumentView::BlockIndex(uint32_t offset) const {
  auto found = std::upper_bound(blocks_.begin(), blocks_.end(), offset,
                                [](uint32_t position, const Block& block) { return position < block.start; });
  return found == blocks_.begin() ? 0 : static_cast<wtf_size_t>(found - blocks_.begin() - 1);
}

const DocumentView::Block& DocumentView::BlockAt(uint32_t offset) const {
  return blocks_[BlockIndex(offset)];
}

uint32_t DocumentView::Hit(float x, float y, TextAffinity* affinity) {
  // Paragraphs are ordered by y: the first not above `y`, or the one before
  // it when that is nearer across the gap.
  const auto below = std::partition_point(blocks_.begin(), blocks_.end(),
                                          [&](const Block& block) { return block.y + block.height < y; });
  const Block* nearest = below == blocks_.end() ? &blocks_.back() : &*below;
  if (below != blocks_.begin() && below != blocks_.end() && y < below->y) {
    const Block& above = *std::prev(below);
    if (y - (above.y + above.height) <= below->y - y) nearest = &above;
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
  const auto first = std::partition_point(blocks_.begin(), blocks_.end(), [&](const Block& block) {
    return y + block.y + block.height + 32 * scale_ < clip_top;
  });
  for (auto it = first; it != blocks_.end() && y + it->y - 32 * scale_ <= clip_bottom; ++it) {
    const Block& block = *it;
    const PhysicalOffset origin = Offset(x + block.x, y + block.y);
    const uint32_t start = std::max(selection.Start(), block.start), end = std::min(selection.End(), block.end);
    if (!selection.Empty() && start < end) {
      const InlineSelection local{block.Position(start), block.Position(end, TextAffinity::kUpstream)};
      for (const auto& rect : block.context->SelectionRectsForPaint(local, origin))
        FillPixelSnapped(raster, rect, PlatformPaint(0xffc9ddfa));
    }
    if (!selection.Empty() && selection.Start() <= block.end && selection.End() > block.end) {
      const auto caret = block.context->CaretRect(block.Position(block.end, TextAffinity::kUpstream));
      const PhysicalRect mark(origin + caret.offset,
                              {LayoutUnit(6 * scale_), LayoutUnit(std::max(24 * scale_, caret.Height().ToFloat()))});
      FillPixelSnapped(raster, mark, PlatformPaint(0xffc9ddfa));
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
  auto* words = WordBreakIterator(base::span<const UChar>(block.text.data(), length));
  if (!words) return {offset, offset};
  // At the paragraph end, select the word before the caret, as Word does.
  const int32_t relative = static_cast<int32_t>(std::min(std::clamp(offset, block.start, block.end) - block.start, length - 1));
  const int32_t start = words->preceding(relative + 1), end = words->following(relative);
  const uint32_t first = block.start + (start == icu::BreakIterator::DONE ? 0 : static_cast<uint32_t>(start));
  const uint32_t last = block.start + (end == icu::BreakIterator::DONE ? length : static_cast<uint32_t>(end));
  return {first, Snap(last)};
}

uint32_t DocumentView::Snap(uint32_t offset) const {
  const Block& block = BlockAt(offset);
  const uint32_t relative = std::clamp(offset, block.start, block.end) - block.start;
  return block.start + *std::lower_bound(block.boundaries.begin(), block.boundaries.end(), relative);
}

// A newline, between two paragraphs, is a space.
bool DocumentView::IsSpace(uint32_t offset) const {
  const Block& block = BlockAt(offset);
  const uint32_t relative = offset - block.start;
  return relative >= block.text.size() || u_isUWhiteSpace(block.text[relative]);
}

uint32_t DocumentView::Move(uint32_t offset, int direction, bool word) const {
  const uint32_t length = blocks_.back().end;
  if (!word) {
    const wtf_size_t index = BlockIndex(offset);
    const Block& block = blocks_[index];
    const auto& boundaries = block.boundaries;
    const uint32_t relative = std::clamp(offset, block.start, block.end) - block.start;
    if (direction > 0) {
      // From the paragraph end, across its newline.
      if (relative >= block.text.size()) return index + 1 < blocks_.size() ? blocks_[index + 1].start : block.end;
      return block.start + *std::upper_bound(boundaries.begin(), boundaries.end(), relative);
    }
    if (!relative) return index ? blocks_[index - 1].end : 0;
    return block.start + *std::prev(std::lower_bound(boundaries.begin(), boundaries.end(), relative));
  }
  if (direction > 0) {
    while (offset < length && !IsSpace(offset)) offset = Move(offset, 1, false);
    while (offset < length && IsSpace(offset)) offset = Move(offset, 1, false);
  } else {
    while (offset && IsSpace(offset - 1)) offset = Move(offset, -1, false);
    while (offset && !IsSpace(offset - 1)) offset = Move(offset, -1, false);
  }
  return offset;
}

} // namespace rich_text
