//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// Scenario 3: third-party accessor, user-provided const_accessor_for specialization.

#include <cuda/std/cassert>
#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

namespace vendor {
template <class T>
struct gpu_accessor {
  using offset_policy    = gpu_accessor;
  using element_type     = T;
  using reference        = T&;
  using data_handle_type = T*;

  _CCCL_HIDE_FROM_ABI constexpr gpu_accessor() noexcept = default;

  _CCCL_TEMPLATE(class _Other)
  _CCCL_REQUIRES(cuda::std::is_convertible_v<_Other (*)[], element_type (*)[]>)
  _CCCL_API constexpr gpu_accessor(gpu_accessor<_Other>) noexcept {}

  [[nodiscard]] _CCCL_API constexpr reference
  access(data_handle_type __p, cuda::std::size_t __i) const noexcept { return __p[__i]; }

  [[nodiscard]] _CCCL_API constexpr data_handle_type
  offset(data_handle_type __p, cuda::std::size_t __i) const noexcept { return __p + __i; }
};
} // namespace vendor

// User specialization in cuda::std.
_CCCL_BEGIN_NAMESPACE_CUDA_STD
template <class T>
struct const_accessor_for<vendor::gpu_accessor<T>>
{
  using type = vendor::gpu_accessor<const T>;
};
_CCCL_END_NAMESPACE_CUDA_STD

__host__ __device__ void test()
{
  using E = cuda::std::extents<int, 2>;
  using A = vendor::gpu_accessor<double>;
  using MD = cuda::std::mdspan<double, E, cuda::std::layout_right, A>;

  double storage[2] = {7, 8};
  MD md{storage, E{}, A{}};

  auto ro = cuda::std::element_cast<const double>(md);
  using RO = decltype(ro);
  static_assert(cuda::std::is_same_v<typename RO::accessor_type,
                                     vendor::gpu_accessor<const double>>);
  assert(ro.data_handle() == md.data_handle());
  assert(ro[0] == 7);
  assert(ro[1] == 8);
}

int main(int, char**)
{
  test();
  return 0;
}
