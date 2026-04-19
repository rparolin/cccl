//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// Trait substitution-fallback: const_accessor_for_t<Foo<T>> == Foo<const T>

#include <cuda/std/mdspan>
#include <cuda/std/type_traits>

#include "test_macros.h"

template <class T>
struct template_accessor
{
  using offset_policy    = template_accessor;
  using element_type     = T;
  using reference        = T&;
  using data_handle_type = T*;
};

__host__ __device__ void test_default_accessor()
{
  using A = cuda::std::default_accessor<int>;
  using C = cuda::std::const_accessor_for_t<A>;
  static_assert(cuda::std::is_same_v<C, cuda::std::default_accessor<const int>>);
}

__host__ __device__ void test_user_template_accessor()
{
  using A = template_accessor<double>;
  using C = cuda::std::const_accessor_for_t<A>;
  static_assert(cuda::std::is_same_v<C, template_accessor<const double>>);
}

int main(int, char**)
{
  test_default_accessor();
  test_user_template_accessor();
  return 0;
}
