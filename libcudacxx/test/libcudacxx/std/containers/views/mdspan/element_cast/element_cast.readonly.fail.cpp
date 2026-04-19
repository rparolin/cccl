//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// The result of element_cast<const T>(md) has a non-writable reference; any
// attempt to assign through it must fail to compile.

#include <cuda/std/mdspan>

int main(int, char**)
{
  using E = cuda::std::extents<int, 3>;
  double storage[3] = {1.0, 2.0, 3.0};
  cuda::std::mdspan<double, E> md{storage, E{}};

  auto ro = cuda::std::element_cast<const double>(md);

  // Expected: ill-formed. ro's reference is `const double&`; assigning
  // through it is not permitted.
  ro[0] = 99.0;

  return 0;
}
