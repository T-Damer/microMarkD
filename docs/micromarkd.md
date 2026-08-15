# microMarkD

microMarkD is a touch-first Markdown vault application for the Xteink X4 Pro.
It is built as a separate firmware flavor on top of CrossPoint's X4 Pro
hardware support rather than as a feature of the focused reader firmware.

## Current bootstrap

The first vertical slice intentionally stays small:

- a dedicated `micromarkd-x4pro` PlatformIO environment;
- a standalone microMarkD home activity;
- five product entry points: Vault, Recent notes, Search, New note, and Git sync;
- the Vault entry opens `/vault` through the existing SD-card browser;
- the remaining entries expose the planned product surface and are wired in
  follow-up changes.

Default CrossPoint environments do not define `MICROMARKD_APP` and keep their
existing home screen and behavior.

## Build

This branch is stacked on the X4 Pro support branch from CrossPoint PR #2983.

```sh
pio run -e micromarkd-x4pro
```

The expected SD-card layout starts with:

```text
/vault/
  Inbox.md
  Projects/
  Attachments/

/.micromarkd/
  index/
  graph/
  sync/
```

Only `/vault` is part of the first bootstrap. The cache layout is documented now
so later indexing, graph, and sync code can share stable boundaries.

## Planned milestones

1. Vault index and Markdown reader with `[[wikilinks]]`.
2. Source editor and atomic note writes.
3. Backlinks, tags, and full-text search.
4. Zoom-dependent tiled graph navigation.
5. Constrained Git proof of concept: one remote, one branch, shallow fetch,
   commit, pull/rebase, and push.
6. X4 Pro simulator profile and deterministic touch tests.

## Design constraints

- keep notes as ordinary UTF-8 Markdown files compatible with desktop Obsidian;
- keep caches disposable and outside the vault;
- route SD access through `HalStorage`;
- avoid animated graph layouts and continuous e-ink repainting;
- keep Git credentials and interrupted-write recovery explicit;
- preserve recovery and crash-report paths from the base firmware.
