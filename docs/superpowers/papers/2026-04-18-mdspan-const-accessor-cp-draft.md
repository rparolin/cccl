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

Independently, `common_reference_with` lacks cross-const specializations for proxy references. The following `static_assert`s — distilled from the motivating godbolt — fail today:

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

No new UB or erroneous behavior is introduced. This is `const` as C++ already defines it.

### The customization-point trait

```cpp
namespace std {
  template<class A>
  struct const_accessor_for;

  template<class A>
  using const_accessor_for_t = typename const_accessor_for<A>::type;
}
```

Resolution (informal):

- **Member hook**: if `typename A::const_accessor_type` is a valid accessor type, that's the answer.
- **Substitution fallback**: otherwise, if `A` is `Tmpl<T, Rest...>` for some class template `Tmpl`, the answer is `Tmpl<std::add_const_t<T>, Rest...>` (if well-formed).
- **User specialization**: a user may specialize `const_accessor_for` in namespace `std` for accessors they do not control.
- **Ill-formed otherwise**: the primary template has no nested `type` and the call site gets a clean diagnostic.

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

### Opt-in on standard accessors

Both `std::default_accessor` and `std::aligned_accessor` carry the nested alias explicitly:

```cpp
template<class ElementType>
struct default_accessor {
  // ...existing members...
  using const_accessor_type = default_accessor<add_const_t<ElementType>>;
};

template<class ElementType, size_t ByteAlignment>
struct aligned_accessor {
  // ...existing members...
  using const_accessor_type =
      aligned_accessor<add_const_t<ElementType>, ByteAlignment>;
};
```

Even though the substitution fallback would produce the same answer, the explicit alias is self-documenting and immune to any future narrowing of the fallback rules.

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
- Ill-formed cases (accessor without a valid const counterpart; target type not cv-reachable from element type) produce clean compile errors at the call site.

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
- **`vector<bool>::reference` cross-const `basic_common_reference`**: analogous to the `atomic_ref` fix, but belongs to the independent re-design of `vector<bool>` and is out of scope here.

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
