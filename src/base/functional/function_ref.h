// Local implementation: non-owning callable adapter.
// Upstream reference: chromium/base/functional/function_ref.h
// Non-owning synchronous callable reference; no allocation or container policy.
#pragma once
#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
namespace bkit::base {

template <typename Signature>
class FunctionRef;
template <typename R, typename... Args>
class FunctionRef<R(Args...)> {
  // absl::functional_internal::VoidPtr: function pointers round-trip through
  // a function pointer type, without relying on conversion to void*.
  union Target {
    void* object;
    void (*function)();
  };

public:
  template <typename F>
    requires(!std::is_same_v<std::remove_cvref_t<F>, FunctionRef> &&
             !std::is_function_v<std::remove_pointer_t<std::decay_t<F>>> &&
             std::is_invocable_r_v<R, F&, Args...>)
  FunctionRef(F&& function)
      : target_{.object = const_cast<void*>(static_cast<const void*>(std::addressof(function)))},
        invoke_([](Target target, Args... args) -> R {
          return static_cast<R>(std::invoke(*static_cast<std::remove_reference_t<F>*>(target.object),
                                            std::forward<Args>(args)...));
        }) {
  }

  // As in absl::FunctionRef, store the function pointer itself. This also
  // accepts function references, and does not borrow a temporary pointer.
  template <typename F>
    requires(std::is_function_v<F> && std::is_invocable_r_v<R, F*, Args...>)
  FunctionRef(F* function)
      : target_{.function = reinterpret_cast<void (*)()>(function)},
        invoke_([](Target target, Args... args) -> R {
          return static_cast<R>(std::invoke(reinterpret_cast<F*>(target.function),
                                            std::forward<Args>(args)...));
        }) {
    assert(function != nullptr);
  }

  R operator()(Args... args) const {
    return invoke_(target_, std::forward<Args>(args)...);
  }

private:
  Target target_;
  R (*invoke_)(Target, Args...);
};

} // namespace bkit::base
