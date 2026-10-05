// Ported from: blink/renderer/platform/fonts/opentype/open_type_caps_support_mpl.cc

/* ***** BEGIN LICENSE BLOCK *****
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ***** END LICENSE BLOCK ***** */

#include <hb-ot.h>
#include <iterator>

#include "open_type_caps_support.h"

namespace bkfont {

bool OpenTypeCapsSupport::SupportsOpenTypeFeature(hb_script_t script,
                                                  uint32_t tag) const {
  hb_face_t* const face = hb_font_get_face(harfbuzz_face_->GetScaledFont());

  if (!hb_ot_layout_has_substitution(face))
    return false;

  // Get the OpenType tag(s) that match this script code.
  hb_tag_t script_tags[2] = {};
  unsigned num_returned_script_tags = std::size(script_tags);
  hb_ot_tags_from_script_and_language(
      static_cast<hb_script_t>(script),
      HB_LANGUAGE_INVALID,
      &num_returned_script_tags,
      script_tags,
      nullptr,
      nullptr);

  const hb_tag_t kGSUB = HB_TAG('G', 'S', 'U', 'B');
  unsigned script_index = 0;
  // Identify for which script a GSUB table is available.
  hb_ot_layout_table_select_script(face, kGSUB, num_returned_script_tags, script_tags, &script_index, nullptr);

  if (hb_ot_layout_language_find_feature(face, kGSUB, script_index, HB_OT_LAYOUT_DEFAULT_LANGUAGE_INDEX, tag, nullptr)) {
    return true;
  }
  return false;
}

} // namespace bkfont
