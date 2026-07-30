# Aurora64 Phase 1 Static Home Implementation Plan

> **For the controller:** Execute this plan with subagent-driven development, one task at a time. Give each implementer only its task plus the scope/protected-boundary rules below, review its diff and verification output, then continue. Implementers must not commit; the controller reviews and commits only after the complete slice passes.

**Goal:** Add a compile-time-feature-flagged, static Aurora64 Home screen with six text-only cards. `Browse Files` enters the unchanged stock Browser; the other five cards are visibly marked placeholders and perform no action.

**Architecture:** Add one independent `MENU_MODE_HOME` view with a one-field Home state: fixed labels live in `home.c`, while only the selected card index lives in `menu_t`. Store an explicit Credits return mode so first-run Credits can return to Home without breaking Browser-launched Menu Information. Gate routes to and from Home with one numeric Make variable that defaults to `0`. Reuse the existing background, layout, box/border, text, action-bar, action, and sound primitives. Do not introduce a library model, scanner, artwork pipeline, launch handoff, persistence, or Home-owned heap memory.

**Tech stack:** C/libdragon, the existing view dispatcher and UI component API, GNU Make, and the existing `aurora64-dev:v0.3.2` linux/amd64 container.

---

## Scope, invariants, and exact behavior

- Work from `/Users/josh/Developer/Aurora64` on branch `aurora64-v0.3.2` after Phase 0 commit `056abe96` and working-title commit `23874a4a`.
- Do not modify or delete the pre-existing untracked `Aurora64_Project_Brief.docx` or `Aurora64_Project_Brief.pdf`.
- Expected implementation files are only:
  - `Makefile`
  - `src/menu/menu_state.h`
  - `src/menu/menu.c`
  - `src/menu/views/views.h`
  - `src/menu/views/home.c` (new)
  - `src/menu/views/startup.c`
  - `src/menu/views/credits.c`
  - `src/menu/views/browser.c`
- Exact build switch:

  ```make
  AURORA64_HOME ?= 0
  N64_CFLAGS += -DFEATURE_AURORA_HOME_ENABLED=$(AURORA64_HOME)
  ```

  `AURORA64_HOME` is an unquoted integer Make variable. The C preprocessor symbol is exactly `FEATURE_AURORA_HOME_ENABLED`, always defined as numeric `0` or `1`, and every gate uses `#if FEATURE_AURORA_HOME_ENABLED` (not `#ifdef`). Default builds therefore remain Browser-first; candidate builds opt in with `AURORA64_HOME=1`.
- Home cards are row-major in a 3-column by 2-row grid. The label table must contain these exact C string literals (each placeholder literal contains exactly one `\n` escape, producing one rendered line break rather than a visible backslash):

  ```c
  "Browse Files",
  "Continue Playing\n(Placeholder)",
  "Favorites\n(Placeholder)",
  "Pearl's Games\n(Placeholder)",
  "Play Together\n(Placeholder)",
  "All Games\n(Placeholder)",
  ```
- Selection starts at index `0` because `menu_t` is zero-initialized and remains in `menu->home.selected` across Home/Browser/Home transitions. `view_home_init()` only repairs an out-of-range value to `0`; it does not reset a valid selection.
- Movement is deliberately **clamped**, not wrapped: Left/Right move one card only when another card exists in the same row; Up/Down move by three only when another row exists. At an outer edge, selection and sound remain unchanged unless another component of a diagonal input is valid. Process at most one valid direction per frame in `Up`, `Down`, `Left`, `Right` precedence so diagonal action flags cannot move twice or become dead at a boundary.
- A on index `0` plays `SFX_ENTER` and sets `menu->next_mode = MENU_MODE_BROWSER`. A on indices `1` through `5` is a complete no-op: no transition, no enter/error sound, no dialog, and no action-bar claim. The cards themselves retain `(Placeholder)` so their status is visible.
- The Home action bar says `A: Browse files` only while card `0` is selected. On placeholder cards it says `Placeholder` and does not mention A. B on Home has no action and is not advertised.
- With the feature enabled, Browser B behavior is unchanged below root; B pops one directory/archive level as today. At filesystem root only, B plays `SFX_EXIT` and returns to `MENU_MODE_HOME`. Its root action-bar text must read `B: Home` as enabled rather than showing the current disabled `B: Back`. With the feature disabled, retain the existing root B no-op and gray `B: Back` presentation exactly.
- Startup priority remains: autoload (when separately compiled/configured), first-run Credits, then normal destination. Change only the normal destination to Home when enabled. Fault routing in `menu_init()` remains untouched. First-run Credits returns to Home when enabled and Browser when disabled; Credits opened from Browser returns to Browser in both configurations.
- Home owns no heap allocation. Do not call `malloc`, `calloc`, `realloc`, `strdup`, `vasnprintf`, path allocation, PNG decode, filesystem enumeration, settings/bookkeeping writes, or any persistence API from `home.c`. Existing text helpers may use their own bounded transient internals; Home itself stores only an integer selection index.
- No artwork/assets, dynamic scanning, metadata, dynamic card allocation, direct ROM launch, details view, favorites/history integration, animation, or persistence belongs in this slice.
- Protected boundaries are out of scope: do not modify `src/boot/**`, `src/flashcart/**`, `cart_load_n64_rom_and_save()`, ROM/save inspection or staging, SC64 commands/configuration, writeback, `boot_params_t`, or boot handoff. Do not route any Home card to `MENU_MODE_LOAD_ROM`.

## Verification strategy (why there is no rendering TDD)

The repository has no test framework (`Makefile` contains only a commented test TODO), and the result is RDP rendering plus controller interaction on N64 hardware. A conventional failing unit test cannot meaningfully assert card geometry, focus visibility, or controller feel without first building a new graphics/input harness, which is outside this static slice. Therefore each task uses code-level acceptance checks, clean builds with both flag values, static grep/diff inspection, and final physical navigation. This is not permission to skip verification: every task must report its commands and observed output to the controller.

Do not run `make run-debug-upload`, `send-file` to `/sc64menu.n64`, launch a ROM, edit settings, delete files, or perform save/writeback tests in this phase. The deployment is volatile and the planned test actions intentionally avoid writes; this is not a claim that normal menu initialization is universally read-only.

---

### Task 1: Add the exact feature flag and compile the new view

**Files:**
- Modify: `Makefile:11-23` (variables/flags)
- Modify: `Makefile:65-80` (`SRCS`)

**Step 1: Confirm preflight identity and scope**

Run:

```sh
cd /Users/josh/Developer/Aurora64
git status --short --branch
git log -2 --oneline
```

Expected: branch `aurora64-v0.3.2`; HEAD `23874a4a`; only the two Project Brief files and, if the controller has not yet committed this plan, `docs/plans/aurora64-phase1-home.md` are untracked. Stop for controller review if tracked changes or other untracked files exist.

**Step 2: Define the numeric feature flag**

Immediately after `MENU_VERSION ?= ...`, add:

```make
AURORA64_HOME ?= 0
```

Append the exact unquoted define to the existing `N64_CFLAGS` assignment (or on the immediately following line with `N64_CFLAGS +=`):

```make
N64_CFLAGS += -DFEATURE_AURORA_HOME_ENABLED=$(AURORA64_HOME)
```

Do not quote `0`, `1`, the Make variable, or the macro value. Do not reuse `FEATURE_AUTOLOAD_ROM_ENABLED`.

**Step 3: Register the source with the build**

Add `menu/views/home.c` next to the other view sources in `SRCS`. Do not add images, fonts, or generated assets.

**Step 4: Perform static acceptance checks**

Run:

```sh
grep -nE 'AURORA64_HOME|FEATURE_AURORA_HOME_ENABLED|menu/views/home\.c' Makefile
git diff --check -- Makefile
git diff -- Makefile
```

Acceptance:
- the default is exactly numeric `0`;
- the compiler define has no escaped quotes;
- `home.c` appears exactly once in `SRCS`;
- no deployment target changed.

**Step 5: Hand off without committing**

Report the diff and checks. Do not run `git add` or `git commit`; the controller retains commit ownership after the full slice.

---

### Task 2: Add Home mode/state, declarations, and dispatcher registration

**Files:**
- Modify: `src/menu/menu_state.h:24-51` and `:90-147`
- Modify: `src/menu/views/views.h:21-50`
- Modify: `src/menu/menu.c:181-205`

**Step 1: Add additive state**

Add `MENU_MODE_HOME` immediately after `MENU_MODE_STARTUP` and before `MENU_MODE_BROWSER`. Keep all existing enum names and ordering otherwise unchanged.

Add this state to `menu_t` near the action/browser state:

```c
struct {
    int32_t selected;
} home;
```

Do not store labels, pointers, paths, allocation state, or a return-mode field here.

**Step 2: Declare the view interface**

Add documented prototypes following the existing view style:

```c
void view_home_init(menu_t *menu);
void view_home_display(menu_t *menu, surface_t *display);
```

**Step 3: Register the view**

In `menu_views[]`, add:

```c
{ MENU_MODE_HOME, view_home_init, view_home_display },
```

Place it after Startup and before Browser. Register it in both builds; the feature flag gates routing, not compilation or registry validity.

**Step 4: Check structural acceptance**

Run:

```sh
grep -n 'MENU_MODE_HOME\|home;\|view_home_' src/menu/menu_state.h src/menu/views/views.h src/menu/menu.c
git diff --check -- src/menu/menu_state.h src/menu/views/views.h src/menu/menu.c
git diff -- src/menu/menu_state.h src/menu/views/views.h src/menu/menu.c
```

Acceptance: exactly one enum entry, one state block, two declarations, and one registry entry exist; no dispatcher loop, `menu_init()` fault route, `menu_deinit()`, or boot transition changed.

**Step 5: Hand off without committing**

Report results to the controller. No direct commit commands.

---

### Task 3: Implement the allocation-free static Home view

**Files:**
- Create: `src/menu/views/home.c`

**Step 1: Define fixed data and geometry**

Create `home.c` using `views.h`, `../sound.h`, and `../ui_components/constants.h`. Use this exact geometry; do not leave row count, interior bounds, padding, text box, or text origin implicit:

```c
#define HOME_CARD_COUNT   6
#define HOME_COLUMNS      3
#define HOME_ROWS         2
#define HOME_CARD_WIDTH   168
#define HOME_CARD_HEIGHT  132
#define HOME_CARD_GAP_X   16
#define HOME_CARD_GAP_Y   16
#define HOME_CARD_X       52
#define HOME_CARD_Y       82
#define HOME_CARD_PADDING_X  12
#define HOME_CARD_PADDING_Y  12
#define HOME_TEXT_OFFSET_Y   1
#define HOME_FOCUS_WIDTH     4
#define HOME_FOCUS_INSET_Y   8
```

For card index `i`, calculate `row = i / HOME_COLUMNS`, `column = i % HOME_COLUMNS`, `outer_x0 = HOME_CARD_X + column * (HOME_CARD_WIDTH + HOME_CARD_GAP_X)`, `outer_y0 = HOME_CARD_Y + row * (HOME_CARD_HEIGHT + HOME_CARD_GAP_Y)`, `outer_x1 = outer_x0 + HOME_CARD_WIDTH`, and `outer_y1 = outer_y0 + HOME_CARD_HEIGHT`. These are the intended outer card bounds, including the border. Because `ui_components_border_draw()` expands by `BORDER_THICKNESS` outside each argument, derive the inset content bounds as follows:

```c
int content_x0 = outer_x0 + BORDER_THICKNESS;
int content_y0 = outer_y0 + BORDER_THICKNESS;
int content_x1 = outer_x1 - BORDER_THICKNESS;
int content_y1 = outer_y1 - BORDER_THICKNESS;
```

Pass `(content_x0, content_y0, content_x1, content_y1)` to **both** `ui_components_box_draw()` and `ui_components_border_draw()`. The box then fills only the content rectangle, while the border expands from those arguments to exactly `(outer_x0, outer_y0, outer_x1, outer_y1)`; never pass the outer bounds to `ui_components_border_draw()`, which would make the card eight pixels wider and taller than documented. Text padding is measured inward from the same content bounds. The text paragraph parameters and print origin must therefore be exactly:

```c
rdpq_textparms_t text_parms = {
    .width = HOME_CARD_WIDTH - (2 * BORDER_THICKNESS) - (2 * HOME_CARD_PADDING_X),
    .height = HOME_CARD_HEIGHT - (2 * BORDER_THICKNESS) - (2 * HOME_CARD_PADDING_Y) - HOME_TEXT_OFFSET_Y,
    .align = ALIGN_CENTER,
    .valign = VALIGN_CENTER,
    .wrap = WRAP_WORD,
};
int text_x = content_x0 + HOME_CARD_PADDING_X;
int text_y = content_y0 + HOME_CARD_PADDING_Y + HOME_TEXT_OFFSET_Y;
```

Call `rdpq_text_print(&text_parms, FNT_DEFAULT, text_x, text_y, home_labels[i])` (or the equivalent `rdpq_text_printn` with the same parameters/origin and exact string length). The selected-card focus marker bounds are exactly `(content_x0, content_y0 + HOME_FOCUS_INSET_Y, content_x0 + HOME_FOCUS_WIDTH, content_y1 - HOME_FOCUS_INSET_Y)`. The height subtraction keeps the one-pixel text offset inside the padded content rectangle while `ALIGN_CENTER`/`VALIGN_CENTER` operate over the remaining defined rectangle; do not additionally pre-center the origin or apply another text offset.

The acceptance arithmetic is: content size is `160x124`; padded text width is `136` and text height is `99`. The third column has outer x bounds `[420, 588)` and border-call x bounds `[424, 584)`, so the expanded border ends at x=588, before `VISIBLE_AREA_X1` (608). The second row has outer y bounds `[230, 362)` and border-call y bounds `[234, 358)`, so the expanded border ends at y=362, before `LAYOUT_ACTIONS_SEPARATOR_Y` (400). Thus every complete card, including its outward-expanding border, remains inside the documented `168x132` outer bounds and safe area.

Use this exact static ROM table, not heap allocation or runtime formatting:

```c
static const char *const home_labels[HOME_CARD_COUNT] = {
    "Browse Files",
    "Continue Playing\n(Placeholder)",
    "Favorites\n(Placeholder)",
    "Pearl's Games\n(Placeholder)",
    "Play Together\n(Placeholder)",
    "All Games\n(Placeholder)",
};
```

**Step 2: Implement clamped input**

Implement a private `process(menu_t *menu)` that computes row/column from `menu->home.selected` and follows this single `if/else if` chain:

1. Up: subtract `HOME_COLUMNS` only if row > 0.
2. Down: add `HOME_COLUMNS` only if row < `HOME_ROWS - 1`.
3. Left: subtract 1 only if column > 0.
4. Right: add 1 only if column < `HOME_COLUMNS - 1`.
5. Enter: only index 0 plays `SFX_ENTER` and selects Browser.

Track whether selection actually changed and play `SFX_CURSOR` once only in that case. An edge press and A on a placeholder do nothing.

**Step 3: Draw with existing primitives**

In a private `draw(menu_t *, surface_t *)`:

1. `rdpq_attach(display, NULL)`.
2. Draw `ui_components_background_draw()` and `ui_components_layout_draw()`.
3. For each card, derive row/column, outer bounds, and inset content bounds; pass the content bounds to `ui_components_box_draw()` with `TAB_ACTIVE_BACKGROUND_COLOR` for the selected card and `TAB_INACTIVE_BACKGROUND_COLOR` otherwise, then pass those same content bounds to `ui_components_border_draw()` so its outward expansion lands on the exact outer bounds. On the selected card only, draw the solid `BORDER_COLOR` focus marker at the exact content-relative bounds above. The shape marker makes focus visible independently of fill color.
4. Render each fixed label using the exact local stack `rdpq_textparms_t`, x/y origin, and single vertical offset specified above. No paragraph build/free and no formatting allocation are needed for card labels.
5. Draw the existing action bar with `ui_components_actions_bar_text_draw()`: `A: Browse files` for index 0, otherwise `Placeholder`. Do not advertise B or any unavailable action.
6. `rdpq_detach_show()`.

Use only existing colors/primitives. Do not modify shared UI component files merely to customize Home.

**Step 4: Implement public callbacks**

`view_home_init()` must clamp invalid values (`< 0` or `>= HOME_CARD_COUNT`) to 0 and do nothing else. `view_home_display()` calls `process()` then `draw()` in the same pattern as Browser and Credits.

**Step 5: Prove scope statically**

Run:

```sh
grep -n 'Browse Files\|Placeholder\|MENU_MODE_BROWSER\|SFX_' src/menu/views/home.c
if grep -nE 'malloc|calloc|realloc|strdup|path_|png_|directory_|settings_|bookkeeping_|MENU_MODE_LOAD_ROM' src/menu/views/home.c; then exit 1; fi
set +e
git diff --no-index --check -- /dev/null src/menu/views/home.c
check_status=$?
git diff --no-index -- /dev/null src/menu/views/home.c
diff_status=$?
set -e
test "$check_status" -eq 1
test "$diff_status" -eq 1
```

Both expected status values are `1` because a valid new file differs from `/dev/null`; any other status fails. This explicit no-index review is mandatory while `home.c` is untracked: ordinary `git diff` does not show it.

Acceptance:
- all six exact labels exist in row-major order;
- only card 0 assigns Browser;
- no placeholder branch assigns `next_mode` or plays enter/error sound;
- no Home-owned heap/filesystem/persistence/decoder/launch API appears;
- each border call receives the `160x124` inset content bounds, expands to (but never beyond) its intended `168x132` outer card bounds, and all outer bounds remain within the safe area;
- text dimensions, origin, padding, and focus marker all derive from those same content bounds, and focus is visible by fill plus a shape marker rather than color-only label text.

**Step 6: Hand off without committing**

Report the new file and checks. No direct commit commands.

---

### Task 4: Gate startup and first-run Credits routes

**Files:**
- Modify: `src/menu/views/startup.c:11-43`
- Modify: `src/menu/views/credits.c:15-27`

**Step 1: Gate only normal startup**

Leave the complete `FEATURE_AUTOLOAD_ROM_ENABLED` block and first-run Credits branch unchanged. In the final normal `else`, use:

```c
#if FEATURE_AURORA_HOME_ENABLED
    menu->next_mode = MENU_MODE_HOME;
#else
    menu->next_mode = MENU_MODE_BROWSER;
#endif
```

This preserves autoload precedence and first-run behavior. Do not touch `menu_init()` or fault routing.

**Step 2: Preserve the Credits caller**

Add an explicit Credits return mode to `menu_t`. Startup sets it to Home under the numeric feature gate and Browser otherwise before entering first-run Credits. Browser sets it to Browser before opening Menu Information. In the existing Credits `menu->actions.back` branch, retain the message reset and `SFX_EXIT`, then return to the stored caller. Validate an unset/invalid value to Browser in `view_credits_init()`.

**Step 3: Inspect route acceptance**

Run:

```sh
grep -n -C 5 'FEATURE_AURORA_HOME_ENABLED' src/menu/views/startup.c src/menu/views/credits.c
grep -n -C 4 'FEATURE_AUTOLOAD_ROM_ENABLED\|MENU_MODE_FAULT' src/menu/views/startup.c src/menu/menu.c
git diff --check -- src/menu/views/startup.c src/menu/views/credits.c
git diff -- src/menu/views/startup.c src/menu/views/credits.c
```

Acceptance:
- normal startup is Home only at macro value 1 and Browser at 0;
- autoload still returns immediately to Load ROM before first-run/normal routing;
- first-run still sets/saves `first_run` and opens Credits;
- first-run Credits uses the gated caller and Browser-launched Credits returns to Browser;
- fault code has no diff.

**Step 4: Hand off without committing**

No direct commit commands.

---

### Task 5: Add deliberate Browser-root return without changing feature-off B

**Files:**
- Modify: `src/menu/views/browser.c:492-589` (`process`)
- Modify: `src/menu/views/browser.c:617-624` (left action bar)

**Step 1: Preserve non-root behavior and add the enabled root branch**

Keep the current non-root branch first and unchanged:

```c
} else if (menu->actions.back && !path_is_root(menu->browser.directory)) {
    /* existing pop/error/sound body unchanged */
```

Immediately after it, add under `#if FEATURE_AURORA_HOME_ENABLED`:

```c
} else if (menu->actions.back && path_is_root(menu->browser.directory)) {
    sound_play_effect(SFX_EXIT);
    menu->next_mode = MENU_MODE_HOME;
```

At macro value 0 this branch must preprocess away, preserving the current root no-op. Do not broaden `path_is_root`, invalidate/free Browser state, or alter archive/directory handling.

**Step 2: Make the action bar truthful in each build**

Under the enabled branch, render `B: Home` at root with `STL_DEFAULT`; away from root retain `B: Back`. Under `#else`, retain the existing text and root-gray style expression exactly. Keep the existing selected-entry A action behavior and right-side controls unchanged.

A clear implementation is to derive local fixed `const char *back_action` and `menu_font_type_t back_style` under preprocessor branches, then pass them to the existing action-bar primitive. Do not allocate or format these strings dynamically.

**Step 3: Inspect exact B behavior**

Run:

```sh
grep -n -C 8 'MENU_MODE_HOME\|B: Home\|path_is_root' src/menu/views/browser.c
git diff --check -- src/menu/views/browser.c
git diff -- src/menu/views/browser.c
```

Acceptance:
- enabled B below root still calls `pop_directory()` and does not enter Home;
- enabled B at root enters Home and plays one exit sound;
- disabled code retains root no-op plus gray `B: Back`;
- Browser list ownership, selection, context menus, file type actions, and ROM-loading transitions are untouched.

**Step 4: Hand off without committing**

No direct commit commands.

---

### Task 6: Format, build, and statically verify both configurations

**Files:**
- Verify all implementation files listed above
- Generated/ignored build artifacts may change; do not edit source outside the list

**Step 1: Review scope before formatting, including the untracked view**

Run:

```sh
cd /Users/josh/Developer/Aurora64
git diff --name-only
git status --short
set +e
git diff --no-index -- /dev/null src/menu/views/home.c
home_diff_status=$?
set -e
test "$home_diff_status" -eq 1
```

Acceptance: tracked changes are limited to the seven existing implementation files; the no-index diff explicitly shows all of new `src/menu/views/home.c`; the two Project Brief files remain untracked and untouched. The plan file may already be tracked from the controller's planning change. Stop if any protected file appears. Never treat a plain `git diff`/`git diff --name-only` as a complete slice review while `home.c` is untracked.

**Step 2: Run repository formatting**

Run in the existing amd64 container:

```sh
docker run --platform linux/amd64 --rm \
  -v "$PWD":/work -w /work aurora64-dev:v0.3.2 \
  bash -lc 'set -e; make format'
```

Re-run `git diff --name-only`. Because `make format` scans all C/H files, stop and restore any unrelated formatting churn rather than accepting it.

**Step 3: Clean-build the unoverridden Make default**

First build without any `AURORA64_HOME` command-line or environment override:

```sh
docker run --platform linux/amd64 --rm \
  -v "$PWD":/work -w /work aurora64-dev:v0.3.2 \
  bash -lc 'set -e; make clean; make all; sha256sum output/N64FlashcartMenu.n64'
```

Acceptance: this clean default build exits 0 and produces all normal outputs as Browser-first behavior. Record warnings and hash; timestamped hashes are observations, not stable expected values.

**Step 4: Clean-build the explicit flag-off configuration**

Build again with explicit numeric `0`:

```sh
docker run --platform linux/amd64 --rm \
  -v "$PWD":/work -w /work aurora64-dev:v0.3.2 \
  bash -lc 'set -e; make clean; make all AURORA64_HOME=0; cp output/N64FlashcartMenu.n64 /tmp/aurora64-home-off.n64; sha256sum output/N64FlashcartMenu.n64'
```

Because container `/tmp` is removed with the container, also preserve the candidate outside the repository if hardware comparison is planned:

```sh
cp output/N64FlashcartMenu.n64 /tmp/aurora64-home-off.n64
shasum -a 256 /tmp/aurora64-home-off.n64
```

Acceptance: the explicit-0 clean build exits 0 and produces all normal outputs. Record warnings and hash; timestamped hashes are observations, not stable expected values.

**Step 5: Clean-build the enabled configuration**

```sh
docker run --platform linux/amd64 --rm \
  -v "$PWD":/work -w /work aurora64-dev:v0.3.2 \
  bash -lc 'set -e; make clean; make all AURORA64_HOME=1; sha256sum output/N64FlashcartMenu.n64'
cp output/N64FlashcartMenu.n64 /tmp/aurora64-home-on.n64
shasum -a 256 /tmp/aurora64-home-on.n64
```

Acceptance: clean build exits 0, `build/menu/views/home.o` exists, normal outputs exist, and the enabled artifact is preserved at `/tmp/aurora64-home-on.n64` for volatile testing.

**Step 6: Run static source and boundary checks**

```sh
grep -R -n '#if FEATURE_AURORA_HOME_ENABLED' \
  src/menu/views/startup.c src/menu/views/browser.c
grep -n 'AURORA64_HOME ?= 0\|FEATURE_AURORA_HOME_ENABLED=$(AURORA64_HOME)' Makefile
if grep -nE 'malloc|calloc|realloc|strdup|path_|png_|directory_|settings_|bookkeeping_|MENU_MODE_LOAD_ROM' src/menu/views/home.c; then exit 1; fi
git diff --check
git diff -- src/boot src/flashcart src/menu/cart_load.c src/menu/views/load_rom.c
git diff --stat
git status --short
set +e
git diff --no-index --check -- /dev/null src/menu/views/home.c
home_check_status=$?
git diff --no-index -- /dev/null src/menu/views/home.c
home_diff_status=$?
set -e
test "$home_check_status" -eq 1
test "$home_diff_status" -eq 1
```

Acceptance:
- all feature-gated route sites use numeric `#if`, while Credits returns through its explicit caller state;
- Home has no forbidden ownership or launch calls;
- protected-boundary diff is empty;
- no whitespace errors;
- the no-index check and full-file diff both included untracked `home.c` and returned the expected difference status `1`;
- Project Brief files are still merely untracked;
- no source outside the approved list changed.

**Step 7: Controller review checkpoint**

The controller reviews the complete tracked diff **and** the explicit `/dev/null`-to-`home.c` no-index diff against every scope bullet. No implementer commits. If code-level acceptance and all three clean builds (default, explicit `0`, and explicit `1`) pass, proceed to physical verification before the controller commits the slice.

---

### Task 7: Perform volatile hardware verification without persistent menu replacement or intentional test-action writes

**Files:**
- Test `/tmp/aurora64-home-on.n64` and `/tmp/aurora64-home-off.n64`; do not copy either to SD

**Step 1: Establish the hardware and persistence baseline**

Use the known-good original N64 + SC64 + controller setup. Do not remove/reinsert SD or Paks while powered. On macOS, verify SC64 and start the already-documented host proxy:

```sh
cd /Users/josh/Developer/Aurora64
./tools/sc64/sc64deployer list
./tools/sc64/sc64deployer info
./tools/sc64/sc64deployer server 0.0.0.0:9064
```

Leave the server running in its own terminal. Record console/Pak/controller, firmware, deployer version, artifact hash, and whether the N64 uses 4 MiB Jumper Pak (the required Phase 1 target). Do not modify configuration to force first-run or autoload scenarios; those routes are statically checked in this slice.

Scope the persistence claim accurately: `upload` deploys the ROM to volatile memory, no command replaces the persistent `/sc64menu.n64`, and the prescribed navigation performs no intentional settings/delete/extract/launch/save action. However, the unchanged menu initialization calls directory creation for its menu and cache paths, `settings_load()` creates the settings file if it is missing, `bookkeeping_load()` creates `history.ini` if it is missing, and a configuration whose `first_run` is true saves the first-run setting before opening Credits. Therefore this plan does **not** promise that every session produces zero incidental SD writes.

If this particular session must have zero incidental writes, preflight the already-installed menu state before upload: confirm the normal menu directory and cache directory already exist; confirm the settings file already exists and has `first_run=false`; and confirm the existing menu `history.ini` file (or the exact equivalent path under the active flashcart storage prefix) already exists. The history-file check is mandatory because `bookkeeping_load()` creates it when absent. Use only state already established through the project's normal setup/inspection process; this plan does not invent or authorize an unsupported read-only SC64/filesystem tool. If any of those facts cannot be confirmed, do not run under a zero-incidental-write requirement and do not report the session as write-free. A normal volatile functional test may proceed only with the narrower claim above.

**Step 2: Upload enabled candidate only to volatile ROM memory**

In another terminal:

```sh
docker run --platform linux/amd64 --rm \
  -v /tmp:/host-tmp aurora64-dev:v0.3.2 \
  sc64deployer --remote host.docker.internal:9064 \
  upload /host-tmp/aurora64-home-on.n64

docker run --platform linux/amd64 --rm \
  aurora64-dev:v0.3.2 \
  sc64deployer --remote host.docker.internal:9064 \
  debug --no-writeback --init reboot
```

This is menu-only volatile ROM deployment. Do **not** use `make run-debug-upload`, `send-file`, or any command that replaces `/sc64menu.n64`. Do not launch a game; the debug `--no-writeback` path does not validate saves and must not be generalized into a claim that menu initialization cannot touch SD.

**Step 3: Verify enabled Home visually and physically**

On hardware, record video/photos and check:

1. Normal startup reaches Home (unless the existing unchanged configuration legitimately routes autoload/first-run first).
2. Six readable text cards form a 3x2 grid inside safe area; card 0 starts selected on a cold zero-initialized session.
3. All five non-Browse cards visibly include `(Placeholder)`.
4. D-pad and analog obey clamped movement. Test all four outer edges, all internal horizontal/vertical moves, and at least one diagonal; focus never leaves indices 0-5 or moves twice per frame.
5. Cursor sound occurs on a real move, not on a clamped edge.
6. On each placeholder, action bar does not advertise A; pressing A causes no transition, dialog, or confirm/error sound.
7. On `Browse Files`, action bar advertises A; pressing A enters the stock Browser.
8. In a subdirectory, B returns one level and does not jump Home. At filesystem root, action bar says `B: Home`; B returns Home and preserves the prior Home selection.
9. Repeat Home -> Browser -> root -> Home at least 20 times on 4 MiB hardware. No hang, corrupt frame, missing input, focus loss, or apparent accumulating slowdown is acceptable.
10. Do not open ROM details, launch ROMs, change settings, invoke Browser delete/default-directory actions, or take any other intentional test action that writes to SD.

**Step 4: Verify flag-off upstream routing behavior**

Volatile-upload `/tmp/aurora64-home-off.n64` with the same two commands, substituting the filename. Verify normal startup reaches Browser, root B remains a no-op with gray `B: Back`, and Home is not reachable through the new routes. This is a behavior comparison, not a binary-equality claim; the registered additive code may still be linked.

**Step 5: Handle the observed debug-reboot controller/PIF hiccup correctly**

A controller/PIF input hiccup has been observed after debug reboot. If the candidate loses input, compare against the stock SD menu before filing a Home defect. **If both the candidate and stock menu lose controller input, fully power the N64 off, reseat the controller while powered off, and power on to recover a known-good baseline.** Do not repeatedly debug-reboot and do not misattribute a shared baseline/PIF condition to Home. Only report a Home regression if input works on the recovered stock baseline and reproducibly fails on the named candidate artifact under the same setup.

**Step 6: Return safely to stock menu and report evidence**

Fully power off normally. A subsequent ordinary power-on should return to the unchanged persistent SD-card menu because the candidate was deployed only to volatile ROM memory. Record the observed return and `git status --short`; do not claim SD byte identity or an absolutely write-free session unless a separately approved read-only comparison procedure was actually run and the zero-incidental-write preflight above passed.

Report each acceptance item as PASS/FAIL with artifact hash, hardware identity, and evidence. Any crash, input failure unique to the candidate after baseline recovery, unexpected SD write, or protected launch/save/SC64 symptom stops the phase and is escalated rather than fixed inside this UI slice.

---

## Full-slice completion gate

The controller may review and commit only after all of the following are true:

- `AURORA64_HOME` defaults to unquoted numeric `0`; clean unoverridden-default, explicit-`0`, and explicit-`1` amd64 container builds all pass.
- Enabled normal startup and first-run Credits return route target Home; autoload/fault behavior is unchanged.
- Disabled startup, Credits, and Browser root-B behavior remain upstream-compatible.
- Home has six exact static labels in a 3x2 grid, clamped focus, persistent in-session selection, truthful action-bar text, one actionable Browser card, and five inert placeholders.
- Browser root B returns Home only in the enabled build; non-root B remains stock.
- No Home-owned allocation, artwork, scan, metadata, persistence, direct launch, save, or SC64 change exists.
- Protected-boundary diff is empty; the Project Brief files remain untouched.
- Volatile physical testing passes on original 4 MiB N64 hardware with no persistent menu replacement, no ROM launch, and no intentional test-action writes, including 20 Home/Browser/Home transitions and the debug-reboot hiccup baseline procedure; any stronger zero-incidental-write claim requires the documented preflight and evidence.
- Implementer tasks contain no commits; after full diff/evidence review, the controller alone decides and performs the commit.
