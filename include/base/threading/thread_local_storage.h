// Windows storage boundary for the extracted WTF ThreadSpecific algorithm.
// Each value is cleared before invoking its destructor. Values set again in
// that slot are drained using Chromium's bounded rescan algorithm.
#pragma once
#include <windows.h>
#include <new>
#include <utility>

namespace base {
class ThreadLocalStorage {
public:
  class Slot {
  public:
    using Destructor = void (*)(void*);
    explicit Slot(Destructor destructor)
        : destructor_(destructor),
          index_(::FlsAlloc(&DestroyEntry)) {
      if (index_ == FLS_OUT_OF_INDEXES) throw std::bad_alloc();
    }
    Slot(const Slot&) = delete;
    Slot& operator=(const Slot&) = delete;
    ~Slot() {
      ::FlsFree(index_);
    }
    void* Get() const {
      const Entry* entry = static_cast<Entry*>(::FlsGetValue(index_));
      return entry ? entry->value : nullptr;
    }
    void Set(void* value) {
      Entry* entry = static_cast<Entry*>(::FlsGetValue(index_));
      if (entry) {
        entry->value = value;
        return;
      }
      entry = new Entry{index_, destructor_, value};
      if (!::FlsSetValue(index_, entry)) {
        delete entry;
        throw std::bad_alloc();
      }
    }

  private:
    struct Entry {
      DWORD index;
      Destructor destructor;
      void* value;
      bool destroying = false;
    };
    static void NTAPI DestroyEntry(void* data) {
      Entry* entry = static_cast<Entry*>(data);
      // A reentrant callback for this Entry leaves ownership with the outer
      // invocation, which is responsible for draining and releasing it.
      if (entry->destroying) return;
      entry->destroying = true;
      ::FlsSetValue(entry->index, entry);
      // OnThreadExitInternal uses kMaxDestructorIterations (256) + 1 and
      // counts the final empty scan too. Do not depend on FLS calling us again.
      unsigned remaining_attempts = 256 + 1;
      bool need_to_scan_destructors = true;
      while (need_to_scan_destructors) {
        need_to_scan_destructors = false;
        void* value = entry->value;
        if (value && entry->destructor) {
          entry->value = nullptr;
          entry->destructor(value);
          need_to_scan_destructors = true;
        }
        if (--remaining_attempts == 0) {
          std::unreachable();
        }
      }
      ::FlsSetValue(entry->index, nullptr);
      delete entry;
    }
    Destructor destructor_;
    DWORD index_;
  };
};
} // namespace base
