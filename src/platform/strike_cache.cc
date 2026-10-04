// Ported from: skia/src/core/SkStrikeCache.cpp

#include "strike_cache.h"

#include <algorithm>
#include <utility>

#include "strike.h"

namespace bkfont {

StrikeCache* StrikeCache::GlobalStrikeCache() {
  static auto* cache = new StrikeCache;
  return cache;
}

std::shared_ptr<Strike> StrikeCache::FindOrCreateStrike(const StrikeSpec& strike_spec) {
  AutoMutexExclusive ac(lock_);
  std::shared_ptr<Strike> strike = InternalFindStrikeOrNull(strike_spec.Descriptor());
  if (strike == nullptr) {
    strike = InternalCreateStrike(strike_spec);
  }
  InternalPurge();
  return strike;
}

std::shared_ptr<Strike> StrikeCache::InternalFindStrikeOrNull(const ScalerContextRec& desc) {
  // Check head because it is likely the strike we are looking for.
  if (head_ != nullptr && head_->GetDescriptor() == desc) {
    return head_->shared_from_this();
  }

  // Do the heavy search looking for the strike.
  auto strike_handle = strike_lookup_.find(desc);
  if (strike_handle == strike_lookup_.end()) {
    return nullptr;
  }
  Strike* strike_ptr = strike_handle->second.get();
  if (head_ != strike_ptr) {
    // Make most recently used
    strike_ptr->prev_->next_ = strike_ptr->next_;
    if (strike_ptr->next_ != nullptr) {
      strike_ptr->next_->prev_ = strike_ptr->prev_;
    } else {
      tail_ = strike_ptr->prev_;
    }
    head_->prev_ = strike_ptr;
    strike_ptr->next_ = head_;
    strike_ptr->prev_ = nullptr;
    head_ = strike_ptr;
  }
  return strike_ptr->shared_from_this();
}

std::shared_ptr<Strike> StrikeCache::InternalCreateStrike(const StrikeSpec& strike_spec) {
  std::unique_ptr<ScalerContext> scaler = strike_spec.CreateScalerContext();
  auto strike = std::make_shared<Strike>(this, strike_spec, std::move(scaler));
  InternalAttachToHead(strike);
  return strike;
}

int StrikeCache::GetCacheCountLimit() const {
  AutoMutexExclusive ac(lock_);
  return cache_count_limit_;
}

int StrikeCache::SetCacheCountLimit(int new_count) {
  AutoMutexExclusive ac(lock_);

  if (new_count < 0) {
    new_count = 0;
  }

  int prev_count = cache_count_limit_;
  cache_count_limit_ = new_count;
  InternalPurge();
  return prev_count;
}

int StrikeCache::GetCacheCountUsed() const {
  AutoMutexExclusive ac(lock_);
  return cache_count_;
}

std::size_t StrikeCache::GetCacheSizeLimit() const {
  AutoMutexExclusive ac(lock_);
  return cache_size_limit_;
}

std::size_t StrikeCache::SetCacheSizeLimit(std::size_t new_limit) {
  AutoMutexExclusive ac(lock_);

  std::size_t prev_limit = cache_size_limit_;
  cache_size_limit_ = new_limit;
  InternalPurge();
  return prev_limit;
}

std::size_t StrikeCache::GetTotalMemoryUsed() const {
  AutoMutexExclusive ac(lock_);
  return total_memory_used_;
}

std::size_t StrikeCache::InternalPurge(std::size_t min_bytes_needed) {
  std::size_t bytes_needed = 0;
  if (total_memory_used_ > cache_size_limit_) {
    bytes_needed = total_memory_used_ - cache_size_limit_;
  }
  bytes_needed = std::max(bytes_needed, min_bytes_needed);
  if (bytes_needed) {
    // no small purges!
    bytes_needed = std::max(bytes_needed, total_memory_used_ >> 2);
  }

  int count_needed = 0;
  if (cache_count_ > cache_count_limit_) {
    count_needed = cache_count_ - cache_count_limit_;
    // no small purges!
    count_needed = std::max(count_needed, cache_count_ >> 2);
  }

  // early exit
  if (!count_needed && !bytes_needed) {
    return 0;
  }

  std::size_t bytes_freed = 0;
  int count_freed = 0;

  // Start at the tail and proceed backwards deleting; the list is in LRU
  // order, with unimportant entries at the tail.
  Strike* strike = tail_;
  while (strike != nullptr && (bytes_freed < bytes_needed || count_freed < count_needed)) {
    Strike* prev = strike->prev_;

    bytes_freed += strike->memory_used_;
    count_freed += 1;
    InternalRemoveStrike(strike);
    strike = prev;
  }

  return bytes_freed;
}

void StrikeCache::InternalAttachToHead(std::shared_ptr<Strike> strike) {
  Strike* strike_ptr = strike.get();
  strike_lookup_.emplace(strike_ptr->GetDescriptor(), std::move(strike));

  cache_count_ += 1;
  total_memory_used_ += strike_ptr->memory_used_;

  if (head_ != nullptr) {
    head_->prev_ = strike_ptr;
    strike_ptr->next_ = head_;
  }

  if (tail_ == nullptr) {
    tail_ = strike_ptr;
  }

  head_ = strike_ptr; // Transfer ownership of strike to the cache list.
}

void StrikeCache::InternalRemoveStrike(Strike* strike) {
  cache_count_ -= 1;
  total_memory_used_ -= strike->memory_used_;

  if (strike->prev_) {
    strike->prev_->next_ = strike->next_;
  } else {
    head_ = strike->next_;
  }
  if (strike->next_) {
    strike->next_->prev_ = strike->prev_;
  } else {
    tail_ = strike->prev_;
  }

  strike->prev_ = strike->next_ = nullptr;
  strike->removed_ = true;
  // The lookup holds the cache's reference and may destroy the strike, so the
  // key must not alias it.
  const ScalerContextRec desc = strike->GetDescriptor();
  strike_lookup_.erase(desc);
}

} // namespace bkfont
