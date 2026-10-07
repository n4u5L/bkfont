#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "style/style_declaration.h"

namespace rich_text {

using Properties = std::vector<std::pair<std::string, std::string>>;
struct PropertySpec {
  const char* name;
  const char* label;
  bool paragraph;
  int tab;                          // Home, typography, paragraph, effects.
  std::vector<std::string> choices; // CSS text, validated by the real style system.
  // Reads CSS text into the property's typed input and sets it on the
  // declaration (StyleDeclaration::Set<Property>()). CSS-wide keywords are
  // handled by BuildDeclaration().
  bool (*apply)(bkfont::StyleDeclaration&, std::string_view) = nullptr;
};
std::span<const PropertySpec> PropertyCatalog();
const PropertySpec* FindProperty(std::string_view);
std::string PropertyValue(const Properties&, std::string_view, std::string_view fallback = {});
void SetProperty(Properties&, std::string_view name, std::string_view value);
bool BuildDeclaration(const Properties&, bkfont::StyleDeclaration&, std::string& error);
bkfont::StyleDeclaration DefaultParagraphStyle();

} // namespace rich_text
