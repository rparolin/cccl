# Customization point for producing a const version of an mdspan accessor

**Status**: Draft — scope revised after reverse-course discussion. Primitive is `std::element_cast<T>` (not `std::as_const`); includes cross-const `basic_common_reference` fixes for `atomic_ref` (B1 scope).
**Author**: Rob Parolin (rparolin@nvidia.com)
**Date**: 2026-04-18
**Scope**: WG21 paper + libcudacxx prototype (in parallel)

---

## Problem

`std::mdspan<T, Extents, Layout, Accessor>` has no standard way to produce
`mdspan<const T, Extents, Layout, ConstAccessor>` over the same data.

For the standard `default_accessor<T>`, users can manually spell
`default_accessor<const T>` and rely on pointer conversion — but there is no
general mechanism:

- No way for a custom accessor to declare its read-only counterpart.
- No way for a user to obtain a read-only counterpart of a third-party
  accessor they do not control.
- No mechanical rule that works for proxy references (e.g.,
  `atomic_ref<T>` → `atomic_ref<const T>`).

This gap is motivated directly by C++26's addition of cv qualifiers to
`atomic_ref` (P3323R1), and the follow-up conversion fix (P3860R1). A custom
mdspan accessor whose `reference` type is `atomic_ref<T>` has no way today
to participate in a read-only view.

Separately, cross-const `common_reference_with` fails for proxy references.
Both `common_reference_with<atomic_ref<T>, const T&>` and
`common_reference_with<vector<bool>::reference, const bool&>` fail today,
along with the mirrored pairs. **This paper closes that gap for
`atomic_ref`** (see the dedicated section below). Without it, an
mdspan-over-proxy-reference cannot be used in standard algorithms that
check `common_reference_with` across the two reference types.

---

## The whole proposal in three sentences

1. Add a **trait** to the standard library: `std::const_accessor_for<A>`.
   It answers the question "what is the const counterpart of accessor `A`?"
   with a type.
2. Let accessor authors **optionally** add one line
   (`using const_accessor_type = …;`) to tell that trait what to use.
3. Add **`std::element_cast<T>`** — a new freestanding function template
   in `<mdspan>` that returns an mdspan whose `element_type` is T (over
   the same data). When T is `add_const_t<element_type>`, it uses the
   trait to produce the read-only counterpart accessor. Declared in
   `<mdspan>`, keeping mdspan independent of `std::ranges`.

The trait and the `std::element_cast<T>` function template are the only
required additions to the standard.
Everything accessor authors or end users write is optional — those are just
the three ways to plug into the trait.

---

## What "const version of an accessor" means

For an accessor `A` with `element_type = T`, the **const version** `C` of
`A` (produced by `const_accessor_for<A>`) satisfies:

1. **`C::element_type` is `add_const_t<T>`.**
2. **`C::reference` is not writable** — you cannot assign through it.
3. **Reads may have side effects.** `C` follows the convention of C++
   const member functions — logging, caching, I/O through
   `reference::operator*` are all permitted.
4. **Same elements.** An mdspan with accessor `C` views the same elements
   as the original mdspan with accessor `A` (consequence of `copyable`
   plus equality-preserving `access` / `offset`).
5. **Read stability is not guaranteed** unless the accessor documents it
   — analogous to how a const member function can return different values
   on successive calls.
6. **`C::offset_policy` is const-preserving**, so composition with
   `submdspan` is well-defined in either order.

These are the full requirements. The detailed wording in *Semantic
contract* (below) builds on this definition.

---

## Who writes what — required vs optional

| Piece | Who writes it | Required? |
|---|---|---|
| `std::const_accessor_for<A>` trait | Standard library (one-time addition) | **Yes** |
| `std::element_cast<T>(mdspan)` function template (declared in `<mdspan>`) | Standard library (one-time addition) | **Yes** |
| `A::const_accessor_type` nested alias | Accessor author | Optional — a helper hook |
| Specialization of `const_accessor_for` in `namespace std` | User / third party | Optional — for accessors you can't modify |

---

## How the trait decides, in plain English

When `std::element_cast<const T>(md)` asks `const_accessor_for<A>` for an answer:

1. **Did the accessor author write a `const_accessor_type` alias on `A`?**
   Use that.
2. **Otherwise, is `A` a class template like `Foo<T>`?** Try `Foo<const T>`
   and use that if it compiles.
3. **Otherwise, did anyone write a `const_accessor_for<A>` specialization?**
   Use that.
4. **None of the above?** Hard error at compile time — this accessor can't
   be const-ified.

(Steps 1 and 3 are the same mechanism in practice — a specialization just
*is* step 1 for a type you don't own.)

---

## Three scenarios, end to end

### Scenario 1 — `default_accessor<double>` (author wrote nothing new)

Trait pattern-matches `default_accessor<T>`, tries `default_accessor<const T>`,
that compiles. Done.

```cpp
mdspan<double, Ext> writable{ptr, …};
auto readonly = std::element_cast<const double>(writable);   // mdspan<const double, Ext>
```

No changes to the accessor. Works for free.

### Scenario 2 — custom `atomic_accessor<T>` (author adds one line)

```cpp
template<class T>
struct atomic_accessor {
  using element_type        = T;
  using reference           = atomic_ref<T>;
  using data_handle_type    = T*;
  using offset_policy       = atomic_accessor;
  using const_accessor_type = atomic_accessor<const T>;  //  <-- the one new line

  reference access(T* p, size_t i) const { return atomic_ref<T>{p[i]}; }
  T* offset(T* p, size_t i) const { return p + i; }
};

mdspan<double, Ext, Lay, atomic_accessor<double>> writable{ptr, …};
auto readonly = std::element_cast<const double>(writable);
// → mdspan<const double, Ext, Lay, atomic_accessor<const double>>
```

### Scenario 3 — `vendor::gpu_accessor<T>` from a library you can't modify

User writes, outside the vendor's code:

```cpp
namespace std {
  template<class T>
  struct const_accessor_for<vendor::gpu_accessor<T>> {
    using type = vendor::gpu_accessor<const T>;
  };
}

mdspan<double, Ext, Lay, vendor::gpu_accessor<double>> writable{…};
auto readonly = std::element_cast<const double>(writable);   // works
```

---

## Why three plug-in points, not one

The three resolution paths — member hook, trait specialization, and
substitution fallback — exist because they serve **three different
audiences**, and **no single path alone covers the cases the other two
handle**.

### The two explicit paths, and why both are needed

Two of the paths let someone *explicitly* tell the trait what the const
counterpart is. They are not interchangeable; each applies when the other
cannot.

**Path 1 — `A::const_accessor_type` member.** For the accessor's
**author** — someone writing `atomic_accessor<T>` in their own library.
They add one line inside the class. Clean, declarative, lives with the
type; no need to reach into `namespace std`.

```cpp
template<class T>
struct atomic_accessor {
  // ...existing members...
  using const_accessor_type = atomic_accessor<const T>;   // the one line
};
```

**Path 2 — `std::const_accessor_for<A>` specialization.** For an
accessor's **user** — someone consuming `vendor::gpu_accessor<T>` that
they cannot edit. Since they don't own the type, they can't add a member
to it; they specialize the trait from outside instead.

```cpp
namespace std {
  template<class T>
  struct const_accessor_for<vendor::gpu_accessor<T>> {
    using type = vendor::gpu_accessor<const T>;
  };
}
```

These two audiences are genuinely distinct. Dropping Path 1 would force
accessor authors to specialize a `std::` template from outside their
class (awkward, and unlike every other accessor customization point).
Dropping Path 2 would leave users of third-party accessors with no
recourse when the vendor hasn't opted in.

### The third path: substitution fallback

For the **standard library / common case** — template accessors like
`default_accessor<T>` where `Tmpl<add_const_t<T>, Rest...>` is the
obvious, mechanical answer. Nobody has to write anything. It keeps
the common case trivial while the two explicit paths handle the rest.

### Precedent for member hooks

mdspan already uses member-typed aliases for accessor customization
(`element_type`, `reference`, `data_handle_type`, `offset_policy`).
Adding `const_accessor_type` as another member follows that pattern
rather than inventing a new convention.

---

## Semantic contract on `C = const_accessor_for_t<A>`

(Requirements the spec imposes on the *outcome*, not on the trait machinery.)

- `C` satisfies the existing accessor concept.
- `C::element_type` is `add_const_t<typename A::element_type>`.
- `C::data_handle_type` is constructible from `A::data_handle_type`.
- `C` is constructible from `A`.
- `C::offset_policy` is const-preserving — i.e.,
  `const_accessor_for_t<typename C::offset_policy>` denotes
  `typename C::offset_policy` — so submdspan stays const under composition.
- **Same-data guarantee**: `C::access(h₂, i)` observes the same element
  identity as `A::access(h₁, i)` whenever `h₂` was constructed from `h₁`.

**Idempotence**: `const_accessor_for_t<const_accessor_for_t<A>>` denotes the
same type as `const_accessor_for_t<A>`. Falls out of the "element_type is
add_const_t<…>" rule.

**Same-elements invariant.** Because accessors and mdspan both model
`copyable` and mdspan copies view the same elements, the mdspan returned
by `std::element_cast<T>(md)` views the same elements as `md` (see
*Same-data guarantee* above). `element_cast` is O(1): no element copy,
no allocation — it constructs a new mdspan from the input's handle,
mapping, and accessor.

**"Const" follows the convention for const member functions in C++.** The
const counterpart's `reference` is non-writable — you cannot assign to or
through it. Side effects during reads (I/O, logging, lazy caching,
mutable-state updates in the accessor) are allowed, analogous to what a
const member function is permitted to do (a const method may log, touch
`mutable` members, or call non-const services; it just cannot modify the
object's non-`mutable` data). Read stability is not a guarantee from the
standard; accessors that require stable reads should document that
guarantee themselves. No new UB or erroneous-behavior rules are
introduced — this is `const` as C++ already defines it.

---

## Accessing the const view — `std::element_cast<T>(md)`

**The plain story**: add a new freestanding function template
`std::element_cast<T>` in `<mdspan>`. Called with an mdspan, it returns
a new mdspan whose `element_type` is T (over the same data). T is
restricted to a cv-qualification change of the input's `element_type` —
no element-type conversion in this paper.

**Allowed target types**: for an input mdspan with `element_type = U`,
the target T must be either:

- **`U`** — identity cast (returns an mdspan of the same type, same data).
- **`add_const_t<U>`** — the const-add cast. This is the primary use
  case motivating this paper.

Other choices (e.g., `double` when `U == float`, or `volatile U`) are
ill-formed in this revision; see the *Future direction* note below.

**Signature** (conceptual):

```cpp
namespace std {
  template<class T, class E, class L, class A>
    requires (is_same_v<T, typename A::element_type>
              || is_same_v<T, add_const_t<typename A::element_type>>)
  constexpr mdspan<T, E, L, /* see below */>
  element_cast(const mdspan<typename A::element_type, E, L, A>& md);
}
```

Where the result accessor type is:

- `A` when `T == element_type` (identity cast).
- `const_accessor_for_t<A>` when `T == add_const_t<element_type>`
  (const-add cast).

**Body** (const-add case):

```cpp
template<class T, class E, class L, class A>
constexpr auto element_cast(const mdspan<typename A::element_type, E, L, A>& md)
  requires (is_same_v<T, add_const_t<typename A::element_type>>)
{
  using C      = const_accessor_for_t<A>;
  using Result = mdspan<T, E, L, C>;
  return Result{
    typename C::data_handle_type{ md.data_handle() },   // const handle
    md.mapping(),                                        // layout unchanged
    C{ md.accessor() }                                   // const accessor
  };
}
```

**Why a new name, not `std::as_const`**: see *Considered alternatives*
below. Short version: `std::as_const<T>(T&)` from `<utility>` is shallow;
an mdspan overload would be deep. Same name, two different semantics.
A dedicated `element_cast<T>` makes the element-type change explicit at
the call site and avoids the collision.

**Why not `std::views::element_cast`**: mdspan is not a range. Keeping
mdspan independent of `std::ranges` / `std::views` preserves clean
responsibility boundaries between library facilities.

**Why freestanding, not a member function**: every other named cast in
the standard (`std::bit_cast<T>`, `std::const_cast<T>`,
`std::static_cast<T>`, `std::reinterpret_cast<T>`) is freestanding. A
member `mdspan::cast<T>()` would be the odd one out and make composition
with other freestanding operations (e.g., `submdspan`) less uniform.

**Why not add a converting constructor to mdspan**: mdspan's existing
converting-constructor template already covers cases where the
underlying accessor provides an implicit `A → const_accessor_for_t<A>`
conversion. For example, `default_accessor<T> → default_accessor<const T>`
is already implicit in C++26, so
```cpp
mdspan<const double, E> ct = mt;   // compiles today for default_accessor
```
just works. `std::element_cast<T>` is the explicit opt-in that also works
when the implicit path isn't available (custom accessors, third-party
trait specializations).

**Constraint**: the function template is removed from overload resolution
when T is not cv-reachable from `element_type` (i.e., not equal to
`element_type` or its const-added form), or when
`const_accessor_for_t<A>` is ill-formed for the const-add case.
Ill-formed calls produce a normal no-viable-overload error.

**Future direction** (out of scope for this paper): a broader
`element_cast<T>` that also permits element-type conversion (e.g.,
`float → double`) or `volatile`. Each raises its own design questions
(alignment, compute-vs-view, layout compatibility, volatile-access
semantics) and deserves a separate paper. The restriction here leaves
the door open — the name already supports the broader interpretation.

---

## Explicit opt-in for standard-provided accessors

Even though the trait's substitution fallback (step 2 of *How the trait decides*) would
produce the correct answer for them automatically, the standard's own
`default_accessor` and `aligned_accessor` carry the nested alias
explicitly. Reasons: self-documenting in the standard text; removes any
doubt for readers; robust to future changes in substitution rules.

```cpp
template<class ElementType>
struct default_accessor {
  using offset_policy       = default_accessor;
  using element_type        = ElementType;
  using reference           = ElementType&;
  using data_handle_type    = ElementType*;
  using const_accessor_type = default_accessor<add_const_t<ElementType>>;  // NEW

  // access(...), offset(...) members unchanged
};

template<class ElementType, size_t ByteAlignment>
struct aligned_accessor {
  // ...existing offset_policy, element_type, reference, data_handle_type...
  using const_accessor_type =
    aligned_accessor<add_const_t<ElementType>, ByteAlignment>;   // NEW
  // access(...), offset(...) unchanged
};
```

The substitution fallback remains useful for user-defined template
accessors that don't opt in. Adding the member to the standard's own
accessors just ensures they never depend on the fallback path.

---

## Composition with `submdspan`

`submdspan` and `std::element_cast<const T>` compose in either order to
produce the same mdspan type. Users can freely write either
`submdspan(std::element_cast<const T>(md))` or
`std::element_cast<const T>(submdspan(md))`.

**Why it works**: `submdspan` switches the accessor to its
`offset_policy`. For the two compositions to agree, we need

```
const_accessor_for_t<A>::offset_policy == const_accessor_for_t<A::offset_policy>
```

which is exactly the *Semantic contract* section's "`C::offset_policy`
is const-preserving" requirement. No new wording is needed at the
`submdspan` spec site — the constraint lives with the accessor
requirements upstream.

**Standard-provided accessors satisfy this by construction**:

- `default_accessor<T>::offset_policy = default_accessor<T>` (itself).
  Const version is `default_accessor<const T>`, same pattern. Commutes.
- `aligned_accessor<T, N>::offset_policy = default_accessor<T>`
  (alignment is lost on offset). Const version is
  `aligned_accessor<const T, N>`, whose `offset_policy` is
  `default_accessor<const T>`, matching
  `const_accessor_for_t<default_accessor<T>>`. Commutes.

**For custom accessors**: authors who set `offset_policy = *this` and
pick the natural template-substitution const counterpart get
commutativity for free. Authors with unusual offset policies must ensure
both sides of the diagram above agree.

---

## libcudacxx prototype plan

**Files to touch** (`libcudacxx/include/cuda/std/__mdspan/`):

- `default_accessor.h` — add
  `const_accessor_type = default_accessor<add_const_t<ElementType>>`.
- `aligned_accessor.h` — add
  `const_accessor_type = aligned_accessor<add_const_t<ElementType>, ByteAlignment>`.
- `mdspan.h` — add the `cuda::std::element_cast<T>` function template.
- **New**: `const_accessor_for.h` — the trait with
  member-then-substitution resolution; included from
  `cuda/std/mdspan`.

**Namespace**: libcudacxx lives in `cuda::std::`, so the function is
`cuda::std::element_cast<T>(md)` and the trait is
`cuda::std::const_accessor_for<A>`.

**Test coverage** (under the existing mdspan test area):

- Scenario 1 — default_accessor: type-check
  `cuda::std::element_cast<const double>(md)`.
- Scenario 2 — custom accessor with `const_accessor_type` member opt-in.
- Scenario 3 — trait specialization for a "can't-modify" accessor.
- Composition: `submdspan(element_cast<const T>(md))` and
  `element_cast<const T>(submdspan(md))` yield the same type.
- Constrained-out behavior:
  - accessor with no valid const counterpart produces a clean compile
    error at the call site;
  - `element_cast<int>(mdspan<double>)` (not cv-reachable) is
    ill-formed.
- Identity case: `element_cast<T>(md)` where `T == element_type`
  returns the same mdspan type.

**ABI impact**: none. Nested type aliases have no storage/vtable
impact; the trait and function template are header-only additions.

**Out of scope for the prototype**:

- `basic_common_reference` specializations in `<atomic>` (they belong
  in the paper's wording but are outside the prototype scope — the
  prototype validates the mdspan-side customization point only).
- Broader `element_cast<T>` that handles element-type conversion
  (separate design question).

---

## Cross-const `basic_common_reference` specializations for `atomic_ref`

**The gap.** For proxy references like `atomic_ref<T>`, the test
`common_reference_with<atomic_ref<T>, const T&>` (and its mirror) fails
today — no `basic_common_reference` specialization covers it. The same
failure is visible for `common_reference_with<atomic_ref<T>,
atomic_ref<const T>>` (and its mirror).

Consequence: standard algorithms that reason about the two reference
types together — anything relying on `indirectly_readable`,
`indirectly_writable`, or range-adaptor pipelines that interoperate
const and non-const views — refuse to compile.

**What this paper proposes.** Add `basic_common_reference`
specializations in `<atomic>` covering the cross-const cases for
`atomic_ref`:

- `atomic_ref<T>` ⇄ `const T&`
- `atomic_ref<const T>` ⇄ `T&`
- `atomic_ref<T>` ⇄ `atomic_ref<const T>`

A handful of specializations, each a few lines. Exact wording to follow
the structure P3323R1 used for the existing `atomic_ref<T>` ⇄ `T&`
relationship.

**Scope note.** `vector<bool>::reference` has similar cross-const gaps
but is out of scope here — it is not used as an mdspan accessor's
`reference` in practice, and fixing it raises independent questions
about the design of `vector<bool>`.

**Coordination.** P2689R3 ("Atomic Refs Bound to Memory Orderings &
Atomic Accessors") is in flight and proposes `basic_atomic_accessor`
for use with mdspan. Its authors are the natural collaborators here —
we should coordinate (co-authorship, cross-referencing, or merging)
rather than propose in parallel.

---

## Open questions / out-of-scope

- **Accessors with no meaningful const version**: some accessors simply
  don't have a semantically sensible read-only counterpart — e.g., an
  accessor over a write-only device or a streaming source. They opt
  out by providing no `const_accessor_type` and no trait specialization;
  `std::element_cast<const T>(md)` is then ill-formed for them — a
  compile error, not a silent wrong result. (Note: accessors whose
  `reference::operator*` has side effects are **not** automatically in
  this category — see the semantic contract section; side effects are
  allowed in the const counterpart just as they are in a const member
  function.)
- **`vector<bool>::reference`**: same cross-const `common_reference_with`
  gap, but not used as an mdspan accessor reference and scoped out.
- **Broader `element_cast<T>`**: allowing element-type conversion
  (`float → double`) or `volatile`-qualification raises independent
  design questions (alignment, compute vs view, volatile access
  semantics). Deliberately deferred; the narrow cv-only primitive
  proposed here leaves room for generalization in a follow-up paper.

---

## Considered alternatives

### Overload `std::as_const` for mdspan

An earlier draft proposed adding an overload of `std::as_const` (from
`<utility>`) for mdspan, declared in `<mdspan>`:

```cpp
namespace std {
  template<class T, class E, class L, class A>
  constexpr mdspan<add_const_t<T>, E, L, const_accessor_for_t<A>>
  as_const(const mdspan<T, E, L, A>& md);
}
```

**Why rejected**: the name `std::as_const` would end up with two
meaningfully different semantics:

- `std::as_const<T>(T&)` (existing, `<utility>`) is **shallow** —
  returns `const T&`.
- The mdspan overload would be **deep** — returns a *new value of a
  different type*, with element access producing const.

A reader seeing `std::as_const(md)` would reasonably expect shallow
behavior and be surprised by the deep result. A dedicated
`std::element_cast<T>(md)` avoids the collision and makes the element-
type change explicit at the call site.

### Use `std::views::as_const` for mdspan

Also rejected. `std::views::as_const` lives in `std::ranges::views` and
is part of the ranges library. mdspan is not a range — no iterators, no
`view` / `range` concept modeling — and coupling it to `std::ranges`
would drag mdspan into a library it has no other relationship with.
Keeping mdspan independent of `std::ranges` preserves clean boundaries
between library facilities.

### Broader `element_cast<T>` with element-type conversion

A fully general `element_cast<T>(md)` that accepts any compatible T,
including conversions like `float → double`, was considered but scoped
out. Element-wise conversion in an mdspan view raises its own
questions — alignment, compute-vs-view semantics, layout
compatibility — and deserves a separate paper. The primitive proposed
here restricts T to cv-qualification changes of `element_type` only;
the name leaves the door open to a future generalization.

### Member function `mdspan::as_const()` or `mdspan::const_view()`

Rejected. Every named cast in the standard is freestanding
(`std::bit_cast<T>`, `std::const_cast<T>`, `std::static_cast<T>`,
`std::reinterpret_cast<T>`), as are range adaptors under `std::views::`.
A member would be the odd one out and would make composition with other
freestanding operations (e.g., `submdspan`) less uniform.

### Separate companion paper for `basic_common_reference`

Considered (see "B2" in brainstorming notes): keep this paper focused
on the mdspan customization point and defer the `atomic_ref` cross-const
`basic_common_reference` fixes to a separate paper. Rejected because the
motivating case (`atomic_ref<const T>` in mdspan, used alongside the
non-const original) only works end-to-end when both changes ship
together. Shipping the customization point alone leaves the motivating example
half-broken. Current scope bundles both (see cross-const section); the
bundle should still coordinate with P2689R3 authors to avoid parallel
proposals.

---

## References

- **P3323R1** — *cv-qualified types in atomic and atomic_ref*. Added
  `atomic_ref<const T>` in C++26; introduced the gap this paper
  addresses.
  <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3323r1.html>

- **P3860R1** — *Proposed Resolution for NB Comment GB13-309:
  `atomic_ref<T>` is not convertible to `atomic_ref<const T>`*. Adds
  the converting constructor; adjacent but does not address
  `common_reference_with`.
  <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3860r1.html>

- **P2689R3** — *Atomic Refs Bound to Memory Orderings & Atomic
  Accessors*. Proposes `basic_atomic_accessor` for use with mdspan;
  natural collaborator for the cross-const `basic_common_reference`
  specializations.
  <https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2689r3.html>

- **LWG 3811** — *`views::as_const` on `ref_view<T>` should return
  `ref_view<const T>`*. Adjacent `as_const` semantics issue for
  ranges; confirms the pattern of `as_const` adaptors needing
  per-case wiring.
  <https://cplusplus.github.io/LWG/issue3811>

- **LWG Active Issues**.
  <https://www.open-std.org/jtc1/sc22/wg21/docs/lwg-active.html>
