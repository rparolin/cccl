# mdspan Const-Accessor Customization Point (libcudacxx Prototype) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prototype the "const version of mdspan accessor" customization point in libcudacxx, per `docs/superpowers/specs/2026-04-18-mdspan-const-accessor-cp-design.md`.

**Architecture:** Trait `cuda::std::const_accessor_for<A>` with member-then-substitution resolution (Approach C, "hybrid"). `const_accessor_type` member aliases on `default_accessor` and `aligned_accessor`. Freestanding `cuda::std::element_cast<T>` function template in `<cuda/std/mdspan>`, restricted to cv-qualification changes (identity and add-const). Compile-time tests for all three design scenarios, submdspan composition, and negative cases.

**Tech Stack:** C++17+ via libcudacxx macros (`_CCCL_API`, `_CCCL_TEMPLATE`, `_CCCL_REQUIRES`, `_CCCL_CONCEPT`, `_CCCL_BEGIN_NAMESPACE_CUDA_STD`). Tests via libcudacxx's lit-based `.pass.cpp` / `.fail.cpp` suite; built through CMake.

---

## Prior state note

**Commit `07af161748`** (on this branch) already created the trait skeleton (substitution fallback only) and its first test under a now-misnamed directory `as_const/`. This plan:

1. Renames the test directory from `as_const/` to `element_cast/` (Task 1 below).
2. Keeps the trait header content from that commit — still valid.
3. Extends the trait with member-hook priority (Task 2), opts in the standard accessors (Tasks 3–4), adds the `element_cast<T>` function template (Task 5), and adds remaining tests (Tasks 6–9).

---

## File Structure

**New files:**
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.default.pass.cpp` (renamed/moved from `as_const/`).
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.member.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.specialization.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.scenario1.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.scenario2.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.composition.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.identity.pass.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.not_cv_reachable.fail.cpp`
- `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.no_const.fail.cpp`

**Modified files:**
- `libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h` — add member-hook path (Task 2).
- `libcudacxx/include/cuda/std/__mdspan/default_accessor.h` — add `const_accessor_type` (Task 3).
- `libcudacxx/include/cuda/std/__mdspan/aligned_accessor.h` — add `const_accessor_type` (Task 4).
- `libcudacxx/include/cuda/std/__mdspan/mdspan.h` — add `cuda::std::element_cast<T>` function template (Task 5).

Commit at the end of every task.

---

## Task 1: Rename test directory and verify existing trait

The prior commit `07af161748` placed `trait.default.pass.cpp` under a directory named `as_const/`. The spec now uses `element_cast<T>`; this task renames the directory to match.

**Files:**
- Move: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const/trait.default.pass.cpp`
  → `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.default.pass.cpp`

- [ ] **Step 1: Rename the directory with `git mv`**

```bash
git mv libcudacxx/test/libcudacxx/std/containers/views/mdspan/as_const \
       libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast
```

- [ ] **Step 2: Verify the test still compiles**

```bash
clang++ -std=c++20 -D__host__= -D__device__= \
  -I libcudacxx/include -I libcudacxx/test/support \
  -c libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.default.pass.cpp \
  -o /tmp/trait.default.o
```

Expected: exit 0, no diagnostics.

- [ ] **Step 3: Commit**

```bash
git commit -m "[libcudacxx] Rename mdspan as_const test dir to element_cast

Aligns test directory with the spec's element_cast<T> naming; no
content changes.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 2: Add member-hook priority to the trait

The trait must prefer `A::const_accessor_type` over substitution. Concept-based detection + a constrained partial specialization on the member-hook path.

**Files:**
- Modify: `libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h`
- Create test: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.member.pass.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
//===----------------------------------------------------------------------===//
// (LLVM Apache 2.0 license + NVIDIA 2026 copyright header — mirror
// trait.default.pass.cpp)
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
```

- [ ] **Step 2: Confirm it fails**

Standalone compile (expected: substitution produces `accessor_member_divergent<const int>`, not `A::const_variant` — second `static_assert` fails):

```bash
clang++ -std=c++20 -D__host__= -D__device__= \
  -I libcudacxx/include -I libcudacxx/test/support \
  -c libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.member.pass.cpp \
  -o /tmp/trait.member.o
```

Expected: FAIL with a `static_assert` failure on `test_member_wins_over_substitution`.

- [ ] **Step 3: Add member-hook detection to the trait**

Edit `libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h`. Add include:

```cpp
#include <cuda/std/__concepts/concept_macros.h>
```

Replace the body inside `_CCCL_BEGIN_NAMESPACE_CUDA_STD` ... `_CCCL_END_NAMESPACE_CUDA_STD` with:

```cpp
template <class _A>
_CCCL_CONCEPT __has_const_accessor_type =
  _CCCL_REQUIRES_EXPR((_A))(typename _A::const_accessor_type);

// Primary template — undefined so ill-formed cases diagnose at the call site.
template <class _A>
struct const_accessor_for;

// Member-hook path (authoritative when present).
template <class _A>
  requires __has_const_accessor_type<_A>
struct const_accessor_for<_A>
{
  using type = typename _A::const_accessor_type;
};

// Substitution fallback for template accessors without the nested alias.
template <template <class, class...> class _Tmpl, class _T, class... _Rest>
  requires (!__has_const_accessor_type<_Tmpl<_T, _Rest...>>)
struct const_accessor_for<_Tmpl<_T, _Rest...>>
{
  using type = _Tmpl<add_const_t<_T>, _Rest...>;
};

template <class _A>
using const_accessor_for_t = typename const_accessor_for<_A>::type;
```

*Pre-C++20 fallback:* if `requires` on partial specializations isn't available on all CI compilers, express the member constraint via `enable_if_t` on a trailing default template parameter. Try the concepts form first.

- [ ] **Step 4: Confirm both trait tests pass**

```bash
clang++ -std=c++20 -D__host__= -D__device__= \
  -I libcudacxx/include -I libcudacxx/test/support \
  -c libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.default.pass.cpp \
  -o /tmp/trait.default.o
clang++ -std=c++20 -D__host__= -D__device__= \
  -I libcudacxx/include -I libcudacxx/test/support \
  -c libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.member.pass.cpp \
  -o /tmp/trait.member.o
```

Expected: both exit 0.

- [ ] **Step 5: Commit**

```bash
git add libcudacxx/include/cuda/std/__mdspan/const_accessor_for.h \
        libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.member.pass.cpp
git commit -m "[libcudacxx] Add member-hook priority to const_accessor_for

Trait now prefers A::const_accessor_type over template substitution. A
concept __has_const_accessor_type drives the member-hook specialization
and the constraint that keeps the substitution fallback from conflicting
with it.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 3: Add `const_accessor_type` to `default_accessor`

**Files:**
- Modify: `libcudacxx/include/cuda/std/__mdspan/default_accessor.h`
- Modify: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/default_accessor/types.pass.cpp`

- [ ] **Step 1: Extend the existing types test**

In `types.pass.cpp`, after the existing static_asserts inside `template <class T> void test()`, add:

```cpp
  static_assert(cuda::std::is_same_v<typename A::const_accessor_type,
                                     cuda::std::default_accessor<cuda::std::add_const_t<T>>>);
```

- [ ] **Step 2: Confirm it fails**

Expected: FAIL with "no type named `const_accessor_type` in `cuda::std::default_accessor<…>`".

- [ ] **Step 3: Add the alias**

Edit `libcudacxx/include/cuda/std/__mdspan/default_accessor.h`. After the existing four `using` lines inside the struct, insert:

```cpp
  using const_accessor_type = default_accessor<add_const_t<_ElementType>>;
```

Ensure `<cuda/std/__type_traits/add_const.h>` is included (add if not already transitively available).

- [ ] **Step 4: Confirm it passes**

- [ ] **Step 5: Commit**

```bash
git add libcudacxx/include/cuda/std/__mdspan/default_accessor.h \
        libcudacxx/test/libcudacxx/std/containers/views/mdspan/default_accessor/types.pass.cpp
git commit -m "[libcudacxx] Add const_accessor_type alias to default_accessor

Explicit opt-in to const_accessor_for, so default_accessor doesn't rely on
the trait's substitution fallback.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 4: Add `const_accessor_type` to `aligned_accessor`

**Files:**
- Modify: `libcudacxx/include/cuda/std/__mdspan/aligned_accessor.h`
- Modify: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/mdspan.aligned_accessor/aligned_accessor.pass.cpp`

- [ ] **Step 1: Extend the aligned_accessor types test**

Locate the block that exercises type aliases and add:

```cpp
  static_assert(cuda::std::is_same_v<
      typename cuda::std::aligned_accessor<T, Align>::const_accessor_type,
      cuda::std::aligned_accessor<cuda::std::add_const_t<T>, Align>>);
```

(Use the test's existing parameter names — `T` and the alignment constant — don't hard-code `Align` if different.)

- [ ] **Step 2: Confirm it fails**

- [ ] **Step 3: Add the alias**

Edit `libcudacxx/include/cuda/std/__mdspan/aligned_accessor.h`. In the class body, after the existing `using` lines, insert:

```cpp
  using const_accessor_type = aligned_accessor<add_const_t<_ElementType>, _ByteAlignment>;
```

(Verify the spelling of the alignment template parameter in the current file before editing.)

- [ ] **Step 4: Confirm it passes**

- [ ] **Step 5: Commit**

```bash
git add libcudacxx/include/cuda/std/__mdspan/aligned_accessor.h \
        libcudacxx/test/libcudacxx/std/containers/views/mdspan/mdspan.aligned_accessor/aligned_accessor.pass.cpp
git commit -m "[libcudacxx] Add const_accessor_type alias to aligned_accessor

Mirrors the default_accessor change.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 5: Add `cuda::std::element_cast<T>` function template + Scenario 1 test

**Files:**
- Modify: `libcudacxx/include/cuda/std/__mdspan/mdspan.h`
- Create test: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.scenario1.pass.cpp`

- [ ] **Step 1: Write the failing Scenario 1 test**

```cpp
//===----------------------------------------------------------------------===//
// (LLVM Apache 2.0 + NVIDIA 2026 copyright — mirror the other tests)
//===----------------------------------------------------------------------===//

// <mdspan>
// Scenario 1: default_accessor.
// cuda::std::element_cast<const double>(md) produces an mdspan<const double>
// over the same data with default_accessor<const double>.

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

  auto ro = cuda::std::element_cast<const double>(md);

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
  auto ro = cuda::std::element_cast<const double>(md);

  assert(ro.data_handle() == md.data_handle());
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

Expected: FAIL with "no member named `element_cast` in `cuda::std`" (function template not defined yet).

- [ ] **Step 3: Add the `element_cast` function template**

Edit `libcudacxx/include/cuda/std/__mdspan/mdspan.h`. Near the top of the file, ensure these includes are present:

```cpp
#include <cuda/std/__mdspan/const_accessor_for.h>
#include <cuda/std/__type_traits/add_const.h>
```

After the `mdspan` class template definition (and any existing deduction guides), within the `_CCCL_BEGIN_NAMESPACE_CUDA_STD` ... `_CCCL_END_NAMESPACE_CUDA_STD` block, add:

```cpp
// const-add overload
template <class _T, class _E, class _L, class _A>
_CCCL_REQUIRES(is_same_v<_T, add_const_t<typename _A::element_type>>)
[[nodiscard]] _CCCL_API constexpr auto
element_cast(const mdspan<typename _A::element_type, _E, _L, _A>& __md) noexcept
  -> mdspan<_T, _E, _L, const_accessor_for_t<_A>>
{
  using _C      = const_accessor_for_t<_A>;
  using _Result = mdspan<_T, _E, _L, _C>;
  return _Result{
    typename _C::data_handle_type{__md.data_handle()},
    __md.mapping(),
    _C{__md.accessor()}};
}

// identity overload
template <class _T, class _E, class _L, class _A>
_CCCL_REQUIRES(is_same_v<_T, typename _A::element_type>)
[[nodiscard]] _CCCL_API constexpr auto
element_cast(const mdspan<typename _A::element_type, _E, _L, _A>& __md) noexcept
  -> mdspan<_T, _E, _L, _A>
{
  return __md;
}
```

If `_CCCL_REQUIRES` on function templates isn't the right macro spelling for this form, use `_CCCL_TEMPLATE(...) _CCCL_REQUIRES(...)` as in other libcudacxx headers. Verify by looking at a sibling use.

- [ ] **Step 4: Confirm it passes**

- [ ] **Step 5: Commit**

```bash
git add libcudacxx/include/cuda/std/__mdspan/mdspan.h \
        libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.scenario1.pass.cpp
git commit -m "[libcudacxx] Add cuda::std::element_cast<T> function template

Freestanding element_cast<T> producing mdspan<T, E, L, const_accessor_for_t<A>>
for the add-const case (and identity for T == element_type). Constrained
to cv-qualification changes of element_type. Keeps mdspan independent of
<ranges>.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 6: Identity-cast test

**Files:**
- Create: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.identity.pass.cpp`

- [ ] **Step 1: Write the test**

```cpp
//===----------------------------------------------------------------------===//
// (standard header)
//===----------------------------------------------------------------------===//

// <mdspan>
// element_cast<T>(md) with T == element_type returns the same mdspan type.

#include <cuda/std/cassert>
#include <cuda/std/mdspan>
#include <cuda/std/type_traits>
#include "test_macros.h"

__host__ __device__ void test_identity_default_accessor()
{
  using E = cuda::std::extents<int, 5>;
  using MD = cuda::std::mdspan<double, E>;
  double storage[5]{};
  MD md{storage, E{}};

  auto same = cuda::std::element_cast<double>(md);
  static_assert(cuda::std::is_same_v<decltype(same), MD>);
  assert(same.data_handle() == md.data_handle());
}

__host__ __device__ void test_identity_const_element()
{
  using E = cuda::std::extents<int, 5>;
  using MD = cuda::std::mdspan<const double, E>;
  const double storage[5] = {1, 2, 3, 4, 5};
  MD md{storage, E{}};

  auto same = cuda::std::element_cast<const double>(md);
  static_assert(cuda::std::is_same_v<decltype(same), MD>);
}

int main(int, char**)
{
  test_identity_default_accessor();
  test_identity_const_element();
  return 0;
}
```

- [ ] **Step 2: Confirm it passes**

- [ ] **Step 3: Commit**

```bash
git add libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.identity.pass.cpp
git commit -m "[libcudacxx] Test element_cast<T> identity case

Verifies element_cast<T>(md) with T == element_type yields the same
mdspan type.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 7: Scenario 2 test — custom accessor with member opt-in

**Files:**
- Create: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.scenario2.pass.cpp`

- [ ] **Step 1: Write the test**

```cpp
//===----------------------------------------------------------------------===//
// (standard header)
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
  assert(ro[0] == 10);
  assert(ro[3] == 40);
}

int main(int, char**)
{
  test();
  return 0;
}
```

- [ ] **Step 2: Confirm it passes** (all required paths are already implemented).

- [ ] **Step 3: Commit**

```bash
git add libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.scenario2.pass.cpp
git commit -m "[libcudacxx] Test Scenario 2: custom accessor with member opt-in

Verifies element_cast<const T> routes through const_accessor_for's
member-hook path when the accessor exposes const_accessor_type.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 8: Scenario 3 test — user trait specialization

**Files:**
- Create: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.specialization.pass.cpp`

- [ ] **Step 1: Write the test**

```cpp
//===----------------------------------------------------------------------===//
// (standard header)
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
```

*Note:* if CI reports ambiguity between the user specialization and the substitution fallback, the fallback's `requires` clause may need to also exclude user-specialized types. The user specialization should be more specialized per C++ partial-ordering and win by default.

- [ ] **Step 2: Confirm it passes**

- [ ] **Step 3: Commit**

```bash
git add libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/trait.specialization.pass.cpp
git commit -m "[libcudacxx] Test Scenario 3: user trait specialization

Verifies that specializing cuda::std::const_accessor_for for an
accessor the user cannot modify routes element_cast<const T> through the
user's mapping.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 9: Composition test — `submdspan` ⇄ `element_cast`

**Files:**
- Create: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.composition.pass.cpp`

- [ ] **Step 1: Write the test**

```cpp
//===----------------------------------------------------------------------===//
// (standard header)
//===----------------------------------------------------------------------===//

// <mdspan>
// submdspan and element_cast<const T> compose in either order.

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

  auto left  = cuda::std::submdspan(cuda::std::element_cast<const double>(md),
                                    cuda::std::full_extent, 0);
  auto right = cuda::std::element_cast<const double>(
                 cuda::std::submdspan(md, cuda::std::full_extent, 0));

  static_assert(cuda::std::is_same_v<decltype(left), decltype(right)>);
}

int main(int, char**)
{
  test_default_accessor_composition();
  return 0;
}
```

- [ ] **Step 2: Confirm it passes**

If FAIL, the offset_policy of the const counterpart does not agree with the const counterpart of the offset_policy. Re-check Task 3's alias.

- [ ] **Step 3: Commit**

```bash
git add libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.composition.pass.cpp
git commit -m "[libcudacxx] Test submdspan/element_cast composition

Verifies submdspan(element_cast<const T>(md)) and
element_cast<const T>(submdspan(md)) yield the same mdspan type for
default_accessor.

Part of mdspan const-accessor customization point prototype."
```

---

## Task 10: Negative tests (.fail.cpp)

Two negative tests: one for a non-cv-reachable target type, one for an accessor with no valid const counterpart.

**Files:**
- Create: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.not_cv_reachable.fail.cpp`
- Create: `libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.no_const.fail.cpp`

- [ ] **Step 1: Write `element_cast.not_cv_reachable.fail.cpp`**

```cpp
//===----------------------------------------------------------------------===//
// (standard header)
//===----------------------------------------------------------------------===//

// <mdspan>
// element_cast<T>(md) with T not cv-reachable from element_type: ill-formed.

#include <cuda/std/mdspan>

int main(int, char**)
{
  using E = cuda::std::extents<int, 3>;
  double storage[3]{};
  cuda::std::mdspan<double, E> md{storage, E{}};

  // Expected: ill-formed. 'int' is not cv-reachable from 'double'.
  auto bad = cuda::std::element_cast<int>(md);
  (void)bad;

  return 0;
}
```

- [ ] **Step 2: Write `element_cast.no_const.fail.cpp`**

```cpp
//===----------------------------------------------------------------------===//
// (standard header)
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
  auto bad = cuda::std::element_cast<const double>(md);
  (void)bad;

  return 0;
}
```

- [ ] **Step 3: Verify both `.fail.cpp` files fail to compile** (the libcudacxx test harness treats compilation failure as success for `.fail.cpp`).

*Note:* the `expected-error` annotation format used by libcudacxx's harness may differ; check a sibling `.fail.cpp` in the repo and match its conventions.

- [ ] **Step 4: Commit**

```bash
git add libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.not_cv_reachable.fail.cpp \
        libcudacxx/test/libcudacxx/std/containers/views/mdspan/element_cast/element_cast.no_const.fail.cpp
git commit -m "[libcudacxx] Negative tests for element_cast

Two .fail.cpp tests:
- element_cast<int>(mdspan<double>): T not cv-reachable from element_type.
- element_cast<const double>(mdspan<_, _, _, opaque_accessor>): no valid
  const counterpart (non-template accessor without member or
  specialization).

Part of mdspan const-accessor customization point prototype."
```

---

## Self-Review

**Spec coverage** (against `docs/superpowers/specs/2026-04-18-mdspan-const-accessor-cp-design.md`):

- *What "const version of an accessor" means* — all six invariants exercised indirectly by Tasks 1–9.
- *Who writes what* — Tasks 2 (trait), 3–4 (member opt-in), 5 (element_cast), 8 (user specialization).
- *Three scenarios* — Tasks 5 (S1), 7 (S2), 8 (S3).
- *Composition with submdspan* — Task 9.
- *Ill-formed cases* — Task 10.
- *Identity case* — Task 6.
- **Out of scope for prototype**: `basic_common_reference` specializations in `<atomic>` (belong in the paper's wording only; prototype validates mdspan-side only).

**Placeholder scan**: test-build commands use standalone `clang++` compile-checks rather than the lit harness, which is acceptable validation for this prototype. If the engineer wants to run the lit suite, the exact `cmake --build --target …` targets must be discovered from the repo's CMake configuration.

**Type consistency**: `const_accessor_for`, `const_accessor_for_t`, `const_accessor_type`, `__has_const_accessor_type`, `element_cast` — spelling consistent across tasks.

**Scope**: libcudacxx prototype only. The `basic_common_reference` specializations (bundled into the WG21 paper per the spec) are NOT implemented here; proving the mdspan-side customization point is the prototype's job.
