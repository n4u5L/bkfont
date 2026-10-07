// Small, strict JSON codec for the example's versioned file format. No exceptions.
#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "base/vector.h"

namespace rich_text {

bool DecodeUTF8(std::string_view, std::u16string&);
std::string EncodeUTF8(std::u16string_view);
std::u16string NormalizeNewlines(std::u16string_view);

struct Json {
  enum class Type {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
  };
  Type type = Type::Null;
  double number = 0;
  bool boolean = false;
  std::string string;
  bkfont::Vector<Json> array;
  bkfont::Vector<std::pair<std::string, Json>> object;
  const Json* Find(std::string_view name) const;
};

bool ParseJson(std::string_view, Json&, std::string& error);
std::string QuoteJson(std::string_view utf8);

} // namespace rich_text
