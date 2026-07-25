# Aurora64 Architecture Audit

Status: pinned upstream baseline audit
Upstream: `Polprzewodnikowy/N64FlashcartMenu`
Baseline: `V0.3.2` / commit `6407ab15f6c19d1bf9ded5104c1c8b3c36471379` on branch `aurora64-v0.3.2`
Scope: architecture only; no Aurora64 product behavior is implemented here

## 1. Baseline verification

The unchanged upstream source builds successfully on the Apple Silicon Mac mini using the project development image under x86_64 emulation.

```sh
git submodule update --init --recursive

docker build --platform linux/amd64 \
  -t aurora64-dev:v0.3.2 \
  -f .devcontainer/flashcart/Dockerfile.sc64deployer .

docker run --platform linux/amd64 --rm \
  -v "$PWD":/work -w /work aurora64-dev:v0.3.2 \
  bash -lc 'set -e; cd libdragon; make clobber -j2; make libdragon tools -j2; make install tools-install -j2; cd /work; make clean; make all'
```

Observed outputs from the verified baseline build (`BUILD_TIMESTAMP` embedded as `2026-07-25 02:24:57 +00:00`; default `MENU_VERSION` was `"Preview release"`):

| Artifact | Size | SHA-256 |
|---|---:|---|
| `output/N64FlashcartMenu.n64` | 1,671,168 bytes | `da4e8674bfc38eed039c77b6dfa8269096b3a90d096f565db7c5e12d44692b16` |
| `output/sc64menu.n64` | 1,671,168 bytes | same |
| `output/menu.bin` | 1,671,168 bytes | same |
| `output/OS64.v64` | 1,671,168 bytes | same |
| `output/OS64P.v64` | 1,671,168 bytes | same |

The vendor-specific outputs are copies of the same built menu ROM (`Makefile:149-165`), so identical hashes within one build are expected. The hash is a one-run observation, not a permanent V0.3.2 fingerprint: `Makefile:12` embeds the current UTC build timestamp, so a clean rebuild can produce a different hash without source changes. The image/toolchain is amd64 and therefore requires amd64 execution or emulation on Apple Silicon; these commands explicitly select `linux/amd64`.

The build leaves tracked upstream source unchanged. The two local Aurora64 brief files remain untracked and untouched.

## 2. Executive architecture summary

N64FlashcartMenu is a single-process, global-state C application built around:

1. A `menu_t` state object shared by all views (`src/menu/menu_state.h:90-147`).
2. A small view registry with `init` and `show` callbacks (`src/menu/menu.c:175-205`).
3. Global controller actions populated once per rendered frame (`src/menu/actions.c`).
4. A transient, directory-oriented browser model (`src/menu/views/browser.c`).
5. A detailed per-ROM inspection structure, `rom_info_t` (`src/menu/rom_info.h:114-176`).
6. A flashcart-neutral loading boundary implemented through `flashcart_t` (`src/flashcart/flashcart.h`, `src/flashcart/flashcart.c`).
7. Hardware-specific drivers, including SummerCart64 (`src/flashcart/sc64/`).
8. A protected boot handoff (`src/boot/`). In the normal ROM launch path it runs after ROM/save staging; the trusted USB `reboot` command is a separate direct-boot path.

The safest Aurora64 design is an additive home/shelf view that retains the stock browser and launch stack. The first implementation should avoid rewriting ROM loading, save handling, SC64 commands, or low-level boot code.

## 3. Startup and main loop

### Entry and initialization

`main()` creates `boot_params_t`, calls `menu_run()`, then disables interrupts and calls `boot()` (`src/main.c:7-20`).

`menu_init()` (`src/menu/menu.c:47-136`) performs the global setup:

- allocates `menu_t`;
- initializes the flashcart and determines `storage_prefix`;
- initializes input, timers, RTC, RSP/RDP, DFS, sound, settings, and bookkeeping;
- creates or loads `<storage>/menu/config.ini` and `<storage>/menu/history.ini`;
- allocates two 640x480 16-bpp display buffers;
- initializes fonts and the cached background;
- prepares persistent browser state.

The initial transition is:

```text
MENU_MODE_NONE -> MENU_MODE_STARTUP
```

A flashcart initialization error replaces startup with `MENU_MODE_FAULT` (`src/menu/menu.c:53-59`).

### View dispatcher

Views are registered as:

```c
{ menu_mode_t id, init(menu_t *), show(menu_t *, surface_t *) }
```

in `src/menu/menu.c:175-205`. There is no dispatcher-level `deinit` callback. Resource-owning views perform local cleanup when they observe that `next_mode` has changed; for example `view_load_rom_display()` at `src/menu/views/load_rom.c:731-745`.

The frame loop (`src/menu/menu.c:227-273`) does the following:

1. Acquire a display surface.
2. Update global input actions.
3. Render/process the active view.
4. Resolve mode transitions and run each incoming view's `init()`.
5. Poll audio, asynchronous PNG decoding, and USB communication.

A transition occurs after the outgoing view has rendered, so the old view receives one final frame. An incoming `init()` can immediately set another `next_mode`, allowing startup to chain directly into the destination view.

### Startup routing

When built with `FEATURE_AUTOLOAD_ROM_ENABLED`, `view_startup_init()` (`src/menu/views/startup.c:11-43`) prioritizes configured ROM autoload. Without that compile-time feature, the default build routes only between first-run Credits and Browser/Home. The full source-level priority is:

1. configured ROM autoload;
2. first-run credits;
3. normal startup into `MENU_MODE_BROWSER`.

For Aurora64, only the third branch should initially change. Autoload, first-run behavior, and fault handling should remain intact.

## 4. Menu state and input

`menu_t` (`src/menu/menu_state.h:90-147`) owns:

- current and next menu mode;
- storage prefix, settings, history/favorites, and boot parameters;
- normalized input actions;
- transient browser state;
- current ROM/disk load state;
- deferred load flags.

The action layer maps controllers to a small vocabulary (`src/menu/actions.c`):

- D-pad/analog: directional movement;
- C-buttons: fast movement;
- A: enter;
- B: back;
- R: options;
- Start: settings;
- L/Z: context.

A shelf view can reuse this action layer without reading controllers directly.

## 5. Browser and current ROM model

### Browser discovery

The stock browser is directory-at-a-time rather than a recursive game library. `load_directory()` (`src/menu/views/browser.c:229-337`) enumerates the current directory, filters protected/hidden entries, classifies by extension, sorts, and builds an array of `entry_t`.

`entry_t` contains only (`src/menu/menu_state.h:71-77`):

- filename;
- entry type;
- size;
- archive index.

ROM detection is extension-based (`z64`, `n64`, `v64`, `rom`). The model has no stable game identity, clean title, full path, cached header information, artwork state, or shelf membership.

The browser state is intentionally persistent across view changes (`src/menu/menu_state.h:119-130`). `view_browser_init()` can consume `menu->browser.select_file`, providing a useful compatibility seam for selecting a known path in the stock browser.

### ROM inspection

`rom_config_load()` (`src/menu/rom_info.c:1262-1307`) builds a detailed `rom_info_t` by:

1. reading and endian-normalizing the ROM header;
2. looking up the game in the internal game database;
3. detecting CIC, save type, TV type, accessories, and other features;
4. loading sibling per-ROM configuration;
5. loading external/global metadata;
6. loading embedded metadata when appropriate.

This inspection pipeline should be reused rather than reimplemented for Details and launch. It should **not** be called as the baseline background scanner merely one ROM at a time: it synchronously probes configuration, ZIP/INI metadata, and embedded metadata, and its metadata payload allocations are not bounded for a cooperative 4 MiB scan. A future scanner needs a separate bounded header/fingerprint reader with hard byte/time/depth limits and cancellation points. Metadata enrichment should be on demand, or should follow only after strict file, compressed/uncompressed payload, string, and allocation limits exist. Any `rom_info_t` created for Details/launch must still be zero-initialized and released with `rom_info_free_meta()`; the raw 20-byte ROM title must never be treated as a NUL-terminated string.

### Clean titles

V0.3.2 does not provide a complete clean-title pipeline:

- browser rows display raw filenames;
- beta settings for file extensions and ROM tags are not applied in the browser;
- the N64 header title is a fixed 20-byte field and may contain padding;
- metadata `name` exists but is not the canonical browser title.

Aurora64 should centralize title resolution with explicit precedence, for example:

```text
metadata name -> normalized ROM header title -> cleaned filename -> original filename
```

The original path and filename must always remain available for loading and diagnostics.

## 6. Metadata and artwork

### Metadata

`rom_info_t.meta` contains name, author, release date, license, website, age rating, and short description (`src/menu/rom_info.h:167-175`).

The compatibility precedence in V0.3.2 is exact and somewhat irregular:

1. Try sibling `<ROM-stem>.meta` as a ZIP containing case-sensitive `metadata.ini`; return immediately on success.
2. Otherwise, if sibling `<ROM-stem>.metadata.ini` exists, try it.
3. Only when that sibling INI does not exist, try the four-character global game-code path under `sd:/menu/metadata/...`; there is no regionless global metadata fallback.
4. After external loading, try embedded metadata only when `meta.name` is still empty.

A malformed sibling INI can therefore suppress global fallback, while an external source with an empty name can still be replaced by embedded metadata behavior.

The current implementation has duplicated parsing and path-resolution logic. Global metadata also hardcodes `sd:/` in one path rather than consistently using `storage_prefix`. Aurora64 should eventually introduce one metadata resolver, but should first preserve compatibility with existing layouts.

The audited V0.3.2 parser reads section `[meta]` and hyphenated serialized keys such as `release-date`, `osi-license`, `age-rating`, and `short-desc`. The shared `n64-tools/n64-flashcart-menu-metadata` repository at audited revision `1f278e49` uses `[metadata]` and underscore keys instead. Folder/artwork paths remain compatible, but metadata serialization does not. Aurora's resolver therefore needs fixtures for both dialects rather than assuming that current shared INIs are parsed unchanged.

### Artwork

`ui_components_boxart_init()` (`src/menu/ui_components/boxart.c:45-173`) resolves external PNG files from:

```text
<storage>/menu/metadata/<game-code path>/boxart_front.png
<storage>/menu/metadata/<regionless game-code path>/boxart_front.png
<storage>/menu/boxart/...                    # legacy fallback
```

Additional names include box front/back/sides/top/bottom and Game Pak front/back.

The component is useful for one selected-game preview. It is not directly suitable for a multi-cover shelf because:

- its dimensions and draw position are fixed for the existing load screen;
- PNG decoding is global and single-flight (`src/menu/png_decoder.c`);
- freeing a loading box-art component aborts the global decoder (`src/menu/ui_components/boxart.c:180-190`).

A shelf therefore needs serialized decode requests, explicit ownership, and a small bounded cache rather than one concurrently loading component per visible cover.

Homebrew artwork currently derives a directory from the raw 20-byte ROM title (`src/menu/ui_components/boxart.c:57-65`) without full filename sanitization. A new resolver must sanitize path separators, control characters, and padding.

## 7. History, favorites, and configuration

History and favorites are fixed arrays of eight items (`src/menu/bookkeeping.h`). Each record contains primary path, optional secondary path, and item type. They are stored in `<storage>/menu/history.ini` and are matched by exact paths (`src/menu/bookkeeping.c:37-128`).

Useful behavior to preserve conceptually:

- MRU insertion;
- deduplication;
- synchronous persistence after changes.

Limitations for a shelf library:

- paths are mutable identity;
- fixed capacity is eight;
- no title, artwork, timestamp, play count, or stable game ID;
- moving a ROM leaves a broken record;
- whole-file INI writes are not atomic.

Aurora64 should use a separate library record keyed by stable identity while leaving the existing history/favorites format intact for compatibility during early phases.

Global settings live in `<storage>/menu/config.ini`. Per-ROM overrides are sibling `.ini` files and include CIC, save type, TV type, cheats, and patches (`src/menu/rom_info.c:1100-1259`). Per-ROM overrides are part of the protected launch behavior and should continue flowing through `rom_config_load()`.

## 8. ROM launch and save pipeline

The protected launch path is:

```text
view_load_rom_display()
  -> load()
  -> cart_load_n64_rom_and_save()
  -> flashcart_load_rom()
  -> flashcart_load_save()
  -> conditional flashcart_set_next_boot_mode()
  -> bookkeeping_history_add()
  -> populate boot_params_t / MENU_MODE_BOOT
  -> menu_deinit()/flashcart_deinit()
  -> boot()
  -> reboot.S handoff
```

Relevant locations:

- launch processing: `src/menu/views/load_rom.c:604-745`;
- generic load policy: `src/menu/cart_load.c:99-142`;
- generic save lifecycle: `src/flashcart/flashcart.c:278-315`;
- hardware handoff: `src/boot/boot.c`, `src/boot/reboot.S`.

`flashcart_load_save()` is the canonical save boundary. It:

1. configures save type;
2. creates a missing save at the exact required size;
3. fills a new save with `0xFF`;
4. rejects incorrect file sizes;
5. loads save contents into cart memory;
6. registers writeback when the driver supports it.

Aurora64 must not call SC64 save functions directly or bypass this generic layer.

One upstream defect is visible in `cart_load_n64_rom_and_save()`: if fast reboot is enabled but unsupported, line 129 returns without freeing the cloned `path_t`. This is a small leak, not a reason to alter the launch design.

### Current loader coupling

`view_load_rom_init()` resolves its source from history, favorites, or `menu->browser.entry` (`src/menu/views/load_rom.c:677-692`). A new shelf cannot safely jump straight to `MENU_MODE_LOAD_ROM` without supplying an explicit source-independent path.

The existing `browser.select_file` helper is not fail-closed: it can load the target's parent directory but leave the first entry selected when the requested basename is missing or filtered. `view_browser_init()` also consumes and frees the pending `path_t`. It is not safe to use unchanged as an automatic shelf-to-launch handoff.

The safest first integration is therefore:

1. clone the selected path and transfer ownership explicitly: `menu->browser.select_file = path_clone(record->rom_path)`;
2. switch to `MENU_MODE_BROWSER`;
3. harden browser selection so it reports failure unless it finds an exact visible `ENTRY_TYPE_ROM` match;
4. on failure, preserve/restore prior browser state and show an error rather than leaving a misleading selection;
5. free or replace any already-pending target and free a remaining pending target during global teardown;
6. after verified selection, require the normal stock A/load flow.

This exposes the browser by design, but proves shelf selection without changing launch or save behavior. Phase 1 must also provide a deliberate route from Browser back to Home (for example, B at the filesystem root or an explicit Home action), and first-run Credits should route to Home. Otherwise entering Browser makes Home unreachable until reboot. A later refinement should add an explicit load-target path and return-mode field to `menu_t`.

## 9. SummerCart64 boundary

Flashcart selection and feature dispatch occur through the generic `flashcart_t` interface (`src/flashcart/flashcart.c`). The SC64 implementation is in `src/flashcart/sc64/sc64.c`, with low-level commands in `sc64_ll.c`/`.h`.

Protected SC64 behavior includes:

- firmware compatibility checks;
- waiting for pending save writeback;
- resetting SC64 configuration during initialization;
- ROM staging address ranges and chunk sizes;
- save-memory mappings;
- FAT-sector writeback registration;
- lock/deinitialization before boot;
- MMIO addresses, command IDs, and firmware ABI.

Aurora64 UI code should never issue SC64 MMIO commands directly. New hardware capabilities should be exposed through the generic flashcart feature/vtable boundary.

The SC64 driver contains unbounded busy waits during command execution and startup. These are existing hardware-failure risks; changing them requires targeted hardware testing rather than UI work.

## 10. USB development path

The upstream development flow uses `sc64deployer` and a proxy at `host.docker.internal:9064` (`remotedeploy.sh`). Relevant make targets are in `Makefile:181-211`.

Runtime USB polling occurs in every menu loop (`src/menu/menu.c:261-265`). `src/menu/usb_comm.c` supports:

- `reboot`;
- `send-file`.

Risks in the current upstream path:

- `run-debug-upload` can depend on one output name while uploading `output/sc64menu.n64`; a full `make all` avoids stale/missing copies;
- `send-file` validates size after opening the destination, so an oversized transfer can truncate/create a file before rejection;
- transfer length parsing is weak;
- this is a trusted developer channel, not a production network service.

The baseline build proves the container includes `sc64deployer`. USB visibility and deployment remain a separate hardware-validation step; this audit did not write to the cart or SD card.

## 11. Memory and asset constraints

Major memory consumers include:

- two 640x480 16-bpp display buffers: about 1.2 MiB;
- optional persistent RGBA16 background: up to 614,400 bytes at 640x480, plus surface/display-list overhead;
- transient full-screen PNG surface: up to about 600 KiB;
- box art: up to about 49 KiB per decoded 158x158 RGBA16 image;
- browser filenames/list allocations;
- full metadata ZIP extraction plus INI allocations;
- MP3 decoder/player state;
- stacks used for USB receive, boot detection, cheats, and SC64 sector tables.

Aurora64 should assume Jumper Pak operation is a hard constraint:

- preserve single-flight PNG decoding;
- use a small bounded cover cache;
- avoid extra full-screen surfaces;
- cap metadata and artwork allocations;
- check allocation failures;
- release off-screen shelf resources deterministically.

Before artwork or recursive scanning is accepted, define measurable initial gates rather than relying on “small” or “bounded”: maximum decoded/visible covers, maximum metadata entry/string size, maximum scan batch, and a minimum free-heap watermark. Exercise repeated Home -> Browser -> Load -> Home transitions on real 4 MiB hardware as well as 8 MiB hardware. Existing allocation assertions and unchecked upstream allocation paths mean new checks do not automatically make every helper recoverable.

Build-time assets are converted by libdragon rules in `Makefile:90-147` and packed into DragonFS. Runtime box art and backgrounds are external SD-card PNGs and use a separate decode/cache pipeline. These two asset paths should remain distinct.

## 12. Historical recommended Aurora64 seam

This section records the pre-implementation milestone sequence from the original audit. Its labels are historical and are superseded by `docs/plans/aurora64-product-roadmap.md`: the additive Home work and source-independent owned-path Details seam were completed across actual Phases 1 and 2, and current Phase 3 is the real-library/index foundation.

### Historical milestone A: additive navigation shell

Add a new independent mode/view while retaining the stock browser:

1. Add `MENU_MODE_HOME` to `src/menu/menu_state.h`.
2. Add `view_home_init()` and `view_home_display()` declarations to `src/menu/views/views.h`.
3. Register the home view in `menu_views[]` in `src/menu/menu.c`.
4. Add the source file to `SRCS` in `Makefile`.
5. Change only the normal startup branch from Browser to Home.
6. Provide `Browse Files` and a deliberate Browser-to-Home return path; route first-run Credits to Home.
7. Use an explicitly bounded source such as existing History/Favorites or a fixed development fixture for the first selectable tiles. Recursive library discovery and artwork are not shell-milestone dependencies.
8. Harden `browser.select_file` with exact ROM-type validation and explicit cloned-path ownership before using it for selection.
9. Require each Home-owned allocation to be released on every outgoing transition, including errors and chained transitions; the dispatcher has no global view `deinit` callback.

This preserves fault handling, first-run credits, autoload, browser fallback, detailed ROM inspection, save handling, and the SC64 boot pipeline.

### Historical milestone B: library model

Introduce a separate library module with:

- full ROM path;
- stable game identity;
- original filename;
- resolved display title;
- game code/region/header summary;
- artwork key and decode state;
- shelf membership and sort order;
- scan/version validity.

Define the stable identity algorithm, collision handling, schema version, and migration behavior before relying on identity across moves. Persist the index recoverably with a temporary file, validation/versioning, atomic replacement where supported, and a corruption fallback. Do not repeat the non-atomic whole-file history design.

Use a bounded header/fingerprint scanner with hard work/allocation limits, copy only bounded summary fields, and keep untrusted sidecar parsing out of the baseline scan until its payload limits and failure behavior are explicit. Do not inflate transient `entry_t`, call the full synchronous `rom_config_load()` scanner-wide, or retain full `rom_info_t` objects as the library model. Browser and library have different ownership, lifetime, and identity requirements.

### Historical milestone C: source-independent Details/launch seam

After the home view is proven, add source-independent launch state:

- explicit target `path_t`;
- explicit return mode/origin;
- a loader init path that does not dereference `browser.entry`.

Specify whether each path is borrowed, cloned, or transferred and exactly which component frees it on success, error, cancellation, and chained transitions. Keep all downstream `rom_config_load()`, `cart_load_n64_rom_and_save()`, `flashcart_*`, save-writeback, and boot behavior unchanged.

## 13. Protected boundaries

Changes should not bypass or casually modify:

1. `boot_params_t`, `boot()`, `reboot.S`, CIC detection, or register handoff.
2. `flashcart_load_save()` creation, size validation, fill, load, and writeback sequence.
3. SC64 address maps, command/config IDs, initialization order, or writeback sector registration.
4. Existing per-ROM config semantics.
5. `usb_comm_poll()` availability during the menu loop.
6. Single-flight PNG decoder ownership.
7. Stock Browser as the safe fallback until the shelf is independently reliable.
8. The launch window after ROM/save staging begins: do not start library persistence, artwork decoding, scanning, or other asynchronous work before boot handoff completes.

## 14. Historical regression recommendations for the direct launch seam

Before the source-independent direct launch seam is accepted on hardware, verify:

- no-save ROMs and every supported save type;
- creation of missing saves and `0xFF` initialization;
- rejection of incorrect save sizes;
- save writeback across game reset and return/re-entry;
- supported and unsupported fast reboot;
- default startup, feature-enabled autoload, first-run, and fault routes;
- SC64 firmware rejection and deinitialization/lock before boot;
- repeated navigation/load cycles on both 4 MiB and 8 MiB systems.

The existing history/favorites path records must continue receiving the exact launched path even after a stable-identity library is introduced.

## 15. Known upstream risks relevant to Aurora64

- View cleanup is manual because the dispatcher has no `deinit` callback.
- Several secondary views hardcode Browser as their return destination.
- ROM loading is coupled to Browser/History/Favorites source state.
- Current title and artwork path handling is inconsistent and sometimes unsanitized.
- Metadata path rules are duplicated and not always storage-prefix neutral.
- History/favorites use mutable paths as identity.
- PNG decoding is single-flight and unsuitable for parallel cover loads.
- Large metadata ZIPs have no explicit allocation cap.
- USB `send-file` validation can truncate/create a destination before rejecting a transfer.
- SC64 low-level busy waits have no timeout.
- Fast-reboot unsupported error path leaks one cloned `path_t`.

These should be tracked, but the first Aurora64 UI slice should avoid broad upstream refactors. The safest sequence is: add a minimal home view, preserve the browser fallback, prove selection through the existing launch path, then extract library/metadata concerns behind narrow interfaces.
