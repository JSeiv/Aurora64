# Aurora64 Phase 2 Static Library, Details, and Launch-Proof Plan

> **For the controller:** Execute this plan with subagent-driven development, one task at a time. Give each implementer only its task plus the scope and ownership rules below. Implementers do not commit. The controller reviews the complete diff and evidence, and creates one controller-owned commit only after every gate passes. Do not push.

**Goal:** Preserve the Phase 1 six-category Home while adding a separately feature-gated, temporary six-game static library reached from Home's existing `All Games` card. A library card opens the existing `MENU_MODE_LOAD_ROM` details/confirmation view through an explicit owned path. Only a later, separately authorized press of A in that existing details view may exercise the stock launch path.

**Architecture:** Add an independent `MENU_MODE_STATIC_LIBRARY` 3x2 view. It owns only an in-session selected index; its temporary read-only fixture labels and relative paths are isolated in its source file for later replacement. Add one explicit pending-ROM-path handoff to the existing Load ROM view and an explicit validated details return mode. Never manufacture a Browser entry or directory. Preserve loader source precedence and all established Browser, History, Favorites, autoload, Datel-editor, error, and boot behavior. UI/details changes may prepare the existing protected launch path, but this phase does not modify that path.

**Target:** Original N64 with 4 MiB Jumper Pak, SummerCart64 firmware v2.20.2, and the six already verified SD fixtures below. The user does not have an Analogue 3D; that platform is explicitly deferred.

**Baseline:** `/Users/josh/Developer/Aurora64`, branch `aurora64-v0.3.2`, HEAD `c3aeeeb99764d891c6ff3fa383f9640ae3759b50`. Phase 1 Home is committed and physically verified. The only baseline-untracked files are `Aurora64_Project_Brief.docx` and `Aurora64_Project_Brief.pdf`; leave both untouched and untracked.

---

## 1. Exact scope, fixtures, and non-negotiable invariants

### 1.1 Exact read-only fixture table

The table below is launch-proof fixture data, not a general library database. Keep spelling, punctuation, Unicode, extensions, and case exact. The first directory really is misspelled `Favorties`; do not correct it and do not alter the SD card.

| Index | Card label | Exact storage-relative path passed to `path_init()` | Verified size |
| ---: | --- | --- | ---: |
| 0 | `Banjo-Kazooie` | `/⭐ Favorties/Banjo-Kazooie (U) (!).v64` | 16 MiB |
| 1 | `The Legend of Zelda: Ocarina of Time` | `/⭐ Favorties/Legend of Zelda, The - Ocarina of Time (U) (V1.2) [!].v64` | 32 MiB |
| 2 | `Mario Kart 64` | `/⭐ Favorties/Mario Kart 64 (U) [!].z64` | 12 MiB |
| 3 | `Tony Hawk's Pro Skater 2` | `/⭐ Favorties/Tony Hawk's Pro Skater 2 (U) [!].z64` | 16 MiB |
| 4 | `Mario Party 3` | `/Nintendo/Mario Party 3 (U) [!].z64` | 32 MiB |
| 5 | `Paper Mario` | `/Nintendo/Paper Mario (U) [!].z64` | 40 MiB |

Rules:

- Store only the relative strings above in the temporary static table. At selection time create the owned full path with `path_init(menu->storage_prefix, (char *)fixture.path)`. This safely preserves `sd:/` (or another active storage prefix) and relies on `path_push()` to remove one leading slash. Do not concatenate `"sd:/"`, use `sprintf`, or bypass `menu->storage_prefix`.
- `path_init()` returns a heap-owned `path_t *`. The explicit pending-path helper takes ownership as documented below. Every replacement, error, B return, ordinary menu shutdown, and repeated round trip must have one matching `path_free()`; no caller may use the pointer after handing it off.
- Do not enumerate, rename, move, rewrite, normalize, or create fixture files/directories. Do not “fix” `Favorties`.
- Keep the static table under a conspicuous comment such as `TEMPORARY AURORA64 LAUNCH-PROOF FIXTURES`; no other view may depend on it. It is deliberately replaceable by a later real library model/scanner.

### 1.2 Feature flags and behavior matrix

Add this exact numeric Make switch next to `AURORA64_HOME`:

```make
AURORA64_LAUNCH_PROOF ?= 0
N64_CFLAGS += -DFEATURE_AURORA_LAUNCH_PROOF_ENABLED=$(AURORA64_LAUNCH_PROOF)
```

It is always numeric and unquoted. Use `#if FEATURE_AURORA_LAUNCH_PROOF_ENABLED` or, for Home routes that require both features, `#if FEATURE_AURORA_HOME_ENABLED && FEATURE_AURORA_LAUNCH_PROOF_ENABLED`; never use `#ifdef` for either Aurora flag.

| Build | Expected startup/reachability | Home `All Games` | Library/details route |
| --- | --- | --- | --- |
| no overrides (default) | Browser-first | unreachable | unreachable; exact Phase 1 default behavior preserved |
| `HOME=0 PROOF=0` | Browser-first | unreachable | unreachable |
| `HOME=1 PROOF=0` (home-only) | Phase 1 Home | exact `All Games\n(Placeholder)`, inert A, action bar `Placeholder` | unreachable |
| `HOME=0 PROOF=1` (proof-only safety) | Browser-first | unreachable | no new public route; Browser fallback unchanged |
| `HOME=1 PROOF=1` (on) | Home | label `All Games` and truthful `A: Open static library` | A opens library; no launch occurs on Home itself |

Do not change `AURORA64_HOME ?= 0`. A default/off build must preserve Phase 1 behavior, not merely compile.

### 1.3 Exact enabled navigation

- Home remains six category cards in the existing row-major order. Only `All Games` changes under the combined gate.
- A on enabled `All Games` plays `SFX_ENTER` and enters `MENU_MODE_STATIC_LIBRARY`. Home never creates a path, enters Load ROM, or launches a game.
- The static library is six cards in a 3x2 row-major grid, with clamped movement and the exact labels/table order above.
- Library selection starts at zero only for zero-initialized state and is repaired to zero only if invalid. It persists through Library -> Details -> B -> Library and Library -> Home -> Library.
- Library A creates the prefixed owned fixture path, submits it through the explicit helper, plays `SFX_ENTER`, and enters `MENU_MODE_LOAD_ROM`.
- Library B plays `SFX_EXIT` and returns to Home without resetting either Home or library selection.
- Library action bars are truthful: `A: View details` and `B: Home`. Details reached from it says `B: Library`; existing callers retain `B: Back` and return Browser.
- Details A remains the stock `A: Load and run ROM` behavior. It is not pressed during the menu/details-only stage.
- Details B from the static library releases details-owned path/metadata/boxart state and returns to `MENU_MODE_STATIC_LIBRARY`; repeated cycles retain no allocation from the prior details visit.
- Existing Browse Files, Browser traversal/root return, History, Favorites, first-run Credits, and Home behavior outside `All Games` remain unchanged.

### 1.4 Protected boundaries

There must be no diff in:

- `src/boot/**`;
- `src/flashcart/**`, including SC64 behavior;
- `src/menu/cart_load.c` or `src/menu/cart_load.h`;
- save detection, save naming, save staging/writeback, bookkeeping formats, or `boot_params_t`;
- deployed SD menu or fixture contents.

Do not call `cart_load_n64_rom_and_save()`, a flashcart API, save API, or boot API from Home or the static library. The only launch remains the existing details-view A path. Do not spoof `menu->browser.directory`, `menu->browser.entry`, `menu->browser.list`, selection, or reload state.

Expected implementation files are limited to:

- `Makefile`;
- `src/menu/menu_state.h`;
- `src/menu/menu.c`;
- `src/menu/views/views.h`;
- `src/menu/views/home.c`;
- `src/menu/views/static_library.c` (new);
- `src/menu/views/load_rom.c`.

If implementation proves another source file necessary, stop for controller approval rather than silently widening scope. The Project Brief files are never in scope.

---

## 2. State and ownership design

Implement these concepts additively in `menu_t`; names may be adjusted only for repository style, not semantics:

```c
struct {
    int32_t selected;
} static_library;

struct {
    path_t *rom_path;             /* existing active details/launch path */
    path_t *pending_rom_path;     /* new, owned, not Browser state */
    bool pending_rom_path_set;    /* distinguishes explicit NULL from no request */
    menu_mode_t pending_return_mode;
    menu_mode_t return_mode;
    bool resume_from_datel;
    /* existing rom_info, disk slots, IDs, flags remain */
} load;
```

Declare the handoff in `src/menu/views/views.h` beside Load ROM:

```c
/**
 * Replace the explicit pending ROM path used by the next Load ROM init.
 * Takes ownership of rom_path, including when later validation fails.
 */
void view_load_rom_set_pending_path(menu_t *menu, path_t *rom_path, menu_mode_t return_mode);
```

### 2.1 Helper contract

`view_load_rom_set_pending_path()` must:

1. free any prior `menu->load.pending_rom_path` before replacement;
2. take unconditional ownership of the supplied pointer (including `NULL`);
3. set `pending_rom_path_set = true` even when the supplied pointer is `NULL`, so an explicit invalid request cannot fall through to another source;
4. store a validated pending return mode: `MENU_MODE_STATIC_LIBRARY` is accepted only under the combined Home/proof gate; every other value becomes `MENU_MODE_BROWSER`;
5. clear `load_history_id` and `load_favorite_id` to `-1`, making the explicit request authoritative over stale source selectors;
6. not modify Browser state, active `rom_path`, `next_mode`, launch flags, or protected code;
7. not load metadata or test the file. Load ROM init remains the single details loader and handles a missing fixture with its existing error UI.

The library performs the helper call before assigning `MENU_MODE_LOAD_ROM`. It never frees the handed-off pointer.

### 2.2 Source selection and fail-closed behavior

Refactor `view_load_rom_init()` around one explicit source resolver, preserving this precedence:

1. **startup autoload:** when the existing compile-time autoload feature is present, recognize this source only when runtime autoload is enabled **and** Startup has prepared a value-bearing active `rom_path` with `menu->load_pending.rom_file == true`. Retain that path and existing immediate-load behavior. Runtime `rom_autoload_enabled` alone is not a source discriminator because the option can be enabled from an ordinary details screen;
2. **Datel details resume:** consume `resume_from_datel` only when an active `rom_path` already exists; reuse the existing details resources rather than cloning/reloading/reallocating them;
3. **explicit pending path:** when `pending_rom_path_set` is true, consume and clear the discriminator, move (do not clone) `pending_rom_path` into `rom_path`, immediately null the pending field, and adopt its validated pending return mode. A moved `NULL` or empty path is an authoritative error and must not fall through;
4. **History:** a non-`-1` history ID must be range-valid, have `BOOKKEEPING_TYPE_ROM`, and point to a value-bearing primary path, then clone it;
5. **Favorites:** a non-`-1` favorite ID must be range-valid, have `BOOKKEEPING_TYPE_ROM`, and point to a value-bearing primary path, then clone it;
6. **Browser:** only when `browser.directory`, `browser.entry`, and `browser.entry->name` are all valid, use the existing `path_clone_push()` behavior.

For autoload/history/favorite/Browser, set the details return mode to Browser. This preserves all existing callers. A selected explicit/history/favorite source is authoritative: if it is invalid, report an error rather than silently falling through to Browser or another source.

When `FEATURE_AUTOLOAD_ROM_ENABLED` is compiled, the existing `Set ROM to autoload` action must derive both directory and filename from the validated active `menu->load.rom_path`, never from `menu->browser.directory` or `menu->browser.entry`. Clone the active path, duplicate its last component, pop the clone to obtain the parent, strip only the filesystem prefix expected by settings, then save the two owned settings strings and release the temporary clone. Fail closed on a missing active path or allocation failure. This preserves Browser-origin behavior while making Static Library details safe and prevents stale Browser state from selecting the wrong game. Enabling autoload from ordinary details must not set `menu->load_pending.rom_file` or cause a later Datel return to be misclassified as startup autoload.

Before resolving a fresh non-Datel source, release any old active ROM metadata/path. If no valid source exists, if a selected ID is invalid, if the explicit path is null/empty, or if `rom_config_load()` fails:

- release any partially active metadata/path;
- clear both load IDs to `-1` where the existing failure path does so;
- clear/resanitize return state to Browser;
- call `menu_show_error()` with a stable existing/error-conversion message;
- leave `rom_path == NULL` and fail closed;
- rely on the unchanged Error view's B -> Browser fallback.

Never dereference Browser state merely because no other pointer was found. This is the key “no spoofing and no source” safety property.

### 2.3 Return-mode validation

Use one private validator in `load_rom.c`. It permits only:

- `MENU_MODE_STATIC_LIBRARY` when both Aurora feature values are 1;
- `MENU_MODE_BROWSER` in every build.

Anything else, including zero-initialized `MENU_MODE_NONE`, becomes Browser. Validate when accepting a pending path and again before B uses the active return mode. This ensures stale/corrupt state cannot route into Boot, Datel, Error, or an unavailable feature.

### 2.4 Teardown lifecycle

Split visual/details teardown from source selection clearly enough to enforce these cases:

- **B to Browser or static library:** close context-menu/overlay state as needed, free boxart, reset metadata-image caches/indexes, call `rom_info_free_meta()`, free `rom_path`, null pointers, clear IDs, clear resume state, and reset return modes to Browser. No details allocation survives.
- **loader/config failure -> Error:** perform the same active-details release, clear `menu->load_pending.rom_file`, and clear any consumed pending discriminator before showing Error. Error B remains Browser. Only a successfully resolved startup autoload may retain the immediate-launch flag until Load ROM display consumes it.
- **Load ROM -> Datel editor:** set `resume_from_datel = true` before transition and do not free path, metadata, boxart, or details context. On Datel -> Load ROM, consume the flag and reuse the active details state without a second `rom_config_load()` or boxart allocation. Existing apply/cancel/save-confirm editor routes remain valid.
- **successful Load ROM -> Boot:** do not free the active ROM path, ROM metadata, or boot inputs before stock cart loading and boot handoff have finished. Preserve current Boot transition semantics.
- **menu shutdown after Boot or any exit:** in `menu_deinit()`, after existing users such as `hdmi_send_game_id()` are finished, free active ROM metadata/path and the new pending path exactly once, clear `pending_rom_path_set`, and clear `menu->load_pending.rom_file`. Null-safe `path_free()` is already supported. This also covers shutdown with an unconsumed pending path.

Do not call the teardown branch simply because `next_mode != MENU_MODE_LOAD_ROM`; keep explicit exceptions for Datel and Boot. Confirm context-menu and static boxart pointers cannot refer to freed objects. A twenty-round-trip test is required because “eventually freed at menu shutdown” is not acceptable for B returns.

---

## 3. Implementation tasks

### Task 1: Preflight and add the numeric feature switch

**Files:** `Makefile`

1. Run:

   ```sh
   cd /Users/josh/Developer/Aurora64
   git status --short --branch
   git rev-parse HEAD
   git branch --show-current
   git check-attr eol -- Makefile src/menu/menu_state.h src/menu/views/home.c
   ```

   Stop if HEAD/branch differ, tracked changes exist, or untracked files exceed the two Project Briefs plus this plan.

2. Add `AURORA64_LAUNCH_PROOF ?= 0` immediately after `AURORA64_HOME ?= 0` and append the exact numeric compiler define. Keep CRLF line endings.
3. Add `menu/views/static_library.c` once in `SRCS`, adjacent to Home.
4. Check that deployment targets (`run-debug-upload`, `send-file`, SC64 recipes) have no diff.
5. Run `git diff --check -- Makefile` and review the exact diff. Hand off without committing.

### Task 2: Add the mode, state, declaration, registry, and shutdown ownership

**Files:** `src/menu/menu_state.h`, `src/menu/views/views.h`, `src/menu/menu.c`

1. Add `MENU_MODE_STATIC_LIBRARY` immediately after `MENU_MODE_HOME`; preserve other enum order.
2. Add the selected-index state and the pending/return/resume fields described in section 2. Do not place fixture strings or Browser aliases in `menu_t`.
3. Add `view_static_library_init()` / `view_static_library_display()` declarations and the exact owned-path helper declaration/documentation.
4. Register the static library view after Home and before Browser. Registration may be unconditional; routing remains feature-gated.
5. Extend `menu_deinit()` to free `pending_rom_path` and active ROM metadata/path exactly once. Preserve `hdmi_send_game_id()` ordering and Boot handoff. Consolidate the existing `path_free(menu->load.rom_path)` rather than duplicating it.
6. Static acceptance:
   - one mode, one registry entry, one state block, one helper declaration;
   - pending and active paths each have a shutdown free;
   - no dispatcher-loop, fault-route, boot, flashcart, or save diff;
   - `git diff --check` passes.

### Task 3: Implement the isolated static library view

**Files:** `src/menu/views/static_library.c` (new)

1. Use the same proven Phase 1 card geometry and drawing primitives from `home.c`: 3 columns, 2 rows, `168x132` outer cards, same gaps/origin/padding/border/focus marker and safe-area arithmetic. Do not create assets or shared UI primitives.
2. Define a private immutable fixture struct/table with `label` and `relative_path`, exactly matching section 1.1. Add compile-time count consistency (for example one `STATIC_LIBRARY_CARD_COUNT` used by table, movement, validation, and loop).
3. Implement clamped input with at most one action per frame in Up, Down, Left, Right, A, B precedence. Play cursor sound only for a real move, enter once for A, and exit once for B.
4. On A:

   ```c
   path_t *path = path_init(menu->storage_prefix, (char *)launch_proof_fixtures[menu->static_library.selected].relative_path);
   view_load_rom_set_pending_path(menu, path, MENU_MODE_STATIC_LIBRARY);
   menu->next_mode = MENU_MODE_LOAD_ROM;
   ```

   The cast is needed because the repository's exact `path_init(const char *prefix, char *string)` API is not const-correct even though `path_push()` does not mutate the fixture bytes. Do not use `path_create()` with an unprefixed SD path.
5. On B, return Home. No path should be allocated for B or mere rendering.
6. Render labels with fixed stack text parameters, and advertise exactly `A: View details` and `B: Home`. No card says “launch”; the library itself does not launch.
7. `view_static_library_init()` repairs an index outside `[0, 5]` to zero and otherwise preserves it.
8. Static rejection grep must show no Browser mutation, cart load, flashcart, save, boot, settings, bookkeeping, filesystem write, PNG decode, or direct `malloc/calloc/realloc/strdup` calls. `path_init()` through the helper is the sole owned allocation route.
9. Explicitly review the untracked file with `git diff --no-index --check -- /dev/null src/menu/views/static_library.c` and a full no-index diff; expected valid-difference status is 1. No commit.

### Task 4: Gate Home's existing All Games card without changing other cards

**Files:** `src/menu/views/home.c`

1. Keep the six-card category Home and exact row-major positions.
2. Under the combined numeric gate only, card index 5 displays `All Games`; otherwise retain the exact Phase 1 literal `All Games\n(Placeholder)`.
3. Under the combined gate, A on index 5 plays one enter sound and enters `MENU_MODE_STATIC_LIBRARY`. Index 0 still enters Browser. Indices 1-4 stay inert. In every other flag combination, index 5 remains inert.
4. Make action text truthful:
   - index 0: `A: Browse files`;
   - enabled index 5: `A: Open static library`;
   - every placeholder: `Placeholder`.
5. Home must not include `path.h`, allocate a path, mention `MENU_MODE_LOAD_ROM`, or set launch pending state. There is no launch from Home.
6. Confirm Phase 1 Browser-root B, selected Home card preservation, labels 1-4, layout, and input behavior have no semantic diff.

### Task 5: Add the explicit Load ROM source and return lifecycle

**Files:** `src/menu/views/load_rom.c`

Implement section 2 in small internal helpers rather than one dense init block:

1. A private `validate_return_mode()` with the exact two allowed outcomes.
2. Public `view_load_rom_set_pending_path()` with replacement/free and unconditional ownership.
3. A private active-details release helper that frees meta/path and nulls/reset state; keep boxart/cache/context cleanup coordinated.
4. A private source resolver implementing autoload -> Datel resume -> explicit pending -> History -> Favorites -> Browser precedence and all validity checks.
5. Refactor `view_load_rom_init()` so a Datel resume performs no duplicate metadata/boxart allocation, while a fresh source loads metadata once.
6. In `set_menu_next_mode()`, set `resume_from_datel` only for its existing Datel transition. Do not set it for B, Error, Boot, or any future arbitrary mode.
7. B chooses validated `return_mode`; update details action text to `B: Library` only when that validated mode is the static library, otherwise retain `B: Back`.
8. On leaving details, release resources for B/Error/other non-Boot, non-Datel exits; preserve successful Boot and Datel resources. Ensure the `rom_filename` static pointer is nulled whenever its owning path is freed.
9. Refactor the compile-time autoload option action to derive its saved directory and filename from a validated active `menu->load.rom_path`; never dereference Browser state for this action. Verify Browser-origin details still save the same values, Static Library details save the selected fixture rather than a stale Browser entry, and enabling the option does not mark the current details visit as startup autoload.
10. Every ordinary details teardown and every source/config init failure clears `menu->load_pending.rom_file`; only a successfully resolved startup autoload retains it until the existing display path consumes it.
11. Keep existing A/load code, progress callback, `cart_load_n64_rom_and_save()`, history addition, boot parameter setup, cheats, other save/configuration options, and protected modules textually/semantically unchanged.
12. Failure tests must prove an explicitly set `NULL` path, an explicitly set empty path, and a missing nonempty path each reach Error and Error B reaches Browser without falling through to stale History/Favorites/Browser state.

Acceptance review must trace all of these caller cases on paper from assignment through cleanup:

- autoload startup;
- Static Library details -> enable autoload -> Datel -> details, without startup-autoload misclassification;
- Browser details -> enable autoload, preserving the existing persisted directory/filename values;
- Browser ROM;
- History ROM;
- Favorite ROM;
- static library ROM;
- details -> Datel -> details -> B;
- details -> Datel -> details -> successful Boot;
- `rom_config_load()` error;
- no source at all;
- pending path replaced before consumption;
- menu shutdown with active path and with pending path.

### Task 6: Format, static review, and clean build matrix

1. Review scope before formatting, including the no-index diff for the new file. Project Brief timestamps/status must remain unchanged.
2. Run repository formatting in the pinned container:

   ```sh
   docker run --platform linux/amd64 --rm \
     -v "$PWD":/work -w /work aurora64-dev:v0.3.2 \
     bash -lc 'set -e; make format'
   ```

   Restore unrelated formatting churn. Confirm all touched Make/C/H files remain CRLF, matching repository attributes.

3. Clean-build and hash all six configurations separately; copy each artifact outside the repository before the next clean:

   ```sh
   # Repeat with the argument strings below.
   docker run --platform linux/amd64 --rm \
     -v "$PWD":/work -w /work aurora64-dev:v0.3.2 \
     bash -lc 'set -e; make clean; make all BUILD_ARGS; sha256sum output/N64FlashcartMenu.n64'
   cp output/N64FlashcartMenu.n64 /tmp/ARTIFACT_NAME.n64
   shasum -a 256 /tmp/ARTIFACT_NAME.n64
   ```

   | Case | Replace `BUILD_ARGS` with | Artifact |
   | --- | --- | --- |
   | default | *(nothing)* | `/tmp/aurora64-phase2-default.n64` |
   | explicit off | `AURORA64_HOME=0 AURORA64_LAUNCH_PROOF=0` | `/tmp/aurora64-phase2-off.n64` |
   | home-only | `AURORA64_HOME=1 AURORA64_LAUNCH_PROOF=0` | `/tmp/aurora64-phase2-home-only.n64` |
   | proof-only safety | `AURORA64_HOME=0 AURORA64_LAUNCH_PROOF=1` | `/tmp/aurora64-phase2-proof-only.n64` |
   | on | `AURORA64_HOME=1 AURORA64_LAUNCH_PROOF=1` | `/tmp/aurora64-phase2-on.n64` |
   | on + autoload compile coverage | `AURORA64_HOME=1 AURORA64_LAUNCH_PROOF=1 FLAGS=-DFEATURE_AUTOLOAD_ROM_ENABLED` | `/tmp/aurora64-phase2-on-autoload.n64` |

   The autoload build is required compile/control-flow coverage for the refactored conditional branch; it is not an additional hardware scope. Review a configured runtime autoload from Startup's prepared path plus immediate-load flag through successful metadata load, the existing display consumption, cart-load request, and Boot handoff. Also review a config/source failure and prove it clears the immediate-load flag and cannot later launch stale state. With a disposable configuration or dedicated harness—not the user's live settings—verify that Browser-origin details persist the same path as upstream, Static Library details persist the selected active fixture rather than stale Browser state, and Details -> Datel -> Details after enabling autoload remains a Datel resume rather than startup autoload.

4. Build a missing-path negative candidate without touching SD: temporarily change only one static fixture string in the worktree to `/Aurora64 Phase 2 deliberately missing/fixture.z64`, clean-build both flags on, copy to `/tmp/aurora64-phase2-missing.n64`, record its hash, then restore the exact verified source table before any review/hardware run. Confirm `git diff` contains no missing-path string afterward. Never commit or hardware-launch this negative candidate.
5. Run `git diff --check`; check CRLF; inspect `git diff --stat`, `git status --short`, and protected-boundary diffs. Grep every Aurora gate for numeric `#if` and every ownership field for create/move/free sites.
6. Confirm no generated outputs are staged and the two Project Briefs remain untouched/untracked.

---

## 4. Independent reviews before hardware

These are two separate reviewer assignments after all builds pass, not self-review labels.

### Review A: independent specification review

Give a reviewer this plan, the complete tracked diff, the full no-index new-file diff, and build matrix. Require a requirement-by-requirement PASS/FAIL report covering fixtures, spelling, storage prefix, feature matrix, routes, labels/action bars, ownership, precedence, error fallback, 4 MiB scope, and protected files. Any FAIL returns to implementation and requires a fresh full matrix.

### Review B: independent quality/safety review

Give a different reviewer exact source plus Review A. Require manual lifetime/control-flow analysis for replacement, missing source, metadata errors, 20 returns, Datel round trip, and Boot handoff. Require checks for null/stale Browser dereferences, double-free/use-after-free, metadata/boxart leaks, enum validation, preprocessor mistakes, and accidental save/SC64 changes. Reviewer must return `APPROVED` or concrete findings. After fixes, both independent reviews are rerun before hardware.

The controller records reviewer identities, revisions reviewed, commands, findings, and approvals. No commit yet.

---

## 5. Hardware verification, staged by risk

### Stage 1: Static/code/build review

This stage is complete only when Tasks 1-6 and both independent reviews pass. It authorizes menu/details-only testing, not a game launch.

Record branch/HEAD, dirty diff hash or patch, toolchain/container identity, all artifact sizes/SHA-256 values, warnings, reviewer evidence, exact N64/SC64/controller/video setup, firmware v2.20.2, and confirmation of the 4 MiB Jumper Pak.

### Stage 2: Volatile menu/details-only test (`--no-writeback`, absolutely no game launch)

1. Use original N64 + 4 MiB Jumper Pak + SC64 v2.20.2. Do not hot-plug SD, cart, controller, or Pak. Start the documented macOS proxy:

   ```sh
   cd /Users/josh/Developer/Aurora64
   ./tools/sc64/sc64deployer list
   ./tools/sc64/sc64deployer info
   ./tools/sc64/sc64deployer server 0.0.0.0:9064
   ```

2. Volatile-upload one candidate at a time. Never run `make run-debug-upload`, never use `send-file /sc64menu.n64`, and never replace the persistent SD menu:

   ```sh
   docker run --platform linux/amd64 --rm \
     -v /tmp:/host-tmp aurora64-dev:v0.3.2 \
     sc64deployer --remote host.docker.internal:9064 \
     upload /host-tmp/aurora64-phase2-on.n64
   ```

3. Before first execution, byte-read back the exact artifact size from cart-ROM offset zero, then compare bytes and hashes. Substitute the recorded size rather than assuming the prior Phase 1 size:

   ```sh
   SIZE=$(stat -f %z /tmp/aurora64-phase2-on.n64)
   ./tools/sc64/sc64deployer --remote 127.0.0.1:9064 \
     dump 0 "$SIZE" /tmp/aurora64-phase2-on-readback.n64
   cmp -s /tmp/aurora64-phase2-on.n64 /tmp/aurora64-phase2-on-readback.n64
   shasum -a 256 /tmp/aurora64-phase2-on.n64 /tmp/aurora64-phase2-on-readback.n64
   ```

4. Execute menu debugging only:

   ```sh
   docker run --platform linux/amd64 --rm \
     aurora64-dev:v0.3.2 \
     sc64deployer --remote host.docker.internal:9064 \
     debug --no-writeback --init reboot
   ```

   **`--no-writeback` does not validate game saves.** It is used only for menu/details navigation, and no A press is permitted on details in this stage. Do not claim this stage proves save creation, loading, retention, standalone writeback, or SD byte identity. Existing menu initialization may perform its already documented incidental setup writes; avoid intentional settings/bookkeeping/delete/extract actions.

5. Enabled candidate acceptance on physical hardware:
   - Home remains six cards; `All Games` is no longer marked placeholder and advertises the static library.
   - A on Home `All Games` opens the six-game 3x2 view but does not launch.
   - all exact labels are readable, focus is clamped, movement/sounds are correct, B returns Home, and both Home/library selections persist;
   - each of the six A presses opens the existing details view with the matching filename/title/size-derived metadata as applicable; `.v64` and `.z64` fixtures both load details;
   - details action bar says `B: Library`; B returns to the same selected game;
   - **never press A on details**;
   - exercise extra/advanced info and image navigation read-only, but do not change options or open Datel during this risk stage unless separately included in a disposable/no-write review;
   - complete at least 20 Library -> Details -> B -> Library round trips, alternating fixtures and including 40 MiB Paper Mario, with no hang, corruption, stale details, input loss, allocation-related degradation, or slowdown;
   - Browse Files fallback remains unchanged: Home card 0 enters Browser, normal file details still B-return Browser, non-root B pops one level, root B returns Home under `AURORA64_HOME=1`.

6. Missing-path negative candidate: volatile upload, byte-readback, and run `/tmp/aurora64-phase2-missing.n64` under the same `--no-writeback` discipline. Select only the deliberately missing card. It must show Error, never stale details and never launch; Error B must return Browser per the existing fallback. Confirm another real fixture still opens correctly after returning through Home. Never press details A.
7. Flag matrix behavior:
   - default/off: Browser-first and no new route;
   - home-only: exact Phase 1 placeholder/inert All Games behavior;
   - proof-only: Browser-first, no reachable library;
   - on: full static route above.
   Byte-readback every artifact actually executed.
8. Power off fully and normally. Ordinary power-on must recover the unchanged stock SD menu and working controller. If debug reboot causes the previously observed controller/PIF hiccup, compare stock after full power-off/reseat while off; do not misattribute a shared baseline issue.
9. Record photos/video and PASS/FAIL evidence. Any unexpected write, crash, unique input failure, wrong path/title, or ownership symptom blocks Stage 3.

### Stage 3: Separate launch/save readiness gate before any details A press

Stage 2 passing does **not** authorize launch. Hold a visible controller checkpoint on the details screen and obtain explicit controller/user approval only after every item below is documented:

1. Rebuild/read back the exact approved on artifact from clean exact source; the temporary missing candidate is prohibited.
2. Establish an authoritative, save-capable execution procedure in which SC64 standalone save writeback is enabled. Do not use `debug --no-writeback` for any launch/save claim. Do not replace `/sc64menu.n64` unless a separate release decision explicitly authorizes that unrelated persistent deployment.
3. Record official SummerCart64 guidance: during normal standalone operation, save writeback occurs automatically about one second after the game finishes saving. Treat that as the product fact, not permission to power off immediately; follow the current official SC64 procedure and capture its source/version at execution time.
4. Inventory the chosen game's actual save type, expected save filename/size, existing save/Pak state, and rollback path. Back up and hash every potentially affected cart save and Controller Pak before testing. Use a disposable save/Pak or no personal data. If backup/restore cannot be proven, the save test is blocked.
5. Confirm fixture legality/hash/byte order and enough SD free space; photograph/record SC64 firmware, N64, Jumper Pak, controller/Pak, artifact, and pre-test save state.
6. Separate the first launch proof from save acceptance:
   - preferred first launch is `Tony Hawk's Pro Skater 2` as a no-cart-save launch proof, with no personal Controller Pak inserted (or only a disposable Pak), or another explicitly approved disposable fixture;
   - press A once on details only after approval, verify stock loader/boot handoff and gameplay, then follow official reset/return/power timing;
   - this proves launch handoff only, not save correctness.
7. Save acceptance is a later explicit sub-gate. Use only a disposable/fully backed-up fixture and save. Make a uniquely observable in-game change, let the game finish saving, observe the official approximately-one-second standalone automatic writeback requirement plus the current official safe-return procedure, then power-cycle and verify reload and expected file/hash behavior. Restore/compare backups as prescribed.
8. Never authorize risk to personal Ocarina of Time, Paper Mario, Mario Party, or other personal saves merely to complete Phase 2. A blocked save test is preferable to unbacked risk.
9. On any wrong save type/path/size, missing backup, failed writeback, SC64 anomaly, or uncertainty, stop, power down only per official procedure, preserve evidence, recover from backup, and do not broaden source scope into protected save/flashcart code.

Analogue 3D is `DEFERRED — hardware unavailable`, not PASS, FAIL, or inferred compatibility.

---

## 6. Final evidence and controller commit gate

The controller may create one commit only after all of the following are evidenced:

- branch/baseline and Project Brief invariants held;
- default numeric `AURORA64_HOME=0` and `AURORA64_LAUNCH_PROOF=0` behavior is preserved;
- default, explicit-off, home-only, proof-only, on, and on-plus-autoload clean builds pass;
- the six exact fixture paths/sizes and misspelled `Favorties` table are correct and storage-prefixed through `path_init()`;
- Home remains six categories, Browse Files is unchanged, All Games is truthful in every matrix case, and Home never launches;
- library/details routes, selection preservation, B return, Error -> Browser fallback, and missing-path case pass;
- autoload/history/favorites/Browser precedence and Browser returns are preserved;
- explicit pending ownership, replacement, menu shutdown, metadata/boxart cleanup, Datel resume, and Boot handoff pass independent review;
- no allocation is retained across 20 details B returns on original 4 MiB hardware;
- every volatile artifact tested has byte-identical SC64 readback evidence;
- menu/details-only testing used `--no-writeback` without a game launch and makes no save-validation claim;
- stock persistent-menu recovery passes;
- protected boot/flashcart/cart-load/save/SC64 files have empty diffs;
- independent specification and quality reviews approve the final revision;
- any actual launch occurred only after the separate readiness approval, and any save acceptance used disposable/backed-up data and normal standalone writeback—not `--no-writeback`;
- Analogue 3D remains explicitly deferred.

Then run and capture:

```sh
cd /Users/josh/Developer/Aurora64
git diff --check
git diff -- src/boot src/flashcart src/menu/cart_load.c src/menu/cart_load.h
git status --short --branch
git diff --stat
git diff
```

The controller reviews the complete diff, stages only the approved Phase 2 source/plan/evidence files (never the Project Briefs or generated artifacts), creates one descriptive commit, verifies `git status` still shows only the two untouched untracked Project Briefs, and records the commit ID. **Do not push.**
