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

1. A **customization point object** `std::const_view` in `<mdspan>`. Called with an mdspan, it returns an mdspan whose element type is `const T` over the same data. Called with an accessor, it returns the accessor's const counterpart. Users customize it for their own accessors via an ADL `tag_invoke` hook; consumers of uncooperative third-party accessors have a small escape-hatch trait `std::const_view_override`.
2. Library-provided handling for `std::default_accessor` and `std::aligned_accessor` through direct overloads inside the CPO. No new members are added to these accessors.
3. `basic_common_reference` specializations in `<atomic>` for cross-const pairs of `std::atomic_ref`, so the resulting mdspan can be used with standard algorithms that require `common_reference_with` across reference types.

No new named cast function is proposed. mdspan's existing converting constructor already covers the common case where an accessor has an implicit `A → A_const` conversion (for example, `default_accessor<T>` in C++26); `std::const_view` is the customization surface for every other accessor.

(3) coordinates with P2689R3 ("Atomic Refs Bound to Memory Orderings & Atomic Accessors"); we expect to merge or cross-reference rather than duplicate.

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

For an accessor `A` with `element_type = T`, the **const version** `C` (produced by `std::const_view(A{})`) satisfies:

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

### How users obtain a const view

This paper does **not** propose a new named cast function with an explicit target-type argument. Two existing mechanisms together cover the needs:

**1. mdspan's converting constructor.** mdspan's constructor template already accepts another mdspan whose template arguments are pairwise compatible. When an accessor type `A` has an implicit `A → A_const` conversion — for example, `default_accessor<T>` has an implicit conversion to `default_accessor<const T>` in C++26 — the following compiles today with no library change:

```cpp
mdspan<double, E> md = /* ... */;
mdspan<const double, E> ro = md;   // no cast, no CPO, no ceremony
```

This is the preferred mechanism when the target type is convenient to write.

**2. `std::const_view` CPO.** When the user either cannot conveniently spell the target type (for custom accessors with nontrivial template parameters) or wants to make the const-ification explicit at the call site, the CPO is the right tool. The target type is deduced:

```cpp
mdspan<double, E, L, my_accessor<double>> md = /* ... */;
auto ro = std::const_view(md);
// decltype(ro) is mdspan<const double, E, L, my_accessor<const double>>
```

The same CPO also works on an accessor directly (`std::const_view(md.accessor())`); the mdspan overload calls into the accessor overload internally.

Neither mechanism is a named cast in the style of `std::bit_cast<T>`. mdspan's design already treats type-changing conversions as a first-class operation through converting constructors, and the CPO supplies the customization surface without needing a separate cast facility.

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

#### The gap in the current standard

In C++26, `std::atomic_ref<T>` ships with a `basic_common_reference` specialization that lets it interoperate with `T&`:

```cpp
common_reference_with<atomic_ref<T>, T&>       // true
common_reference_with<atomic_ref<const T>, const T&>   // true
```

But no specialization exists for the **cross-const** pairs. The following concepts all evaluate to `false` in C++26:

```cpp
common_reference_with<atomic_ref<T>,       const T&>       // false (gap)
common_reference_with<atomic_ref<const T>, T&>             // false (gap)
common_reference_with<atomic_ref<T>,       atomic_ref<const T>>   // false (gap)
```

The consequence: any facility that requires a common reference across the proxy-reference type and a reference to a (possibly const-qualified) `T` breaks at the concept check. `indirectly_readable`, `indirectly_writable`, `indirectly_copyable`, and the range adaptors that interoperate iterators with differing const-qualification all fall into this bucket.

This is exactly the gap that blocks using `std::const_view(md)` on an `atomic_ref`-backed mdspan alongside the non-const original in any standard algorithm that asks these questions.

#### The proposed specializations

Sketched (final wording in [atomics.ref.*]). The three specializations needed are:

```cpp
// atomic_ref<T> ⇄ const T&
template <class T, template<class> class TQ, template<class> class UQ>
struct basic_common_reference<atomic_ref<T>, T, TQ, UQ>
{
  using type = /* see below */;
};

// atomic_ref<const T> ⇄ T&
template <class T, template<class> class TQ, template<class> class UQ>
struct basic_common_reference<atomic_ref<const T>, T, TQ, UQ>
{
  using type = /* see below */;
};

// atomic_ref<T> ⇄ atomic_ref<const T>
template <class T, template<class> class TQ, template<class> class UQ>
struct basic_common_reference<atomic_ref<T>, atomic_ref<const T>, TQ, UQ>
{
  using type = /* see below */;
};
```

Plus the mirror-image specializations for each pair (since `basic_common_reference` is not symmetric at the specialization level).

#### What the `type` should be

The final wording for each `type` is a design question to settle during LEWG wording review. Two reasonable choices:

1. **`atomic_ref<const T>`**, preserving the proxy nature of the common reference. This matches the existing diagonal (`atomic_ref<T>` ⇄ `T&` resolves to `atomic_ref<T>`): the proxy type is canonical when either operand is a proxy.
2. **`const T&`**, collapsing to the plain const reference. Simpler for algorithms that prefer to reason about reference types directly.

Either choice closes the concept-level gap (both sides are convertible to the common type). The choice affects which operations remain most ergonomic downstream; the paper's R1 wording will pick one after consulting with the P2689R3 authors and SG1.

A proof-of-design for this gap-closing property is included in the prototype (see *Implementation experience*). Using a minimal `fake_atomic_ref<T>` that mimics the C++26 shape, the prototype demonstrates that the six specializations above (three pairs + mirrors) make all cross-const `common_reference_with` checks succeed.

#### Scope notes

- **`std::vector<bool>::reference`** has analogous cross-const gaps but is **not** used as an mdspan accessor reference in practice. Fixing it raises independent questions about the design of `std::vector<bool>` and is deferred to a separate effort.
- **Other proxy references** that may arise in user code (GPU accessors, distributed-memory references, custom atomics) are the accessor author's responsibility to specialize correctly. The pattern above is the template to follow.

#### Why `<atomic>`, not a separate proxy-reference utility library

One alternative considered was to define a generic "proxy-reference cross-const" utility once and apply it per-type. Rejected because the specializations are naturally type-specific: the `type` chosen for `atomic_ref` pairs may differ from what's right for `vector<bool>::reference` pairs, and forcing uniformity would overfit both. Keeping the specializations co-located with the proxy type in `<atomic>` is consistent with how P3323R1 placed the existing same-const specialization.

---

## Design rationale and considered alternatives

### Why not overload `std::as_const`

An earlier draft considered adding an overload of `std::as_const(md)` in `<mdspan>`. This was rejected: `std::as_const<T>(T&)` from `<utility>` is **shallow** (returns `const T&`), while an mdspan version would be **deep** (returns a new mdspan value with a const element type). Reusing the name under one namespace would give readers two different semantics under one identifier. A dedicated `std::const_view` CPO with its own name makes the mechanism explicit and avoids the collision.

### Why `std::`, not `std::ranges::` / `std::views::`

`std::views::*` and `std::ranges::` are the ranges library. `std::mdspan` is not a range — no iterators, no `view` concept modeling in the ranges sense — and placing mdspan's const-view facility in the ranges namespace would couple two libraries that otherwise have no dependency. `std::const_view` lives in `<mdspan>`, in `std::`, to keep responsibility boundaries clean. This also avoids the name collision discussed under *Naming* below.

### Why a CPO, not a named cast function

Early drafts proposed a named cast function template (`std::element_cast<T>(md)`). Two problems drove the shift to a CPO:

- **mdspan's idiom is conversion, not named casts.** mdspan's primary type-changing mechanism is its converting constructor, which kicks in implicitly when the accessor types convert; adding a named cast on top duplicates machinery the library already has. A CPO is an additive customization surface, not a replacement conversion facility.
- **A named cast needs an explicit target-type argument.** `std::element_cast<const T>(md)` forces the user to spell out the element type at the call site. For common cases that's fine; for custom accessors with elaborate template parameters it's awkward. The CPO deduces the target and avoids that burden.

### Why freestanding, not a member

A member function `mdspan::const_view()` was considered. Rejected for three reasons: named casts in the standard are freestanding (`bit_cast`, `const_cast`, `static_cast`, `reinterpret_cast`); range adaptors are freestanding; and the same CPO must work on both `mdspan` *and* bare accessors (via ADL), which a member function on `mdspan` cannot do. See *Why a CPO, not a member function* below for the full argument.

### Why a CPO, not a member function

The CPO dispatches on argument type:

- `std::const_view(md)` handles the mdspan case.
- `std::const_view(acc)` handles a bare accessor.

A member `mdspan::const_view()` can only handle the first. An accessor-only CPO plus an mdspan member would duplicate the customization surface and force authors of custom accessors to reason about two customization paths instead of one. One freestanding CPO is the single source of truth.

### The converting constructor is the primary mechanism

mdspan's existing converting-constructor template handles the common case where the underlying accessor provides an implicit `A → A_const` conversion. `default_accessor<T>` has such an implicit conversion in C++26, so

```cpp
mdspan<const double, E> ct = mt;
```

already compiles today for the default accessor with no library change. The CPO `std::const_view` is the additive surface for cases the converting constructor doesn't cover — accessors without an implicit conversion, third-party accessors, or code where the user doesn't want to spell out the target type. The CPO does not replace the converting constructor; it fills the gap.

### Why bundle `basic_common_reference` here (vs. split paper)

Splitting was considered and rejected. The motivating example (`atomic_ref<const T>` in mdspan, used alongside the non-const original) only works end-to-end with both changes landed. Shipping the mdspan customization point alone leaves users with a syntactically correct mdspan that cannot be used in standard algorithms that exercise `common_reference_with` across reference types. The bundled approach keeps users from hitting that pothole.

### Why some accessors deliberately have no const version

Not every accessor should have a const counterpart. Some accessors wrap APIs whose semantics are intrinsically read-and-write:

- An accessor backed by a file opened read-write: reading and writing go through the same descriptor; there is no read-only mode to switch to.
- An accessor over a network endpoint whose each access may change the destination or carry request metadata; "const" doesn't have a meaningful analog.
- An accessor maintaining a cache or connection pool where every access has side effects not reducible to pure reads.

For these accessors, "the const counterpart" is not merely inconvenient to define — it has no natural meaning. The correct design choice by the author is to **not opt in**: they provide no ADL `tag_invoke` hook, and no one specializes `const_view_override` on their behalf. `std::const_view(md)` on such an accessor is ill-formed, and that is the right answer.

### Why no universal fall-back

`std::ranges::views::as_const` handles its corresponding problem with a mix of specific cases (e.g., `span<T, Ext>` → `span<const T, Ext>`) and a general-purpose `as_const_view<V>` wrapper that works for any input view by wrapping iterators. See [range.as.const].

An analogous universal fall-back for mdspan — something like "for any accessor without an opt-in, synthesize a wrapping accessor that delegates to the original but converts its `reference` type to const" — was considered and rejected. Two reasons:

**1. It would silently defeat the "no const version" choice.** An accessor author who deliberately did not opt in would find their accessor silently const-ified anyway through the fall-back. That erases the distinction between "I haven't gotten around to adding const support" and "a const version of this accessor has no meaningful semantics"; the library would assume the former by default.

**2. The wrapping would be semantically wrong for side-effectful accessors.** A wrapping "const-ify" adapter that re-dispatches reads through the original accessor still executes the original accessor's access — side effects and all. For a file-backed or network-backed accessor, that means the wrapper still opens files or sends requests; "const" would be a lie about what the code does.

The chosen design — no universal fall-back — respects the author's explicit non-participation. The diagnostic is at the call site, clear, and the author retains the option to add an opt-in later if the const-version semantics do make sense for their accessor.

### Naming the operation — `const_view`, and why not the alternatives

We considered five candidate names and chose **`std::const_view`**. The rejected alternatives and the specific concern for each:

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

A libcudacxx prototype of the CPO-based design is implemented on the CCCL fork (branch `feature/mdspan-const-accessor-cp-design`). It provides:

- `cuda::std::const_view` as a customization point object declared in `<cuda/std/mdspan>`, with direct overloads for `cuda::std::default_accessor` and `cuda::std::aligned_accessor`.
- Author-side customization via ADL `tag_invoke` hooks; consumer-side escape hatch via `cuda::std::const_view_override`.
- No nested alias on the standard-library accessors.
- No template-substitution fallback.

The prototype validates that:

- `std::const_view(md)` composes cleanly with `std::submdspan`: the two operations commute type-wise for `default_accessor` and `aligned_accessor`.
- `std::const_view(md)` is O(1) — no element copy, no allocation — and the result's `data_handle()` is the same address as the input's.
- An accessor with neither an ADL hook nor a `const_view_override` specialization produces a clean "no matching call" diagnostic at the call site (not a deep metaprogramming cascade).
- A separate design-demonstration test exercises the proposed `basic_common_reference` specializations against a minimal proxy type that mimics `atomic_ref<T>` / `atomic_ref<const T>`, showing that the specializations close the cross-const `common_reference_with` gap. The production `basic_common_reference` specializations belong in `<atomic>` rather than the prototype's `<mdspan>` scope.

The prototype's tests are organized as `.pass.cpp` / `.fail.cpp` under `libcudacxx/test/libcudacxx/std/containers/views/mdspan/const_view/`. All positive tests compile and run; both negative tests (`no_opt_in.fail.cpp`, `readonly.fail.cpp`) correctly fail to compile as intended.

---

## Wording

*(Wording is placeholder in R0. Final revision will include concrete diffs against the latest working draft for §[mdspan], §[mdspan.syn], and §[atomics.ref.generic] / §[atomics.ref.operations].)*

### Approximate wording changes

1. **§[mdspan.syn]**: add the `const_view` customization point object and the `const_view_override` primary-template escape-hatch trait to the `<mdspan>` synopsis.
2. **§[mdspan.const_view]** (new subclause): define `const_view` as a customization point object. Specify:
    - the `mdspan` overload (semantic contract: returns an `mdspan<add_const_t<T>, E, L, C>` where `C` is the result of applying `const_view` to the input accessor; `data_handle()` and `mapping()` are preserved);
    - the accessor overload dispatch (ADL `tag_invoke` hook → `const_view_override<A>::type` → library built-in);
    - the library built-in overloads for `default_accessor<T>` → `default_accessor<const T>` and `aligned_accessor<T, N>` → `aligned_accessor<const T, N>`;
    - the diagnostic-quality requirement (no-matching-call diagnostic at the call site).
3. **§[atomics.ref.*]**: add `basic_common_reference` specializations for the cross-const `atomic_ref` pairs.

No changes to `[mdspan.accessor.default]` or `[mdspan.accessor.aligned]` are proposed; no new members are added to the standard accessors.

Concrete wording to follow in R1 after LEWG/SG1 design feedback.

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
