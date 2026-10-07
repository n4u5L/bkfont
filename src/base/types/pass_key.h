// Ported from: chromium/base/types/pass_key.h
// access token used by extracted constructors.
#pragma once
namespace bkit::base {

template <typename T>
class PassKey {
  friend T;
  constexpr PassKey() = default;
};

} // namespace bkit::base
