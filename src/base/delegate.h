// LoopMaxPro - callable with fixed size storage. Replaces std::function without using the heap.
// Accepts function pointers and lambdas whose captures fit in Cap bytes.
#pragma once
#include <stddef.h>
#include <new>
#include <type_traits>
#include <utility>

namespace lmp {

template <class Signature, size_t Cap = 2 * sizeof(void*)>
class Delegate;

template <class R, class... A, size_t Cap>
class Delegate<R(A...), Cap> {
 public:
  constexpr Delegate() = default;
  constexpr Delegate(decltype(nullptr)) {}

  template <class F,
            class = typename std::enable_if<
                !std::is_same<typename std::decay<F>::type, Delegate>::value &&
                std::is_invocable_r<R, typename std::decay<F>::type&, A...>::value>::type>
  Delegate(F&& f) {
    using Fn = typename std::decay<F>::type;
    static_assert(sizeof(Fn) <= Cap,
                  "callable too large for this Delegate: capture less or raise the capacity");
    static_assert(alignof(Fn) <= alignof(void*), "callable alignment not supported");
    static_assert(std::is_trivially_copyable<Fn>::value,
                  "Delegate accepts only trivially copyable callables");
    new (storage_) Fn(std::forward<F>(f));
    call_ = [](const unsigned char* s, A... a) -> R {
      Fn* fn = reinterpret_cast<Fn*>(const_cast<unsigned char*>(s));
      return (*fn)(std::forward<A>(a)...);
    };
  }

  explicit operator bool() const { return call_ != nullptr; }
  R operator()(A... a) const { return call_(storage_, std::forward<A>(a)...); }
  void reset() { call_ = nullptr; }

 private:
  alignas(void*) unsigned char storage_[Cap] = {};
  R (*call_)(const unsigned char*, A...) = nullptr;
};

}  // namespace lmp
