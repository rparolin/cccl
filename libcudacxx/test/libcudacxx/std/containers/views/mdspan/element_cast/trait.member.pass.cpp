//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// const_accessor_for prefers A::const_accessor_type when present.

#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

template <class T>
struct accessor_with_member {
  using offset_policy       = accessor_with_member;
  using element_type        = T;
  using reference           = T&;
  using data_handle_type    = T*;
  using const_accessor_type = accessor_with_member<const T>;
};

// Distinguishable from template substitution: a type substitution would NOT produce.
template <class T>
struct accessor_member_divergent {
  using offset_policy    = accessor_member_divergent;
  using element_type     = T;
  using reference        = T&;
  using data_handle_type = T*;
  struct const_variant {
    using offset_policy    = const_variant;
    using element_type     = const T;
    using reference        = const T&;
    using data_handle_type = const T*;
  };
  using const_accessor_type = const_variant;
};

__host__ __device__ void test_member_matches_substitution()
{
  using A = accessor_with_member<int>;
  static_assert(cuda::std::is_same_v<cuda::std::const_accessor_for_t<A>,
                                     accessor_with_member<const int>>);
}

__host__ __device__ void test_member_wins_over_substitution()
{
  using A = accessor_member_divergent<int>;
  static_assert(cuda::std::is_same_v<cuda::std::const_accessor_for_t<A>,
                                     typename A::const_variant>);
}

int main(int, char**)
{
  test_member_matches_substitution();
  test_member_wins_over_substitution();
  return 0;
}
