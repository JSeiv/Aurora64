# Aurora64 Comparative Menu Research and Architecture Decision

Status: design checkpoint complete; implementation has not begun from these findings
Aurora baseline: `JSeiv/Aurora64` commit `9513af2418d1918b4ee52983398ff8cc7ec20aa8`
Companion roadmap: `docs/plans/aurora64-product-roadmap.md`
Research questions: `Aurora64_Codex_Research_Guide.md`

## 1. Decision summary

Aurora64 should remain an additive product layer on N64FlashcartMenu, not a rewrite.

The recommended direction is:

1. Keep the existing initialization, input normalization, view dispatcher, Browser, ROM inspection, per-ROM options, cart loading, save lifecycle, flashcart abstraction, SC64 driver, and boot handoff.
2. Keep Aurora's existing Home view and the Phase 2 explicit Library -> Details path handoff.
3. Replace the six-game static fixture with a bounded, versioned library index generated from ROM headers and the existing metadata ecosystem.
4. Build a games-first cover interface on that index with one serialized artwork loader and a small, measurable cache.
5. Keep Browse Files as a first-class fallback and diagnostic path.
6. Add favorites, recent games, collections, filters, screensavers, and optional previews only after identity, indexing, navigation, and 4 MiB memory behavior are proven.
7. Do not rewrite or bypass the protected launch/save/boot path.

This direction can plausibly leave 80–90% of the inherited source unchanged. That percentage is an architectural target, not a measured LOC claim. Aurora will add library, metadata-resolution, artwork-cache, and presentation modules while preserving the proven core.

## 2. Reproducible source manifest

| Project | Exact source | Audited revision | Revision date | Relationship | License |
|---|---|---|---|---|---|
| Aurora64 baseline | `https://github.com/JSeiv/Aurora64` | `9513af2418d1918b4ee52983398ff8cc7ec20aa8` | local reviewed Phase 2 commit | GitHub fork of N64FlashcartMenu | inherited AGPL-3.0 |
| N64FlashcartMenu | `https://github.com/Polprzewodnikowy/N64FlashcartMenu` | `6407ab15f6c19d1bf9ded5104c1c8b3c36471379` | 2026-05-23 | primary upstream | AGPL-3.0 |
| Tavinho SummerCart64 Menu | `https://github.com/Guga-Tavinho/Tavinho-SummerCart64Menu` | `de9c1232e39c065337c56d9b5b033bdf1a5e55c1` | 2026-07-03 | separate AGPL repository derived from the same ecosystem | AGPL-3.0 |
| N64ever | `https://github.com/bjerreman/N64FlashcartMenu-N64ever` | `0bc801d2c100f4c141d93bda6cdf585b5ac4e950` | 2026-06-29 | fork of N64FlashcartMenu | AGPL-3.0 |
| Shared metadata | `https://github.com/n64-tools/n64-flashcart-menu-metadata` | `1f278e49af6e6238f3b1f9b5864c89738e2de041` | 2026-04-17 | independent data repository | Unlicense declaration at repository root |

Repository metadata, source trees, build/asset tools, license files, and exact implementation paths were inspected. README statements are not treated as proof when source evidence differs or is absent.

No external repository was modified. No Aurora source code, SD content, persistent `/sc64menu.n64`, or hardware state was changed during this audit.

## 3. N64FlashcartMenu: direct answers

### How is the application structured?

It is a single-process C application centered on one heap-allocated `menu_t` object:

- `src/main.c`: creates `boot_params_t`, runs `menu_run()`, then invokes the protected boot handoff.
- `src/menu/menu.c`: global initialization, view registry, frame loop, transitions, polling, and teardown.
- `src/menu/menu_state.h`: current/next mode, input actions, browser state, settings, bookkeeping, active load state, and boot parameters.
- `src/menu/views/*.c`: individual screens.
- `src/menu/rom_info.c`: ROM header inspection, game database lookup, metadata, configuration, and launch-relevant settings.
- `src/menu/cart_load.c`: generic ROM/save staging policy.
- `src/flashcart/`: flashcart-neutral API and hardware drivers.
- `src/boot/`: final register and execution handoff.

The frame loop acquires a display surface, updates normalized actions, processes/draws the active view, applies state transitions, polls sound and the single-flight PNG decoder, and services USB communication.

### Is the browser simply another view?

Yes. Browser is `MENU_MODE_BROWSER`, registered like the other views in `menu_views[]` in `src/menu/menu.c`. It owns persistent directory/list/selection state inside `menu_t`, but it is not the application shell. Aurora's new Home and library views can coexist beside it.

### Where is ROM launching implemented?

The details screen initiates launch, but the protected path spans several layers:

```text
src/menu/views/load_rom.c
  -> cart_load_n64_rom_and_save()
src/menu/cart_load.c
  -> flashcart_load_rom()
  -> flashcart_load_save()
src/flashcart/flashcart.c
  -> hardware-specific driver
src/menu/views/load_rom.c
  -> populate boot_params_t and MENU_MODE_BOOT
src/main.c / src/boot/
  -> boot handoff
```

Phase 2 already added a source-independent, owned pending ROM path into the existing details view. That was the required Aurora seam; it did not change cart loading, save handling, SC64 commands, or boot code.

### Where is metadata loaded?

`rom_config_load()` in `src/menu/rom_info.c` is the canonical ROM inspection pipeline. It reads and endian-normalizes the ROM header, applies the internal database, determines CIC/save/TV/features, loads per-ROM configuration, loads external metadata, and may load embedded metadata.

External metadata compatibility currently includes:

- sibling `<ROM-stem>.meta` ZIP containing case-sensitive `metadata.ini`;
- sibling `<ROM-stem>.metadata.ini`;
- global four-character game-code paths below `/menu/metadata/`.

The existing precedence and some path logic are irregular. Aurora should preserve compatibility while introducing one resolver behind a narrow interface instead of duplicating the rules in each view.

### Where is box art rendered?

`src/menu/ui_components/boxart.c` resolves and owns box-art media. `src/menu/views/load_rom.c` displays it. Runtime PNG work is performed by the global single-flight decoder in `src/menu/png_decoder.c`.

The component is suitable for one selected game's details panel. It is not yet a shelf cache: dimensions and placement are details-oriented, decoder ownership is global, and freeing a loading component can abort the global decode.

### Which UI components are reusable?

Reusable now:

- normalized `menu->actions` input vocabulary;
- view registration and mode transitions;
- backgrounds and standard layout helpers;
- text and font helpers;
- cards/boxes, action bars, context menus, message boxes, and loaders;
- sound effects and polling;
- selected-game box-art path resolution and one-at-a-time decoding;
- details, settings, history, favorites, Controller Pak, RTC, system, flashcart, and error views.

Reusable after extraction/adaptation:

- box-art path resolution independent of fixed details geometry;
- title normalization and metadata resolution;
- list/grid selection mechanics;
- a bounded artwork cache layered over the single-flight decoder.

Not reusable as the Aurora library model:

- transient `entry_t` Browser records;
- eight-entry path-only history/favorites arrays;
- full retained `rom_info_t` instances for every game.

### Which parts should not be modified unless absolutely necessary?

Protected boundaries:

1. `src/boot/**`, `boot_params_t`, CIC/register handoff, and `reboot.S`.
2. `src/flashcart/**` hardware command IDs, address maps, initialization order, writeback registration, and deinitialization/lock behavior.
3. `src/menu/cart_load.c` ROM/save staging sequence.
4. `flashcart_load_save()` save creation, exact-size validation, `0xFF` fill, staging, and writeback.
5. Existing per-ROM CIC/save/TV/cheat/patch semantics.
6. `usb_comm_poll()` availability in the menu loop.
7. Global single-flight PNG ownership until deliberately replaced with equivalent tested behavior.
8. Browser as a fallback until the library is independently reliable.
9. The launch window after ROM/save staging starts; no scan, index write, or artwork decode should compete with boot handoff.

## 4. Tavinho SummerCart64 Menu

### Cover-art handling

Tavinho retains the shared `/menu/metadata/<game-code>/` organization and extends `src/menu/ui_components/boxart.c` with PNG and video media. In `src/menu/views/load_rom.c`, it probes a fixed sequence: front video, front/back/sides/top/bottom box art, and front/back Game Pak images. Missing media is skipped.

The Browser preview in `src/menu/views/browser.c` has a useful behavior:

- wait 300 ms after selection stabilizes;
- inspect only a ROM entry;
- load configured image or video preview;
- optionally fall back between image and video;
- free pending/loaded media on selection or view change.

That debounce/fallback policy is worth adopting. Its global static state and details-specific component remain unsuitable as Aurora's final asset manager.

### Video previews

The host converter `tools/video_to_bmv.py` uses ffmpeg and writes a simple `.bmv` container:

- magic `SC64VID\0`;
- little-endian width, height, fps, flags, and frame count;
- raw big-endian RGBA5551 frames;
- maximum converter dimensions 320x240 and maximum 30 fps.

Runtime code in `src/menu/ui_components/boxart.c` opens the `.bmv`, allocates one decoded frame surface, and reads raw frames. It may seek ahead to match time. There is no runtime video decompression.

At 320x240, each frame is 153,600 bytes. At 30 fps, the SD stream demand is about 4.6 MB/s before filesystem overhead. This is simple but expensive and must not become an initial 4 MiB requirement.

### Audio previews

A same-basename `.wav64` sidecar is loaded for video preview. Playback uses libdragon mixer/waveform facilities; Browser settings independently enable video preview and preview audio.

The implementation starts/stops audio with component lifetime, but evidence of robust audio/video synchronization, underrun instrumentation, or simultaneous stress with scans and PNG work was not found. Aurora should treat preview audio as optional polish, not a foundation.

### Metadata organization

Tavinho uses the upstream game-code metadata layout. Its details screen adds regionless artwork fallback and a homebrew title-derived path. The homebrew path still needs stronger sanitization before Aurora should copy the behavior.

### Performance considerations

Useful patterns:

- 300 ms selection debounce before media work;
- one selected preview instead of loading every list row;
- host-side conversion to N64-ready pixel formats;
- image/video fallback;
- explicit component teardown on selection/view change;
- preview disabled by default in settings.

Risks:

- raw video bandwidth at maximum settings;
- synchronous ROM inspection and file I/O after the debounce;
- global/static media ownership;
- no measured free-heap watermark or 4 MiB stress evidence in the audited source;
- preview, MP3/BGM, PNG decoding, and USB all share one menu loop;
- several media paths and labels remain branded or hard-coded.

### Asset pipeline

Useful host-side tools:

- `tools/video_to_bmv.py`: ffmpeg video to raw RGBA5551 `.bmv`.
- `tools/image_to_scimg.py`: ffmpeg image to raw RGBA5551 `.scimg`.
- Git LFS attributes for ROMs, videos, audio, generated images, build products, and archives.

Aurora should adopt the principle of versioned preconverted assets, not copy the formats unchanged without validation. A production format needs explicit versioning, dimensions/length validation, overflow checks, and corruption handling.

### Screensaver implementation

`src/menu/menu.c` records last activity and enters `MENU_MODE_SCREENSAVER` only from Browser, History, or Favorites after a configured 60/90/120 second timeout. It stores the return mode. `src/menu/views/screensaver.c` enumerates `/menu/images`, loads preconverted `.scimg` frames, rotates images, and exits on input.

A 640x480 RGBA16 `.scimg` is about 600 KiB plus header and runtime surface overhead. The concept—idle timer, explicit allowed origins, remembered return mode, preconverted media—is useful. Full-screen allocation and directory enumeration should be deferred until library navigation and memory instrumentation are stable.

### Languages

`src/menu/i18n.c` provides Portuguese, English, and Spanish through a compile-time table and exact source-string lookup. This proves a lightweight translation layer is feasible. Aurora should not copy the table literally: exact Portuguese strings are keys, some strings remain untranslated, and one static 1024-byte buffer is reused for prefixed translations. Stable message IDs or per-view string tables are safer.

### Which ideas are worth borrowing?

Adopt or adapt:

- delayed selected-item media loading;
- image/video fallback policy;
- host-side asset conversion;
- explicit media lifetime and cancellation;
- idle timeout plus validated return mode;
- media settings defaulting off;
- lightweight localization as a later layer.

### Which decisions do not fit Aurora?

Reject or defer:

- video/audio as an early required feature;
- raw 320x240@30 media as a default;
- globally branded/hard-coded presentation;
- a details component serving as the global shelf cache;
- direct reuse of unsanitized title-derived paths;
- treating repository asset bundles or generated binaries as product source.

### Can portions be reused directly?

The AGPL-3.0 license is compatible with Aurora's inherited AGPL-3.0 obligations, but direct copying should remain selective and preserve attribution. The cleanest approach is to reimplement the small policies—debounce, fallback, idle routing—behind Aurora interfaces and only copy isolated, well-reviewed helpers when that is clearly less risky.

## 5. N64ever

### Why does its interface feel better than a file browser?

`src/menu/views/cover_menu.c` turns the current directory into a 2x2 visual choice rather than exposing filesystem rows. Large covers, D-pad focus, immediate A selection, and limited on-screen actions emphasize games rather than files. This reduces reading and makes the N64 feel like a console library.

### How is navigation organized?

The audited implementation uses:

- four cover slots in a 2x2 grid;
- clamped directional movement;
- A to enter the selected ROM details/load flow;
- B to return to the Browser;
- Start for settings;
- C-button actions for session favorites;
- a separate favorites grid reached from the cover interface.

The design is simple and legible, but it has no complete paging/scrolling library model.

### What makes it console-like?

- artwork is primary and filenames are secondary;
- only game choices are visible;
- selection is spatial rather than list-cursor based;
- actions are short and controller-native;
- settings/favorites are secondary overlays or destinations;
- the visual hierarchy resembles a launcher, not a file manager.

### What should Aurora adopt?

- games-first visual hierarchy;
- consistent grid geometry and focus treatment;
- D-pad/A/B navigation with minimal instructional text;
- cover art as identity reinforcement, not merely details decoration;
- favorites/recent/collections as library views rather than folders;
- immediate fallback to Browser when the curated layer cannot satisfy a task.

### What limitations remain?

At the audited revision:

- only the first four ROMs in the current directory are represented;
- directory scanning stops after filling the four cover slots;
- no complete paging, sorting, recursive scan, or persistent index exists;
- favorites are process-memory state, not durable storage;
- identity is path/entry oriented rather than a stable game key;
- titles are derived from ROM/header data without a full canonical-title resolver;
- artwork uses direct `/menu/boxart/<game-code>.png` assumptions rather than the full metadata resolver;
- missing/corrupt asset and allocation behavior is not a production-quality cache;
- initialization order performs display/view work before the standard menu initialization path is fully established;
- no source evidence of a 4 MiB/8 MiB hardware matrix, heap watermark, long-list stress, or launch/save regression suite was found.

N64ever is strong UX evidence and weak production architecture evidence. Aurora should borrow the interaction model, not transplant `cover_menu.c` as its library subsystem.

## 6. Shared metadata ecosystem

### Current schema and layout

At the pinned revision, the repository contains:

- 1,616 `metadata.ini` files;
- 1,672 PNG assets;
- about 2.35 MB of INI data;
- about 1.85 GB of PNG data;
- no generated global index or manifest.

Typical path:

```text
metadata/<game-code[0]>/<game-code[1]>/<game-code[2]>/<region>/
  metadata.ini
  boxart_front.png
  boxart_back.png
  boxart_left.png
  boxart_right.png
  boxart_top.png
  boxart_bottom.png
  gamepak_front.png
  gamepak_back.png
```

The shared repository serializes fields as:

```ini
[metadata]
name=
author=
release_date=
osi_license=
website=
age_rating=
short_description=
```

This does **not** exactly align with the audited upstream V0.3.2 parser. `src/menu/rom_info.c` reads section `[meta]` and hyphenated keys such as `release-date`, `osi-license`, `age-rating`, and `short-desc`. C struct members such as `release_date` are not serialized key names. Artwork path compatibility is separate from INI field compatibility.

Aurora must treat both forms as input dialects in compatibility fixtures and a central resolver. It must not silently claim that the current shared INIs are parsed by upstream unchanged. Fixtures are required for sibling `.meta` ZIP, sibling `.metadata.ini`, global shared-repository INI, and embedded metadata.

### Should Aurora extend this format instead of inventing another?

Yes, but the current dialect mismatch must be reconciled. Aurora should treat the repository as the canonical shared descriptive-data and artwork convention, accept both the repository and V0.3.2 parser dialects, and propose one documented canonical serialization upstream. Its local generated index is a cache of ROM/header/path/metadata results—not a competing metadata database.

### Can optional fields be added backward compatibly?

Yes at the INI level once the section/key dialect is explicit: compatible parsers can ignore unknown keys. Additions still require naming and semantic agreement before use. Recommended shared candidates include:

- `genre` or a clearly defined multi-value `genres` convention;
- `release_year` only if it adds value beyond `release_date`;
- `developer` and `publisher` rather than overloading `author`;
- `players` with a documented syntax;
- `collections` or `tags` with escaping/multi-value rules;
- external content-rating system and value fields if needed.

Do not place personal/runtime data such as play time, last played, favorite status, or a user's rating in the shared metadata repository. Those belong in Aurora's local preferences/activity store. If “play time” means estimated completion length, use an explicitly named shared field and define its units/source.

### Should Aurora contribute improvements upstream?

Yes, when the fields are broadly useful and have documented semantics. Aurora-specific scan caches, UI state, personal collections, play history, and generated indexes should remain local. Shared schema, validation tools, metadata corrections, and non-personal tags are good upstream candidates.

### Identity and duplicate limitations

Four-character game code plus region is useful for metadata/artwork lookup, but it is not a unique ROM identity. Revisions, hacks, translations, homebrew, prototypes, byte-order variants, and dumps can share or lack codes. Aurora's stable identity must therefore combine normalized header information with a stronger fingerprint and retain the exact path. Metadata lookup and library identity are related but distinct.

### Can the database remain untouched?

For the next phases, the repository's source data can remain unchanged. Aurora needs a compatibility resolver because current upstream V0.3.2 does not parse the repository's serialized dialect directly. It can build a compact local cache from either dialect. Schema reconciliation should be proposed upstream separately and must not block header indexing or cover-grid work.

### Licensing and provenance caution

The repository declares the Unlicense, but its README also credits LaunchBox/ABeezy for artwork. A repository-level software/data license does not automatically prove the right to redistribute every scan or logo. Aurora may support user-installed compatible asset packs and the path convention immediately. Bundling or redistributing a large art pack should wait for a documented provenance review.

## 7. Architecture questions: explicit answers

### 1. Can Aurora inherit 80–90% of the existing codebase?

Yes as a design target. Preserve the application core and add focused modules/views. Do not use the percentage as a release claim until changed LOC and protected-boundary diffs are measured.

### 2. Can the existing ROM launcher remain untouched?

Yes below the details-source seam. Phase 2 already proved explicit Library -> Details selection while leaving `cart_load.c`, flashcart code, save behavior, and boot code untouched. Future library work should continue to hand an owned path to the details view and let the stock launch path proceed only after explicit A confirmation.

### 3. Can the existing metadata database remain untouched?

Yes as source data for upcoming work, but not through the V0.3.2 parser unchanged. Consume both known dialects through a bounded resolver and derive a local versioned index. Propose dialect reconciliation and broadly useful optional fields upstream later.

### 4. Can the existing box-art renderer be reused?

Yes for one selected image and path compatibility. No as-is for a multi-cover library. Extract/adapt resolution and draw behavior into a serialized asset manager with bounded decoded surfaces and explicit cancellation.

### 5. Can Aurora add a completely new Home while keeping Browser secondary?

Yes, and Phase 1 already proved it. Browse Files remains a Home card and Browser root-B returns to Home when Aurora Home is enabled.

### 6. Which project has the cleanest implementation in each area?

| Area | Best evidence | Aurora conclusion |
|---|---|---|
| Cover-art UX | N64ever | Adopt the visual hierarchy and controller interaction, not its four-entry data model. |
| Cover-art media behavior | Tavinho | Adapt delayed load, image/video fallback, and cleanup. |
| Metadata compatibility | N64FlashcartMenu + shared metadata repo | Preserve game-code paths, accept both audited INI dialects, and work toward one canonical schema. |
| Navigation architecture | N64FlashcartMenu view dispatcher + Aurora Phase 1/2 | Continue additive views; apply N64ever's games-first layout. |
| Performance foundation | N64FlashcartMenu baseline | Preserve proven polling/renderer/loader behavior and add budgets. No external fork supplied enough benchmark evidence to claim superiority. |
| Asset loading ideas | Tavinho host converters; upstream single-flight PNG | Use preconversion where justified, but retain serialized bounded runtime loading. |

### 7. Which decisions should Aurora adopt?

- N64FlashcartMenu: additive views, shared state only for cross-view contracts, stock details/launch stack, Browser fallback, flashcart abstraction.
- Tavinho: media debounce, graceful fallback, preconverted optional media, remembered screensaver origin, media off by default.
- N64ever: games-first cover grid, spatial navigation, minimal chrome, favorites as a library destination.
- Shared metadata: game-code folder compatibility and contributor-driven common schema.

### 8. How does Aurora fit on top of the existing menu?

```text
main.c
  |
  v
N64FlashcartMenu initialization + frame loop + normalized input
  |
  v
View dispatcher ---------------------------------------------------+
  |                                                               |
  +--> Aurora Home                                                |
  |      |                                                        |
  |      +--> All Games / Favorites / Recent / Collections        |
  |      |       |                                                |
  |      |       v                                                |
  |      |   Aurora Library View                                  |
  |      |       |                                                |
  |      |       +--> Library index (versioned local cache)        |
  |      |       +--> Metadata resolver (shared INI conventions)   |
  |      |       +--> Asset manager (single-flight, bounded cache) |
  |      |       |                                                |
  |      |       +-- selected owned ROM path ------------------+   |
  |      |                                                    |   |
  |      +--> Browse Files --> stock Browser ------------------+   |
  |                                                           v   |
  +--> History / Favorites ----------------------------> stock Details
                                                              |
                                                              | explicit A
                                                              v
                                                  protected ROM/save loader
                                                              |
                                                  flashcart abstraction / SC64
                                                              |
                                                        protected boot handoff
```

Critical rule: the Aurora index and artwork manager never call SC64, save, cart-load, or boot APIs. They produce a selected owned path. The existing details/launcher stack remains the authority.

## 8. Adoption decision table

| Idea | Decision | Reason / prerequisite |
|---|---|---|
| Additive Home and library views | Adopt now | Proven in Phase 1/2; preserves Browser and launcher. |
| Versioned local ROM index | Adopt next | Required for complete library, stable startup, and sorting. |
| Stable identity separate from metadata key | Adopt next | Paths and four-character codes are insufficient. |
| Existing metadata folder/data | Adopt now | Ecosystem path-compatible; the current INI dialect mismatch requires a compatibility resolver. |
| Central metadata/art resolver | Adopt next | Removes duplicated/irregular path logic while preserving compatibility. |
| N64ever-style cover-first grid | Adapt after index | Strong UX; needs complete paging, caching, fallback, and persistence. |
| Existing details box-art component | Reuse now for details | Already integrated and bounded to one selected game. |
| Multi-cover cache using current component unchanged | Reject | Fixed geometry and global decoder ownership do not scale safely. |
| Tavinho 300 ms preview debounce | Adapt later | Good policy for selected covers/video; unnecessary for static text indexing. |
| Host-side media preconversion | Adapt later | Useful after format validation and provenance decisions. |
| Video/audio previews | Defer | Bandwidth, loop contention, memory, and asset-size risk. |
| Screensaver | Defer | Good architecture concept; full-screen media is not a Phase 3 prerequisite. |
| Lightweight localization | Defer | Useful after strings and navigation stabilize; use stable IDs rather than source-string keys. |
| Persistent personal favorites/recent | Adapt after index | Needs stable IDs, migrations, and recoverable writes. |
| Shared play history/user ratings in metadata repo | Reject | Personal/runtime state does not belong in common descriptive metadata. |
| Bundle the 1.85 GB art dataset | Reject pending provenance review | Operationally excessive and scan rights are not established by repository license alone. |
| Replace Browser | Reject | Browser is a valuable fallback, diagnostic tool, and compatibility path. |
| Rewrite launcher/save/SC64/boot | Reject | High regression risk with no product benefit. |

## 9. Principal risks and validation requirements

### Original 4 MiB N64

This is the primary constraint. Existing display buffers consume roughly 1.2 MiB. Background, PNG, audio, lists, and metadata are additional. New work must measure rather than assume:

- free heap before/after scan batches;
- peak heap while decoding and changing covers;
- exact library-record and string-pool sizes;
- repeated Home -> Library -> Details -> Library cycles;
- missing/corrupt metadata and artwork;
- large and deeply nested SD libraries;
- both `.v64` and `.z64` paths.

Initial art policy should use one decode at a time and a hard decoded-art budget. If host-preconverted 120x90 thumbnails are used, one selected 158x158 image plus four thumbnails is 136,328 bytes (about 133.1 KiB) of RGBA16 pixel data before decoder, row-buffer, surface, allocator, and retained-library overhead. The current PNG decoder does not resize; it allocates native PNG dimensions. Phase 4 must therefore define preconverted thumbnails or budget a separate downsampling stage rather than assuming draw dimensions reduce decode memory.

### Startup and SD performance

Do not recursively scan the entire card before presenting Home. Use a structurally valid cache immediately as a possibly stale snapshot, then revalidate configured roots incrementally. Structural validity and freshness are separate states. Root configuration identity, canonical/overlapping roots, additions, removals, ROM changes, metadata changes, and mutation during scanning need explicit reconciliation rules.

The scanner must not call the full synchronous `rom_config_load()` pipeline for every ROM. That path can probe ZIP/INI/embedded metadata and allocate declared payload sizes without the limits a cooperative scanner needs. Phase 3 requires a bounded header/fingerprint reader with maximum bytes/time per poll and cancellation points. Metadata enrichment is on demand or follows only after strict compressed/uncompressed/file/string caps and allocation-failure handling exist.

The service needs one owner and lifecycle: initialize with the menu, poll only in safe modes, pause/cancel and quiesce before details launch staging, and free on global teardown. Completed scans publish immutable index snapshots with generation IDs; views retain stable identity/owned paths rather than pointers into a mutable snapshot.

### Identity

Define and test:

- normalization across `.z64`, `.v64`, and `.n64` byte orders;
- fingerprint input and collision policy;
- region/revision handling;
- homebrew/hack identity;
- moved/renamed ROM reconciliation;
- exact path retention for launch and diagnostics.

### Regression safety

Starting in Phase 3, every library phase must prove:

- default/off build still starts Browser;
- Aurora-enabled build retains Home and Browse Files;
- Library selection reaches the matching details view;
- details B returns to the originating library selection;
- missing selected paths show Error and B returns to the same library origin when that origin remains valid; Phase 2 intentionally returned Error B to Browser and is not retroactively claimed to satisfy this refinement;
- stock Browser/History/Favorites/details behavior is unchanged;
- protected boot/flashcart/cart-load diffs are empty;
- launch/save claims are made only after the separate backed-up hardware gate.

### Research gaps

This audit did not build or run Tavinho or N64ever, benchmark their media paths, or test them on 4 MiB hardware. Their source is used as architectural evidence, not as a hardware certification. Art provenance also remains unresolved for redistribution.

## 10. Conclusion

The research supports the original Aurora goal and narrows the implementation path. The next milestone is not a richer static screen, a new launcher, or video. It is a reliable real-library foundation that feeds the already-proven Home -> Library -> Details seam. Once that index is stable on original 4 MiB hardware, Aurora can add the N64ever-style cover experience using the existing metadata ecosystem and a deliberately bounded adaptation of the upstream/Tavinho art loading ideas.
