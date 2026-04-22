# Customization Point for a const view of `std::mdspan`

| | |
|---|---|
| **Document #** | DNNNN (to be assigned) |
| **Date** | 2026-04-18 |
| **Reply-to** | Rob Parolin <rparolin@nvidia.com> |
| **Audience** | LEWG (mdspan), SG1 (atomic) |
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

1. A customization-point trait `std::const_accessor_for<A>` that names the const counterpart of an accessor `A`, with a hybrid member/specialization/substitution resolution.
2. A freestanding function template `std::element_cast<T>(md)` in `<mdspan>` that produces the const view.
3. Nested `const_accessor_type` aliases on `std::default_accessor` and `std::aligned_accessor` to make their const counterparts explicit.
4. `basic_common_reference` specializations in `<atomic>` for cross-const pairs of `std::atomic_ref`, so the resulting mdspan can be used with standard algorithms that require `common_reference_with` across reference types.

(4) coordinates with P2689R3 ("Atomic Refs Bound to Memory Orderings & Atomic Accessors"); we expect to merge or cross-reference rather than duplicate.

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

and view a buffer as `mdspan<double, E, L, atomic_accessor<double>>`. But there is no way, in the current standard, to construct a read-only view of that same data with `atomic_ref<const double>` as its reference. Users must manually spell the const-accessor and data-handle types every time they need a read-only view — and for a custom accessor the required conversions are not always even available.

### The `common_reference_with` gap

Independently, `common_reference_with` lacks cross-const specializations for proxy references. The following `static_assert`s fail today:

```cpp
static_assert(std::common_reference_with<std::atomic_ref<float>,       const float&>);
static_assert(std::common_reference_with<std::atomic_ref<const float>, float&>);
static_assert(std::common_reference_with<std::vector<bool>::reference,       const bool&>);
static_assert(std::common_reference_with<std::vector<bool>::const_reference, bool&>);
```

Consequence: any standard facility that checks `common_reference_with` across a proxy and a non-proxy reference — including `indirectly_readable`, `indirectly_writable`, and range adaptors that interoperate const and non-const views — refuses to compile. Without closing this gap, an mdspan customization point alone is only useful for non-proxy accessors; the motivating `atomic_ref` case doesn't work end to end.

### Why bundle both in one paper

The mdspan-side customization point and the `atomic_ref` cross-const `basic_common_reference` fixes are independently small, but they together solve the motivating example. Shipping the customization point alone leaves proxy-reference accessors half-broken; shipping the `basic_common_reference` fixes alone doesn't give users a way to construct the const view. Bundling avoids the bad intermediate state where neither half is useful.

This paper's `<atomic>` additions overlap with P2689R3. Section *Coordination* (below) proposes merging or cross-referencing rather than parallel proposals.

---

## Proposal overview

### What "const version of an accessor" means

For an accessor `A` with `element_type = T`, the **const version** `C` (produced by `const_accessor_for<A>`) satisfies:

1. `C::element_type` is `std::add_const_t<T>`.
2. `C::reference` is not writable.
3. Reads may have side effects — follows the convention of const member functions in C++.
4. An mdspan with accessor `C` views the same elements as the original mdspan with accessor `A`.
5. Read stability is not guaranteed unless the accessor documents it.
6. `C::offset_policy` is const-preserving, so composition with `submdspan` is well-defined in either order.

This is `const` as C++ already defines it.

### The `const_view` customization point object

`std::const_view` is a **customization point object** (CPO) — an inline `constexpr` function object of unspecified type, declared in `<mdspan>`. Users call it with either an `mdspan` or an accessor:

```cpp
namespace std {
  inline constexpr /* unspecified */ const_view = /* unspecified */;
}
```

- `std::const_view(md)` where `md` is an `mdspan<T, E, L, A>` returns an `mdspan<const T, E, L, C>` (where `C` is the const counterpart of `A`) over the same data.
- `std::const_view(acc)` where `acc` is an accessor returns the const counterpart accessor. The `mdspan` overload delegates to this one internally.

#### Customization paths

Customization is through either of two mechanisms, consulted in priority order:

**1. Author-facing: ADL `tag_invoke` hook.** An accessor author provides a free function in their own namespace; `const_view` finds it via argument-dependent lookup:

```cpp
namespace user_lib {
  template <class T>
  struct atomic_accessor { /* element_type, reference, access(), offset(), ... */ };

  // The library's const_view finds this via ADL on atomic_accessor<T>.
  template <class T>
  atomic_accessor<const T>
  tag_invoke(decltype(std::const_view), atomic_accessor<T>) noexcept
  { return {}; }
}
```

The author never opens `namespace std` and never writes a specialization. This is the modern C++ customization-point idiom shared with `std::ranges::begin`, `std::execution` CPOs, and similar facilities.

**2. Consumer-facing: trait override escape hatch.** A consumer using a *third-party* accessor — one whose author did not provide a `tag_invoke` hook and which the consumer cannot modify — can specialize an escape-hatch trait:

```cpp
namespace std {
  template <class T>
  struct const_view_override<vendor::gpu_accessor<T>> {
    using type = vendor::gpu_accessor<const T>;
  };
}
```

`const_view_override` is a small trait that exists solely for this escape-hatch role. It is consulted only when no ADL hook is found. The precedent for user specializations of standard-library templates already exists for `std::hash<UserType>` and `std::formatter<UserType>`.

**3. Library-provided built-ins.** The standard library provides direct handling for `std::default_accessor<T>` and `std::aligned_accessor<T, N>` — their const counterparts are `std::default_accessor<const T>` and `std::aligned_accessor<const T, N>` respectively. No template-argument substitution fallback is proposed; the built-ins are enumerated explicitly.

If none of these paths yield a result, `std::const_view(x)` is ill-formed with a clean "no matching call" diagnostic at the call site.

#### Why both an ADL hook and a trait override

The two customization paths serve two genuinely different audiences, and **neither alone covers the other's case**:

- The **ADL hook** is for the accessor's **author**. They own the type and provide a free function next to it in their own namespace. No `namespace std` gymnastics; follows the standard's modern CPO customization pattern.
- The **trait override** is for the accessor's **consumer**. When the user does not own the accessor type and the vendor did not opt in, ADL has nowhere to find a hook. The trait override is the escape hatch — analogous to specializing `std::hash<UserType>` today.

Dropping the ADL hook would force every accessor author to specialize a standard-library template in `namespace std` for their own type — awkward and inconsistent with how recent C++ customization points are designed. Dropping the trait override would strand consumers of uncooperative third-party accessors.

The library built-ins are orthogonal to these two: they cover the standard accessors directly so users never write anything in the common case.

#### How `const_view(mdspan)` is implemented

The `mdspan` overload is not customizable; the library always provides the same implementation, which delegates to the accessor overload:

```cpp
// Sketch; actual wording will be in [mdspan.const_view].
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

The constraint `requires { const_view(a); }` ensures the `mdspan` overload is only viable when the accessor has a resolvable const counterpart; otherwise the entire call is ill-formed, consistent with the accessor-level diagnostic.

### The cast function

```cpp
namespace std {
  // In <mdspan>. T is restricted to cv-qualification changes of element_type.
  template<class T, class E, class L, class A>
    requires (is_same_v<T, typename A::element_type>
              || is_same_v<T, add_const_t<typename A::element_type>>)
  constexpr auto
  element_cast(const mdspan<typename A::element_type, E, L, A>& md)
    -> mdspan<T, E, L, /* see-below */>;
}
```

Where the result accessor type is:

- `A` when `T == typename A::element_type` (identity cast).
- `const_accessor_for_t<A>` when `T == add_const_t<typename A::element_type>` (const-add cast).

Other choices — element-type conversion (`float → double`) or `volatile`-qualification — are ill-formed in this revision; see *Future work*.

### Handling of standard accessors

No changes are proposed to `std::default_accessor` or `std::aligned_accessor` themselves — **no new member, no nested alias**. Their const counterparts are handled by direct overloads inside the `const_view` CPO:

- `std::default_accessor<T>` → `std::default_accessor<const T>`
- `std::aligned_accessor<T, N>` → `std::aligned_accessor<const T, N>`

Both accessors already provide implicit converting constructors between the non-const and const instantiations in C++26, so the overload bodies are trivial (the result is default-constructible and compatible with the source via the existing converting constructor).

#### Why not a nested member alias

An earlier draft added a nested `const_accessor_type` alias to both accessors. That alias is redundant:

- For `default_accessor<T>` and `aligned_accessor<T, N>`, the const counterpart is mechanically `Foo<const T, …>`; the CPO's built-in overload already covers it with two lines of library code.
- Adding a standard-mandated member to these accessors sets a precedent for *all* user accessors to carry that member, which is precisely what the CPO model is intended to replace.
- Users who write their own accessors whose const version is *not* `Foo<const T, …>` are served by the ADL hook (a free function, not a member), which is the idiomatic shape. Mixing member-aliases and ADL hooks as two customization mechanisms doubles the surface and the documentation burden for no structural benefit.

#### Why no template-substitution fallback

Also considered — and rejected — was a generic fallback that, for any accessor `Tmpl<T, Rest...>`, would substitute to `Tmpl<add_const_t<T>, Rest...>`. Two problems:

- **Compiler fragility.** Template-template argument matching in partial specializations — especially with packs — has had persistent interop bugs across Clang and GCC. The CUTLASS project has felt these pains. Relying on this in a standard-library customization point bakes in implementation divergence.
- **Silent miscompilation on non-orthogonal parameters.** User accessors commonly have template parameters whose meaning depends on the first (e.g., a policy defaulted from `T`, alias templates, tag-dependent bool parameters). A blanket substitution would produce the wrong answer without any diagnostic.

The chosen design enumerates the library-supported accessors directly and requires explicit opt-in (via ADL hook or trait override) for any user accessor whose const version is not a standard-library one.

### `basic_common_reference` specializations in `<atomic>`

Add specializations covering the cross-const pairs for `atomic_ref`:

- `atomic_ref<T>` ⇄ `const T&`
- `atomic_ref<const T>` ⇄ `T&`
- `atomic_ref<T>` ⇄ `atomic_ref<const T>`

Each is a handful of lines mirroring the structure P3323R1 used for `atomic_ref<T>` ⇄ `T&`.

**Scope note**: `std::vector<bool>::reference` has analogous cross-const gaps but is **not** a mdspan accessor reference in practice; fixing it raises independent questions about the design of `vector<bool>` and is deferred to a separate effort.

---

## Design rationale and considered alternatives

### Why `element_cast<T>`, not `std::as_const`

An earlier draft proposed an overload of `std::as_const(md)` declared in `<mdspan>`. This was rejected: `std::as_const<T>(T&)` from `<utility>` is shallow (returns `const T&`), while the mdspan version would be deep (returns a new mdspan value with a const element type). The name collision would give readers two different semantics under one identifier. `element_cast<T>` makes the element-type change explicit, matches the pattern of other named casts (`bit_cast`, `const_cast`, …), and avoids the collision.

### Why not `std::views::element_cast`

`std::views::*` is the `ranges` namespace. `mdspan` is not a range — no iterators, no `view` concept modeling — and coupling it to `std::ranges` for this operation would drag mdspan into a library it has no other relationship with. Keeping mdspan independent of `std::ranges` preserves clean responsibility boundaries.

### Why freestanding, not a member

Every named cast in the standard (`bit_cast`, `const_cast`, `static_cast`, `reinterpret_cast`) is freestanding; range adaptors under `std::views::` are freestanding too. A member `mdspan::cast<T>()` would be the odd one out and make composition with other freestanding operations (e.g., `submdspan`) less uniform.

### Why not add a converting constructor

mdspan's existing converting-constructor template already handles the case where the underlying accessor provides an implicit `A → const_accessor_for_t<A>` conversion. For example, `default_accessor<T> → default_accessor<const T>` is already implicit in C++26, so `mdspan<const double, E> ct = mt;` compiles today for the default accessor. `element_cast<T>` is the explicit opt-in that also works when the implicit path isn't available — for custom accessors or third-party trait specializations.

### Why bundle `basic_common_reference` here (vs. split paper)

Splitting was considered and rejected. The motivating example (`atomic_ref<const T>` in mdspan, used alongside the non-const original) only works end-to-end with both changes landed. Shipping the mdspan customization point alone leaves users with a syntactically correct mdspan that cannot be used in standard algorithms that exercise `common_reference_with` across reference types. The bundled approach keeps users from hitting that pothole.

### Why narrow `element_cast<T>` (only cv-changes) for this paper

A fully general `element_cast<T>(md)` that also permits element-type conversion (`float → double`) was considered. It raises its own questions — alignment, compute-vs-view semantics, layout compatibility — each of which deserves a separate paper. The restriction here to cv-qualification changes is a deliberately small first step; the chosen name leaves the door open for a future generalization.

### Naming the operation — `const_view`, and why not the alternatives

(*Forward-looking: this subsection records the naming decision for R1 after early design review. The surrounding subsections and the main body still use the R0 provisional name `element_cast`; R1 will reconcile.*)

Independent of whether the final mechanism is a templated function or a customization point object (see open design questions in R1), the operation needs a name. We considered five candidates and chose **`std::const_view`**. The rejected alternatives and the specific concern for each:

**`std::as_const_view`** — the strongest semantic match to the ranges vocabulary, but `std::ranges::as_const_view<V>` already exists in `<ranges>` as a **class template** with a **structurally different design**:

- `std::ranges::as_const_view<V>` is a **wrapper class** that stores the underlying view in a data member, exposes a `.base()` accessor to unwrap, and synthesizes const behavior through `basic_const_iterator` at iteration time. The element type of the wrapped view does **not** change.
- Our operation returns a plain `std::mdspan<const T, E, L, /* const-counterpart accessor */>`. It does **not** wrap; the result has no `.base()`; the `element_type` template parameter is directly changed; const-ness is carried by the accessor, not synthesized at access time.

Reusing the name would mislead readers who know the ranges version. They would expect `.base()` to work, expect a wrapper type, expect element-type preservation — and none of those hold. The namespace difference (`std::` vs. `std::ranges::`) does not defuse the confusion: the identifier reads the same in documentation, teaching material, error messages, and search results. Sharing one name between two structurally different operations papers over a real design difference and demands an apologetic "read this paragraph first" explanation that good naming should avoid.

**`std::make_const_view`** — has legitimate precedent via the non-owning adapter factories `std::make_reverse_iterator`, `std::make_move_iterator`, and `std::make_format_args`. Rejected because the dominant `std::make_*` idiom is *owning* factories: `std::make_shared`, `std::make_unique`, `std::make_pair`, `std::make_tuple`, `std::make_optional`, `std::make_any` all allocate or take ownership of newly-constructed objects. A reader encountering `std::make_const_view(md)` cold would likely assume allocation or ownership semantics, neither of which applies to our operation.

**`std::to_const`** — follows the `std::to_X` family (`to_address`, `to_underlying`, `to_string`, `to_chars`, `to_integer`, `to_array`): short and verb-like. Rejected because it reads as parallel to `std::as_const` (from `<utility>`), which is specifically the *shallow* const operation. A name that reads "like as_const but verb-ier" implies shallow semantics; our operation is deep (changes the element type). The name would obscure precisely the property it needs to advertise.

**`std::view_as_const`** — maximally explicit and avoids all collisions, but uses a verb-noun-qualifier word order that no existing standard-library name follows. Would be a novel coinage without an established pattern to cite; the burden of justifying the shape would distract from the design.

**`std::const_view`** (selected) — uses the standard's established `const_X` vocabulary (`const_iterator`, `const_reference`, `const_pointer`, `const_iterator_t`, `const_range_reference_t`). No collision with `std::ranges::as_const_view` or with `std::views::as_const`. Does not carry the owning-factory connotation of `std::make_*`. No shallow-const confusion. The one mild concern — that `const_X` identifiers in the standard are usually types, not function objects — is cushioned by the fact that no existing standard identifier is `std::const_view` at the top level, so no competing reader expectation exists to fight.

---

## Coordination with P2689R3

P2689R3 ("Atomic Refs Bound to Memory Orderings & Atomic Accessors", Edwards, Maher, Hoemmen) is actively proposing `basic_atomic_accessor<T, MemOrder>` for use with mdspan. Its authors are the natural collaborators for the `basic_common_reference` specializations proposed here — those specializations are the piece that makes their accessor usable in read-only form alongside the non-const original. We expect either:

- Merge: fold this paper's `<atomic>` additions into P2689Rn.
- Reference: P2689Rn cites this paper (or vice versa) for the `basic_common_reference` piece; neither duplicates the other's wording.

Recommendation from the authors of this paper: merge if scope timing permits; otherwise explicit cross-reference.

---

## Implementation experience

A libcudacxx prototype is in progress on the CCCL fork ([branch link TBD when polished]). The prototype implements the mdspan-side pieces (trait, `default_accessor` / `aligned_accessor` member alias, `cuda::std::element_cast<T>`) under the `cuda::std::` namespace. The `basic_common_reference` specializations are deferred from the prototype since they belong in `<atomic>` rather than `<mdspan>`.

The prototype validates that:

- The member-first-then-substitution trait resolution composes correctly with both standard and user-defined accessors.
- `element_cast<T>` is O(1) and preserves the mdspan's mapping and data handle by construction.
- `submdspan(element_cast<const T>(md))` and `element_cast<const T>(submdspan(md))` yield the same mdspan type for `default_accessor` and `aligned_accessor`.
- Ill-formed cases (accessor without a valid const counterpart; target type T that is neither `element_type` nor `add_const_t<element_type>`) produce clean compile errors at the call site.

The prototype's tests are organized as `.pass.cpp` / `.fail.cpp` under `libcudacxx/test/.../mdspan/element_cast/`.

---

## Wording

*(Wording is placeholder in R0. Final revision will include concrete diffs against the latest working draft for §[mdspan], §[mdspan.accessor.default], §[mdspan.accessor.aligned], and §[atomics.ref.generic] / §[atomics.ref.operations].)*

### Approximate wording changes

1. **§[mdspan.syn] / §[mdspan.accessor.reqmts]**: introduce the `const_accessor_for` trait template, its specializations, and the `const_accessor_for_t` alias template; state the semantic requirements on the resolved type.
2. **§[mdspan.syn]**: add the `element_cast` function template declaration.
3. **§[mdspan.mdspan]**: define the `element_cast` function template's semantics (the two overloads: identity and const-add).
4. **§[mdspan.accessor.default]**: add the `const_accessor_type` alias to `default_accessor`.
5. **§[mdspan.accessor.aligned]**: add the `const_accessor_type` alias to `aligned_accessor`.
6. **§[atomics.ref.*]**: add `basic_common_reference` specializations for the cross-const `atomic_ref` pairs.

Concrete wording to follow in R1 after LEWG/SG1 design feedback.

---

## Future work

- **Broader `element_cast<T>`**: permit element-type conversion (`float → double`) or `volatile`-qualification. Each raises independent design questions (alignment, compute-vs-view semantics, volatile-access semantics) and deserves its own paper. The name `element_cast` already supports the broader interpretation; this paper's narrow restriction is a deliberate first step.

---

## References

- **P3323R1** — *cv-qualified types in atomic and atomic_ref*. Added `atomic_ref<const T>` in C++26; introduced the gap this paper addresses. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3323r1.html>
- **P3860R1** — *Proposed Resolution for NB Comment GB13-309: `atomic_ref<T>` is not convertible to `atomic_ref<const T>`*. Adds the converting constructor; adjacent but does not address `common_reference_with`. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3860r1.html>
- **P2689R3** — *Atomic Refs Bound to Memory Orderings & Atomic Accessors*. Proposes `basic_atomic_accessor` for use with mdspan; natural collaborator for the cross-const `basic_common_reference` specializations. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2689r3.html>
- **LWG 3811** — *`views::as_const` on `ref_view<T>` should return `ref_view<const T>`*. Adjacent `as_const` semantics issue for ranges; confirms the per-case wiring pattern. <https://cplusplus.github.io/LWG/issue3811>
- **LWG Active Issues**. <https://www.open-std.org/jtc1/sc22/wg21/docs/lwg-active.html>

---

## Acknowledgements

To be completed in R1 after review (reviewers, proposed coordinators from P2689R3, CCCL contributors).

---

*Design document this paper derives from: `docs/superpowers/specs/2026-04-18-mdspan-const-accessor-cp-design.md` (in the CCCL repository).*
