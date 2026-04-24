//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// cuda::std::const_view(md) must be ill-formed when the mdspan's accessor
// has no ADL-found `const_view` free function and is not one of the
// library-built-in accessors (default_accessor, aligned_accessor).

#include <cuda/std/mdspan>

// Non-template accessor; no ADL hook; no const_view_override.
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

  // Expected: ill-formed — no resolution path applies.
  [[maybe_unused]] auto bad = cuda::std::const_view(md);

  return 0;
}
