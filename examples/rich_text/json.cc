#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdint>

namespace rich_text {
namespace {

void AppendUTF16(std::u16string& out, uint32_t ch) {
  if (ch <= 0xffff)
    out.push_back(static_cast<char16_t>(ch));
  else {
    ch -= 0x10000;
    out.push_back(static_cast<char16_t>(0xd800 + (ch >> 10)));
    out.push_back(static_cast<char16_t>(0xdc00 + (ch & 0x3ff)));
  }
}
void AppendUTF8(std::string& out, uint32_t ch) {
  if (ch < 0x80)
    out.push_back(static_cast<char>(ch));
  else if (ch < 0x800) {
    out.push_back(static_cast<char>(0xc0 | (ch >> 6)));
    out.push_back(static_cast<char>(0x80 | (ch & 63)));
  } else if (ch < 0x10000) {
    out.push_back(static_cast<char>(0xe0 | (ch >> 12)));
    out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 63)));
    out.push_back(static_cast<char>(0x80 | (ch & 63)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (ch >> 18)));
    out.push_back(static_cast<char>(0x80 | ((ch >> 12) & 63)));
    out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 63)));
    out.push_back(static_cast<char>(0x80 | (ch & 63)));
  }
}

class Parser {
public:
  explicit Parser(std::string_view source)
      : source_(source) {
  }
  bool Parse(Json& result, std::string& error) {
    if (source_.starts_with("\xef\xbb\xbf")) position_ = 3;
    const bool ok = Value(result, 0);
    Space();
    if (!ok || position_ != source_.size()) {
      error = "Invalid JSON at byte " + std::to_string(position_) + ": " +
              (reason_.empty() ? "unexpected trailing data" : reason_);
      return false;
    }
    error.clear();
    return true;
  }

private:
  void Space() {
    while (position_ < source_.size() && (source_[position_] == ' ' || source_[position_] == '\t' ||
                                          source_[position_] == '\r' || source_[position_] == '\n')) ++position_;
  }
  bool Fail(const char* reason) {
    reason_ = reason;
    return false;
  }
  bool Take(char ch) {
    if (position_ < source_.size() && source_[position_] == ch) {
      ++position_;
      return true;
    }
    return false;
  }
  bool Hex(uint32_t& value) {
    value = 0;
    for (int i = 0; i < 4; ++i) {
      if (position_ == source_.size()) return false;
      const char c = source_[position_++];
      int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                               : c >= 'A' && c <= 'F'   ? c - 'A' + 10
                                                                        : -1;
      if (digit < 0) return false;
      value = value * 16 + digit;
    }
    return true;
  }
  bool String(std::string& out) {
    if (!Take('"')) return Fail("expected string");
    while (position_ < source_.size()) {
      const unsigned char c = source_[position_++];
      if (c == '"') {
        std::u16string decoded;
        return DecodeUTF8(out, decoded) || Fail("invalid UTF-8");
      }
      if (c < 0x20) return Fail("unescaped control character");
      if (c != '\\') {
        out.push_back(static_cast<char>(c));
        continue;
      }
      if (position_ == source_.size()) break;
      switch (source_[position_++]) {
      case '"':
        out += '"';
        break;
      case '\\':
        out += '\\';
        break;
      case '/':
        out += '/';
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        uint32_t code;
        if (!Hex(code)) return Fail("invalid Unicode escape");
        if (code >= 0xd800 && code <= 0xdbff) {
          uint32_t low;
          if (!Take('\\') || !Take('u') || !Hex(low) || low < 0xdc00 || low > 0xdfff)
            return Fail("unpaired high surrogate");
          code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
        } else if (code >= 0xdc00 && code <= 0xdfff)
          return Fail("unpaired low surrogate");
        AppendUTF8(out, code);
        break;
      }
      default:
        return Fail("unknown escape");
      }
    }
    return Fail("unterminated string");
  }
  bool Value(Json& out, unsigned depth) {
    if (depth > 32) return Fail("nesting exceeds 32 levels");
    if (++values_ > 400000) return Fail("too many JSON values");
    Space();
    if (position_ == source_.size()) return Fail("expected value");
    if (source_[position_] == '"') {
      out.type = Json::Type::String;
      return String(out.string);
    }
    if (Take('{')) {
      out.type = Json::Type::Object;
      Space();
      if (Take('}')) return true;
      do {
        if (out.object.size() >= 128) return Fail("too many object members");
        Space();
        std::string key;
        if (!String(key)) return false;
        if (out.Find(key)) return Fail("duplicate object key");
        Space();
        if (!Take(':')) return Fail("expected colon");
        Json child;
        if (!Value(child, depth + 1)) return false;
        out.object.emplace_back(std::move(key), std::move(child));
        Space();
        if (Take('}')) return true;
      } while (Take(','));
      return Fail("expected comma or closing brace");
    }
    if (Take('[')) {
      out.type = Json::Type::Array;
      Space();
      if (Take(']')) return true;
      do {
        Json child;
        if (!Value(child, depth + 1)) return false;
        out.array.push_back(std::move(child));
        Space();
        if (Take(']')) return true;
      } while (Take(','));
      return Fail("expected comma or closing bracket");
    }
    for (const auto literal : {std::string_view("null"), std::string_view("true"), std::string_view("false")}) {
      if (source_.substr(position_).starts_with(literal)) {
        position_ += literal.size();
        out.type = literal == "null" ? Json::Type::Null : Json::Type::Boolean;
        out.boolean = literal == "true";
        return true;
      }
    }
    const size_t start = position_;
    Take('-');
    if (!Take('0')) {
      if (position_ == source_.size() || source_[position_] < '1' || source_[position_] > '9')
        return Fail("expected number");
      Digits();
    }
    if (Take('.') && !Digits()) return Fail("missing fractional digits");
    if (Take('e') || Take('E')) {
      if (!Take('+')) Take('-');
      if (!Digits()) return Fail("missing exponent digits");
    }
    const auto result = std::from_chars(source_.data() + start, source_.data() + position_, out.number);
    if (result.ec != std::errc{} || !std::isfinite(out.number)) return Fail("number out of range");
    out.type = Json::Type::Number;
    return true;
  }
  bool Digits() {
    const size_t start = position_;
    while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') ++position_;
    return start != position_;
  }
  std::string_view source_;
  size_t position_ = 0;
  size_t values_ = 0;
  std::string reason_;
};

} // namespace

bool DecodeUTF8(std::string_view input, std::u16string& out) {
  out.clear();
  for (size_t i = 0; i < input.size();) {
    const uint8_t first = static_cast<uint8_t>(input[i++]);
    uint32_t code = first;
    unsigned continuation = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      code = first & 31;
      continuation = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      code = first & 15;
      continuation = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      code = first & 7;
      continuation = 3;
    } else if (first >= 0x80)
      return false;
    if (i + continuation > input.size()) return false;
    for (unsigned j = 0; j < continuation; ++j) {
      const uint8_t ch = static_cast<uint8_t>(input[i++]);
      if ((ch & 0xc0) != 0x80) return false;
      code = (code << 6) | (ch & 63);
    }
    if ((continuation == 1 && code < 0x80) || (continuation == 2 && code < 0x800) ||
        (continuation == 3 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    AppendUTF16(out, code);
  }
  return true;
}

std::string EncodeUTF8(std::u16string_view input) {
  std::string out;
  for (size_t i = 0; i < input.size(); ++i) {
    uint32_t code = input[i];
    if (code >= 0xd800 && code <= 0xdbff && i + 1 < input.size() && input[i + 1] >= 0xdc00 && input[i + 1] <= 0xdfff)
      code = 0x10000 + ((code - 0xd800) << 10) + input[++i] - 0xdc00;
    else if (code >= 0xd800 && code <= 0xdfff)
      code = 0xfffd;
    AppendUTF8(out, code);
  }
  return out;
}

std::u16string NormalizeNewlines(std::u16string_view input) {
  std::u16string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == u'\r') {
      if (i + 1 < input.size() && input[i + 1] == u'\n') ++i;
      out.push_back(u'\n');
    } else
      out.push_back(input[i]);
  }
  return out;
}

const Json* Json::Find(std::string_view name) const {
  if (type != Type::Object) return nullptr;
  for (const auto& [key, value] : object)
    if (key == name) return &value;
  return nullptr;
}
bool ParseJson(std::string_view source, Json& out, std::string& error) {
  Json result;
  if (!Parser(source).Parse(result, error)) return false;
  out = std::move(result);
  return true;
}
std::string QuoteJson(std::string_view utf8) {
  std::string out = "\"";
  constexpr char hex[] = "0123456789abcdef";
  for (const unsigned char c : utf8) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 32) {
        out += "\\u00";
        out += hex[c >> 4];
        out += hex[c & 15];
      } else
        out += static_cast<char>(c);
    }
  }
  return out + '"';
}

} // namespace rich_text
