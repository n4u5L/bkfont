// Local implementation: non-owning callable adapter.
// Upstream reference: chromium/base/functional/function_ref.h
// Non-owning synchronous callable reference; no allocation or container policy.
#pragma once
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
namespace bkit::base {

template <typename Signature>
class FunctionRef;
template <typename R, typename... Args>
class FunctionRef<R(Args...)> {
public:
  template <typename F>
    requires(!std::is_same_v<std::remove_cvref_t<F>, FunctionRef>)
  FunctionRef(F&& function)
      : object_(const_cast<void*>(static_cast<const void*>(std::addressof(function)))),
        invoke_([](void* object, Args... args) -> R {
          return std::invoke(*static_cast<std::remove_reference_t<F>*>(object),
                             std::forward<Args>(args)...);
        }) {
  }
  R operator()(Args... args) const {
    return invoke_(object_, std::forward<Args>(args)...);
  }

private:
  void* object_;
  R (*invoke_)(void*, Args...);
};

} // namespace bkit::base
