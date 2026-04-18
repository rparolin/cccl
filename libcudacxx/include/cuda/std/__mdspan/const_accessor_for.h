// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

#ifndef _CUDA_STD___MDSPAN_CONST_ACCESSOR_FOR_H
#define _CUDA_STD___MDSPAN_CONST_ACCESSOR_FOR_H

#include <cuda/std/detail/__config>

#if defined(_CCCL_IMPLICIT_SYSTEM_HEADER_GCC)
#  pragma GCC system_header
#elif defined(_CCCL_IMPLICIT_SYSTEM_HEADER_CLANG)
#  pragma clang system_header
#elif defined(_CCCL_IMPLICIT_SYSTEM_HEADER_MSVC)
#  pragma system_header
#endif // no system header

#include <cuda/std/__type_traits/add_const.h>

#include <cuda/std/__cccl/prologue.h>

_CCCL_BEGIN_NAMESPACE_CUDA_STD

// Primary template — intentionally undefined so ill-formed cases produce
// a clean "no type named 'type'" diagnostic at the call site.
template <class _A>
struct const_accessor_for;

// Substitution-fallback partial specialization for template accessors of the
// form Tmpl<T, Rest...>. Member-hook priority will be added in a follow-up
// commit.
template <template <class, class...> class _Tmpl, class _T, class... _Rest>
struct const_accessor_for<_Tmpl<_T, _Rest...>>
{
  using type = _Tmpl<add_const_t<_T>, _Rest...>;
};

template <class _A>
using const_accessor_for_t = typename const_accessor_for<_A>::type;

_CCCL_END_NAMESPACE_CUDA_STD

#include <cuda/std/__cccl/epilogue.h>

#endif // _CUDA_STD___MDSPAN_CONST_ACCESSOR_FOR_H
