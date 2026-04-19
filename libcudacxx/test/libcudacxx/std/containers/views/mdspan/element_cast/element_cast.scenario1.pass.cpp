//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// Scenario 1: default_accessor.
// cuda::std::element_cast<const double>(md) produces an mdspan<const double>
// over the same data with default_accessor<const double>.

#include <cuda/std/cassert>
#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

__host__ __device__ void test_type()
{
  using E = cuda::std::extents<int, 3, 4>;
  using A = cuda::std::default_accessor<double>;
  using MD = cuda::std::mdspan<double, E, cuda::std::layout_right, A>;

  double storage[12]{};
  MD md{storage, E{}};

  auto ro = cuda::std::element_cast<const double>(md);

  using RO = decltype(ro);
  static_assert(cuda::std::is_same_v<typename RO::element_type, const double>);
  static_assert(cuda::std::is_same_v<typename RO::accessor_type,
                                     cuda::std::default_accessor<const double>>);
  static_assert(cuda::std::is_same_v<typename RO::extents_type, E>);
}

__host__ __device__ void test_same_data()
{
  using E = cuda::std::extents<int, 3>;
  double storage[3] = {1.0, 2.0, 3.0};
  cuda::std::mdspan<double, E> md{storage, E{}};
  auto ro = cuda::std::element_cast<const double>(md);

  assert(ro.data_handle() == md.data_handle());
  assert(ro[0] == 1.0);
  assert(ro[1] == 2.0);
  assert(ro[2] == 3.0);
}

int main(int, char**)
{
  test_type();
  test_same_data();
  return 0;
}
