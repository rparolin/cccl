//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// element_cast<T>(md) is only well-formed when T is the mdspan's
// element_type or add_const_t<element_type>. Any other T is a compile error.

#include <cuda/std/mdspan>

int main(int, char**)
{
  using E = cuda::std::extents<int, 3>;
  double storage[3]{};
  cuda::std::mdspan<double, E> md{storage, E{}};

  // Expected: ill-formed. 'int' is neither 'double' nor 'const double'.
  [[maybe_unused]] auto bad = cuda::std::element_cast<int>(md);

  return 0;
}
