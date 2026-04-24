# libcudacxx proposal and implementation agent pack

A starter pack of focused agent prompts for Claude Code to help with:

- ISO C++ proposal scoping
- paper drafting
- wording review
- design review
- libcudacxx implementation planning
- conformance test planning
- patch review

## Recommended workflow

For proposal work:

1. `proposal-scoper.md`
2. `paper-drafter.md`
3. `design-reviewer.md`
4. `wording-reviewer.md`

For implementation work:

1. `libcudacxx-implementer.md`
2. `conformance-test-writer.md`
3. `patch-reviewer.md`

## Conservative first-run setup

Use these in read/analyze/draft mode first.

- Let `proposal-scoper`, `paper-drafter`, `design-reviewer`, and `wording-reviewer` read files and draft text.
- Let `libcudacxx-implementer` propose a patch plan and optional diff sketches, but not auto-edit files unless you explicitly ask.
- Let `conformance-test-writer` draft tests and test matrices.
- Let `patch-reviewer` review diffs only.

## Common output contract

Ask each agent to respond using this shape:

```text
Summary
Assumptions
Findings
Risks
Open questions
Recommended next agent
```

For implementation-oriented work, also ask for:

```text
Touched areas
Patch plan
Tests needed
Compatibility risks
```

## Suggested Claude Code usage pattern

Example:

```text
Use proposal-scoper on this feature idea. Read the notes below and produce a proposal brief.
```

```text
Use wording-reviewer on this draft wording. Identify ambiguity, missing constraints, and likely LWG objections.
```

```text
Use libcudacxx-implementer on this accepted design. Inspect the repo and produce a staged implementation plan for libcudacxx.
```

## Shared guidance for all agents

Every agent in this pack should:

- distinguish standardization issues from libcudacxx implementation issues
- explicitly discuss host-only, device-only, and host+device applicability when relevant
- note compiler and toolchain assumptions when they matter
- call out divergence risk from upstream libc++ when relevant
- prefer reviewable, incremental work over big rewrites
- be explicit about uncertainty instead of guessing

## Files

- `proposal-scoper.md`
- `paper-drafter.md`
- `design-reviewer.md`
- `wording-reviewer.md`
- `libcudacxx-implementer.md`
- `conformance-test-writer.md`
- `patch-reviewer.md`

