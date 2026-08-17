# microMarkD

microMarkD is a touch-first Markdown vault application for the Xteink X4 Pro.
It is built as a separate firmware flavor on top of CrossPoint's X4 Pro
hardware support rather than as a feature of the focused reader firmware.

## Current bootstrap

The first usable vertical slice intentionally stays small:

- a dedicated `micromarkd-x4pro` PlatformIO environment;
- a standalone microMarkD home activity;
- five product entry points: Vault, Recent notes, Search, New note, and Git sync;
- the Vault entry opens a dedicated Markdown-only browser rooted at `/vault`;
- tapping a note opens it, while a long press offers Open, Edit, and Delete;
- the vault browser navigates nested folders but cannot escape the vault root;
- New note asks for a title, derives a FAT-safe unique `.md` path under
  `/vault`, and opens a line-oriented source editor before any file is created;
- the editor supports line replacement, insertion above or below, deletion,
  appending, explicit save, and a save/discard prompt for unsaved changes;
- interrupted saves are recovered once when the microMarkD home screen starts;
- `.md` and `.markdown` files open in the streaming text reader with Markdown
  headings, quotes, lists, separators, and simple inline markers rendered;
- Markdown is parsed before line wrapping, and visible fragments are measured
  with their actual font style and block indentation before pages are indexed;
- long Markdown blocks can continue across pages without losing their style,
  list indentation, or wikilink hit regions;
- `[[wikilinks]]` and `[[target|labels]]` are underlined and tappable;
- relative links resolve beside the current note, then from the vault root;
- the reader keeps an in-session note history so Back returns through followed
  wikilinks before leaving the reader;
- Recent notes, Search, and Git sync remain planned product surfaces.

Default CrossPoint environments do not define `MICROMARKD_APP` and keep their
existing home screen and behavior.

## Pagination and memory model

Markdown uses a separate measured page index rather than the TXT reader's raw
line index. The reader processes bounded sequential windows from the SD card,
parses source lines before wrapping, and never loads the entire note into RAM.
Each indexed page stores two cursors:

1. the byte offset of the source line in the Markdown file;
2. the byte offset inside that line's parsed, visible text.

This allows a heading, list item, quote, or wikilink label to continue on the
next page without reparsing an already wrapped fragment as new Markdown. Link
ranges are sliced together with the visible text, so every displayed fragment
of a wrapped wikilink remains tappable.

The disposable `markdown-index.bin` cache is validated against the note size,
viewport width, lines per page, font, margin, and paragraph alignment. A source
line longer than the 8 KiB streaming window is deliberately treated as
sequential fragments; Markdown constructs spanning that boundary are not
preserved in this bootstrap.

## Editor, save, and recovery model

The source editor keeps one bounded note in memory as individual UTF-8 lines.
The initial implementation accepts notes up to 128 KiB, 1024 lines, and 1024
bytes per line. Those limits keep the row previews and keyboard field bounded
on the device; exceeding one fails closed without modifying the note.

Saving never truncates the canonical note in place:

1. write, flush, and close `<note>.tmp`;
2. write and close `<note>.tmp.ready` containing a fixed completion marker;
3. rename the previous note to `<note>.bak`, when it exists;
4. rename the complete temporary file to the canonical `.md` path;
5. restore the backup if the final rename fails;
6. remove the completion marker, backup, and disposable reading cache after
   success.

The marker distinguishes a fully closed temporary file from a partial file left
by a power loss during a new-note write. Recovery scans ordinary, non-hidden
folders below `/vault` and follows these deterministic rules:

- a canonical note always wins; stale sidecars are removed;
- when the canonical note is absent, a temporary file is promoted only when its
  completion marker is valid;
- otherwise an available backup is restored;
- an incomplete temporary file without a canonical note or backup is discarded
  rather than exposed as a valid Markdown note;
- unreadable markers and failed filesystem operations are left for a later
  retry and reported in the Vault subtitle.

The startup scan is bounded to 512 directories so a corrupted or unexpectedly
large directory tree cannot grow memory use without limit. Explicit note
deletion removes all recovery sidecars first, preventing a deleted note from
being resurrected on the next boot.

FAT directory updates are not transactional, but this sequence prevents a
power loss during content writing from leaving the canonical note half-written.
New-note cancellation leaves no empty file behind.

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

1. Add heading-anchor navigation and aliases.
2. Vault-wide index, recent notes, and richer source navigation.
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
