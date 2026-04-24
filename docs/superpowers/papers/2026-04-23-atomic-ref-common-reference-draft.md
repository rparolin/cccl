#  `basic_common_reference` specializations for `std::atomic_ref`

| | |
|---|---|
| **Document #** | DNNNN (to be assigned) |
| **Date** | 2026-04-23 |
| **Reply-to** | Rob Parolin <rparolin@nvidia.com> |
| **Audience** | SG1 |
| **Target** | C++29 |
| **Revision** | R0 (draft) |

**Related papers / issues**:
[P3323R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3323r1.html),
[P3860R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3860r1.html),
[P2689R3](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2689r3.html),
DNNNN ("Customization Point for a const view of `std::mdspan`", motivating consumer).

---

## Abstract

`std::atomic_ref` does not interoperate with plain references through the `common_reference_with` machinery. In current C++26, *every* non-identity `common_reference_with` query involving `atomic_ref` against `T&`, `const T&`, `atomic_ref<T>`, or `atomic_ref<const T>` — same-const and cross-const alike — evaluates to `false`.

This paper proposes six `basic_common_reference` partial specializations in `<atomic>` that close the full gap. The resolved common type is `atomic_ref<T>` when both operands are at the same non-const element type, and `atomic_ref<const T>` otherwise — preserving proxy semantics across the common-reference relation.

---

## Motivation

### The gap in the current standard

C++26 shipped `atomic_ref<const T>` (P3323R1) and the converting constructor (P3860R1), but neither added a `basic_common_reference` specialization. As a result, every non-identity `common_reference_with` query across `atomic_ref` and references to its element type evaluates to `false`:

```cpp
// All of these fail in current C++26:

static_assert(std::common_reference_with<std::atomic_ref<T>,       T&>);                     // fails
static_assert(std::common_reference_with<std::atomic_ref<const T>, const T&>);               // fails
static_assert(std::common_reference_with<std::atomic_ref<T>,       const T&>);               // fails
static_assert(std::common_reference_with<std::atomic_ref<const T>, T&>);                     // fails
static_assert(std::common_reference_with<std::atomic_ref<T>,       std::atomic_ref<const T>>); // fails
```

Each failure has the same root cause: with no `basic_common_reference` partial specialization matching the operand pair, the `common_reference_t` cascade falls through to the primary template (which defines no `type` member), so the type is undefined and `common_reference_with` evaluates to `false`. `common_reference_with<atomic_ref<T>, atomic_ref<T>>` succeeds through the identity-type path; same-type pairs are not part of the gap.

### Why this matters

**1. Range adaptors over atomic buffers.** A user with a `span<T>` wrapped by a proxy iterator that yields `atomic_ref<T>` cannot pass the range (or a const version of it) into standard algorithms that check iterator reference compatibility. `indirectly_readable`, `indirectly_writable`, and `indirectly_copyable` each probe `common_reference_with` through `iter_common_reference_t`. The mismatch reports as a concept failure deep inside the algorithm's constraint, not at the user's call site.

**2. Generic algorithms comparing two reference types.** Algorithm templates that require two reference parameters to share a common reference fail to instantiate when one side is an atomic proxy, even for benign read-against-write comparisons at matching element types. User code spelling `common_reference_with<R1, R2>` as its own precondition check — for example, to dispatch between atomic and non-atomic fast paths — routes atomic-ref inputs into the wrong path.

### The motivating consumer

A companion paper (DNNNN, *Customization Point for a const view of `std::mdspan`*) needs these specializations to make `std::atomic_ref`-backed mdspan accessors usable end-to-end. An accessor whose const counterpart produces an mdspan over `atomic_ref<const T>` cannot be used with the non-const original in any algorithm that asks `common_reference_with`. That paper consumes this one; this paper stands alone.

### Why split from the mdspan paper

- **Different audience.** The mdspan customization point object is an LEWG question about mdspan's customization surface. The `basic_common_reference` work is an SG1 question about atomics. Bundling pushes one audience to review material outside its expertise.
- **Independent value.** The gap harms non-mdspan code (section above). Users blocked today do not benefit from a paper that stalls on unrelated mdspan review.
- **No technical dependency.** The specializations here are self-contained; the mdspan paper references them but does not require them to be in the same document.

---

## Proposal

Add six `basic_common_reference` partial specializations to `<atomic>`. Together they cover the full matrix of non-identity pairs.

```cpp
// Pair 1: atomic_ref<T> against (possibly const-qualified) reference to T.
// Resolves to atomic_ref<T> for same-const, atomic_ref<const T> for cross-const.

template <class T, template<class> class TQ, template<class> class UQ>
struct basic_common_reference<atomic_ref<T>, T, TQ, UQ> {
  using type = atomic_ref<
    conditional_t<is_const_v<remove_reference_t<UQ<T>>>, const T, T>>;
};

template <class T, template<class> class TQ, template<class> class UQ>
struct basic_common_reference<T, atomic_ref<T>, TQ, UQ> {
  using type = atomic_ref<
    conditional_t<is_const_v<remove_reference_t<TQ<T>>>, const T, T>>;
};

// Pair 2: atomic_ref<const T> against (possibly const-qualified) reference to T.
// Resolves to atomic_ref<const T> unconditionally — either operand's const-ness suffices.

template <class T, template<class> class TQ, template<class> class UQ>
struct basic_common_reference<atomic_ref<const T>, T, TQ, UQ> {
  using type = atomic_ref<const T>;
};

template <class T, template<class> class TQ, template<class> class UQ>
struct basic_common_reference<T, atomic_ref<const T>, TQ, UQ> {
  using type = atomic_ref<const T>;
};

// Pair 3: atomic_ref<T> against atomic_ref<const T>.
// Resolves to atomic_ref<const T> — the stricter of the two proxies.

template <class T, template<class> class TQ, template<class> class UQ>
struct basic_common_reference<atomic_ref<T>, atomic_ref<const T>, TQ, UQ> {
  using type = atomic_ref<const T>;
};

template <class T, template<class> class TQ, template<class> class UQ>
struct basic_common_reference<atomic_ref<const T>, atomic_ref<T>, TQ, UQ> {
  using type = atomic_ref<const T>;
};
```

**Mandates.** The following Mandates applies to each specialization: `T` meets the requirements in `[atomics.ref.generic]` for `atomic_ref<T>` to be a valid specialization.

**Partial ordering.** Each specialization fixes a pattern in its first two type arguments while leaving `TQ` and `UQ` open. The six first-two-argument patterns — `(atomic_ref<T>, T)`, `(T, atomic_ref<T>)`, `(atomic_ref<const T>, T)`, `(T, atomic_ref<const T>)`, `(atomic_ref<T>, atomic_ref<const T>)`, `(atomic_ref<const T>, atomic_ref<T>)` — are pairwise deduction-disjoint: for any concrete operand pair, at most one pattern unifies, because the `T` appearing in the second argument must be the same template parameter as the `T` inside the first's `atomic_ref<…>`. Each specialization is unambiguously more specialized than the primary `basic_common_reference` template under `[temp.func.order]`.

**How `TQ` and `UQ` are bound.** In a `common_reference_t<A, B>` query, the library peels `A` and `B` down to unqualified `T1` and `T2` and evaluates `basic_common_reference<T1, T2, TQ, UQ>::type`, where `TQ<X>` yields `X` re-qualified with the cv-ref qualifiers stripped from `A`, and `UQ<X>` similarly for `B`. Two worked examples:

- `common_reference_t<atomic_ref<int>&, const int&>` binds `T1 = atomic_ref<int>`, `T2 = int`, `TQ<X> = X&`, `UQ<X> = const X&`. Pair 1 (first specialization) evaluates `UQ<int>` as `const int&`, `is_const_v<remove_reference_t<const int&>>` is `true`, so the result is `atomic_ref<const int>`.
- `common_reference_t<atomic_ref<int>&&, int&>` binds `T1 = atomic_ref<int>`, `T2 = int`, `TQ<X> = X&&`, `UQ<X> = X&`. Pair 1 evaluates `UQ<int>` as `int&`, `is_const_v<remove_reference_t<int&>>` is `false`, so the result is `atomic_ref<int>`.

Specializations 3–6 do not inspect `TQ`/`UQ`; their result is determined by the first two type arguments alone.

### Why this coverage, not cross-const only

An earlier iteration of this work covered only the cross-const pairs, under the mistaken premise that C++26 already supplied the same-const case. It does not: `common_reference_with<atomic_ref<T>, T&>` fails today. Proposing only cross-const specializations would leave the same-const gap unfixed — a strictly worse outcome. Covering the full matrix in one paper is smaller to review than two sequential papers, because any later same-const paper would be bound by this one's common-type convention.

---

## Design rationale

### Why `atomic_ref` (not a plain reference) as the common type

Both `atomic_ref<T>` / `atomic_ref<const T>` and `T&` / `const T&` close the concept-level gap; both operands are convertible to either. The proxy is preferred because it preserves atomic-access semantics through the common-reference relation: a generic algorithm that reads through a plain-reference common type would silently lose the atomicity the user requested. The rule is also uniform — *if any operand is an atomic reference, the common reference is an atomic reference at the stricter const-qualification* — so downstream dispatch that matches on reference type sees a consistent signal.

A plain-reference common type remains coherent and is rejected on these grounds, not on soundness.

### Why in `<atomic>`, not a separate proxy-reference utility

A generic "proxy-reference common-reference" utility that third-party proxy types could opt into was considered and rejected. The common-type choice is naturally type-specific: what is right for `atomic_ref` pairs may differ from what is right for `vector<bool>::reference` pairs, or for user-defined GPU-memory references. Forcing uniformity across proxy kinds would overfit all of them. Placing the specializations in `<atomic>` co-locates them with the type they serve, and keeps each proxy type's common-reference policy independently decidable.

### Scope notes — what this paper does not touch

- **`std::vector<bool>::reference`** has analogous cross-const gaps against `bool&` and `const bool&`. Those gaps are real but are entangled with open questions about the design of `std::vector<bool>` itself (the proxy returns `bool` by value, not by reference, which creates additional binding concerns with `common_reference_with`). This paper does not address them. A future paper may take them up following the pattern here.
- **`std::pair<T, U>` cross-const** and the adjacent cases in tuple-like types are outside scope. They concern `common_reference` behaviour for composite types, not for individual proxy references.
- **User-defined proxy references** (GPU-memory references, distributed-memory references, custom lock-free wrappers) are their authors' responsibility to specialize. The pattern in this paper — six specializations, proxy as the common type at the stricter const-qualification — is the template to follow.

---

## Alternatives considered

### Resolve to plain references / cover only cross-const pairs

Both are addressed in *Design rationale* and *Why this coverage, not cross-const only* above. The plain-reference alternative is rejected on atomicity-preservation grounds; the cross-const-only alternative is rejected because the same-const gap is equally broken today and a sequential paper would be bound by this one's common-type convention.

### Ship as an LWG issue against `[atomics.ref.generic]`

The change is well-scoped enough that an LWG issue looked plausible. Rejected: the design question ("what is the common type when one operand is a proxy") requires SG1 review, not wording polish. LWG is the wrong forum to settle that question, and an issue resolution cannot substitute for design-group input.

### Wait for P2689R3 to carry the wording

Rejected because P2689R3's scope is accessors, not `common_reference_with` on `atomic_ref` itself. The specializations here belong in `<atomic>` regardless of whether P2689R3 ships; the non-mdspan motivations are independent.

---

## Impact on existing code

No valid program that compiles today becomes ill-formed. The proposed specializations only add `true` where `common_reference_with` previously evaluated to `false` for the affected pairs; no case currently evaluating to `true` changes.

A program that relies on one of the affected `common_reference_with` queries being `false` — for example, an SFINAE predicate that specifically selects an overload when the concept fails for `(atomic_ref<T>, T&)` — would observe a behaviour change. This is the same class of observable change that any `common_reference_with` extension produces. The authors are not aware of code relying on the gap.

A user program that has written its own `basic_common_reference` specialization for one of the six template-ID patterns addressed here is already ill-formed no-diagnostic-required under `[namespace.std]`: `basic_common_reference` is a standard-library template, and user specializations must depend on at least one program-defined type, which `atomic_ref<T>` with a standard-library or built-in `T` is not. The proposed specializations therefore cannot break conforming code; they can only expose latent IFNDR programs.

---

## Implementation experience

A libcudacxx-based design-demonstration is implemented on the CCCL branch `feature/mdspan-const-accessor-cp-design`. The prototype exercises the proposed specializations against a minimal `fake_atomic_ref<T>` proxy type that mimics the C++26 `atomic_ref` shape (converting constructor from `atomic_ref<T>` to `atomic_ref<const T>`, construction from `T&` and `const T&`). With the six specializations in place, all affected `common_reference_with` checks succeed; without them, the checks fail as they do against the standard library today.

Heavier implementation experience — the customization point object itself — lives in the companion mdspan paper.

The prototype is host-only for the specialized proxy type. On libcudacxx, the production `atomic_ref` is available in both host and device code; the specializations are expected to compile identically in both.

---

## Coordination with P2689R3

P2689R3 ("Atomic Refs Bound to Memory Orderings & Atomic Accessors") proposes `basic_atomic_accessor<T, MemOrder>` for mdspan. Its authors are natural collaborators: these specializations are what `basic_atomic_accessor` needs for read-only composition alongside the non-const original.

Two coordination options:

1. **Cross-reference.** This paper carries the `<atomic>` wording; P2689Rn cites it and assumes the six specializations are in place for its use of `atomic_ref` via `basic_atomic_accessor`. Neither paper duplicates the other.
2. **Merge.** The `<atomic>` additions fold into a later revision of P2689. Feasible if scope timing aligns; the six specializations are small enough to embed.

Option (1) minimizes committee-routing friction because this paper targets SG1 directly while P2689R3 targets LEWG. Authors of P2689R3 are invited to comment.

---

## Known gaps / TODO for R1

- **SG1 coordination.** Confirm the common-type choice (`atomic_ref` at the stricter const-qualification) and the coordination option (cross-reference vs. merge) with the authors of P2689R3 before R1.
- **Standardese wording.** R0 describes the design; normative wording (synopsis diff, `[atomics.ref.common_reference]` subclause, Mandates phrasing, feature-test macro) is deferred to a later revision.
- **Interaction with future `vector<bool>` / `pair` cross-const work.** Declared out of scope. If SG1 wants a uniform framing across proxy types, it belongs in a separate paper rather than an R1 expansion here.

---

## References

- **P3323R1** — *cv-qualified types in atomic and atomic_ref*. Added `atomic_ref<const T>` in C++26. Verified on 2026-04-23 against the published text: the paper modifies `atomic_ref`'s class template constraints but does not add any `basic_common_reference` specialization. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3323r1.html>
- **P3860R1** — *`atomic_ref<T>` is not convertible to `atomic_ref<const T>`*. Adds the converting constructor; prerequisite for `atomic_ref<const T>` being reachable from `atomic_ref<T>`. Verified: also does not add a `basic_common_reference` specialization. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3860r1.html>
- **P2689R3** — *Atomic Refs Bound to Memory Orderings & Atomic Accessors*. Natural collaborator. <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2689r3.html>
- **DNNNN** — *Customization Point for a const view of `std::mdspan`*. Motivating consumer of the specializations proposed here. (Companion paper, same author, same submission cycle.)
- **`[atomics.ref.generic]`**, **`[atomics.ref.operations]`**, **`[meta.trans.other]`**, **`[support.limits.general]`**, **`[atomics.syn]`** — C++ working draft.

---

## Acknowledgements

To be completed in R1 after review (SG1 reviewers, P2689R3 coordinators, CCCL contributors).

---

*This paper is split from the companion customization-point paper (DNNNN, 2026-04-18 draft). The design notes underlying the split live in the CCCL repository at `docs/superpowers/papers/2026-04-18-mdspan-const-accessor-cp-draft.md`.*
