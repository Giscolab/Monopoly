# Repository guardrails

## Immutable legacy source

`Source/` is the original retail Monopoly source corpus and is a read-only reference.

- Never edit, delete, rename, reformat, regenerate, or add files under `Source/`.
- All porting and modernization work belongs under `modern/` or supporting repository tooling outside `Source/`.
- Before committing or pushing, repository hooks verify that the complete Git tree for `Source/` still matches the locked retail baseline.
- CI verifies the same tree hash independently on every relevant run.
- If a task appears to require changing `Source/`, stop and implement the adaptation in the modern code instead.
