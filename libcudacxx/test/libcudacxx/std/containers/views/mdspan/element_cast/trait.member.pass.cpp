//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
// const_accessor_for's member-hook path must take priority over the
// template-substitution fallback. This test covers two cases:
//
//   1. The member alias points to Foo<const T> — the same type substitution
//      would produce. Both paths agree; the trait should return that type.
//   2. The member alias points to a DIFFERENT type than substitution would
//      produce. The trait must return the member-specified type, proving
//      the author's explicit choice is honored.

#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

// Case 1: member alias points to Foo<const T> — matches what substitution
// would produce.
template <class T>
struct accessor_member_matches_substitution {
  using offset_policy       = accessor_member_matches_substitution;
  using element_type        = T;
  using reference           = T&;
  using data_handle_type    = T*;
  using const_accessor_type = accessor_member_matches_substitution<const T>;
};

// Case 2: member alias points to `custom_const_type`, which is deliberately
// NOT accessor_member_differs_from_substitution<const T>. If the trait
// incorrectly ran substitution, it would produce the latter; the fact that
// it produces `custom_const_type` proves the member hook won.
template <class T>
struct accessor_member_differs_from_substitution {
  using offset_policy    = accessor_member_differs_from_substitution;
  using element_type     = T;
  using reference        = T&;
  using data_handle_type = T*;

  struct custom_const_type {
    using offset_policy    = custom_const_type;
    using element_type     = const T;
    using reference        = const T&;
    using data_handle_type = const T*;
  };

  using const_accessor_type = custom_const_type;
};

__host__ __device__ void test_both_paths_agree()
{
  using A = accessor_member_matches_substitution<int>;
  static_assert(cuda::std::is_same_v<cuda::std::const_accessor_for_t<A>,
                                     accessor_member_matches_substitution<const int>>);
}

__host__ __device__ void test_member_wins_when_paths_diverge()
{
  using A = accessor_member_differs_from_substitution<int>;
  static_assert(cuda::std::is_same_v<cuda::std::const_accessor_for_t<A>,
                                     typename A::custom_const_type>);
}

int main(int, char**)
{
  test_both_paths_agree();
  test_member_wins_when_paths_diverge();
  return 0;
}
