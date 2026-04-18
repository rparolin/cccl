# mdspan Const-Accessor Customization Point (libcudacxx Prototype) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prototype the "const version of mdspan accessor" customization point in libcudacxx, as specified in `docs/superpowers/specs/2026-04-18-mdspan-const-accessor-cp-design.md`.

**Architecture:** Introduce a trait `cuda::std::const_accessor_for<A>` with member-then-substitution resolution (Approach C, "hybrid"). Add `const_accessor_type` member aliases to `default_accessor` and `aligned_accessor`. Add a `cuda::std::as_const(mdspan)` overload in `<cuda/std/mdspan>`. Validate with compile-time tests for all three design scenarios, submdspan composition, and the ill-formed case.

**Tech Stack:** C++17+ via libcudacxx macros (`_CCCL_API`, `_CCCL_TEMPLATE`, `_CCCL_REQUIRES`, `_CCCL_CONCEPT`, `_CCCL_BEGIN_NAMESPACE_CUDA_STD`). Tests via libcudacxx's lit-based `.pass.cpp` / `.fail.cpp` suite; built through CMake.

---

## File Structure

**New files**:
- `libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h` — primary template, specializations, helper alias.
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.default.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.member.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.specialization.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.scenario1.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.scenario2.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.composition.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.no_const.fail.cpp`

**Modified files**:
- `libcudacxx/include/cuda/std/__mdspan/default_accessor.h` — add `const_accessor_type` alias.
- `libcudacxx/include/cuda/std/__mdspan/aligned_accessor.h` — add `const_accessor_type` alias.
- `libcudacxx/include/cuda/std/__mdspan/mdspan.h` — add `cuda::std::as_const(mdspan)` overload.
- `libcudacxx/include/cuda/std/mdspan` (umbrella) — include the new trait header.

Commit at the end of every task.

---

## Task 1: Create trait skeleton with substitution-fallback only

**Files:**
- Create: `libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h`
- Modify: `libcudacxx/include/cuda/std/mdspan` (include the new header)
- Create test: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.default.pass.cpp`

- [ ] **Step 1: Write the failing test first**

Create `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.default.pass.cpp`:

```cpp
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

// <mdspan>

// Trait substitution-fallback: const_accessor_for_t<Foo<T>> == Foo<const T>

#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

template <class T>
struct template_accessor {
  using offset_policy    = template_accessor;
  using element_type     = T;
  using reference        = T&;
  using data_handle_type = T*;
};

__host__ __device__ void test_default_accessor()
{
  using A = cuda::std::default_accessor<int>;
  using C = cuda::std::const_accessor_for_t<A>;
  static_assert(cuda::std::is_same_v<C, cuda::std::default_accessor<const int>>);
}

__host__ __device__ void test_user_template_accessor()
{
  using A = template_accessor<double>;
  using C = cuda::std::const_accessor_for_t<A>;
  static_assert(cuda::std::is_same_v<C, template_accessor<const double>>);
}

int main(int, char**)
{
  test_default_accessor();
  test_user_template_accessor();
  return 0;
}
```

- [ ] **Step 2: Run test to confirm it fails**

Build and run the single test (command pattern — adjust to repo's standard test driver):

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.trait.default
```

Expected: FAIL with "no type named `const_accessor_for_t` in namespace `cuda::std`" (header doesn't exist yet).

- [ ] **Step 3: Create the trait header with substitution default only**

Create `libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h`:

```cpp
// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
//
//===----------------------------------------------------------------------===//

#ifndef _CUDA_STD___MDSPAN_CONST_ACCESSOR_FOR_H
#define _CUDA_STD___MDSPAN_CONST_ACCESSOR_FOR_H

#include <cuda/std/detail/__config>

#if defined(_CCCL_IMPLICIT_SYSTEM_HEADER_GCC)
#  pragma GCC system_header
#elif defined(_CCCL_IMPLICIT_SYSTEM_HEADER_CLANG)
#  pragma clang system_header
#elif defined(_CCCL_IMPLICIT_SYSTEM_HEADER_MSVC)
#  pragma system_header
#endif // no system header

#include <cuda/std/__type_traits/add_const.h>

#include <cuda/std/__cccl/prologue.h>

_CCCL_BEGIN_NAMESPACE_CUDA_STD

// Primary template — intentionally undefined so that ill-formed cases produce
// a clean "no type named 'type'" diagnostic at the call site.
template <class _A>
struct const_accessor_for;

// Substitution-fallback partial specialization for template accessors of the
// form Tmpl<T, Rest...>. This specialization is subsumed by an
// opt-in partial specialization (added in the next task) when the accessor
// provides a nested `const_accessor_type`.
template <template <class, class...> class _Tmpl, class _T, class... _Rest>
struct const_accessor_for<_Tmpl<_T, _Rest...>>
{
  using type = _Tmpl<add_const_t<_T>, _Rest...>;
};

template <class _A>
using const_accessor_for_t = typename const_accessor_for<_A>::type;

_CCCL_END_NAMESPACE_CUDA_STD

#include <cuda/std/__cccl/epilogue.h>

#endif // _CUDA_STD___MDSPAN_CONST_ACCESSOR_FOR_H
```

- [ ] **Step 4: Wire the header into the umbrella**

Modify `libcudacxx/include/cuda/std/mdspan` to add (alphabetical placement among the `__mdspan/*` includes):

```cpp
#include <cuda/std/__mdspan/const_accessor_for.h>
```

- [ ] **Step 5: Run the test to confirm it passes**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.trait.default
```

Expected: PASS (static_asserts compile).

- [ ] **Step 6: Commit**

```bash
git add libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h \
        libcudacxx/include/cuda/std/mdspan \
        libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.default.pass.cpp
git commit -m "[libcudacxx] Add const_accessor_for trait skeleton

Introduces cuda::std::const_accessor_for<A> with a template-substitution
fallback for accessors of the form Tmpl<T, Rest...>. Member-hook and user
specialization paths added in subsequent commits.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 2: Add member-hook priority to trait

The trait must prefer `A::const_accessor_type` over substitution. This requires (1) a concept that detects the nested alias and (2) a partial specialization that wins over the substitution default when the member exists.

**Files:**
- Modify: `libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h`
- Create test: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.member.pass.cpp`

- [ ] **Step 1: Write the failing test**

Create `trait.member.pass.cpp`:

```cpp
// <mdspan>
//
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

// Distinguishable from template substitution: different spelling of the
// const type that substitution would *not* produce.
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
  using C = cuda::std::const_accessor_for_t<A>;
  static_assert(cuda::std::is_same_v<C, accessor_with_member<const int>>);
}

__host__ __device__ void test_member_wins_over_substitution()
{
  using A = accessor_member_divergent<int>;
  using C = cuda::std::const_accessor_for_t<A>;
  static_assert(cuda::std::is_same_v<C, typename A::const_variant>);
}

int main(int, char**)
{
  test_member_matches_substitution();
  test_member_wins_over_substitution();
  return 0;
}
```

- [ ] **Step 2: Run the test to confirm it fails**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.trait.member
```

Expected: FAIL on `test_member_wins_over_substitution` — substitution produces `accessor_member_divergent<const int>`, not `A::const_variant`.

- [ ] **Step 3: Add member-hook concept and partial specialization**

Edit `libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h`. After the existing includes, add:

```cpp
#include <cuda/std/__concepts/concept_macros.h>
```

And replace the body (between `_CCCL_BEGIN_NAMESPACE_CUDA_STD` and `_CCCL_END_NAMESPACE_CUDA_STD`) with:

```cpp
template <class _A>
_CCCL_CONCEPT __has_const_accessor_type =
  _CCCL_REQUIRES_EXPR((_A))(typename _A::const_accessor_type);

// Primary template — undefined, so ill-formed cases produce a clean
// "no type named 'type'" diagnostic at the call site.
template <class _A>
struct const_accessor_for;

// Member-hook path (authoritative when present).
template <class _A>
  requires __has_const_accessor_type<_A>
struct const_accessor_for<_A>
{
  using type = typename _A::const_accessor_type;
};

// Substitution fallback for template accessors Tmpl<T, Rest...> that do NOT
// expose a nested const_accessor_type. Constrained so as not to conflict
// with the member-hook specialization above.
template <template <class, class...> class _Tmpl, class _T, class... _Rest>
  requires (!__has_const_accessor_type<_Tmpl<_T, _Rest...>>)
struct const_accessor_for<_Tmpl<_T, _Rest...>>
{
  using type = _Tmpl<add_const_t<_T>, _Rest...>;
};

template <class _A>
using const_accessor_for_t = typename const_accessor_for<_A>::type;
```

*Note:* libcudacxx supports pre-C++20 configurations. If `requires`-clauses on partial specializations aren't available on all supported compilers, the engineer should express the constraint via `enable_if_t` on a trailing default template parameter instead:

```cpp
template <class _A, class _Enable = void>
struct const_accessor_for;

template <class _A>
struct const_accessor_for<_A, enable_if_t<__has_const_accessor_type<_A>>>
{ using type = typename _A::const_accessor_type; };

// ... and analogous for the substitution path, but this is more intricate
// because partial specialization of the primary takes a single template
// parameter. The cleanest pre-C++20 form is to use a single detection trait
// driven by priority tags. Leave the concepts form above as the primary path
// and only fall back to enable_if if the CI reports the requires-clause is
// unsupported.
```

- [ ] **Step 4: Run both trait tests to confirm they pass**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.trait.default
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.trait.member
```

Expected: both PASS.

- [ ] **Step 5: Commit**

```bash
git add libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h \
        libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.member.pass.cpp
git commit -m "[libcudacxx] Add member-hook priority to const_accessor_for

Trait now prefers A::const_accessor_type over template substitution. The
__has_const_accessor_type concept drives both the member-hook partial
specialization and the constraint that keeps the substitution fallback
from conflicting with it.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 3: Add `const_accessor_type` to `default_accessor`

**Files:**
- Modify: `libcudacxx/include/cuda/std/__mdspan/default_accessor.h`
- Modify: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/default_accessor/types.pass.cpp`

- [ ] **Step 1: Extend the existing types test to assert the new member**

Edit `default_accessor/types.pass.cpp`. After the existing static_asserts inside `template <class T> void test()`, add:

```cpp
  static_assert(cuda::std::is_same_v<typename A::const_accessor_type,
                                     cuda::std::default_accessor<cuda::std::add_const_t<T>>>);
```

- [ ] **Step 2: Run the test to confirm it fails**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.default_accessor.types
```

Expected: FAIL with "no type named `const_accessor_type` in `cuda::std::default_accessor<…>`".

- [ ] **Step 3: Add the alias to `default_accessor`**

Edit `libcudacxx/include/cuda/std/__mdspan/default_accessor.h`. After the existing four `using` lines inside the struct, insert:

```cpp
  using const_accessor_type = default_accessor<add_const_t<_ElementType>>;
```

You may also need an additional include:

```cpp
#include <cuda/std/__type_traits/add_const.h>
```

(check whether it is already transitively included; add if not).

- [ ] **Step 4: Run the test to confirm it passes**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.default_accessor.types
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libcudacxx/include/cuda/std/__mdspan/default_accessor.h \
        libcudacxx/test/libcudacxx/std/containers/views/mdspan/default_accessor/types.pass.cpp
git commit -m "[libcudacxx] Add const_accessor_type alias to default_accessor

default_accessor<T> exposes default_accessor<const T> as its
const counterpart, wiring it into the const_accessor_for customization
point explicitly (rather than relying on the substitution fallback).

Part of mdspan const-accessor customization point prototype."
```

---

## Task 4: Add `const_accessor_type` to `aligned_accessor`

**Files:**
- Modify: `libcudacxx/include/cuda/std/__mdspan/aligned_accessor.h`
- Modify: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/mdspan.aligned_accessor/aligned_accessor.pass.cpp`

- [ ] **Step 1: Extend the aligned_accessor types test**

In `aligned_accessor.pass.cpp`, locate the block that exercises type aliases and add:

```cpp
  static_assert(cuda::std::is_same_v<
      typename cuda::std::aligned_accessor<T, Align>::const_accessor_type,
      cuda::std::aligned_accessor<cuda::std::add_const_t<T>, Align>>);
```

(Use the template parameter names the existing test uses — `T` and the alignment constant — don't hard-code `Align` if the test uses a different identifier.)

- [ ] **Step 2: Confirm it fails**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.mdspan.aligned_accessor.aligned_accessor
```

Expected: FAIL with "no type named `const_accessor_type`".

- [ ] **Step 3: Add the alias**

Edit `libcudacxx/include/cuda/std/__mdspan/aligned_accessor.h`. In the class body, after the existing `using` lines, insert:

```cpp
  using const_accessor_type = aligned_accessor<add_const_t<_ElementType>, _ByteAlignment>;
```

(Use the actual spelling of the alignment template parameter — verify by reading the file before editing.)

- [ ] **Step 4: Confirm it passes**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.mdspan.aligned_accessor.aligned_accessor
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libcudacxx/include/cuda/std/__mdspan/aligned_accessor.h \
        libcudacxx/test/libcudacxx/std/containers/views/mdspan/mdspan.aligned_accessor/aligned_accessor.pass.cpp
git commit -m "[libcudacxx] Add const_accessor_type alias to aligned_accessor

Mirrors the default_accessor change: aligned_accessor<T, N> exposes
aligned_accessor<const T, N> as its const counterpart, wiring it into
const_accessor_for explicitly.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 5: Add `cuda::std::as_const(mdspan)` overload + Scenario 1 test

**Files:**
- Modify: `libcudacxx/include/cuda/std/__mdspan/mdspan.h`
- Create test: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.scenario1.pass.cpp`

- [ ] **Step 1: Write the failing Scenario 1 test**

Create `as_const.scenario1.pass.cpp`:

```cpp
// <mdspan>
//
// Scenario 1: default_accessor. cuda::std::as_const(md) produces an
// mdspan<const T> over the same data with default_accessor<const T>.

#include <cuda/std/cassert>
#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

__host__ __device__ void test_type()
{
  using E = cuda::std::extents<int, 3, 4>;
  using A = cuda::std::default_accessor<double>;
  using MD = cuda::std::mdspan<double, E, cuda::std::layout_right, A>;

  double storage[12]{};
  MD md{storage, E{}};

  auto ro = cuda::std::as_const(md);

  using RO = decltype(ro);
  static_assert(cuda::std::is_same_v<typename RO::element_type, const double>);
  static_assert(cuda::std::is_same_v<typename RO::accessor_type,
                                     cuda::std::default_accessor<const double>>);
  static_assert(cuda::std::is_same_v<typename RO::extents_type, E>);
}

__host__ __device__ void test_same_data()
{
  using E = cuda::std::extents<int, 3>;
  double storage[3] = {1.0, 2.0, 3.0};
  cuda::std::mdspan<double, E> md{storage, E{}};
  auto ro = cuda::std::as_const(md);

  assert(ro.data_handle() == md.data_handle());
  // Element identity: values visible through ro match storage.
  assert(ro[0] == 1.0);
  assert(ro[1] == 2.0);
  assert(ro[2] == 3.0);
}

int main(int, char**)
{
  test_type();
  test_same_data();
  return 0;
}
```

- [ ] **Step 2: Confirm it fails**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.as_const.scenario1
```

Expected: FAIL on `auto ro = cuda::std::as_const(md)` — existing `std::as_const(T&)` would return `const mdspan<...>&`, which is `mdspan<double,…>` (not `mdspan<const double,…>`), so the subsequent static_asserts would fail.

- [ ] **Step 3: Add the overload to `mdspan.h`**

Edit `libcudacxx/include/cuda/std/__mdspan/mdspan.h`. After the `mdspan` class template definition (and any existing deduction guides), add within the `_CCCL_BEGIN_NAMESPACE_CUDA_STD` ... `_CCCL_END_NAMESPACE_CUDA_STD` block:

```cpp
// Constrained on const_accessor_for_t<_AccessorPolicy> being valid.
template <class _ElementType, class _Extents, class _LayoutPolicy, class _AccessorPolicy>
_CCCL_API constexpr auto
as_const(const mdspan<_ElementType, _Extents, _LayoutPolicy, _AccessorPolicy>& __md) noexcept
  -> mdspan<add_const_t<_ElementType>,
            _Extents,
            _LayoutPolicy,
            const_accessor_for_t<_AccessorPolicy>>
{
  using _ConstA = const_accessor_for_t<_AccessorPolicy>;
  using _Result = mdspan<add_const_t<_ElementType>, _Extents, _LayoutPolicy, _ConstA>;
  return _Result{
    typename _ConstA::data_handle_type{__md.data_handle()},
    __md.mapping(),
    _ConstA{__md.accessor()}};
}
```

Add the include for the trait near the top of `mdspan.h`:

```cpp
#include <cuda/std/__mdspan/const_accessor_for.h>
#include <cuda/std/__type_traits/add_const.h>
```

Verify these aren't already included before adding.

- [ ] **Step 4: Confirm it passes**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.as_const.scenario1
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libcudacxx/include/cuda/std/__mdspan/mdspan.h \
        libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.scenario1.pass.cpp
git commit -m "[libcudacxx] Add cuda::std::as_const(mdspan) overload

Freestanding overload that produces mdspan<const T, E, L, const_accessor_for_t<A>>
over the same data. Constrained out when the accessor has no valid const
counterpart. Mdspan stays independent of <ranges>; the overload lives
alongside the mdspan class template in <cuda/std/mdspan>.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 6: Scenario 2 test — custom accessor with member opt-in

No implementation change — this exercises the paths already implemented. It verifies that a user-defined accessor (not `default_accessor`) with a proxy reference and an explicit `const_accessor_type` works end to end.

**Files:**
- Create test: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.scenario2.pass.cpp`

- [ ] **Step 1: Write the test**

```cpp
// <mdspan>
//
// Scenario 2: custom accessor with explicit const_accessor_type member.

#include <cuda/std/cassert>
#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

// Minimal proxy-reference-style accessor. Does NOT match the default
// template substitution trivially — we test that the explicit member is
// used.
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

  auto ro = cuda::std::as_const(md);
  using RO = decltype(ro);

  static_assert(cuda::std::is_same_v<typename RO::accessor_type,
                                     minimal_proxy_accessor<const double>>);
  static_assert(cuda::std::is_same_v<typename RO::element_type, const double>);

  assert(ro.data_handle() == md.data_handle());
  assert(ro[0] == 10);
  assert(ro[3] == 40);
}

int main(int, char**)
{
  test();
  return 0;
}
```

- [ ] **Step 2: Run the test**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.as_const.scenario2
```

Expected: PASS (all required paths are already implemented).

- [ ] **Step 3: Commit**

```bash
git add libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.scenario2.pass.cpp
git commit -m "[libcudacxx] Test Scenario 2: custom accessor with member opt-in

Verifies that a user-defined accessor exposing const_accessor_type is
correctly picked up by cuda::std::as_const(mdspan) via the member-hook
path of const_accessor_for.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 7: Scenario 3 test — user trait specialization

No implementation change; validates the specialization escape hatch for accessors the user can't modify.

**Files:**
- Create test: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.specialization.pass.cpp`

- [ ] **Step 1: Write the test**

```cpp
// <mdspan>
//
// Scenario 3: third-party accessor; user specializes const_accessor_for.

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

// User specialization outside of the vendor's control.
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

  auto ro = cuda::std::as_const(md);
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
```

- [ ] **Step 2: Run the test**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.trait.specialization
```

Expected: PASS (user specialization wins over substitution fallback because the primary template is more specialized).

*Note:* if the specialization does not preempt the substitution fallback for template accessors, the constraint on the substitution partial specialization needs to also exclude user-specialized types. Practically, a user specialization of `const_accessor_for<vendor::gpu_accessor<T>>` is a *more specialized* partial specialization of the primary, so C++ partial-ordering picks it. If CI reports an ambiguity, revisit the constraints — add `&& !__user_specialized<...>` to the substitution partial specialization.

- [ ] **Step 3: Commit**

```bash
git add libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.specialization.pass.cpp
git commit -m "[libcudacxx] Test Scenario 3: user trait specialization

Validates the escape hatch for accessors the user cannot modify:
specializing cuda::std::const_accessor_for<vendor_accessor<T>> overrides
the substitution fallback and routes cuda::std::as_const through the
user's mapping.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 8: Composition test — `submdspan` ⇄ `as_const`

Validates that `submdspan(as_const(md))` and `as_const(submdspan(md))` produce the same mdspan type.

**Files:**
- Create test: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.composition.pass.cpp`

- [ ] **Step 1: Write the test**

```cpp
// <mdspan>
//
// submdspan and as_const compose in either order.

#include <cuda/std/cassert>
#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

__host__ __device__ void test_default_accessor_composition()
{
  using E = cuda::std::extents<int, 4, 5>;
  using MD = cuda::std::mdspan<double, E>;
  double storage[20]{};
  MD md{storage, E{}};

  auto left  = cuda::std::submdspan(cuda::std::as_const(md),
                                    cuda::std::full_extent, 0);
  auto right = cuda::std::as_const(cuda::std::submdspan(md,
                                    cuda::std::full_extent, 0));

  static_assert(cuda::std::is_same_v<decltype(left), decltype(right)>);
}

int main(int, char**)
{
  test_default_accessor_composition();
  return 0;
}
```

- [ ] **Step 2: Run the test**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.as_const.composition
```

Expected: PASS. If FAIL, the `offset_policy` of `default_accessor<const T>` is not matching `const_accessor_for_t<default_accessor<T>::offset_policy>`. Re-check Task 3's alias definition.

- [ ] **Step 3: Commit**

```bash
git add libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.composition.pass.cpp
git commit -m "[libcudacxx] Test submdspan/as_const composition

submdspan(as_const(md)) and as_const(submdspan(md)) yield the same type
for default_accessor, confirming the const-preserving offset_policy
requirement from the spec.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 9: Ill-formed case — `.fail.cpp` negative test

Verifies that `cuda::std::as_const(md)` is constrained out when the accessor has no usable const counterpart.

**Files:**
- Create test: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.no_const.fail.cpp`

- [ ] **Step 1: Write the failing-compile test**

```cpp
// <mdspan>
//
// Accessor with no usable const counterpart: cuda::std::as_const(md) must
// be ill-formed (no viable overload).

#include <cuda/std/mdspan>

// Non-template accessor; substitution fallback can't produce a const
// counterpart. No const_accessor_type member; no user specialization.
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

  // Expected: this line fails to compile — const_accessor_for<opaque_accessor>
  // has no nested 'type', so the trait alias is ill-formed and the as_const
  // overload is constrained out.
  auto ro = cuda::std::as_const(md); // expected-error {{no matching function for call to 'as_const'}}
  (void)ro;

  return 0;
}
```

*Note:* libcudacxx's `.fail.cpp` driver checks that the file fails to compile. The `// expected-error` annotation syntax may or may not be used by the harness — verify by looking at an existing `.fail.cpp` in the repo before finalizing the comment.

- [ ] **Step 2: Run the test**

```bash
cmake --build <build_dir> --target libcudacxx.test.std.containers.views.mdspan.as_const.as_const.no_const
```

Expected: The `.fail.cpp` driver should report PASS because the compilation failed as expected.

If the driver instead reports a different diagnostic, refine the test's expected-error annotation or restructure the test so the error is localized to the `cuda::std::as_const(md)` call.

- [ ] **Step 3: Commit**

```bash
git add libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/as_const.no_const.fail.cpp
git commit -m "[libcudacxx] Negative test: accessor without const counterpart

Verifies that cuda::std::as_const(mdspan) is constrained out of overload
resolution when the accessor's const_accessor_for trait yields no type
(non-template accessor, no member, no user specialization).

Part of mdspan const-accessor customization point prototype."
```

---

## Self-Review

**Spec coverage** (against `docs/superpowers/specs/2026-04-18-mdspan-const-accessor-cp-design.md`):

- Section 1 (trait core) — Tasks 1, 2 (skeleton + member hook + substitution)
- Section 2 (`std::as_const` overload) — Task 5
- Section 3 (explicit opt-in for default/aligned) — Tasks 3, 4
- Section 4 (submdspan composition) — Task 8
- Section 5 (libcudacxx prototype plan) — this plan
- Section 6 (open questions / out-of-scope) — intentionally no tasks; cross-const `common_reference_with` and `element_cast` are explicitly deferred
- Three scenarios — Tasks 5 (S1), 6 (S2), 7 (S3)
- Ill-formed case — Task 9
- Idempotence / same-data guarantee — covered implicitly via Scenario 1 runtime assertion

**Placeholder scan**: the one intentional placeholder is the CMake/lit target-name pattern (`libcudacxx.test....`) — executors must adjust to the repo's actual target names. The `.fail.cpp` expected-error comment is also flagged as a "verify against existing files" note. No TODOs, no "implement later" markers.

**Type consistency**: `const_accessor_for`, `const_accessor_for_t`, `const_accessor_type`, `__has_const_accessor_type` — spelling consistent across tasks.

**Scope**: contained. Prototype only. Paper draft is a separate plan.
