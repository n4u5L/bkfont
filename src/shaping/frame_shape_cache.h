// Ported from: blink/renderer/platform/fonts/shaping/frame_shape_cache.h
// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "base/hash_map.h"
#include "platform_export.h"
#include "text/text_direction.h"
#include "base/text/string_hash.h"
#include "base/text/wtf_string.h"
#include "base/vector_backed_linked_list.h"
#include "paint/geometry.h"

namespace bkfont {

class PlainTextItem;
class PlainTextNode;
class ShapeResult;

// FrameShapeCache manages a cache for PlainTextNode, and a cache for
// ShapeResult and ink bounds. Each instance belongs to a specific `Font` and
// whitespace-normalization mode, as in upstream PlainTextPainter. Callers must
// replace the cache when the font's fallback list is invalidated.
//
// The caches are aware of frames distinguished by DidSwitchFrame() calls. This
// allows the caches to purge old entries when the frame is switched.
class PLATFORM_EXPORT FrameShapeCache {
public:
  FrameShapeCache();

  FrameShapeCache(const FrameShapeCache&) = delete;
  FrameShapeCache& operator=(const FrameShapeCache&) = delete;

  // This function should be called between the end of an animation frame and
  // the beginning of the next animation frame.
  void DidSwitchFrame();

  // Cache entry for the PlainTextNode cache.
  // Clients must not access `list_index`.
  struct NodeEntry {
    // Cached data.
    std::shared_ptr<PlainTextNode> node;
    // A field for LRU management. It's kNotFound for entries created
    // in the initial frame.
    wtf_size_t list_index;
  };

  // Find a PlainTextNode cache entry for the specified `text` and `direction`.
  // If it's not found, new entry is created.
  NodeEntry* FindOrCreateNodeEntry(const String& text, TextDirection direction);

  // This should be called if FindOrCreateNodeEntry() didn't find an
  // existing entry.
  void RegisterNodeEntry(const String& text,
                         TextDirection direction,
                         std::shared_ptr<PlainTextNode> node,
                         NodeEntry* entry);

  // Cache entry for the ShapeResult cache.
  // Clients must not access `list_index`.
  struct ShapeEntry {
    // Cached data.
    std::shared_ptr<const ShapeResult> shape_result;
    RectF ink_bounds;
    // A field for LRU management. It's kNotFound for entries created
    // in the initial frame.
    wtf_size_t list_index;
  };

  // Find a ShapeResult cache entry for the specified `text` and `direction`.
  // If it's not found, new entry is created.
  ShapeEntry* FindOrCreateShapeEntry(const String& word,
                                     TextDirection direction);

  // This should be called if FindOrCreateShapeEntry() didn't find an
  // existing entry.
  void RegisterShapeEntry(const PlainTextItem& item, ShapeEntry* entry);

private:
  using KeyType = std::pair<String, TextDirection>;
  struct ListKey {
    KeyType key;
    uint32_t generation;
  };
  using LruList = VectorBackedLinkedList<ListKey>;

  template <typename E>
  E* FindOrCreateEntry(const String& text,
                       TextDirection direction,
                       HashMap<KeyType, E>& map,
                       LruList& lru_list);
  wtf_size_t ListIndexForNewEntry(const String& text,
                                  TextDirection direction,
                                  LruList& lru_list);
  template <typename E>
  void RemoveOldEntries(HashMap<KeyType, E>& map, LruList& lru_list);

  template <typename E>
  void LimitCacheSize(HashMap<KeyType, E>& map,
                      LruList& lru_list,
                      wtf_size_t limit);

  HashMap<KeyType, NodeEntry> node_map_;
  LruList node_lru_list_;

  HashMap<KeyType, ShapeEntry> shape_map_;
  LruList shape_lru_list_;

  static constexpr uint32_t kInitialFrame = 0;
  uint32_t frame_generation_ = kInitialFrame;

  // True if a cache has a new entry in the current frame.
  bool added_new_entries_ = false;
};

} // namespace bkfont
