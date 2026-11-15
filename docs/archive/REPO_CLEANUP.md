# Arcade-1 Repository Cleanup & Consolidation

## Purpose

Clean up and consolidate the Arcade-1 repository without changing the behavior or architecture of the Arcade-1 machine.

The repository has accumulated several development phases, experiments, milestone documents, and historical artifacts. The current project is substantially more mature than the organization of the repository suggests.

The goal is to make the repository clearly communicate the **current Arcade-1 architecture**, while preserving useful historical material in an explicit archive.

This is a **repository organization task**, not a machine rewrite.

---

# 1. Critical rules

## 1.1 Do not change machine behavior

Do not modify the implementation of the Arcade-1 machine merely for organizational reasons.

In particular, do not alter:

- CPU behavior
- memory map
- MMIO behavior
- video behavior
- audio behavior
- timing
- input behavior
- ROM format
- `.arc1` format
- guest ABI
- SDK semantics
- golden outputs
- regression expectations

If moving a file requires modifying an include path, build path, or script path, that is acceptable.

Changing functionality is not.

---

## 1.2 Preserve the regression system

The following are compatibility artifacts:

```text
tests/golden/
tests/roms/
```

Do not regenerate, rewrite, or "clean up" their contents unless a path change absolutely requires it.

After the repository restructuring:

```text
make check
```

must still pass.

Also run the most complete available test target before and after the restructuring.

---

## 1.3 Inspect before moving

Before making changes:

1. Inspect the complete repository tree.
2. Inspect `Makefile`.
3. Inspect `AGENTS.md`.
4. Inspect all root-level Markdown documents.
5. Inspect `arcforge/`.
6. Inspect `art/`.
7. Inspect `tools/`.
8. Inspect `programs/`.
9. Inspect `tests/`.
10. Search the repository for references to every file/directory that will be moved.

Do not blindly move files based only on their names.

---

# 2. Target conceptual architecture

The repository should communicate these major layers:

```text
                         ARCADE-1
                            │
          ┌─────────────────┼─────────────────┐
          │                 │                 │
       MACHINE             SDK             ARCFORGE
          │                 │                 │
       runtime          game API        asset authoring
          │                 │                 │
          └─────────────────┼─────────────────┘
                            │
                          GAMES
                            │
                     tests / tooling
```

The important distinction is:

### Arcade-1 machine

The actual console/runtime.

### SDK

The interface used to create guest programs/games.

### ArcForge

The asset authoring/generation system.

### Games

Actual Arcade-1 guest programs.

### Tools

Generic development utilities that are not themselves the ArcForge system.

---

# 3. ArcForge is a first-class subsystem

Do NOT bury ArcForge under `tools/`.

Do NOT treat ArcForge as merely an art helper.

Keep:

```text
arcforge/
```

as a first-class top-level subsystem.

ArcForge represents the deterministic asset-authoring pipeline:

```text
asset specification
        │
        ▼
     ArcForge
        │
        ├── archetypes
        ├── named recipes/features
        ├── deterministic construction
        ├── palette handling
        ├── animation
        ├── .px fragments
        ├── validation
        └── asset generation
        │
        ▼
   Arcade-1 assets
        │
        ▼
       game
```

The cleanup must therefore preserve and clarify ArcForge rather than hide it.

---

# 4. Desired repository structure

Do not force every item into this structure if the existing implementation makes a different arrangement clearly more correct.

The target direction is:

```text
arcade_1/
│
├── README.md
├── AGENTS.md
├── Makefile
├── LICENSE
│
├── src/
│   ├── arcade1/
│   └── libretro/
│
├── programs/
│   └── ...              # KEEP initially
│
├── arcforge/
│   └── ...              # FIRST-CLASS SUBSYSTEM
│
├── art/
│   └── ...              # source/art assets
│
├── tools/
│   └── ...              # generic development tools
│
├── tests/
│   ├── golden/
│   ├── roms/
│   └── ...
│
├── docs/
│   ├── SPEC.md
│   ├── ARCHITECTURE.md
│   ├── SDK.md
│   ├── CHEATSHEET.md
│   ├── GAME_DEV_GUIDE.md
│   ├── PROGRESS.md
│   ├── ROADMAP.md
│   │
│   ├── arcforge/
│   │   ├── ASSET_PIPELINE.md
│   │   └── ...
│   │
│   ├── experiments/
│   │   └── ...
│   │
│   └── archive/
│       └── ...
│
├── deps/
│
└── fpga/
    └── ...              # only if/when actually present
```

This is a **target architecture**, not a command to blindly create empty directories.

---

# 5. Root directory cleanup

The root should become intentionally boring.

Target root:

```text
README.md
AGENTS.md
Makefile
LICENSE
src/
programs/
arcforge/
art/
tools/
tests/
docs/
deps/
```

Potentially:

```text
fpga/
```

when FPGA implementation actually exists.

Avoid leaving historical documents and development rounds at the root.

The root should communicate:

> This is Arcade-1: the machine, SDK/game environment, ArcForge, tools, tests, and documentation.

---

# 6. Documentation classification

Classify every root-level Markdown file.

Use these categories:

```text
CURRENT
HISTORICAL
EXPERIMENTAL
REFERENCE
DELETE
```

Do not delete useful history simply because it is old.

Move historical material into `docs/archive/`.

Move experiments into `docs/experiments/`.

Only delete documents that are clearly obsolete, duplicated, misleading, or superseded.

---

# 7. Documents that should become current documentation

The following documents represent the current conceptual system and should be preserved:

```text
SPEC.md
ARCHITECTURE.md
SDK.md
CHEATSHEET.md
GAME_DEV_GUIDE.md
AI_NATIVE_DEVELOPMENT.md
AssetEcosystem.md
FUTURE_IDEAS.md
PROGRESS.md
```

Move them under `docs/` where appropriate.

Suggested destinations:

```text
docs/SPEC.md
docs/ARCHITECTURE.md
docs/SDK.md
docs/CHEATSHEET.md
docs/GAME_DEV_GUIDE.md
docs/AI_DEVELOPMENT.md
docs/FUTURE_IDEAS.md
docs/PROGRESS.md
```

For:

```text
AssetEcosystem.md
```

prefer:

```text
docs/arcforge/ASSET_PIPELINE.md
```

if its content is primarily about the ArcForge asset ecosystem.

Do not mechanically rename it if inspection shows that a different name is more accurate.

---

# 8. Roadmap consolidation

The repository currently contains several generations of roadmap/milestone documents.

Examples include:

```text
MILESTONES_1.md
MILESTONES_2.md
ROADMAP_V3.md
ROADMAP_V4.md
PROGRESS.md
FUTURE_IDEAS.md
```

Do not leave multiple competing documents appearing to be authoritative.

Create:

```text
docs/ROADMAP.md
```

as the **single current roadmap**.

It should answer:

> What is the project doing next?

It should NOT reproduce the entire historical roadmap.

Extract only the still-relevant future direction.

Then archive historical roadmap documents:

```text
docs/archive/
    MILESTONES_1.md
    MILESTONES_2.md
    ROADMAP_V3.md
    ROADMAP_V4.md
```

Use Git history as an additional historical record.

---

# 9. PROGRESS.md

`PROGRESS.md` should answer:

> Where is Arcade-1 now?

It should not function as a 900-line historical diary.

Condense it into something approximately like:

```text
# Arcade-1 Progress

## Current status

## Machine status

## SDK status

## ArcForge status

## Game status

## Test/regression status

## FPGA status

## Completed milestones

## Current blockers

## Next milestone
```

Keep important historical information in Git history or `docs/archive/`.

Do not invent project status.

Use the actual repository state.

---

# 10. Clearly obsolete documentation

Inspect:

```text
knowledge_base.md
```

If it contains obsolete commands, obsolete architecture, obsolete milestone state, obsolete infrastructure, or references to systems no longer present in the repository, delete it rather than preserving it as apparently-current documentation.

If there is genuinely valuable historical information inside it, extract only that information into an appropriately named historical document before deleting the misleading original.

The repository must not contain a document that gives an AI or developer a false description of the current system.

---

# 11. Superseded asset documentation

Inspect:

```text
ASSETS_PROPOSITION.md
```

If it explicitly represents an earlier proposal superseded by the current ArcForge/asset architecture:

Move it to:

```text
docs/archive/ASSETS_PROPOSITION.md
```

Do not keep it at root.

The current ArcForge architecture should be authoritative.

---

# 12. Historical milestone documents

Inspect and archive:

```text
BEFORE_M25.md
MILESTONES_1.md
MILESTONES_2.md
ROADMAP_V3.md
ROADMAP_V4.md
```

Suggested:

```text
docs/archive/
```

Do not delete unless they are completely duplicated and contain no historical value.

---

# 13. Experiments

Historical experiments must not look like current machine architecture.

Inspect:

```text
HD_EXPERIMENT.md
VOXEL_EXPERIMENT.md
experiments/hd/
round3/
round4/
round5/
round6/
round7/
```

Classify each.

Likely destination:

```text
docs/experiments/
archive/experiments/
archive/rounds/
```

depending on whether the content is primarily documentation or source material.

Do not move active game code into an archive merely because it originated as an experiment.

---

# 14. `shmup/`

Inspect the actual contents and references before deciding.

Determine whether it is:

1. an active Arcade-1 game,
2. an active game experiment,
3. a historical experiment,
4. obsolete code.

If it is an active game, preserve it as active project material.

If the repository's existing conventions permit it, its eventual destination may be:

```text
games/shmup/
```

However:

**Do not perform a `programs/ → games/` migration in the same cleanup unless the existing structure makes it trivial and safe.**

That is a separate architectural migration.

---

# 15. Do not prematurely split `programs/`

The current project may still rely heavily on:

```text
programs/
```

for:

- guest source
- SDK headers
- `crt0.S`
- `link.ld`
- game builds
- examples

Do not blindly transform it into:

```text
games/
sdk/
examples/
```

during this cleanup.

First make the repository clean without disturbing the existing build architecture.

A future migration may introduce:

```text
sdk/
games/
examples/
```

but that should be a deliberate change.

---

# 16. Makefile

Inspect the current `Makefile` before moving anything.

It is a major integration point.

Do not rewrite it merely to make it aesthetically smaller.

For every moved file/directory:

1. Find all Makefile references.
2. Update paths.
3. Search for hard-coded paths elsewhere.
4. Run the relevant targets.

A future Makefile modularization may introduce:

```text
mk/
    machine.mk
    games.mk
    tests.mk
    sdk.mk
    art.mk
```

but **do not perform that refactor unless necessary for this cleanup**.

The current task is repository consolidation, not build-system redesign.

---

# 17. ArcForge documentation structure

Inspect the entire current `arcforge/` tree.

Do not assume its internal organization.

Produce a concise ArcForge README if one does not already exist:

```text
arcforge/README.md
```

It should explain:

1. What ArcForge is.
2. What problem it solves.
3. The deterministic asset pipeline.
4. Input/spec format.
5. Archetypes.
6. Named recipes/features.
7. `.px` fallback fragments.
8. Generated outputs.
9. Validation.
10. How ArcForge relates to `art/`.
11. How ArcForge relates to games.
12. How an AI/developer should use it.

Do not invent capabilities that are not actually implemented.

Clearly distinguish:

```text
implemented
planned
experimental
```

---

# 18. `art/` vs `arcforge/`

Maintain this conceptual separation:

```text
arcforge/
    asset-generation system
    recipes
    archetypes
    generation code
    validators
    asset pipeline

art/
    source artwork
    palettes
    source/reference assets
    generated or curated art resources
```

If an existing file clearly belongs to ArcForge implementation, move it there.

If it is actual art/source data, keep it in `art/`.

If uncertain, inspect references before deciding.

---

# 19. Create a real root README

If `README.md` is missing or inadequate, create/update it.

It should be concise.

It should answer:

```text
What is Arcade-1?

What is the machine?

What is ArcForge?

How do I build it?

How do I run it?

How do I run tests?

How do I build a game?

Where is the specification?

Where is the SDK documentation?

Where is ArcForge documentation?

What is experimental?
```

It should link to the canonical documents.

It should not duplicate large portions of them.

---

# 20. Documentation authority

Establish the following hierarchy:

```text
SPEC.md
    ↓
machine contract

ARCHITECTURE.md
    ↓
why the machine is designed this way

SDK.md
    ↓
developer tooling / SDK

GAME_DEV_GUIDE.md
    ↓
how to create games

CHEATSHEET.md
    ↓
quick API reference

ArcForge documentation
    ↓
how assets are authored/generated

ROADMAP.md
    ↓
what comes next

PROGRESS.md
    ↓
where the project is now

docs/experiments/
    ↓
what was explored/learned

docs/archive/
    ↓
historical material
```

No archived document should appear to supersede the current specification.

---

# 21. Search for stale references

After all moves, search the entire repository for:

- old filenames
- old paths
- old directory names
- obsolete commands
- references to archived architecture
- references to deleted files

Examples:

```text
rg "ROADMAP_V4"
rg "ASSETS_PROPOSITION"
rg "knowledge_base"
rg "round7"
rg "round6"
rg "HD_EXPERIMENT"
rg "VOXEL_EXPERIMENT"
```

Use the actual filenames found in the repository.

Fix references that should now point to the current documents.

Historical documents may reference historical paths; do not unnecessarily rewrite history unless it creates confusion.

---

# 22. Git history

Use `git mv` for tracked moves where appropriate.

Do not squash history.

Do not rewrite Git history.

Do not delete old commits.

The goal is to make the current tree clean while retaining the project's history.

---

# 23. Validation

Before making changes, record:

```text
git status
git branch --show-current
git rev-parse HEAD
```

Run the existing test/build baseline.

After restructuring:

```text
git status
make
make check
```

and any additional test targets already documented by the project.

Also test:

- building the machine
- building the CLI
- building at least one guest/game
- running the relevant headless test
- ArcForge tests
- art tests if present
- regression/golden tests

Do not assume that `make` alone is sufficient.

Use the repository's actual documented validation commands.

---

# 24. Do not "fix" unrelated issues

If cleanup reveals unrelated problems:

```text
TODO
broken feature
incorrect game behavior
FPGA issue
asset-generation bug
performance issue
```

do not silently fix them as part of this task.

Record them separately.

Only fix something if the fix is required because a file was moved or renamed.

---

# 25. Final required report

At the end, produce a report containing:

## Changed

List every significant move/rename/deletion.

## Preserved

Confirm that:

- machine behavior was not intentionally changed
- golden artifacts were preserved
- ROM artifacts were preserved
- SDK behavior was not intentionally changed

## ArcForge

Describe the resulting ArcForge location and structure.

## Documentation

List:

- current documents
- archived documents
- deleted documents

## Tests

Report the exact commands run and their results.

## Remaining cleanup

List things deliberately NOT changed because they should be a separate task.

---

# 26. Important: stop if the repository differs substantially

This document is based on the repository's current conceptual structure.

If inspection reveals that the actual repository has changed substantially since this plan was written:

**do not force the proposed tree.**

Instead:

1. inspect the current state,
2. preserve the principles of this document,
3. adapt the concrete moves,
4. explain the deviations in the final report.

The principles are more important than exact filenames.

---

# 27. Definition of success

The cleanup is successful when a new developer—or an AI agent—can clone the repository and understand within a few minutes:

```text
Arcade-1
├── machine
├── SDK/games
├── ArcForge
├── tools
├── tests
└── documentation
```

and can immediately identify:

```text
What is authoritative?
What is current?
What is experimental?
What is historical?
How do I build?
How do I test?
How do I create a game?
How do I create assets?
```

The repository should no longer require understanding the project's entire development history in order to understand its current architecture.

---

# Final principle

**Clean the history around the machine, not the machine itself.**

Arcade-1's current architecture should become easier to see.

ArcForge should become more visible, not less.

Historical experiments should remain available, but should stop masquerading as current architecture.

The machine, SDK, tests, and deterministic behavior are the things to protect.