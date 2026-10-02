// Source: base/types/pass_key.h; access token used by extracted constructors.
#pragma once
namespace base {
template <typename T>
class PassKey {
  friend T;
  constexpr PassKey() = default;
};
} // namespace base
