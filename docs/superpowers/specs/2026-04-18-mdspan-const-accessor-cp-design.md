# Customization point for producing a const version of an mdspan accessor

**Status**: Draft — design sections complete, pending user review.
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
`atomic_ref` (P3323), and the follow-up conversion fix (P3860). A custom
mdspan accessor whose `reference` type is `atomic_ref<T>` has no way today
to participate in a read-only view.

Separately, cross-const `common_reference_with` fails for proxy references
(see godbolt tests with `atomic_ref<T>` vs `const T&`, and
`vector<bool>::reference` vs `const bool&`). This is **out of scope** for
this paper; a companion paper should address it.

---

## The whole proposal in three sentences

1. Add a **trait** to the standard library: `std::const_accessor_for<A>`.
   It answers the question "what is the const counterpart of accessor `A`?"
   with a type.
2. Let accessor authors **optionally** add one line
   (`using const_accessor_type = …;`) to tell that trait what to use.
3. Overload **`std::as_const`** (from `<utility>`) for mdspan so it uses
   the trait to produce a read-only mdspan over the same data. The overload
   is declared in `<mdspan>`, keeping mdspan independent of `std::ranges`.

The trait and the `std::as_const` overload are the only required additions
to the standard.
Everything accessor authors or end users write is optional — those are just
the three ways to plug into the trait.

---

## Who writes what — required vs optional

| Piece | Who writes it | Required? |
|---|---|---|
| `std::const_accessor_for<A>` trait | Standard library (one-time addition) | **Yes** |
| `std::as_const(mdspan)` overload (declared in `<mdspan>`) | Standard library (one-time addition) | **Yes** |
| `A::const_accessor_type` nested alias | Accessor author | Optional — a helper hook |
| Specialization of `const_accessor_for` in `namespace std` | User / third party | Optional — for accessors you can't modify |

---

## How the trait decides, in plain English

When `std::as_const(md)` asks `const_accessor_for<A>` for an answer:

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
auto readonly = std::as_const(writable);   // mdspan<const double, Ext>
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
auto readonly = std::as_const(writable);
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
auto readonly = std::as_const(writable);   // works
```

---

## Why "hybrid"

Each of the three plug-in points exists so a **different person** can opt in:

- The **accessor author** uses the nested alias (clean, declarative, lives
  with the type).
- The **end user** uses the specialization (when the accessor author hasn't
  opted in and they can't edit the code).
- The **standard library** uses substitution as a convenient default for
  simple template-based accessors like `default_accessor`, so nobody has to
  write anything for the common case.

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

---

## Accessing the const view — `std::as_const(md)`

**The plain story**: overload the existing `std::as_const` (from
`<utility>`) for mdspan, with the overload declared in `<mdspan>`. When
called with an mdspan, it returns a new mdspan whose element type is
`const T` and whose accessor is `const_accessor_for_t<A>`, viewing the
same data.

**mdspan stays independent of `std::ranges` and `std::views`.** mdspan is
not a range — no iterators, doesn't model `view` or `range` concepts — and
the design deliberately avoids coupling it to the ranges library. The
freestanding `std::as_const` from `<utility>` is the simpler, namespace-
clean precedent.

**Signature** (conceptual):

```cpp
namespace std {
  // Existing in <utility>:
  template<class T> constexpr add_const_t<T>& as_const(T& t) noexcept;
  template<class T> void as_const(const T&&) = delete;

  // New overload, declared in <mdspan>:
  template<class T, class E, class L, class A>
    requires /* const_accessor_for_t<A> is valid */
  constexpr mdspan<add_const_t<T>, E, L, const_accessor_for_t<A>>
  as_const(const mdspan<T, E, L, A>& md);
}
```

**Body** (roughly):

```cpp
template<class T, class E, class L, class A>
constexpr auto as_const(const mdspan<T, E, L, A>& md) {
  using C      = const_accessor_for_t<A>;
  using Result = mdspan<add_const_t<T>, E, L, C>;
  return Result{
    typename C::data_handle_type{ md.data_handle() },   // const handle
    md.mapping(),                                        // layout unchanged
    C{ md.accessor() }                                   // const accessor
  };
}
```

**Why not a new member function?** `std::as_const` is already a freestanding
function in the standard. Introducing `mdspan::as_const()` as a member
would invent a new mechanism when the existing name fits the intent ("make
this read-only"). Reusing the name keeps the user-facing surface consistent
with the rest of the standard.

**Note on overload semantics.** The existing `std::as_const(T&)` returns
`add_const_t<T>&` (shallow — makes the *reference* const). The new overload
for mdspan returns a **new value of a different type** —
`mdspan<const T, …, const_accessor_for_t<A>>` (deep — changes *element*
access). Same name, two semantics. This is honest overloading: the user
intent is the same ("make this read-only"); the mechanism differs because
mdspan's "read-only" is encoded in its element type, not in the const-ness
of the mdspan object. Overload resolution dispatches unambiguously on the
argument type. The partial-ordering and the `const T&&`-deleted overload
should naturally yield the mdspan overload for mdspan arguments; the
precise wording is a spec detail for WG21.

**Why not add a converting constructor to mdspan?** mdspan's existing
converting-constructor template already covers this case whenever the
underlying accessor provides an implicit `A → const_accessor_for_t<A>`
conversion. For example, `default_accessor<T> → default_accessor<const T>`
is already implicit in C++26, so
```cpp
mdspan<const double, E> ct = mt;   // compiles today for default_accessor
```
just works. `std::as_const(md)` is the explicit opt-in that also works
when the implicit path isn't available (custom accessors, third-party
specializations).

**Constraint**: the overload is removed from overload resolution when
`const_accessor_for_t<A>` is ill-formed; calls produce a normal
no-viable-overload error.

---

## Explicit opt-in for standard-provided accessors

Even though the trait's substitution fallback (Section 1, step 2) would
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

`submdspan` and `std::as_const` compose in either order to produce the
same mdspan type. Users can freely write either
`submdspan(std::as_const(md))` or `std::as_const(submdspan(md))`.

**Why it works**: `submdspan` switches the accessor to its
`offset_policy`. For the two compositions to agree, we need

```
const_accessor_for_t<A>::offset_policy == const_accessor_for_t<A::offset_policy>
```

which is exactly Section 1's "`C::offset_policy` is const-preserving"
requirement. No new wording is needed at the `submdspan` spec site — the
constraint lives with the accessor requirements upstream.

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
- `mdspan.h` — add the `cuda::std::as_const(mdspan)` overload.
- **New**: `const_accessor_for.h` — the trait with
  member-then-substitution resolution; included from
  `cuda/std/mdspan`.

**Namespace**: libcudacxx lives in `cuda::std::`, so the overload is
`cuda::std::as_const(md)` and the trait is
`cuda::std::const_accessor_for<A>`.

**Test coverage** (under the existing mdspan test area):

- Scenario 1 — default_accessor: type-check `cuda::std::as_const(md)`.
- Scenario 2 — custom accessor with `const_accessor_type` member opt-in.
- Scenario 3 — trait specialization for a "can't-modify" accessor.
- Composition: `submdspan(as_const(md))` and `as_const(submdspan(md))`
  yield the same type.
- Constrained-out behavior: accessor with no valid const counterpart
  produces a clean compile error at the call site.

**ABI impact**: none. Nested type aliases have no storage/vtable
impact; the trait and overload are header-only additions.

**Out of scope for the prototype**:

- Cross-const `common_reference_with` specializations (companion paper).
- Generalized `element_cast<T>` (separate design question).

---

## Open questions / out-of-scope

- **Cross-const `common_reference_with`**: proxy references
  (`atomic_ref<T>` vs `const T&`, `vector<bool>::reference` vs
  `const bool&`) lack the off-diagonal `basic_common_reference`
  specializations. Addressing this is the companion paper's job.
- **Accessors with no usable const version**: stateful proxies with
  side-effectful `reference::operator*` may not have a meaningful
  "read-only" counterpart. They opt out by providing no
  `const_accessor_type` and no trait specialization. `std::as_const(md)`
  is then ill-formed for them — a compile error, not a silent wrong
  result.
- **Naming — `element_cast<T>`**: a more general facility would handle
  cv-qualification *and* element-type conversion (e.g., `float` → `const
  double`). Deliberately deferred; the narrow const-only CP is
  tractable on its own and leaves room for a future generalization
  paper.
