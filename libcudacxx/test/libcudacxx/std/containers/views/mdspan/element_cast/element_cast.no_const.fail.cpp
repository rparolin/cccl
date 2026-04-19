//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// Accessor without a valid const counterpart: element_cast<const T>(md) is ill-formed.

#include <cuda/std/mdspan>

// Non-template accessor; no const_accessor_type; no user specialization.
struct opaque_accessor {
  using offset_policy    = opaque_accessor;
  using element_type     = double;
  using reference        = double&;
  using data_handle_type = double*;

  reference access(data_handle_type p, cuda::std::size_t i) const noexcept { return p[i]; }
  data_handle_type offset(data_handle_type p, cuda::std::size_t i) const noexcept { return p + i; }
};

int main(int, char**)
{
  using E = cuda::std::extents<int, 3>;
  using MD = cuda::std::mdspan<double, E, cuda::std::layout_right, opaque_accessor>;
  double storage[3]{};
  MD md{storage, E{}, opaque_accessor{}};

  // Expected: ill-formed. const_accessor_for<opaque_accessor> has no 'type'.
  [[maybe_unused]] auto bad = cuda::std::element_cast<const double>(md);

  return 0;
}
