// Named rules replace selector matching; the node's rule list defines order.
// The change callback receives the rule name, so only referencing nodes are
// invalidated (StyleEngine's rule-feature invalidation, without selectors).
// Names are AtomicStrings, like class names; a null name is never a rule.
#pragma once

#include <cstdint>

#include "base/functional/callback.h"
#include "base/hash_map.h"
#include "base/text/atomic_string.h"
#include "base/text/atomic_string_hash.h"
#include "style/style_declaration.h"

namespace bkfont {

class StyleSheet {
public:
  using ChangeCallback = base::RepeatingCallback<void(const AtomicString&)>;
  explicit StyleSheet(ChangeCallback on_change = {}) : on_change_(std::move(on_change)) {}
  StyleSheet(const StyleSheet&) = delete;
  StyleSheet& operator=(const StyleSheet&) = delete;
  bool SetRule(const AtomicString& name, const StyleDeclaration& declaration) {
    if (name.IsNull()) return false;
    const auto found = rules_.find(name);
    if (found != rules_.end() && found->value == declaration) return false;
    rules_.Set(name, declaration);
    Changed(name);
    return true;
  }
  bool RemoveRule(const AtomicString& name) {
    if (name.IsNull()) return false;
    const auto found = rules_.find(name);
    if (found == rules_.end()) return false;
    // Notify after erasing; the callback may look the rule up again. Keep a
    // reference in case `name` refers to the erased key.
    const AtomicString removed = name;
    rules_.erase(found);
    Changed(removed);
    return true;
  }
  const StyleDeclaration* Rule(const AtomicString& name) const {
    if (name.IsNull()) return nullptr;
    const auto found = rules_.find(name);
    return found == rules_.end() ? nullptr : &found->value;
  }
  uint64_t Revision() const { return revision_; }

private:
  void Changed(const AtomicString& name) {
    ++revision_;
    if (on_change_) on_change_.Run(name);
  }
  HashMap<AtomicString, StyleDeclaration> rules_;
  ChangeCallback on_change_;
  uint64_t revision_ = 0;
};

} // namespace bkfont
