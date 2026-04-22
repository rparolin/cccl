// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

#ifndef _CUDA_STD___MDSPAN_CONST_VIEW_H
#define _CUDA_STD___MDSPAN_CONST_VIEW_H

#include <cuda/std/detail/__config>

#if defined(_CCCL_IMPLICIT_SYSTEM_HEADER_GCC)
#  pragma GCC system_header
#elif defined(_CCCL_IMPLICIT_SYSTEM_HEADER_CLANG)
#  pragma clang system_header
#elif defined(_CCCL_IMPLICIT_SYSTEM_HEADER_MSVC)
#  pragma system_header
#endif // no system header

#include <cuda/std/__concepts/concept_macros.h>
#include <cuda/std/__fwd/mdspan.h>
#include <cuda/std/__mdspan/aligned_accessor.h>
#include <cuda/std/__mdspan/default_accessor.h>
#include <cuda/std/__type_traits/add_const.h>
#include <cuda/std/__type_traits/is_same.h>
#include <cuda/std/__utility/declval.h>
#include <cuda/std/cstddef>

#include <cuda/std/__cccl/prologue.h>

_CCCL_BEGIN_NAMESPACE_CUDA_STD

// Escape-hatch trait for third-party accessors: users specialize this in
// cuda::std for accessors whose authors did not provide a tag_invoke hook
// and which the user cannot modify.
template <class _A>
struct const_view_override
{};

namespace __const_view_impl
{

// ADL dispatch tag. Users' hooks look like:
//   tag_invoke(cuda::std::__const_view_impl::const_view_fn_tag{}, MyAccessor<T>)
struct const_view_fn_tag
{};

template <class _A>
_CCCL_CONCEPT __has_adl_hook =
  _CCCL_REQUIRES_EXPR((_A), _A __a)(tag_invoke(const_view_fn_tag{}, __a));

template <class _A>
_CCCL_CONCEPT __has_trait_override =
  _CCCL_REQUIRES_EXPR((_A))(typename(typename const_view_override<_A>::type));

template <class _A>
_CCCL_CONCEPT __is_default_accessor_like =
  _CCCL_REQUIRES_EXPR((_A))(typename(typename _A::element_type))
  && is_same_v<_A, default_accessor<typename _A::element_type>>;

template <class>
struct __is_aligned_accessor_of : false_type
{};

template <class _T, size_t _N>
struct __is_aligned_accessor_of<aligned_accessor<_T, _N>> : true_type
{};

template <class _A>
_CCCL_CONCEPT __is_aligned_accessor_like = __is_aligned_accessor_of<_A>::value;

struct const_view_fn
{
  // Path 1: ADL tag_invoke hook.
  _CCCL_TEMPLATE(class _A)
  _CCCL_REQUIRES(__has_adl_hook<_A>)
  [[nodiscard]] _CCCL_API constexpr auto operator()(_A __a) const
    noexcept(noexcept(tag_invoke(const_view_fn_tag{}, __a))) //
    -> decltype(tag_invoke(const_view_fn_tag{}, __a))
  {
    return tag_invoke(const_view_fn_tag{}, __a);
  }

  // Path 2: trait override.
  _CCCL_TEMPLATE(class _A)
  _CCCL_REQUIRES((!__has_adl_hook<_A>) _CCCL_AND __has_trait_override<_A>)
  [[nodiscard]] _CCCL_API constexpr auto operator()(_A) const noexcept //
    -> typename const_view_override<_A>::type
  {
    return typename const_view_override<_A>::type{};
  }

  // Path 3a: built-in for default_accessor.
  _CCCL_TEMPLATE(class _A)
  _CCCL_REQUIRES((!__has_adl_hook<_A>) _CCCL_AND(!__has_trait_override<_A>)
                   _CCCL_AND __is_default_accessor_like<_A>)
  [[nodiscard]] _CCCL_API constexpr auto operator()(_A) const noexcept //
    -> default_accessor<add_const_t<typename _A::element_type>>
  {
    return {};
  }

  // Path 3b: built-in for aligned_accessor.
  _CCCL_TEMPLATE(class _T, size_t _N)
  _CCCL_REQUIRES((!__has_adl_hook<aligned_accessor<_T, _N>>)
                   _CCCL_AND(!__has_trait_override<aligned_accessor<_T, _N>>))
  [[nodiscard]] _CCCL_API constexpr auto operator()(aligned_accessor<_T, _N>) const noexcept //
    -> aligned_accessor<add_const_t<_T>, _N>
  {
    return {};
  }

  // mdspan overload: returns an mdspan with const element type over the same
  // data, using const_view to pick the const-counterpart accessor.
  //
  // SFINAE on the return type: if `(*this)(md.accessor())` is ill-formed,
  // the return type is ill-formed and this overload is removed from overload
  // resolution.
  template <class _T, class _E, class _L, class _A>
  [[nodiscard]] _CCCL_API constexpr auto operator()(const mdspan<_T, _E, _L, _A>& __md) const
    -> mdspan<add_const_t<_T>,
              _E,
              _L,
              decltype(::cuda::std::declval<const const_view_fn&>()(__md.accessor()))>
  {
    using _C      = decltype((*this)(__md.accessor()));
    using _Result = mdspan<add_const_t<_T>, _E, _L, _C>;
    return _Result{typename _C::data_handle_type{__md.data_handle()}, __md.mapping(), (*this)(__md.accessor())};
  }
};

} // namespace __const_view_impl

inline constexpr __const_view_impl::const_view_fn const_view{};

_CCCL_END_NAMESPACE_CUDA_STD

#include <cuda/std/__cccl/epilogue.h>

#endif // _CUDA_STD___MDSPAN_CONST_VIEW_H
