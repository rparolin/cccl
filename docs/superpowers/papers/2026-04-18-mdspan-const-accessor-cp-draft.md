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

mdspan's existing converting constructor already covers the common case where an accessor has an implicit `A → A_const` conversion (for example, `default_accessor<T>` in C++26); `std::const_view` is the customization surface for every other accessor. No new casting operation is necessary.

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
  reference access(T* p, std::size_t i) const noexcept { return std::atomic_ref<T>{p[i]}; }
  T*        offset(T* p, std::size_t i) const noexcept { return p + i; }
};
```

and view a buffer as `mdspan<double, E, L, atomic_accessor<double>>`. But there is no way, in the current standard, to construct a read-only view of that same data with `atomic_ref<const double>` as its reference. Users must manually spell the const-accessor and data-handle types, and for custom accessors the required conversions may not exist.

### The `common_reference_with` gap (handled in a companion paper)

In current C++26, `common_reference_with` has no specializations for `std::atomic_ref`: every non-identity pair (`atomic_ref<T>` against `T&`, `const T&`, `atomic_ref<const T>`, and the mirror combinations) evaluates to `false`. That gap blocks the motivating `atomic_ref`-backed case from composing with standard algorithms. It is closed by a companion SG1 paper (`2026-04-23-atomic-ref-common-reference-draft.md`); this paper assumes that work lands and does not propose it.

---

## Proposal overview

### What "const version of an accessor" means

For an accessor `A` with `element_type = T`, the **const version** `C` (produced by `std::const_view(A{})`) satisfies:

1. `C::element_type` is `std::add_const_t<T>`.
2. `C::reference` is the access type through which an element of type `C::element_type` is accessed, in the same sense that `A::reference` is the access type for `A::element_type`. For language-reference accessors this means `C::reference` is `const T&`; for proxy-reference accessors it is the corresponding proxy over `const T` (e.g. `atomic_ref<T>` → `atomic_ref<const T>`). Because `C::element_type` is const-qualified, no operation on a value of type `C::reference` modifies the accessed element.
3. `C::data_handle_type` is constructible from `A::data_handle_type`. For the standard accessors this is automatic (both use `T*`, and `const T*` is constructible from `T*`); custom accessors with fancy pointer types — device pointers, tagged pointers, pointers carrying memory-space information — must provide the analogous conversion.
4. An mdspan with accessor `C` views the same elements as the original mdspan with accessor `A`.
5. `C::offset_policy` is itself a const version of `A::offset_policy`, so that `std::submdspan(std::const_view(md), …)` and `std::const_view(std::submdspan(md, …))` yield type-equivalent mdspans.

A `const_view` is a read-only alias of the source's storage, not a snapshot. Modifications to the underlying data through the original view, through another accessor, or — for synchronizing proxy references such as `atomic_ref` — through concurrent writes are visible through subsequent reads. `const_view` imposes no ordering, atomicity, or freshness guarantees beyond those already provided by the source accessor's `reference` type.

### The `const_view` customization point object

`std::const_view` is a **customization point object** (CPO) — an inline `constexpr` function object of unspecified type, declared in `<mdspan>`. Users call it with either an `mdspan` or an accessor:

```cpp
namespace std {
  inline constexpr /* unspecified */ const_view = /* unspecified */;
}
```

- `std::const_view(md)` where `md` is an `mdspan<T, E, L, A>` returns an `mdspan<const T, E, L, C>` (where `C` is the const counterpart of `A`) over the same data.
- `std::const_view(acc)` where `acc` is an accessor returns the const counterpart accessor.

#### Customization paths

Customization is through two mechanisms:

**1. Author-facing: ADL-found `const_view` free function.** An accessor author provides a free function literally named `const_view` in their own namespace; `std::const_view` finds it via argument-dependent lookup:

```cpp
namespace user_lib {
  template <class T>
  struct atomic_accessor { /* element_type, reference, access(), offset(), ... */ };

  // std::const_view finds this via ADL on atomic_accessor<T>.
  template <class T>
  atomic_accessor<const T>
  const_view(atomic_accessor<T>) noexcept
  { return {}; }
}
```

This is the niebloid pattern used by `std::ranges::begin`, `std::views::as_const`, and the `std::execution` CPOs: an ADL-poisoned function object in `std::` that users customize by writing a specifically-named free function in their own namespace — no `namespace std` specialization.

**2. Library-provided built-ins.** The standard library provides direct handling for `std::default_accessor<T>` and `std::aligned_accessor<T, N>` — their const counterparts are `std::default_accessor<const T>` and `std::aligned_accessor<const T, N>` respectively.

If neither path yields a result, `std::const_view(x)` is ill-formed with a clean "no matching call" diagnostic at the call site.

#### What about third-party accessors the consumer can't modify?

A user of a *third-party* accessor whose vendor did not provide an ADL `const_view` hook has no way to reach into the vendor's namespace to add one. This is a real scenario. The paper's answer is to **wrap the type** — define a local accessor that wraps the vendor's accessor and carries a `const_view` hook in its own namespace. This section is explicit about the cost of that answer, because the cost is real and asymmetric with the `std::ranges` analogy.

A minimal wrapping adapter:

```cpp
namespace my_lib {
  template <class T>
  struct const_view_adapter {
    using offset_policy    = const_view_adapter;
    using element_type     = T;
    using reference        = T&;
    using data_handle_type = T*;

    vendor::gpu_accessor<T> base_;

    _CCCL_HIDE_FROM_ABI constexpr const_view_adapter() = default;

    _CCCL_API constexpr reference
    access(data_handle_type __p, std::size_t __i) const noexcept { return base_.access(__p, __i); }

    _CCCL_API constexpr data_handle_type
    offset(data_handle_type __p, std::size_t __i) const noexcept { return base_.offset(__p, __i); }
  };

  // Opt in for the adapter — not for vendor::gpu_accessor itself.
  template <class T>
  const_view_adapter<const T>
  const_view(const_view_adapter<T>) noexcept
  { return {}; }
}
```

The user then constructs their mdspan using `my_lib::const_view_adapter<T>` as its accessor rather than `vendor::gpu_accessor<T>` directly, and `std::const_view` works on it via the adapter's ADL hook.

**Where the `std::ranges` analogy breaks.** `std::views::all(r)` and `std::ranges::subrange(b, e)` are applied at the *use site*: given any range, the user wraps it at the point of consumption, once. The wrap-and-hook pattern above is different — wrapping happens at mdspan **construction** site, not at use site. Every place in the program that constructs an mdspan over the vendor's accessor must be changed to construct over `my_lib::const_view_adapter` instead. For existing codebases that have already adopted the vendor accessor in many places, this is a codebase-wide refactor, not a local retrofit.

There is a second cost. Once a user switches to the adapter, third-party APIs expecting `mdspan<T, E, L, vendor::gpu_accessor<T>>` no longer accept the adapter-based mdspan without another explicit conversion at the interop boundary. The two worlds no longer share a common mdspan type.

**The design trade-off.** An earlier iteration included a `const_view_override<A>` trait users could specialize in `std::` to opt a third-party accessor in at the use site, without wrapping. That trait was rejected (see *Why ADL customization* below) to keep the customization surface uniform with modern standard-library facilities. The cost falls on users who cannot modify vendor headers; the paper accepts it because the wrap-and-hook pattern is explicit and greppable, and because greenfield code constructing over locally-owned accessors pays nothing. Users retrofitting existing code should budget for the refactor.

#### Why ADL customization (and not member-typed aliases or trait specialization)

The single ADL-found `const_view` free function replaces two alternative customization mechanisms that earlier drafts considered:

- **Member-typed alias** (`A::const_accessor_type`) was rejected because it forces every accessor to carry a new library-mandated member and doesn't scale to accessors whose const version is not `Foo<const T>`.
- **Trait specialization in `namespace std`** (`std::const_view_override`) was rejected because it mixes the older `std::hash` pattern with the modern CPO pattern, encouraging `namespace std` specialization where a wrapper accessor is the cleaner alternative.

#### How `const_view(mdspan)` is implemented

The `mdspan` overload is not customizable; the library always provides the same implementation, which delegates to the accessor overload:

```cpp
// Sketch; final standardese deferred to a later revision.
template <class T, class E, class L, class A>
constexpr auto const_view_fn::operator()(const mdspan<T, E, L, A>& md) const
  requires requires (A a) { const_view(a); }
{
  using C      = decltype(const_view(md.accessor()));
  using Result = mdspan<add_const_t<T>, E, L, C>;
  return Result{
    typename C::data_handle_type{md.data_handle()},
    md.mapping(),
    const_view(md.accessor())
  };
}
```

The constraint ensures the `mdspan` overload is viable only when the accessor has a resolvable const counterpart.

### How users obtain a const view

This paper does **not** propose a new named cast function with an explicit target-type argument. Two existing mechanisms together cover the needs:

**1. mdspan's converting constructor.** mdspan's constructor template already accepts another mdspan whose template arguments are pairwise compatible. When an accessor type `A` has an implicit `A → A_const` conversion — for example, `default_accessor<T>` has an implicit conversion to `default_accessor<const T>` in C++26 — the following compiles today with no library change:

```cpp
mdspan<double, E> md = /* ... */;
mdspan<const double, E> ro = md;   // no cast, no CPO, no ceremony
```

**2. `std::const_view` CPO.** When the user cannot conveniently spell the target type (for custom accessors with nontrivial template parameters) or wants to make the const-ification explicit at the call site, the CPO is the right tool. The target type is deduced:

```cpp
mdspan<double, E, L, my_accessor<double>> md = /* ... */;
auto ro = std::const_view(md);
// decltype(ro) is mdspan<const double, E, L, my_accessor<const double>>
```

### Handling of standard accessors

No changes are proposed to `std::default_accessor` or `std::aligned_accessor` themselves — **no new member, no nested alias**. Their const counterparts are handled by direct overloads inside the `const_view` CPO:

- `std::default_accessor<T>` → `std::default_accessor<const T>`
- `std::aligned_accessor<T, N>` → `std::aligned_accessor<const T, N>`

Both accessors already provide implicit converting constructors between the non-const and const instantiations in C++26; the overload bodies return a default-constructed const instantiation.

---

## Design rationale and considered alternatives

### Why not overload `std::as_const`

An earlier draft considered adding an overload of `std::as_const(md)` in `<mdspan>`. This was rejected: `std::as_const<T>(T&)` from `<utility>` is **shallow** (returns `const T&`), while an mdspan version would be **deep** (returns a new mdspan value with a const element type). Reusing the name under one namespace would give readers two different semantics under one identifier. A dedicated `std::const_view` CPO with its own name makes the mechanism explicit and avoids the collision.

### Why a CPO, not a named cast function

Early drafts proposed a named cast function template (`std::element_cast<T>(md)`). Two problems drove the shift to a CPO:

- **mdspan's idiom is conversion, not named casts.** mdspan's primary type-changing mechanism is its converting constructor, which kicks in implicitly when the accessor types convert; adding a named cast on top duplicates machinery the library already has. A CPO is an additive customization surface, not a replacement conversion facility.
- **A named cast needs an explicit target-type argument.** `std::element_cast<const T>(md)` forces the user to spell out the element type at the call site. For common cases that's fine; for custom accessors with elaborate template parameters it's awkward. The CPO deduces the target and avoids that burden.

### Why freestanding, not a member

A member function `mdspan::const_view()` was considered and rejected. Four reasons:

**1. The CPO must accept both mdspans and bare accessors.** A member on `mdspan` can only dispatch on `mdspan`. The CPO is also required to operate on an accessor directly: `std::const_view(md.accessor())` is how custom accessors opt in, and how the mdspan overload itself dispatches internally. A member `mdspan::const_view()` covers only half of the needed surface; an accessor-only free CPO plus an mdspan member would split the customization story in two, forcing authors of custom accessors to reason about two paths instead of one.

**2. ADL customization requires a free-function surface.** Users customize by writing a free function literally named `const_view` in their accessor's namespace, found via argument-dependent lookup — the `std::ranges::begin` / `std::views::as_const` / `std::execution` pattern. That customization surface is structurally a free function. A member on `mdspan` cannot host it; the member would have to call *into* some other accessor-level hook, which would end up being the free CPO this paper is already proposing. The member would be a thin forwarder over the real customization point — a duplicated surface.

**3. Type-changing operations on values are freestanding in the standard.** `bit_cast`, `const_cast`, `static_cast`, `reinterpret_cast`, `as_const`, `forward`, `move`, the ranges adaptors, and modern CPOs are all freestanding. A member `mdspan::const_view()` would be the only named type-changing operation on an existing standard-library value type delivered as a member. The novelty has no payoff.

**4. Customization concerns belong with the thing being customized.** The operation is fundamentally about producing the const counterpart of an *accessor*; the mdspan case is a downstream composition. Putting the entry point on `mdspan` concentrates accessor-customization machinery inside the `mdspan` class definition, where it doesn't belong and where it cannot be extended for types that compose with mdspan (for example, future mdspan-adjacent view types) without further surgery on `mdspan` itself.

One freestanding customization point object is the single source of truth, and matches how every analogous facility in the modern standard library is designed.

### Naming the operation — `const_view`, and why not the alternatives

We considered five candidate names and chose **`std::const_view`**. The rejected alternatives and the specific concern for each:

**`std::as_const_view`** — the strongest semantic match to the ranges vocabulary, but `std::ranges::as_const_view<V>` already exists in `<ranges>` as a **class template** with a **structurally different design**:

- `std::ranges::as_const_view<V>` is a **wrapper class** that stores the underlying view in a data member, exposes a `.base()` accessor to unwrap, and synthesizes const behavior through `basic_const_iterator` at iteration time. The element type of the wrapped view does **not** change.
- Our operation returns a plain `std::mdspan<const T, E, L, /* const-counterpart accessor */>`. It does **not** wrap; the result has no `.base()`; the `element_type` template parameter is directly changed; const-ness is carried by the accessor, not synthesized at access time.

Reusing the name would mislead readers who know the ranges version. They would expect `.base()` to work, expect a wrapper type, expect element-type preservation — and none of those hold. The namespace difference (`std::` vs. `std::ranges::`) does not defuse the confusion: the identifier reads the same in documentation, teaching material, error messages, and search results. Sharing one name between two structurally different operations papers over a real design difference and demands an apologetic "read this paragraph first" explanation that good naming should avoid.

**`std::make_const_view`** — rejected because the dominant `std::make_*` idiom is *owning* factories (`make_shared`, `make_unique`, `make_pair`, `make_tuple`, `make_optional`); cold readers would infer allocation or ownership, neither of which applies here.

**`std::to_const`** — rejected because it reads as a verb-ier parallel to `std::as_const` (from `<utility>`), which is specifically the *shallow* operation; the name would obscure precisely the property it needs to advertise.

**`std::view_as_const`** — rejected because verb-noun-qualifier word order has no precedent in the standard library; justifying the novelty would distract from the design.

**`std::const_view`** (selected) — uses the standard's established `const_X` vocabulary (`const_iterator`, `const_reference`, `const_pointer`, `const_iterator_t`, `const_range_reference_t`). No collision with `std::ranges::as_const_view` or `std::views::as_const`. Does not carry the owning-factory connotation of `std::make_*`. No shallow-const confusion.

---

## Coordination with P2689R3

P2689R3 (Edwards, Maher, Hoemmen) proposes `basic_atomic_accessor<T, MemOrder>` for use with mdspan. The `const_view` customization point proposed here applies to it through the ADL-found `const_view` free function that P2689R3 or a follow-up is expected to provide; no wording in this paper mentions `basic_atomic_accessor` directly. Coordination on the `basic_common_reference` specializations lives in the companion SG1 paper.

---

## Implementation experience

A libcudacxx prototype of the CPO-based design is implemented on the CCCL fork (branch `feature/mdspan-const-accessor-cp-design`). It provides:

- `cuda::std::const_view` as a customization point object declared in `<cuda/std/mdspan>`, with direct overloads for `cuda::std::default_accessor` and `cuda::std::aligned_accessor`.
- Author-side customization via ADL-found `const_view` free functions (niebloid pattern, matching `std::ranges::begin`). No `namespace std` specialization path; consumers of uncooperative third-party accessors wrap the vendor type in a local adapter.

The prototype validates that:

- `std::const_view(md)` composes cleanly with `std::submdspan`: the two operations commute type-wise for `default_accessor` and `aligned_accessor`.
- `std::const_view(md)` is O(1) — no element copy, no allocation — and the result's `data_handle()` is the same address as the input's.
- An accessor with no ADL hook produces a clean "no matching call" diagnostic at the call site (not a deep metaprogramming cascade).

The prototype's tests are organized as `.pass.cpp` / `.fail.cpp` under `libcudacxx/test/libcudacxx/std/containers/views/mdspan/const_view/`. All positive tests compile and run; both negative tests (`no_opt_in.fail.cpp`, `readonly.fail.cpp`) correctly fail to compile as intended. Implementation experience for the companion `basic_common_reference` work is reported separately.

---

## References

- **P3323R1** — *cv-qualified types in atomic and atomic_ref*. Added `atomic_ref<const T>` in C++26; introduced the gap this paper addresses. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3323r1.html>
- **P3860R1** — *Proposed Resolution for NB Comment GB13-309: `atomic_ref<T>` is not convertible to `atomic_ref<const T>`*. Adds the converting constructor; adjacent but does not address `common_reference_with`. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3860r1.html>
- **P2689R3** — *Atomic Refs Bound to Memory Orderings & Atomic Accessors*. Proposes `basic_atomic_accessor` for use with mdspan; natural collaborator for the companion `basic_common_reference` specializations. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2689r3.html>
- **LWG 3811** — *`views::as_const` on `ref_view<T>` should return `ref_view<const T>`*. Adjacent `as_const` semantics issue for ranges; confirms the per-case wiring pattern. <https://cplusplus.github.io/LWG/issue3811>
- **`[range.access.begin]`** in the C++ working draft — precedent for the expression-equivalence cascade and poison-pill pattern used by this paper's wording.
- **`[customization.point.object]`** in the C++ working draft — requirements on customization point objects.
- **`[cmp.alg]`** in the C++ working draft — precedent for the synopsis form of a customization point object declared directly in `namespace std`.
- **Companion SG1 paper** — `2026-04-23-atomic-ref-common-reference-draft.md` (same repository). Proposes the `basic_common_reference` specializations for `atomic_ref` (same-const and cross-const) that make the motivating case compose end-to-end.
- **LWG Active Issues**. <https://www.open-std.org/jtc1/sc22/wg21/docs/lwg-active.html>

---

## Acknowledgements

To be completed in R1 after review (reviewers, proposed coordinators from P2689R3, CCCL contributors).

---
