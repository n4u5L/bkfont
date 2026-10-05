// Local implementation: deterministic lifetime for font cache observers.

#include "font_cache_client.h"

#include "font_cache.h"

namespace bkfont {

FontCacheClient::~FontCacheClient() {
  while (!font_caches_.empty()) {
    font_caches_.back()->RemoveClient(this);
  }
}

} // namespace bkfont
