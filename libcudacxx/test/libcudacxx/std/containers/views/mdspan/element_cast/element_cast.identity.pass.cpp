//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// element_cast<T>(md) with T == element_type returns the same mdspan type.

#include <cuda/std/cassert>
#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

__host__ __device__ void test_identity_default_accessor()
{
  using E = cuda::std::extents<int, 5>;
  using MD = cuda::std::mdspan<double, E>;
  double storage[5]{};
  MD md{storage, E{}};

  auto same = cuda::std::element_cast<double>(md);
  static_assert(cuda::std::is_same_v<decltype(same), MD>);
  assert(same.data_handle() == md.data_handle());
}

__host__ __device__ void test_identity_const_element()
{
  using E = cuda::std::extents<int, 5>;
  using MD = cuda::std::mdspan<const double, E>;
  const double storage[5] = {1, 2, 3, 4, 5};
  MD md{storage, E{}};

  auto same = cuda::std::element_cast<const double>(md);
  static_assert(cuda::std::is_same_v<decltype(same), MD>);
}

int main(int, char**)
{
  test_identity_default_accessor();
  test_identity_const_element();
  return 0;
}
