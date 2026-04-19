//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// Scenario 2: custom accessor with explicit const_accessor_type member.

#include <cuda/std/cassert>
#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

template <class T>
struct minimal_proxy_accessor {
  using offset_policy       = minimal_proxy_accessor;
  using element_type        = T;
  using reference           = T&;
  using data_handle_type    = T*;
  using const_accessor_type = minimal_proxy_accessor<const T>;

  _CCCL_HIDE_FROM_ABI constexpr minimal_proxy_accessor() noexcept = default;

  _CCCL_TEMPLATE(class _Other)
  _CCCL_REQUIRES(cuda::std::is_convertible_v<_Other (*)[], element_type (*)[]>)
  _CCCL_API constexpr minimal_proxy_accessor(minimal_proxy_accessor<_Other>) noexcept {}

  [[nodiscard]] _CCCL_API constexpr reference
  access(data_handle_type __p, cuda::std::size_t __i) const noexcept { return __p[__i]; }

  [[nodiscard]] _CCCL_API constexpr data_handle_type
  offset(data_handle_type __p, cuda::std::size_t __i) const noexcept { return __p + __i; }
};

__host__ __device__ void test()
{
  using E = cuda::std::extents<int, 4>;
  using MD = cuda::std::mdspan<double, E, cuda::std::layout_right,
                               minimal_proxy_accessor<double>>;

  double storage[4] = {10, 20, 30, 40};
  MD md{storage, E{}, minimal_proxy_accessor<double>{}};

  auto ro = cuda::std::element_cast<const double>(md);
  using RO = decltype(ro);

  static_assert(cuda::std::is_same_v<typename RO::accessor_type,
                                     minimal_proxy_accessor<const double>>);
  static_assert(cuda::std::is_same_v<typename RO::element_type, const double>);

  assert(ro.data_handle() == md.data_handle());
  assert(ro[0] == storage[0]);
  assert(ro[3] == storage[3]);
}

int main(int, char**)
{
  test();
  return 0;
}
