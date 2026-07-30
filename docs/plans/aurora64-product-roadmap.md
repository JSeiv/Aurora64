# Aurora64 Product Roadmap After Comparative Research

> **For Hermes:** Before implementing any phase below, write a phase-specific plan and execute it with subagent-driven development, specification review, quality/safety review, and the stated hardware gate.

**Goal:** Turn Aurora64 from a proven additive Home/static-library prototype into a reliable games-first Nintendo 64 library while preserving N64FlashcartMenu's launcher, save lifecycle, Browser, flashcart abstraction, and boot handoff.

**Architecture:** Aurora remains a presentation and library layer over N64FlashcartMenu. A versioned local index and metadata/artwork resolver feed Aurora views. Selection transfers one owned ROM path to the existing details screen; only that screen may enter the protected stock launch path.

**Target:** Original Nintendo 64 with 4 MiB Jumper Pak and SummerCart64. Expansion Pak enhancements may be added later but cannot become the baseline requirement.

**Research basis:** `docs/research/aurora64_comparative_menu_audit.md`

---

## 1. Current baseline

### Phase 0 — Upstream and hardware baseline: COMPLETE

Proven:

- pinned N64FlashcartMenu V0.3.2 baseline;
- Apple Silicon/x86_64 container build path;
- volatile SummerCart64 USB upload and byte readback;
- stock persistent SD menu recovery;
- architecture/protected-boundary audit.

### Phase 1 — Additive Aurora Home: COMPLETE

Proven:

- separately feature-gated Home view;
- six-category 3x2 presentation;
- Browser retained as Browse Files;
- Browser root-B return to Home;
- default/off build retains upstream Browser-first behavior;
- original 4 MiB hardware navigation.

### Phase 2 — Static All Games and explicit details seam: COMPLETE

Proven:

- six-game 3x2 static library;
- explicit owned ROM path into the stock details view;
- no manufactured Browser state;
- `.v64` and `.z64` details loading;
- Library -> Details -> B selection preservation;
- missing-path fail-closed behavior;
- 20 repeated details round trips on original 4 MiB hardware;
- protected cart-load/save/SC64/boot boundaries unchanged.

Not yet proven:

- actual game launch from Aurora;
- save creation, loading, or writeback;
- recursive library discovery;
- durable library identity/indexing;
- multi-cover artwork performance;
- external art redistribution rights.

## 2. Revised phase order

The research changes the old sequence in one important way: build identity and indexing before a rich cover interface. Do not jump from the static fixture directly to video, a large art pack, or a launcher rewrite.

```text
Phase 3: real library identity/index
    -> Phase 4: cover-first All Games
        -> Phase 5: personal library views
            -> Phase 6: metadata/schema polish
                -> Phase 7: optional media and screensaver
                    -> Phase 8: release hardening and branch promotion
```

The independent launch/save readiness gate can begin alongside these phases. A disposable/no-cart-save launch proof is a prerequisite for Phase 5 Recently Played behavior. Full save creation/writeback acceptance remains separate, and no product phase may claim save safety until that gate passes.

---

## 3. Phase 3 — Real library foundation

### User-visible outcome

`All Games` shows the real ROM collection instead of the six hard-coded fixtures. A text-first or placeholder-card presentation is acceptable in this phase; correctness and responsiveness matter more than artwork.

### Architectural outcome

Add a library subsystem separate from Browser and `rom_info_t`:

- canonical exact ROM path;
- stable identity/fingerprint;
- normalized original header summary;
- game code and region/revision fields;
- resolved display title;
- metadata/artwork lookup key;
- scan status and source timestamps/sizes needed for cache validation.

### Required design work before code

1. Define byte-order normalization for `.z64`, `.v64`, and `.n64` before fingerprinting.
2. Define stable identity, collision handling, and behavior for hacks/homebrew/prototypes.
3. Define the on-SD cache schema with magic, version, record count, lengths, and checksum/validation.
4. Define maximum record count, path length, title length, metadata string length, and total RAM budget.
5. Define cache update/recovery: write new, validate, replace; reject unsupported/corrupt versions and rescan.
6. Define structural validity separately from freshness: stale-while-revalidate behavior, root-configuration identity, per-record ROM and metadata dependency signatures, additions/removals/changes, overlapping-root deduplication, and mutations during scanning.
7. Define scan roots/exclusions without turning Browser paths into library identity.
8. Decide whether the first implementation scans a configured ROM root or a small list of roots; avoid an unbounded whole-card scan.
9. Freeze quantitative limits after the discovery spike: bytes/time per poll, maximum service gap, records, roots, depth, path/title/string-pool bytes, cache-write memory, startup latency, and minimum free-heap watermarks.

### Recommended modules

Exact names must be confirmed in the phase-specific implementation plan, but responsibilities should remain separate:

```text
src/menu/library/library.c/.h          records, ownership, lookup, paging
src/menu/library/library_scan.c/.h     bounded directory and header inspection
src/menu/library/library_cache.c/.h    versioned persistence and validation
src/menu/library/game_identity.c/.h    byte order and stable fingerprint
src/menu/library/title_resolver.c/.h   metadata/header/filename precedence
```

Do not put the complete library into `entry_t`, `menu_t`, or `static_library.c`.

### Execution policy

- Present Home immediately.
- Load a structurally valid cache as an immutable, possibly stale snapshot, then revalidate roots in the background.
- Give one menu-owned `library_service` an explicit `init/poll/pause/resume/cancel/restart/free` lifecycle. Poll only in documented safe modes; never perform scanner work in an incoming view's synchronous `init()`.
- Scan incrementally with hard byte/time/depth work budgets and cancellation points; never block input/USB/sound for an entire traversal or one unbounded file operation.
- Use a bounded header/fingerprint reader. Do not call full `rom_config_load()` during the baseline scan because sidecar/embedded ZIP and INI parsing is synchronous and not allocation-bounded.
- Copy only bounded summary fields. Enrich metadata on demand or only after strict file, compressed/uncompressed payload, string, and allocation limits are implemented.
- Publish a completed scan as a new immutable index generation. Views retain stable identities and owned path copies, never pointers into a snapshot that may be replaced or sorted.
- A pause request finishes only the current bounded work unit, drains callbacks, closes transient I/O, and reaches an observable `QUIESCED` state. Details transitions wait for that state. Returning to Library explicitly resumes the same unpublished builder generation.
- Cancellation also reaches `QUIESCED`, discards the unpublished builder, and retains the last immutable published snapshot. Restart creates a fresh builder/generation; partial cancelled data is never published. ROM/save staging, boot handoff, and global teardown require cancellation and quiescence, not merely a pause request.
- Do not decode artwork while scanning.
- Keep Browse Files available even if scan/cache fails.

### Acceptance gates

Host/static:

- deterministic identity fixtures for `.z64`, `.v64`, and `.n64` forms of the same bytes;
- collision and malformed-header tests;
- cache round-trip, truncated file, bad count, oversized length, wrong checksum/version, and interrupted-update tests;
- stale-cache/root-change tests for additions, removals, same-size replacement, metadata dependency changes, overlapping roots, and mutation during a scan;
- oversized/slow/failing reads, mid-read truncation, cancellation, allocation failure, and work-budget tests;
- bounded path/title handling and no raw 20-byte title used as a C string;
- scanner lifecycle and generation tests covering pause/resume, cancel/restart, view change, rescan swap, Details transition, callback/I/O quiescence, Boot quiescence, and teardown;
- protected-boundary diff is empty;
- default/off and Aurora-enabled build matrix passes.

Hardware on original 4 MiB:

- Home remains responsive while an uncached scan progresses;
- USB polling and controller input remain responsive;
- measured scan work per frame, maximum input/USB service gap, cached/uncached startup latency, and minimum free heap satisfy the numeric thresholds frozen after the spike;
- a structurally valid stale cache appears immediately and reconciles additions/removals/changes without dangling UI references;
- real ROM count and selected paths match the SD contents within configured roots;
- `.v64` and `.z64` duplicates/variants follow the documented identity policy;
- All Games -> Details shows the exact selected ROM;
- Details B returns to All Games with the same selection;
- a ROM removed after indexing shows Error without stale details or launch, then returns to the same All Games context when safe; Phase 3 must add a validated Error origin/context because Phase 2 intentionally returned Error B to Browser;
- repeated rescan/details cycles do not reduce the measured free-heap watermark over time.

Phase 4 is blocked until the Phase 3 evidence records numeric limits for free heap, work/service gaps, startup latency, record/root/depth/string bounds, cache-write peak memory, and explicit OOM behavior.

### Explicit deferrals

- decoded cover grid;
- video/audio;
- screensaver;
- persistent personal favorites/recent;
- common metadata schema changes;
- launcher/save/SC64 modifications.

---

## 4. Phase 4 — Cover-first All Games experience

### User-visible outcome

All Games becomes the first true Aurora library screen: a paged, games-first grid with clean titles, stable focus, fast D-pad navigation, graceful placeholder art, and selected-game information. It should feel closer to a console launcher than a file browser.

### Architectural outcome

Add an artwork resolver/cache over the Phase 3 index:

- preserve `/menu/metadata/<game-code>/` and legacy compatibility;
- resolve candidate paths centrally;
- allow region-aware and documented regionless fallback;
- serialize all PNG decode requests through the existing single-flight decoder;
- cancel stale selection requests;
- cap decoded surfaces and bytes;
- separate asset identity from component geometry;
- release off-page assets deterministically.

The current PNG decoder allocates at native dimensions and does not produce thumbnails. Phase 4 must choose an actual thumbnail path. The default recommendation is a host-side install/generation tool that writes versioned, validated N64-ready 120x90 thumbnails while retaining the original PNG for stock Details. Runtime downsampling is permitted only if its source-surface and working-memory peak passes the 4 MiB budget.

One global artwork arbiter must coordinate Library and stock Details. It owns generation tokens/callback lifetime, serializes decoder use, cancels and reaches idle before a Library -> Details transition, and defines retry/priority behavior so Details cannot receive `PNG_ERR_BUSY` and permanently lose art.

### Initial UI policy

- retain the proven 3x2 Aurora geometry unless hardware evidence favors 2x2;
- load visible art incrementally rather than all at transition time;
- prioritize selected card, then nearest visible neighbors;
- show deterministic placeholders immediately;
- keep text/title navigation usable with no art pack installed;
- do not make art availability a condition for launch/details.

### Initial memory experiment

Start with a hard decoded-art ceiling rather than an entry count alone. With host-preconverted thumbnails, one proposed experiment is:

- one 158x158 selected image;
- up to four 120x90 thumbnails;
- 136,328 bytes, about 133.1 KiB, of raw RGBA16 pixel data before decoder context, row buffer, surface, allocator, and retained-library overhead.

Instrument actual allocation cost and lower the budget if the 4 MiB free-heap gate requires it. Do not retain six full-size covers simply because six cards are visible.

### Acceptance gates

- zero-art, partial-art, full-art, corrupt-PNG, oversized-PNG, wrong-dimension, and rapid-selection tests;
- no simultaneous PNG decodes;
- no stale cover appears after rapid movement;
- Library cancellation reaches decoder-idle before Details; Details retries/prioritizes its request and never loses art solely because Library was decoding;
- preconverted thumbnail magic/version/dimensions/length and corruption tests, or equivalent bounded runtime-downsampling tests;
- missing art never blocks details/launch;
- long paging/selection stress on original 4 MiB hardware;
- measured peak and post-cycle heap watermarks recorded;
- Browser/details box art still works;
- scan/cache work does not compete with active decoding beyond defined budgets;
- protected launcher/save/SC64/boot diffs remain empty.

### Explicit deferrals

- video and preview audio;
- bundled giant art packs;
- animated backgrounds;
- full-screen screensaver;
- theme/plugin systems.

---

## 5. Phase 5 — Personal library views

**Prerequisite:** complete the approved disposable/no-cart-save launch proof in section 9. Full save-writeback acceptance may remain pending, but Recently Played and play-count behavior cannot be accepted from details-only navigation.

### User-visible outcome

Home destinations become useful:

- Favorites;
- Recently Played;
- Collections;
- optional sorting/filtering inside All Games.

### Architectural outcome

Create a small versioned personal state store keyed by stable Phase 3 identity, not mutable paths. Keep shared metadata and personal state separate.

Candidate local fields:

- favorite flag/order;
- last played timestamp when RTC is available;
- play count;
- user collections;
- per-view sort/filter preference;
- last selected identity/page.

### Persistence rules

- unknown future fields/versions fail safely;
- writes are recoverable and validated;
- moving a ROM reconciles through stable identity;
- missing games remain diagnosable and can be pruned explicitly;
- legacy eight-entry upstream History/Favorites continue to receive exact paths for compatibility until a deliberate migration decision.

### Acceptance gates

- add/remove/toggle persistence across power cycle;
- duplicate identity and moved-path behavior;
- interrupted/corrupt state-file recovery;
- RTC absent/invalid behavior;
- large collection paging and memory bounds;
- upstream History/Favorites and Browse Files remain functional;
- launched exact path is still recorded by the protected stock bookkeeping path.

---

## 6. Phase 6 — Metadata and discovery polish

### User-visible outcome

Richer details, useful filters, cleaner titles, and consistent regional/revision display without breaking existing metadata packs.

### Architectural outcome

Finish a central metadata resolver and generate compact cache fields from compatible shared metadata. The resolver must accept both audited dialects: the shared repository's `[metadata]`/underscore keys and upstream V0.3.2's `[meta]`/hyphenated keys. Keep large descriptions or rarely used fields on demand if retaining them all would exceed the RAM budget.

### Shared-schema policy

Good candidates for an upstream proposal:

- developer and publisher;
- genres/tags with a documented multi-value syntax;
- player count;
- release year only if needed beyond date;
- content-rating system/value;
- broad non-personal collections.

Aurora-local only:

- favorite state;
- play count/last played;
- personal collections;
- user rating;
- UI sort/filter state.

### Upstream contribution gate

Before adding shared fields:

1. reconcile and document the section/key dialect, exact semantics, and examples;
2. prove compatibility with sibling ZIP, sibling INI, global repository INI, and embedded metadata fixtures;
3. prove old parsers ignore optional additions harmlessly;
4. add validation tooling or schema checks;
5. submit broadly useful changes to `n64-tools/n64-flashcart-menu-metadata` rather than creating an Aurora-only descriptive database;
6. keep Aurora functional when none of the optional fields exist.

### Asset provenance gate

Do not bundle or redistribute the shared 1.85 GB PNG corpus until individual/source-level permissions are documented. Supporting user-installed compatible assets does not require bundling them.

---

## 7. Phase 7 — Optional media, screensaver, and localization

### User-visible outcome

Optional selected-game video/audio previews, a library screensaver, and localized Aurora strings. Every feature defaults off until it passes the original 4 MiB gate.

### Video/audio design requirements

Adapt Tavinho's useful policies:

- selection debounce;
- image/video fallback;
- host-side conversion;
- explicit start/stop/cancel ownership;
- no preview work during launch staging.

Do not adopt its container unchanged without adding:

- format version;
- validated dimensions, fps, frame count, and file length;
- checked multiplication/overflow;
- corruption fallback;
- explicit bandwidth and decoded-surface budgets;
- instrumentation for dropped frames/audio underruns;
- conservative baseline resolution/fps below the 320x240@30 maximum unless hardware evidence supports it.

### Screensaver design requirements

- explicit allowed origin modes;
- validated remembered return mode;
- idle input reset;
- preconverted assets;
- bounded image list and one active surface;
- no loss of library selection/context;
- no full-card enumeration every time it starts.

### Localization requirements

- stable message IDs or scoped string tables;
- no source-language string as the lookup key;
- no shared mutable translation buffer whose pointer can be retained by UI state;
- English baseline first, then additional language packs/tables;
- layout tests for longer labels.

### Acceptance gates

- preview off/default path is behaviorally and memory-equivalent to prior phase;
- video-only, audio-only, fallback, missing/corrupt media, rapid selection, and view-exit stress;
- simultaneous sound effects/BGM/preview policy documented;
- 4 MiB heap, SD bandwidth, USB responsiveness, and input latency measured;
- screensaver return preserves exact view and selection;
- no media operation crosses into cart-load/save/boot handoff.

---

## 8. Phase 8 — Release hardening and branch promotion

### User-visible outcome

A coherent Aurora64 release branch suitable to become the project's own `main`, while retaining `upstream` for selective incorporation from N64FlashcartMenu.

### Required work

- complete original 4 MiB regression matrix;
- repeat relevant checks with Expansion Pak as an enhancement matrix, not baseline;
- complete backed-up launch and save-writeback acceptance;
- document SD installation, asset installation, recovery, and stock-menu rollback;
- audit licenses/attribution for copied code and distributed assets;
- validate malformed/untrusted SD content limits;
- profile startup, scan, paging, art loading, details transitions, and free heap;
- update architecture and user documentation;
- prune obsolete Aurora feature flags only after migration compatibility is decided;
- deliberately promote reviewed Aurora history to project `main` without rewriting inherited history casually;
- retain `origin` as `JSeiv/Aurora64` and `upstream` as `Polprzewodnikowy/N64FlashcartMenu`.

### Release gates

- clean build/precommit/static gates;
- independent specification and quality reviews;
- byte-identical SC64 artifact readback for every executed release candidate;
- stock persistent menu recovery;
- no unbacked personal save risk;
- no unresolved high-severity memory/ownership failures;
- all third-party attribution and redistribution decisions documented.

---

## 9. Parallel launch/save readiness gate

This gate was intentionally not completed in Phase 2. It may be scheduled separately, but it must not be conflated with UI progress.

1. Build the exact approved source and byte-read it back after volatile SC64 upload.
2. Use normal standalone save-writeback behavior for save testing; `--no-writeback` is menu-only and cannot prove saves.
3. Inventory and back up every potentially affected save and Controller Pak.
4. Use a no-cart-save or disposable first launch proof.
5. Make a separate, explicit save-acceptance run using disposable/backed-up data.
6. Verify the expected save file path/type/size, in-game change, writeback timing, power cycle, and reload.
7. Stop on any save-type, path, size, writeback, firmware, or recovery uncertainty.
8. Never modify persistent `/sc64menu.n64` merely to perform a volatile launch test.

Until the first disposable/no-cart-save launch proof passes, wording must remain: “Aurora selection and details are hardware-proven; launch and save behavior from Aurora are not yet accepted.” After that proof but before save acceptance, wording must distinguish the results: “Aurora launch handoff is accepted; save creation/writeback is not yet accepted.”

## 10. Immediate next step

Write a Phase 3 implementation plan only. Do not implement artwork, media, personal state, or metadata extensions in that plan.

The Phase 3 plan should begin with a narrow discovery spike that answers:

1. What configured root or roots should be scanned on Josh's current SD layout?
2. What stable fingerprint can be computed incrementally with acceptable SD I/O?
3. How many ROMs and how much path/title data exist on the current card?
4. What free heap is available at Home, Library, and Details on the original 4 MiB system?
5. What cache update primitives are reliable on the mounted filesystem?
6. What host-side tests can exercise identity/cache corruption without hardware?
7. What byte/time budget per frame preserves a measured input/USB service-gap target?
8. What maximum roots, depth, records, pooled string bytes, and cache-write memory fit the measured heap?
9. How will structural cache validity, stale-while-revalidate freshness, and root/metadata changes be represented?
10. Where will the menu-owned service be initialized, polled, paused, cancelled, quiesced, and freed?

After those measurements, freeze the Phase 3 schema, lifecycle, failure behavior, and numeric budgets before writing the full scanner.
