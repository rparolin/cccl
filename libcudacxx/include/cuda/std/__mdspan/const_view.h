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

namespace __const_view_impl
{

// ADL poison: ensures that unqualified `const_view(a)` inside the CPO's
// operator() does not find the CPO object itself (cuda::std::const_view) —
// it finds user-supplied free functions via ADL instead.
//
// Users customize by providing a `const_view(MyAccessor)` free function in
// their own namespace; ordinary ADL from the CPO's body reaches it.
void const_view() = delete;

template <class _Accessor>
_CCCL_CONCEPT __has_adl_hook = _CCCL_REQUIRES_EXPR((_Accessor), _Accessor __a)(const_view(__a));

template <class>
struct __is_default_accessor_of : false_type
{};

template <class _ElementType>
struct __is_default_accessor_of<default_accessor<_ElementType>> : true_type
{};

template <class _Accessor>
_CCCL_CONCEPT __is_default_accessor_like = __is_default_accessor_of<_Accessor>::value;

template <class>
struct __is_aligned_accessor_of : false_type
{};

template <class _ElementType, size_t _ByteAlignment>
struct __is_aligned_accessor_of<aligned_accessor<_ElementType, _ByteAlignment>> : true_type
{};

template <class _Accessor>
_CCCL_CONCEPT __is_aligned_accessor_like = __is_aligned_accessor_of<_Accessor>::value;

struct const_view_fn
{
  // Path 1: ADL-found `const_view(A)` free function in the accessor's namespace.
  _CCCL_TEMPLATE(class _Accessor)
  _CCCL_REQUIRES(__has_adl_hook<_Accessor>)
  [[nodiscard]] _CCCL_API constexpr auto operator()(_Accessor __a) const
    noexcept(noexcept(const_view(__a))) //
    -> decltype(const_view(__a))
  {
    return const_view(__a);
  }

  // Path 2a: built-in for default_accessor.
  _CCCL_TEMPLATE(class _Accessor)
  _CCCL_REQUIRES((!__has_adl_hook<_Accessor>) _CCCL_AND __is_default_accessor_like<_Accessor>)
  [[nodiscard]] _CCCL_API constexpr auto operator()(_Accessor) const noexcept //
    -> default_accessor<add_const_t<typename _Accessor::element_type>>
  {
    return {};
  }

  // Path 2b: built-in for aligned_accessor.
  _CCCL_TEMPLATE(class _ElementType, size_t _ByteAlignment)
  _CCCL_REQUIRES((!__has_adl_hook<aligned_accessor<_ElementType, _ByteAlignment>>))
  [[nodiscard]] _CCCL_API constexpr auto operator()(aligned_accessor<_ElementType, _ByteAlignment>) const noexcept //
    -> aligned_accessor<add_const_t<_ElementType>, _ByteAlignment>
  {
    return {};
  }

  // mdspan overload: returns an mdspan with const element type over the same
  // data, using const_view to pick the const-counterpart accessor.
  //
  // SFINAE on the return type: if `(*this)(md.accessor())` is ill-formed,
  // the return type is ill-formed and this overload is removed from overload
  // resolution.
  template <class _ElementType, class _Extents, class _LayoutPolicy, class _Accessor>
  [[nodiscard]] _CCCL_API constexpr auto operator()(const mdspan<_ElementType, _Extents, _LayoutPolicy, _Accessor>& __md) const
    -> mdspan<add_const_t<_ElementType>,
              _Extents,
              _LayoutPolicy,
              decltype(::cuda::std::declval<const const_view_fn&>()(__md.accessor()))>
  {
    using _ConstAccessor = decltype((*this)(__md.accessor()));
    using _Result        = mdspan<add_const_t<_ElementType>, _Extents, _LayoutPolicy, _ConstAccessor>;
    return _Result{typename _ConstAccessor::data_handle_type{__md.data_handle()}, __md.mapping(), (*this)(__md.accessor())};
  }
};

} // namespace __const_view_impl

_CCCL_GLOBAL_CONSTANT auto const_view = __const_view_impl::const_view_fn{};

_CCCL_END_NAMESPACE_CUDA_STD

#include <cuda/std/__cccl/epilogue.h>

#endif // _CUDA_STD___MDSPAN_CONST_VIEW_H
