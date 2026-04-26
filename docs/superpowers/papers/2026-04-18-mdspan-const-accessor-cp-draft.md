# Customization Point for a const view of `std::mdspan`

| | |
|---|---|
| **Document #** | DNNNN (to be assigned) |
| **Date** | 2026-04-18 |
| **Reply-to** | Rob Parolin <rparolin@nvidia.com> |
| **Audience** | LEWG (mdspan) |
| **Target** | C++29 |
| **Revision** | R0 (draft) |

**Related papers / issues**:
[P3323R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3323r1.html),
[P3860R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3860r1.html),
[P2689R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2689r3.html),
[LWG 3811](https://cplusplus.github.io/LWG/issue3811).

---

## Abstract

`std::mdspan<T, …, Accessor>` has no standard mechanism for producing a read-only view of the same data with `const T` as its element type. This paper proposes:

1. A **customization point object** `std::const_view` in `<mdspan>`. Called with an mdspan, it returns an mdspan whose element type is `const T` over the same data. Called with an accessor, it returns the accessor's const counterpart. Users customize it by writing a free function `const_view` in the accessor's own namespace, found via ADL — the niebloid pattern used by `std::ranges::begin`.
2. Library-provided handling for `std::default_accessor` and `std::aligned_accessor` through direct overloads inside the CPO. No new members are added to these accessors.
3. The `<atomic>`-side work needed to make the motivating `atomic_ref` case compose end-to-end — `basic_common_reference` specializations for `std::atomic_ref` — is split into a companion paper (`2026-04-23-atomic-ref-common-reference-draft.md`, SG1 audience). This paper cross-references that work but does not propose it.

---

## Motivation

### The missing operation

C++26 added `atomic_ref<const T>` via P3323R1 and plugged the conversion gap in P3860R1. This makes `atomic_ref`-based mdspan accessors natural: a user can write

```cpp
template <class T>
struct atomic_accessor {
  using element_type     = T;
  using reference        = std::atomic_ref<T>;
  using data_handle_type = T*;
  using offset_policy    = atomic_accessor;
  reference access(T* p, std::size_t i) const noexcept;
  T*        offset(T* p, std::size_t i) const noexcept;
};
```

There is no standard mechanism to construct a read-only view of `mdspan<double, E, L, atomic_accessor<double>>` with `atomic_ref<const double>` as its reference; users must manually spell the const-accessor and data-handle types, and for custom accessors the required conversions may not exist. The same shape arises for any proxy-reference accessor — bit-packing, endian-conversion, P2689R3's `basic_atomic_accessor` — where the read-only counterpart's `reference` differs structurally from the writable one.

### The `common_reference_with` gap (handled in a companion paper)

In current C++26, `common_reference_with` has no specializations for `std::atomic_ref`: every non-identity pair (`atomic_ref<T>` against `T&`, `const T&`, `atomic_ref<const T>`, and the mirror combinations) evaluates to `false`. That gap blocks the motivating `atomic_ref`-backed case from composing with standard algorithms. It is closed by a companion SG1 paper (`2026-04-23-atomic-ref-common-reference-draft.md`); this paper assumes that work lands and does not propose it.

---

## Proposal overview

### What "const version of an accessor" means

For an accessor `A` with `element_type = T`, the **const version** `C` (produced by `std::const_view(A{})`) satisfies:

1. `C::element_type` is `std::add_const_t<T>`.
2. `C::reference` is the access type for `C::element_type`. For language-reference accessors this is `const T&`; for proxy-reference accessors it is the proxy over `const T` (e.g. `atomic_ref<T>` → `atomic_ref<const T>`).
3. `C::data_handle_type` is direct-list-initializable from an lvalue of type `A::data_handle_type`, non-narrowing. For the standard accessors this is automatic; accessors with fancy pointer types (device pointers, tagged pointers) must provide the conversion.
4. An mdspan with accessor `C` views the same elements as the original mdspan with accessor `A`.
5. `C::offset_policy` equals `decltype(std::const_view(std::declval<typename A::offset_policy>()))`. This is a precondition on accessor authors: ADL hooks must satisfy it, and it is a *Mandates* on the `std::const_view` overload that consumes the accessor. Without it, `std::submdspan(std::const_view(md), …)` and `std::const_view(std::submdspan(md, …))` are well-formed individually but yield distinct mdspan types — a source-breaking inconsistency for any code that touches both.

A `const_view` is a read-only alias of the source's storage, not a snapshot. Modifications through the original view or concurrent writes through proxy references are visible through subsequent reads. `const_view` imposes no ordering, atomicity, or freshness guarantees beyond those the source accessor's `reference` already provides.

### The `const_view` customization point object

`std::const_view` is a customization point object declared in `<mdspan>`:

```cpp
namespace std {
  inline constexpr /* unspecified */ const_view = /* unspecified */;
}
```

#### Customization paths

**1. Author-facing: ADL-found `const_view` free function.** An accessor author provides a free function named `const_view` in their own namespace; `std::const_view` finds it via ADL — the niebloid pattern used by `std::ranges::begin`:

```cpp
namespace user_lib {
  template <class T>
  struct atomic_accessor { /* element_type, reference, access(), offset(), ... */ };

  template <class T>
  atomic_accessor<const T>
  const_view(atomic_accessor<T>) noexcept
  { return {}; }
}
```

**2. Library-provided built-ins.** The CPO has direct overloads for `std::default_accessor<T>` and `std::aligned_accessor<T, N>`, yielding `std::default_accessor<const T>` and `std::aligned_accessor<const T, N>` respectively.

#### What about third-party accessors the consumer can't modify?

A user of a third-party accessor whose vendor did not provide an ADL hook cannot reach into the vendor's namespace to add one. The paper's answer is to **wrap the type**: define a local accessor that forwards the accessor protocol to the vendor's accessor, and carry the `const_view` hook on the wrapper. Users then construct their mdspan over the wrapper instead of the vendor type.

Unlike the `std::ranges` analogy (`views::all`, `ranges::subrange`) which is applied at the *use site*, wrap-and-hook happens at mdspan **construction** site. Every construction point must change, which is a codebase-wide refactor for existing code that has already adopted the vendor accessor. A second cost: third-party APIs expecting `mdspan<…, vendor::accessor<T>>` no longer accept the wrapper-based mdspan without another explicit conversion.

#### Why ADL customization (and not member-typed aliases or trait specialization)

The single ADL-found `const_view` free function replaces two alternative customization mechanisms that earlier drafts considered:

- **Member-typed alias** (`A::const_accessor_type`) was rejected because it forces every accessor to carry a new library-mandated member and doesn't scale to accessors whose const version is not `Foo<const T>`.
- **Trait specialization in `namespace std`** (`std::const_view_override`) was rejected because it mixes the older `std::hash` pattern with the modern CPO pattern, encouraging `namespace std` specialization where a wrapper accessor is the cleaner alternative.

#### How `const_view(mdspan)` is implemented

The CPO follows the `std::ranges::begin` pattern. Its call operator lives in an internal namespace that carries a deleted `const_view()` "poison pill", so the unqualified name `const_view(a)` inside the CPO body finds user-supplied free functions via argument-dependent lookup and never rebinds to the CPO object itself. The `mdspan` overload is not customizable; the library always provides the same implementation, which re-enters the CPO on the accessor — reaching Path 1 (ADL hook) or Paths 2/3 (library built-ins). A sketch (final standardese deferred):

```cpp
namespace std::__const_view_impl {

  // Poison pill: unqualified `const_view(a)` inside operator() below does not
  // find the CPO — it finds user-supplied free functions via ADL.
  void const_view() = delete;

  struct const_view_fn {
    // Accessor overload — three mutually exclusive paths:
    //   1. ADL-found free function `const_view(A)` in A's namespace.
    //   2. built-in for default_accessor<T>   → default_accessor<const T>.
    //   3. built-in for aligned_accessor<T,N> → aligned_accessor<const T,N>.
    // Paths 2/3 are disabled whenever Path 1 would be viable, so a user-
    // supplied ADL hook wins over a library built-in when both apply.

    template <class A>
      requires requires (A a) { const_view(a); }
    constexpr auto operator()(A a) const
      noexcept(noexcept(const_view(a)))
      -> decltype(const_view(a))
    { return const_view(a); }

    // ...built-ins for default_accessor<T> and aligned_accessor<T,N>...

    // mdspan overload: SFINAE on the return type — when no accessor path is
    // viable, the return type is ill-formed and the overload drops out.
    template <class T, class E, class L, class A>
    constexpr auto operator()(const mdspan<T, E, L, A>& md) const
      -> mdspan<add_const_t<T>, E, L,
                decltype(declval<const const_view_fn&>()(md.accessor()))>
    {
      using C = decltype((*this)(md.accessor()));
      return mdspan<add_const_t<T>, E, L, C>{
        typename C::data_handle_type{md.data_handle()},
        md.mapping(),
        (*this)(md.accessor())
      };
    }
  };
}

inline constexpr __const_view_impl::const_view_fn const_view{};
```

### How users obtain a const view

Users have two paths, depending on the accessor:

**Implicit converting constructor.** When `A` has an implicit `A → A_const` conversion (for example, C++26's `default_accessor<T> → default_accessor<const T>`), mdspan's existing converting constructor handles the conversion with no library change:

```cpp
mdspan<double, E> md = /* ... */;
mdspan<const double, E> ro = md;
```

**`std::const_view` CPO.** Otherwise — or when the user prefers explicit const-ification without spelling the target type — the CPO deduces the result:

```cpp
mdspan<double, E, L, my_accessor<double>> md = /* ... */;
auto ro = std::const_view(md);
// decltype(ro) is mdspan<const double, E, L, my_accessor<const double>>
```

---

## Design rationale and considered alternatives

### Why not overload `std::as_const`

`std::as_const<T>(T&)` in `<utility>` is **shallow** (returns `const T&`), while an mdspan version would be **deep** (returns a new mdspan value with a const element type). Sharing one name for two semantically different operations was rejected in favor of a dedicated `std::const_view` CPO.

### Why a CPO, not a named cast function

Early drafts proposed a named cast function template (`std::element_cast<T>(md)`). Two problems drove the shift to a CPO:

- **mdspan's idiom is conversion, not named casts.** mdspan's primary type-changing mechanism is its converting constructor, which kicks in implicitly when the accessor types convert; adding a named cast on top duplicates machinery the library already has. A CPO is an additive customization surface, not a replacement conversion facility.
- **A named cast needs an explicit target-type argument.** `std::element_cast<const T>(md)` forces the user to spell out the element type at the call site. For common cases that's fine; for custom accessors with elaborate template parameters it's awkward. The CPO deduces the target and avoids that burden.

### Why freestanding, not a member

A member function `mdspan::const_view()` was considered and rejected:

- **The customization target is the accessor, not the mdspan.** ADL customization requires a free-function surface on the accessor's namespace; a member on `mdspan` cannot host it. The CPO also needs to accept a bare accessor (`std::const_view(md.accessor())`), which a member on `mdspan` cannot dispatch on. A member would end up as a thin forwarder over the real free-function CPO this paper already proposes.
- **Standard-library precedent.** `bit_cast`, `as_const`, `forward`, `move`, the ranges adaptors, and modern CPOs are freestanding. A member `mdspan::const_view()` would be the only named type-changing operation on a standard-library value type delivered as a member.

### Naming the operation — `const_view`, and why not the alternatives

We chose **`std::const_view`** over four alternatives:

**`std::as_const_view`** — rejected because `std::ranges::as_const_view<V>` already exists as a structurally different design: a wrapper class with `.base()`, preserved element type, and const synthesized at iteration via `basic_const_iterator`. Our operation returns a plain `mdspan<const T, …>` with no wrapper and a directly-changed element type. The namespace difference does not defuse the confusion — the identifier reads the same in error messages, documentation, and search results.

**`std::make_const_view`** — rejected because the dominant `std::make_*` idiom is *owning* factories (`make_shared`, `make_unique`, `make_pair`); cold readers would infer allocation or ownership.

**`std::to_const`** — rejected because it reads as a verb-ier parallel to `std::as_const` (from `<utility>`), which is the *shallow* operation.

**`std::view_as_const`** — rejected because verb-noun-qualifier word order has no precedent in the standard library.

`std::const_view` uses the standard's established `const_X` vocabulary (`const_iterator`, `const_reference`, `const_pointer`) and has no collision with the alternatives above.

---

## Coordination with P2689R3

P2689R3 (Edwards, Maher, Hoemmen) proposes `basic_atomic_accessor<T, MemOrder>` for use with mdspan. The `const_view` customization point proposed here applies to it through the ADL-found `const_view` free function that P2689R3 or a follow-up is expected to provide; no wording in this paper mentions `basic_atomic_accessor` directly. Coordination on the `basic_common_reference` specializations lives in the companion SG1 paper.

---

## Implementation experience

A libcudacxx prototype of the CPO-based design is implemented on the CCCL fork (branch `feature/mdspan-const-accessor-cp-design`). The prototype validates that:

- `std::const_view(md)` composes cleanly with `std::submdspan`: the two operations commute type-wise for `default_accessor` and `aligned_accessor`.
- `std::const_view(md)` is O(1) — no element copy, no allocation — and the result's `data_handle()` is the same address as the input's.
- An accessor with no ADL hook produces a clean "no matching call" diagnostic at the call site (not a deep metaprogramming cascade).

Tests are organized as `.pass.cpp` / `.fail.cpp` under `libcudacxx/test/libcudacxx/std/containers/views/mdspan/const_view/`. All positive tests compile and run; both negative tests fail to compile as intended.

---

## References

- **P3323R1** — *cv-qualified types in atomic and atomic_ref*. Added `atomic_ref<const T>` in C++26; introduced the gap this paper addresses. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3323r1.html>
- **P3860R1** — *Proposed Resolution for NB Comment GB13-309: `atomic_ref<T>` is not convertible to `atomic_ref<const T>`*. Adds the converting constructor; adjacent but does not address `common_reference_with`. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3860r1.html>
- **P2689R3** — *Atomic Refs Bound to Memory Orderings & Atomic Accessors*. Proposes `basic_atomic_accessor` for use with mdspan; natural collaborator for the companion `basic_common_reference` specializations. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2689r3.html>
- **LWG 3811** — *`views::as_const` on `ref_view<T>` should return `ref_view<const T>`*. Adjacent `as_const` semantics issue for ranges; confirms the per-case wiring pattern. <https://cplusplus.github.io/LWG/issue3811>
- **`[range.access.begin]`** in the C++ working draft — precedent for the expression-equivalence cascade and poison-pill pattern used by this paper's wording.
- **`[customization.point.object]`** in the C++ working draft — requirements on customization point objects.
- **Companion SG1 paper** — `2026-04-23-atomic-ref-common-reference-draft.md` (same repository). Proposes the `basic_common_reference` specializations for `atomic_ref` that make the motivating case compose end-to-end.

---

## Acknowledgements

To be completed in R1 after review (reviewers, proposed coordinators from P2689R3, CCCL contributors).

---
