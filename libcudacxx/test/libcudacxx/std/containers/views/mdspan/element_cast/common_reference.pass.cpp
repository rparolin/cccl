//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>
//
// Design demonstration for the cross-const basic_common_reference
// specializations proposed in the mdspan const-accessor customization
// point paper.
//
// The motivating gap: common_reference_with fails for cross-const pairs
// of proxy references — for example:
//
//   common_reference_with<atomic_ref<T>,       const T&>  // fails
//   common_reference_with<atomic_ref<const T>, T&>        // fails
//
// The paper proposes basic_common_reference specializations in <atomic>
// for these pairs. libcudacxx does not yet implement atomic_ref<const T>
// (still pre-P3323R1 state), so we cannot add the real specializations
// to the production header without substantial upstream work. This test
// uses a minimal `fake_atomic_ref<T>` that mimics the C++26 shape — with
// both cv-qualified and cv-unqualified instantiations — to validate the
// design of the specializations end to end.
//
// Claim proven: with the specializations below, common_reference_with
// succeeds for all four cross-const pairs that originally failed.

#include <cuda/std/concepts>
#include <cuda/std/type_traits>
#include "test_macros.h"

// ---------------------------------------------------------------------------
// Minimal proxy type mimicking C++26 atomic_ref<T>.
// ---------------------------------------------------------------------------

template <class T>
struct fake_atomic_ref
{
  T* __p;
  _CCCL_API constexpr fake_atomic_ref(T& __t) noexcept : __p(&__t) {}
  _CCCL_API constexpr operator T() const noexcept { return *__p; }
  _CCCL_API constexpr T load() const noexcept { return *__p; }
  _CCCL_API constexpr void store(T __v) const noexcept { *__p = __v; }
};

// atomic_ref<const T>: read-only; no store().
template <class T>
struct fake_atomic_ref<const T>
{
  const T* __p;
  _CCCL_API constexpr fake_atomic_ref(const T& __t) noexcept : __p(&__t) {}
  _CCCL_API constexpr operator T() const noexcept { return *__p; }
  _CCCL_API constexpr T load() const noexcept { return *__p; }
};

// ---------------------------------------------------------------------------
// Proposed basic_common_reference specializations (per paper proposal).
// ---------------------------------------------------------------------------
//
// Two things to notice about the specializations below:
//
//   1. They live in `cuda::std::` because that is libcudacxx's namespace.
//      The actual paper proposes the equivalent specializations in `std::`.
//
//   2. Each specialization sets the resulting `type` to the plain value
//      type `T`. That is the minimum needed to make `common_reference_with`
//      accept the pair — both sides are convertible to `T`, so the
//      concept is satisfied and the motivating static_asserts at the
//      bottom of this file compile.
//
//      `T` is NOT what production wording should ultimately pick. A real
//      `common_reference_t<A, B>` should preserve cv and reference
//      qualifiers per the C++ common-reference rules (for example,
//      `const T&` for the read-only cases). Getting that exactly right
//      is a wording concern the paper's R1 revision will address. For
//      this test — which only proves the gap can be closed — the
//      simplification is intentional.

_CCCL_BEGIN_NAMESPACE_CUDA_STD

// fake_atomic_ref<T>  <->  T
template <class T, template<class> class _TQual, template<class> class _UQual>
struct basic_common_reference<fake_atomic_ref<T>, T, _TQual, _UQual>
{
  using type = T;
};

template <class T, template<class> class _TQual, template<class> class _UQual>
struct basic_common_reference<T, fake_atomic_ref<T>, _TQual, _UQual>
{
  using type = T;
};

// fake_atomic_ref<const T>  <->  T
template <class T, template<class> class _TQual, template<class> class _UQual>
struct basic_common_reference<fake_atomic_ref<const T>, T, _TQual, _UQual>
{
  using type = T;
};

template <class T, template<class> class _TQual, template<class> class _UQual>
struct basic_common_reference<T, fake_atomic_ref<const T>, _TQual, _UQual>
{
  using type = T;
};

// fake_atomic_ref<T>  <->  fake_atomic_ref<const T>
template <class T, template<class> class _TQual, template<class> class _UQual>
struct basic_common_reference<fake_atomic_ref<T>, fake_atomic_ref<const T>, _TQual, _UQual>
{
  using type = T;
};

template <class T, template<class> class _TQual, template<class> class _UQual>
struct basic_common_reference<fake_atomic_ref<const T>, fake_atomic_ref<T>, _TQual, _UQual>
{
  using type = T;
};

_CCCL_END_NAMESPACE_CUDA_STD

// ---------------------------------------------------------------------------
// The four cross-const cases called out in the motivation — all pass
// with the specializations above.
// ---------------------------------------------------------------------------

static_assert(cuda::std::common_reference_with<fake_atomic_ref<float>,       const float&>);
static_assert(cuda::std::common_reference_with<fake_atomic_ref<const float>, float&>);
static_assert(cuda::std::common_reference_with<const float&, fake_atomic_ref<float>>);
static_assert(cuda::std::common_reference_with<float&,       fake_atomic_ref<const float>>);

// The cross-proxy pair also passes.
static_assert(cuda::std::common_reference_with<fake_atomic_ref<float>,       fake_atomic_ref<const float>>);
static_assert(cuda::std::common_reference_with<fake_atomic_ref<const float>, fake_atomic_ref<float>>);

// And the same-const diagonals continue to work.
static_assert(cuda::std::common_reference_with<fake_atomic_ref<float>,       float&>);
static_assert(cuda::std::common_reference_with<fake_atomic_ref<const float>, const float&>);

int main(int, char**)
{
  return 0;
}
