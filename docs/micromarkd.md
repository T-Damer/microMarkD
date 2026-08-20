# microMarkD

microMarkD is a touch-first Markdown vault application for the Xteink X4 Pro.
It is built as a separate firmware flavor on top of CrossPoint's X4 Pro
hardware support rather than as a feature of the focused reader firmware.

## Current application surface

The current bootstrap exposes:

- **Vault** — a Markdown-only browser rooted at `/vault`;
- **Recent notes** — persistent reader history filtered to existing vault notes;
- **Search** — bounded, incremental full-text search over the vault;
- **Tags** — an indexed tag browser backed by disposable metadata records;
- **New note** — title entry followed by the line-oriented Markdown editor;
- **Git sync** — reserved product surface; synchronization is not implemented yet.

Default CrossPoint environments do not define `MICROMARKD_APP` and keep their
existing home screen and behavior.

## Vault, reading, and navigation

The Vault browser navigates nested directories without allowing navigation
outside `/vault`. Tapping a note opens it. A long press exposes Open, Edit,
Backlinks, and Delete.

`.md` and `.markdown` files use the existing streaming TXT reader with a
microMarkD Markdown mode. Markdown is parsed before wrapping and visible
fragments are measured with their actual font style and block indentation.
The current renderer supports headings, quotes, bullets, ordered lists,
separators, simple inline emphasis markers, and Obsidian-style wikilinks.

`[[wikilinks]]` and `[[target|labels]]` are underlined and tappable. Direct
links use the same precedence as the current reader: beside the current note,
then from the vault root. A leading slash explicitly addresses the vault root.
The reader keeps an in-session note history so Back returns through followed
wikilinks before leaving the reader.

Heading fragments are supported:

```markdown
[[#Local heading]]
[[Projects/Plan#Risks]]
[[Projects/Plan#Risks|Open risks]]
```

The target heading's source byte offset is mapped to the measured Markdown page
index, so the reader opens the page containing that heading. Alias-aware
resolution is implemented by the metadata catalog and backlinks layer; normal
reader clicks still use direct path/basename resolution and do not yet fall back
to frontmatter aliases.

## Pagination and memory model

Markdown uses a separate measured page index rather than the TXT reader's raw
line index. The reader processes bounded sequential windows from the SD card
and never loads an entire note into RAM. Each indexed page stores:

1. the byte offset of the source line in the Markdown file;
2. the byte offset inside that line's parsed visible text.

This allows a heading, list item, quote, or wikilink label to continue on the
next page without being reparsed as a new Markdown block. Link ranges are
sliced together with visible text, so wrapped wikilink fragments remain
tappable.

The disposable `markdown-index.bin` reading cache is validated against the note
size, viewport width, lines per page, font, margin, and paragraph alignment. A
source line longer than the 8 KiB reading window is treated as sequential
fragments; Markdown constructs spanning that boundary are not preserved.

## Editor, save, and recovery model

The source editor keeps one bounded note in memory as individual UTF-8 lines.
The current limits are 128 KiB per note, 1024 lines, and 1024 bytes per editable
line. Exceeding a limit fails closed without modifying the canonical file.

New-note cancellation creates no file. A save uses a completion-marked
replacement sequence instead of truncating the canonical note in place:

1. write and close `<note>.tmp`;
2. write `<note>.tmp.ready` with a fixed completion marker;
3. rename the previous canonical note to `<note>.bak`, when it exists;
4. rename the complete temporary file to the canonical `.md` path;
5. restore the backup if promotion fails;
6. remove stale sidecars and the reading cache after success;
7. rebuild the disposable metadata index for the saved bytes.

Startup recovery recursively scans ordinary directories under `/vault` with a
bounded directory count. A canonical note wins over stale sidecars; a temporary
file is promoted only when its completion marker is valid; otherwise a backup
is restored when available. Incomplete temporary files without a canonical or
backup are discarded. Recovery invalidates metadata records whenever the
canonical content may have changed.

## Disposable metadata index

microMarkD stores per-note metadata outside the vault:

```text
/.micromarkd/index/<hash-of-vault-path>.midx
```

The versioned record contains:

- canonical vault path;
- source size and streaming fingerprint;
- frontmatter aliases;
- frontmatter and inline tags;
- headings with exact source byte offsets;
- outgoing wikilinks and optional heading fragments;
- a truncation flag when bounded metadata collections overflow.

Corrupt, stale, incorrectly hashed, out-of-vault, or unsupported-version index
records are treated as disposable and rejected. Save, delete, and recovery are
wired to the metadata lifecycle.

Existing Obsidian vaults copied directly to the SD card do not need to be
resaved. `MarkdownVaultIndexer` recursively enumerates Markdown notes and builds
missing records incrementally. It reads at most a few KiB per UI loop,
reuses valid existing records, skips files over its configured size bound, and
caps the number of directories and notes. Physical source lines over 8 KiB are
skipped for metadata parsing and mark the record partial, while later lines
continue to be indexed.

## Aliases, tags, and backlinks

`MarkdownCatalog` builds a bounded in-memory view from index records. It
normalizes ASCII/common-Cyrillic case and whitespace, aggregates tags, and
resolves wikilink targets using:

1. current-folder explicit path;
2. vault-root explicit path;
3. unique basename or frontmatter alias.

Ambiguous aliases are never guessed. Paths containing traversal outside the
vault are rejected.

The Tags screen first incrementally indexes/reuses the current vault, then shows
each indexed tag and note count. Opening a tag lists matching notes; notes can
be opened or edited.

The Backlinks screen uses two bounded phases: first it builds/reuses the vault
metadata catalog, then it scans indexed outgoing links in small batches.
Backlinks therefore understand unique aliases as well as explicit paths without
loading all note bodies into memory at once. Results are marked partial when
configured limits, truncated metadata, or filesystem/index failures prevent a
complete answer.

## Search and recent notes

Recent notes reuse CrossPoint's persistent reader history but expose only
existing `.md`/`.markdown` paths inside `/vault`.

Search recursively enumerates the vault and reads note content incrementally.
It checks title, relative folder, and full text, carries a UTF-8-safe overlap
between read chunks, creates bounded snippets, and stops at explicit limits for
files, directories, results, bytes per note, and total scanned bytes. Search
results can be opened or edited. Alias/tag/heading metadata is not yet used as
a search ranking signal.

## Build

This branch is stacked on the X4 Pro support branch from CrossPoint PR #2983.

```sh
pio run -e micromarkd-x4pro
```

Expected SD-card layout:

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

Only the index directory is active today; graph and sync locations reserve
stable cache/state boundaries for later milestones.

## Simulator status

A full-firmware integration experiment with the external CrossPoint simulator
was intentionally removed from the active branch. The external simulator is
useful, but its native Arduino/ESP-IDF/FreeInk shims currently lag the CrossPoint
X4 Pro base used by microMarkD. Carrying those compatibility patches in this PR
would couple the app to unrelated Wi-Fi, power, OTA, and platform APIs.

A future simulator target should be a thin, microMarkD-focused desktop target
(or should consume an upstream simulator integration once that contract is
stable). Real X4 Pro validation remains required for touch behavior, e-paper
refresh characteristics, SD timing, PSRAM pressure, and power-loss behavior.

## Next milestones

1. Add alias fallback to normal reader wikilink clicks and metadata-aware search
   ranking.
2. Add note rename/move and folder creation with safe cache/index invalidation.
3. Add zoom-dependent, tiled graph navigation using the indexed link graph.
4. Implement constrained Git synchronization: one remote, one branch, shallow
   fetch, commit, pull/rebase, and push.
5. Add a stable X4 Pro/microMarkD simulator target and deterministic touch tests.

## Design constraints

- keep notes as ordinary UTF-8 Markdown files compatible with desktop Obsidian;
- keep caches disposable and outside the vault;
- route SD access through `HalStorage`;
- bound scans, indexes, collections, and per-loop work;
- avoid animated graph layouts and continuous e-ink repainting;
- keep Git credentials and interrupted-write recovery explicit;
- preserve recovery and crash-report paths from the base firmware.
