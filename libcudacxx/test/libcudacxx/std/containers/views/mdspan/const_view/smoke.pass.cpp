//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// Smoke test for cuda::std::const_view customization point object.
// Covers the two resolution paths (ADL hook, library default_accessor /
// aligned_accessor built-ins), the wrap-and-hook pattern for third-party
// accessors, and submdspan composition.

#include <cuda/std/cassert>
#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

// --------------------------------------------------------------------------
// Scenario 1 — default_accessor (library-provided built-in).
// --------------------------------------------------------------------------

__host__ __device__ void scenario1_default_accessor()
{
  using E  = cuda::std::extents<int, 3>;
  double storage[3] = {10, 20, 30};
  cuda::std::mdspan<double, E> md{storage, E{}};

  auto ro = cuda::std::const_view(md);
  using RO = decltype(ro);

  static_assert(cuda::std::is_same_v<typename RO::element_type, const double>);
  static_assert(cuda::std::is_same_v<typename RO::accessor_type, cuda::std::default_accessor<const double>>);
  static_assert(cuda::std::is_same_v<typename RO::extents_type, E>);

  assert(ro.data_handle() == md.data_handle());
  assert(ro[0] == storage[0]);
  assert(ro[2] == storage[2]);
}

// --------------------------------------------------------------------------
// Scenario 1b — aligned_accessor (library-provided built-in).
// --------------------------------------------------------------------------

__host__ __device__ void scenario1b_aligned_accessor()
{
  using E = cuda::std::extents<int, 4>;
  using A = cuda::std::aligned_accessor<double, 32>;
  using MD = cuda::std::mdspan<double, E, cuda::std::layout_right, A>;

  alignas(32) double storage[4] = {1, 2, 3, 4};
  MD md{storage, E{}, A{}};

  auto ro = cuda::std::const_view(md);
  using RO = decltype(ro);

  static_assert(cuda::std::is_same_v<typename RO::accessor_type,
                                     cuda::std::aligned_accessor<const double, 32>>);
  static_assert(cuda::std::is_same_v<typename RO::element_type, const double>);
}

// --------------------------------------------------------------------------
// Scenario 2 — custom accessor, author opts in via ADL tag_invoke hook.
// --------------------------------------------------------------------------

namespace user_lib {

template <class T>
struct atomic_accessor {
  using offset_policy    = atomic_accessor;
  using element_type     = T;
  using reference        = T&;
  using data_handle_type = T*;

  _CCCL_HIDE_FROM_ABI constexpr atomic_accessor() noexcept = default;

  _CCCL_TEMPLATE(class _Other)
  _CCCL_REQUIRES(cuda::std::is_convertible_v<_Other (*)[], element_type (*)[]>)
  _CCCL_API constexpr atomic_accessor(atomic_accessor<_Other>) noexcept {}

  [[nodiscard]] _CCCL_API constexpr reference
  access(data_handle_type __p, cuda::std::size_t __i) const noexcept { return __p[__i]; }

  [[nodiscard]] _CCCL_API constexpr data_handle_type
  offset(data_handle_type __p, cuda::std::size_t __i) const noexcept { return __p + __i; }
};

// Author-provided ADL hook. Found via ADL on atomic_accessor<T>.
template <class T>
_CCCL_API constexpr atomic_accessor<const T>
tag_invoke(::cuda::std::__const_view_impl::const_view_fn_tag, atomic_accessor<T>) noexcept
{
  return {};
}

} // namespace user_lib

__host__ __device__ void scenario2_adl_hook()
{
  using E  = cuda::std::extents<int, 4>;
  using A  = user_lib::atomic_accessor<double>;
  using MD = cuda::std::mdspan<double, E, cuda::std::layout_right, A>;

  double storage[4] = {1, 2, 3, 4};
  MD md{storage, E{}, A{}};

  auto ro = cuda::std::const_view(md);
  using RO = decltype(ro);

  static_assert(cuda::std::is_same_v<typename RO::accessor_type,
                                     user_lib::atomic_accessor<const double>>);
  static_assert(cuda::std::is_same_v<typename RO::element_type, const double>);

  assert(ro.data_handle() == md.data_handle());
  assert(ro[0] == storage[0]);
  assert(ro[3] == storage[3]);
}

// --------------------------------------------------------------------------
// Scenario 3 — third-party accessor the consumer cannot modify.
// The vendor did not provide a tag_invoke hook; the consumer wraps the
// vendor accessor in a local adapter and puts the ADL hook on the adapter.
// --------------------------------------------------------------------------

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

// Vendor provides no tag_invoke hook. The consumer cannot modify this
// namespace.

} // namespace vendor

// Consumer's own adapter around vendor::gpu_accessor. Forwards the accessor
// protocol. The ADL hook lives on the adapter, in the consumer's namespace.
namespace consumer {

template <class T>
struct gpu_adapter {
  using offset_policy    = gpu_adapter;
  using element_type     = T;
  using reference        = T&;
  using data_handle_type = T*;

  vendor::gpu_accessor<T> base_;

  _CCCL_HIDE_FROM_ABI constexpr gpu_adapter() noexcept = default;

  _CCCL_TEMPLATE(class _Other)
  _CCCL_REQUIRES(cuda::std::is_convertible_v<_Other (*)[], element_type (*)[]>)
  _CCCL_API constexpr gpu_adapter(gpu_adapter<_Other> __o) noexcept
    : base_(__o.base_) {}

  [[nodiscard]] _CCCL_API constexpr reference
  access(data_handle_type __p, cuda::std::size_t __i) const noexcept { return base_.access(__p, __i); }

  [[nodiscard]] _CCCL_API constexpr data_handle_type
  offset(data_handle_type __p, cuda::std::size_t __i) const noexcept { return base_.offset(__p, __i); }
};

template <class T>
_CCCL_API constexpr gpu_adapter<const T>
tag_invoke(::cuda::std::__const_view_impl::const_view_fn_tag, gpu_adapter<T>) noexcept
{
  return {};
}

} // namespace consumer

__host__ __device__ void scenario3_wrap_and_hook()
{
  using E = cuda::std::extents<int, 2>;
  using A = consumer::gpu_adapter<double>;
  using MD = cuda::std::mdspan<double, E, cuda::std::layout_right, A>;

  double storage[2] = {7, 8};
  MD md{storage, E{}, A{}};

  auto ro = cuda::std::const_view(md);
  using RO = decltype(ro);

  static_assert(cuda::std::is_same_v<typename RO::accessor_type,
                                     consumer::gpu_adapter<const double>>);
  assert(ro.data_handle() == md.data_handle());
  assert(ro[0] == storage[0]);
  assert(ro[1] == storage[1]);
}

// --------------------------------------------------------------------------
// Composition: submdspan and const_view commute.
// --------------------------------------------------------------------------

__host__ __device__ void composition_default_accessor()
{
  using E = cuda::std::extents<int, 4, 5>;
  using MD = cuda::std::mdspan<double, E>;
  double storage[20]{};
  MD md{storage, E{}};

  auto left  = cuda::std::submdspan(cuda::std::const_view(md),
                                    cuda::std::full_extent, 0);
  auto right = cuda::std::const_view(
                 cuda::std::submdspan(md, cuda::std::full_extent, 0));

  static_assert(cuda::std::is_same_v<decltype(left), decltype(right)>);
}

int main(int, char**)
{
  scenario1_default_accessor();
  scenario1b_aligned_accessor();
  scenario2_adl_hook();
  scenario3_wrap_and_hook();
  composition_default_accessor();
  return 0;
}
