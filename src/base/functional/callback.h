// Text predicates keep Chromium's owning callback and Run() calling convention.
#pragma once
#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include "base/functional/callback_forward.h"
#include "base/immediate_crash.h"
namespace bkfont::base {
template <typename R, typename... Args>
class RepeatingCallback<R(Args...)> {
public:
  RepeatingCallback() = default;
  RepeatingCallback(const RepeatingCallback&) = default;
  RepeatingCallback(RepeatingCallback&&) = default;
  RepeatingCallback& operator=(const RepeatingCallback&) = default;
  RepeatingCallback& operator=(RepeatingCallback&&) = default;
  template <typename F>
    requires(!std::same_as<std::remove_cvref_t<F>, RepeatingCallback> && std::constructible_from<std::function<R(Args...)>, F>)
  RepeatingCallback(F&& callable) {
    std::function<R(Args...)> function(std::forward<F>(callable));
    if (function) {
      callable_ =
          std::make_shared<std::function<R(Args...)>>(std::move(function));
    }
  }
  R Run(Args... args) const& {
    // Like Chromium's BindStateHolder, copies share the bound state. Keep it
    // alive if invoking the callback destroys or reassigns this callback.
    auto callable = callable_;
    if (!callable) ImmediateCrash();
    return (*callable)(std::forward<Args>(args)...);
  }
  R Run(Args... args) && {
    auto callable = std::move(callable_);
    if (!callable) ImmediateCrash();
    return (*callable)(std::forward<Args>(args)...);
  }
  explicit operator bool() const {
    return static_cast<bool>(callable_);
  }

private:
  std::shared_ptr<std::function<R(Args...)>> callable_;
};
} // namespace bkfont::base
