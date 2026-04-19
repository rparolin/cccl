//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// submdspan and element_cast<const T> compose in either order to produce
// the same mdspan type.

#include <cuda/std/cassert>
#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

__host__ __device__ void test_default_accessor_composition()
{
  using E = cuda::std::extents<int, 4, 5>;
  using MD = cuda::std::mdspan<double, E>;
  double storage[20]{};
  MD md{storage, E{}};

  auto left  = cuda::std::submdspan(cuda::std::element_cast<const double>(md),
                                    cuda::std::full_extent, 0);
  auto right = cuda::std::element_cast<const double>(
                 cuda::std::submdspan(md, cuda::std::full_extent, 0));

  static_assert(cuda::std::is_same_v<decltype(left), decltype(right)>);
}

int main(int, char**)
{
  test_default_accessor_composition();
  return 0;
}
