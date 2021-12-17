﻿#pragma once

#include <functional>
#include <utility>

namespace std23
{

template<auto V> struct nontype_t
{
    explicit nontype_t() = default;
};

template<auto V> inline constexpr nontype_t<V> nontype{};

template<class R, class F, class... Args>
requires std::is_invocable_r_v<R, F, Args...>
constexpr R invoke_r(F &&f, Args &&...args) noexcept(
    std::is_nothrow_invocable_r_v<R, F, Args...>)
{
    if constexpr (std::is_void_v<R>)
        std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    else
        return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
}

template<class Sig> struct _qual_fn_sig_common;

template<class R, class... Args> struct _qual_fn_sig_common<R(Args...)>
{
    template<class T> using prepend = R(T, Args...);
    static constexpr bool is_noexcept = false;

    template<class... T>
    static constexpr bool is_invocable_using =
        std::is_invocable_r_v<R, T..., Args...>;
};

template<class R, class... Args> struct _qual_fn_sig_common<R(Args...) noexcept>
{
    template<class T> using prepend = R(T, Args...);
    static constexpr bool is_noexcept = true;

    template<class... T>
    static constexpr bool is_invocable_using =
        std::is_nothrow_invocable_r_v<R, T..., Args...>;
};

template<class R, class... Args>
struct _qual_fn_sig_common<R(Args...) const> : _qual_fn_sig_common<R(Args...)>
{
};

template<class R, class... Args>
struct _qual_fn_sig_common<R(Args...) const noexcept>
    : _qual_fn_sig_common<R(Args...) noexcept>
{
};

template<class T, class Self>
inline constexpr bool _is_not_self =
    not std::is_same_v<std::remove_cvref_t<T>, Self>;

template<class T> struct _unwrap_reference
{
    using type = T;
};

template<class U> struct _unwrap_reference<std::reference_wrapper<U>>
{
    using type = U;
};

template<class T>
using _remove_and_unwrap_reference_t =
    _unwrap_reference<std::remove_reference_t<T>>::type;

template<class Sig> struct _qual_fn_sig : _qual_fn_sig_common<Sig>
{
    template<class F, class... T>
    static constexpr bool is_memfn_invocable_using =
        _qual_fn_sig_common<Sig>::template is_invocable_using<F, T...>
            and std::is_member_pointer_v<F>;

    template<class T>
    static constexpr bool is_lvalue_invocable_using =
        _qual_fn_sig_common<Sig>::template is_invocable_using<T &>;
};

struct _function_ref_base
{
    union storage {
        void *p_ = nullptr;
        void const *cp_;
        void (*fp_)();

        constexpr storage() noexcept = default;

        template<class T>
        requires std::is_object_v<T>
        constexpr explicit storage(T *p) noexcept : p_(p)
        {
        }

        template<class T>
        requires std::is_object_v<T>
        constexpr explicit storage(T const *p) noexcept : cp_(p)
        {
        }

        template<class T>
        requires std::is_function_v<T>
        constexpr explicit storage(T *p) noexcept
            : fp_(reinterpret_cast<decltype(fp_)>(p))
        {
        }
    };

    template<class T> constexpr static auto get(storage obj)
    {
        if constexpr (std::is_const_v<T>)
            return static_cast<T *>(obj.cp_);
        else if constexpr (std::is_object_v<T>)
            return static_cast<T *>(obj.p_);
        else
            return reinterpret_cast<T *>(obj.fp_);
    }
};

template<class Sig> class function_ref;

#define _FUNCTION_REF_SPECIALIZATION(Sig, T_cv)                                \
    template<class R, class... Args>                                           \
    class function_ref<Sig> : _function_ref_base                               \
    {                                                                          \
        using signature = _qual_fn_sig<Sig>;                                   \
        storage obj_;                                                          \
        typename signature::template prepend<storage> *fptr_ = nullptr;        \
                                                                               \
      public:                                                                  \
        function_ref() = default;                                              \
                                                                               \
        template<class F>                                                      \
        function_ref(F *f) noexcept requires std::is_function_v<F> and         \
            signature::template is_invocable_using<F>                          \
            : obj_(f), fptr_([](storage fn_, Args... args) {                   \
                return std23::invoke_r<R>(get<F>(fn_),                         \
                                          std::forward<Args>(args)...);        \
            })                                                                 \
        {                                                                      \
        }                                                                      \
                                                                               \
        template<class F, class T = _remove_and_unwrap_reference_t<F>>         \
        function_ref(F &&f) noexcept requires                                  \
            _is_not_self<F, function_ref> and                                  \
            signature::template is_lvalue_invocable_using<T_cv>                \
            : obj_(std::addressof(static_cast<T &>(f))),                       \
              fptr_([](storage fn_, Args... args) {                            \
                  return std23::invoke_r<R, T_cv &>(                           \
                      *get<T>(fn_), std::forward<Args>(args)...);              \
              })                                                               \
        {                                                                      \
        }                                                                      \
                                                                               \
        template<auto F>                                                       \
        constexpr function_ref(nontype_t<F>) noexcept requires                 \
            signature::template is_invocable_using<decltype(F)>                \
            : fptr_([](storage, Args... args) {                                \
                return std23::invoke_r<R>(F, std::forward<Args>(args)...);     \
            })                                                                 \
        {                                                                      \
        }                                                                      \
                                                                               \
        template<auto F, class T>                                              \
        function_ref(nontype_t<F>, T_cv &obj) noexcept requires                \
            signature::template is_invocable_using<decltype(F), decltype(obj)> \
            : obj_(std::addressof(obj)),                                       \
              fptr_([](storage this_, Args... args) {                          \
                  return std23::invoke_r<R>(F, *(get<T_cv>(this_)),            \
                                            std::forward<Args>(args)...);      \
              })                                                               \
        {                                                                      \
        }                                                                      \
                                                                               \
        template<auto F, class T>                                              \
        function_ref(nontype_t<F>, T_cv *obj) noexcept requires                \
            signature::template is_memfn_invocable_using<decltype(F),          \
                                                         decltype(obj)>        \
            : obj_(obj), fptr_([](storage this_, Args... args) {               \
                return std23::invoke_r<R>(F, get<T_cv>(this_),                 \
                                          std::forward<Args>(args)...);        \
            })                                                                 \
        {                                                                      \
        }                                                                      \
                                                                               \
        template<auto F, class T>                                              \
        function_ref(nontype_t<F> f,                                           \
                     std::reference_wrapper<T> obj) noexcept requires          \
            signature::template is_memfn_invocable_using<decltype(F),          \
                                                         decltype(obj)>        \
            : function_ref(f, obj.get())                                       \
        {                                                                      \
        }                                                                      \
                                                                               \
        constexpr R operator()(Args... args) const                             \
            noexcept(signature::is_noexcept)                                   \
        {                                                                      \
            return fptr_(obj_, std::forward<Args>(args)...);                   \
        }                                                                      \
                                                                               \
        constexpr void swap(function_ref &other) noexcept                      \
        {                                                                      \
            std::swap(obj_, other.obj_);                                       \
            std::swap(fptr_, other.fptr_);                                     \
        }                                                                      \
                                                                               \
        friend constexpr void swap(function_ref &lhs,                          \
                                   function_ref &rhs) noexcept                 \
        {                                                                      \
            lhs.swap(rhs);                                                     \
        }                                                                      \
    };

_FUNCTION_REF_SPECIALIZATION(R(Args...), T);
_FUNCTION_REF_SPECIALIZATION(R(Args...) noexcept, T);
_FUNCTION_REF_SPECIALIZATION(R(Args...) const, T const);
_FUNCTION_REF_SPECIALIZATION(R(Args...) const noexcept, T const);

#undef _FUNCTION_REF_SPECIALIZATION

template<class T> struct _adapt_signature;

template<class F>
requires std::is_function_v<F>
struct _adapt_signature<F *>
{
    using type = F;
};

template<class Fp> using _adapt_signature_t = _adapt_signature<Fp>::type;

template<class T> struct _drop_first_arg_to_invoke;

template<class R, class T, class... Args>
struct _drop_first_arg_to_invoke<R (*)(T, Args...)>
{
    using type = R(Args...);
};

template<class R, class T, class... Args>
struct _drop_first_arg_to_invoke<R (*)(T, Args...) noexcept>
{
    using type = R(Args...);
};

template<class T, class Cls>
requires std::is_object_v<T>
struct _drop_first_arg_to_invoke<T Cls::*>
{
    using type = T();
};

template<class T, class Cls>
requires std::is_function_v<T>
struct _drop_first_arg_to_invoke<T Cls::*>
{
    using type = T;
};

template<class Fp>
using _drop_first_arg_to_invoke_t = _drop_first_arg_to_invoke<Fp>::type;

// clang-format off

template<class F>
requires std::is_function_v<F>
function_ref(F *) -> function_ref<F>;

// clang-format on

template<auto V>
function_ref(nontype_t<V>) -> function_ref<_adapt_signature_t<decltype(V)>>;

template<auto V>
function_ref(nontype_t<V>, auto)
    -> function_ref<_drop_first_arg_to_invoke_t<decltype(V)>>;

} // namespace std23
