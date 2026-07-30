# Aurora64 Phase 3 Real-Library Foundation Implementation Plan

> **For Hermes:** Use the `subagent-driven-development` skill to implement this plan task by task. Every code task requires specification review first and quality review second before the next task begins.

**Goal:** Replace the six-game launch-proof fixture with a bounded, recoverable, games-first library index while preserving the proven Home -> Details path and the stock launch/save/SC64/boot pipeline on an original 4 MiB Nintendo 64.

**Architecture:** A menu-owned `library_service_t` incrementally enumerates configured roots, parses ROM headers, computes byte-order-independent identities, builds an immutable snapshot, and exposes status to Home and All Games. The initial target slice is read-only and in-memory; two-slot SD cache persistence remains disabled until the scanner, lifecycle, memory, and filesystem primitives pass dedicated host and 4 MiB hardware gates.

**Tech Stack:** C11, libdragon at pinned submodule revision `5cb976aab11eb30622c33b112c120a1107eedb5e`, FatFs-backed SummerCart64 storage, Acutest host tests, Docker image `aurora64-dev:v0.3.2`, SummerCart64 firmware/deployer v2.20.2.

---

## 1. Baseline and scope

Implementation base:

- parent commit: `909f1f3082f8b27240e58ea15124c42ea8ea43bb`;
- branch: `feature/phase3-library-foundation`;
- Phase 2 implementation commit: `9513af2418d1918b4ee52983398ff8cc7ec20aa8`;
- current protected launch seam: `view_load_rom_set_pending_path()`;
- hardware baseline: original 4 MiB N64 + SummerCart64 v2.20.2;
- persistent next boot: `Bootloader -> Menu from SD card`.

Preserve these local, untracked files without opening, staging, deleting, or modifying them during implementation:

- `Aurora64_Codex_Research_Guide.md`;
- `Aurora64_Project_Brief.docx`;
- `Aurora64_Project_Brief.pdf`.

### In scope

- public directory-enumerator close support in pinned libdragon;
- native host-test harness;
- bounded ROM-header parsing;
- normalized ROM identity across `.z64`, `.v64`, and `.n64` byte orders;
- bounded root normalization and traversal;
- immutable library snapshots and deterministic duplicate handling;
- cooperative menu-owned service lifecycle;
- real All Games text-card view over the snapshot;
- identity-backed selection preservation;
- owned-path Details handoff;
- contextual Error -> All Games return;
- read-only/in-memory scanner hardware acceptance;
- strict two-slot cache codec and persistence, but only after a separate approval gate.

### Explicitly out of scope

- cover-art grid or artwork cache;
- metadata enrichment beyond header/title/filename fallback;
- video, audio previews, or screensaver;
- persistent favorites/recent/history migration;
- shared-metadata schema changes;
- launcher, save, CIC, flashcart, SC64 command, or boot-handoff redesign;
- persistent `/sc64menu.n64` replacement;
- game launch/save acceptance unless separately approved under the existing launch/save gate.

### Protected files and behaviors

No Phase 3 implementation task may modify:

- `src/menu/cart_load.c`;
- `src/menu/bookkeeping.c` or `src/menu/bookkeeping.h`;
- `src/flashcart/**`;
- `src/boot/**`;
- `src/main.c`;
- the order inside the existing N64 `load()` staging body in `src/menu/views/load_rom.c`.

`src/menu/views/load_rom.c`, `load_disk.c`, `load_emulator.c`, and `usb_comm.c` may receive lifecycle preconditions only. They must not rewrite protected staging behavior.

---

## 2. Gate model

### Pre-flight gate: repository and hardware safety

Before each task:

```sh
git status --short --branch
git rev-parse HEAD
git submodule status libdragon
```

Required:

- expected branch and base lineage;
- no unknown tracked edits;
- only the three preserved local documents untracked;
- no persistent SD-menu write command in the task.

Failure behavior: block before modification, identify the mismatch, and resume only after reconciliation.

### Revision gate: task review

Each task follows:

1. fresh implementation subagent;
2. exact spec-compliance review;
3. fixes until spec PASS;
4. code-quality/safety review;
5. fixes until APPROVED;
6. direct orchestrator verification;
7. task commit.

Maximum three review cycles. If findings do not decrease, escalate instead of looping indefinitely.

### Escalation gate: platform decisions

Pause for Josh before:

- pushing a libdragon fork/branch or Aurora branch;
- enabling any new SD cache write on real media;
- using disposable cloned media for failure testing;
- replacing persistent `/sc64menu.n64`;
- launching a game or testing saves;
- changing the stable identity algorithm after cache/schema data exists.

### Abort gate: safety or ownership violation

Stop immediately if:

- scanner/cache work enters ROM/save staging or Boot while not quiesced;
- an active directory/file handle cannot be closed or accounted for;
- allocation/length bounds are bypassed;
- the prior immutable snapshot is lost on failed refresh;
- protected launch/save/boot files change unexpectedly;
- a command would write persistent menu state without approval.

---

## 3. Provisional discovery hypotheses—not frozen policy

The values below are only upper-bound hypotheses for Task 2A’s measurement ROM. They must not become production constants, schemas, allocations, or acceptance claims until the early discovery report is reviewed.

```c
#define DISCOVERY_MAX_ROOTS              4
#define DISCOVERY_MAX_DEPTH              16
#define DISCOVERY_MAX_RECORDS            2048
#define DISCOVERY_MAX_PATHS              4096
#define DISCOVERY_MAX_PATH_BYTES         196608
#define DISCOVERY_SCAN_READ_BYTES        4096
#define DISCOVERY_SCAN_ENTRIES_PER_POLL  1
#define DISCOVERY_SCAN_BUDGET_US         1000
```

Hypotheses to test, not assumptions to embed:

- candidate default root is storage root `/`, with `/menu` and filesystem/system directories excluded through a documented bounded list;
- candidate ROM extensions are those already recognized by the project (`z64`, `n64`, `v64`, `rom`), followed by N64 header-magic validation;
- `settings.default_directory` is not silently reinterpreted as a library root;
- Phase 3 does not add a root editor;
- candidate stable identity is a full SHA-256 over the logical ROM stream canonicalized to z64 byte order.

Task 2A must measure the actual card roots/population, path/depth distribution, free heap, service gaps, and normalized hashing throughput before Tasks 3–14 freeze interfaces or limits. Its reviewed report replaces every `DISCOVERY_*` hypothesis with accepted production values and records the identity decision. If full normalized SHA-256 throughput is unacceptable, stop at the escalation gate; do not silently choose a weaker persisted identity.

After Task 2A, the frozen identity policy must state:

- all 256 fingerprint bits retained in RAM and cache;
- path, filename, size, timestamps, header tuple, and quick source checks are attributes—not identity;
- one logical record per fingerprint only when equal fingerprints have compatible source attributes;
- an injected equal-fingerprint collision with contradictory size/header attributes fails the candidate generation closed;
- all discovered paths retained with a deterministic primary path.

---

## 4. Task 1 — Add the native host-test harness

**Objective:** Establish repeatable host tests before production library code exists.

**Files:**

- Create: `tests/Makefile`
- Create: `tests/test_main.c`
- Create: `tests/support/test_alloc.c`
- Create: `tests/support/test_alloc.h`
- Modify: `Makefile`
- Modify: `.github/workflows/build.yml`

### Step 1: Capture the current absence of a test target

Run:

```sh
make test
```

Expected: failure because the root Makefile has only a commented-out test placeholder.

### Step 2: Add strict native targets

`tests/Makefile` must compile production-pure modules with:

```make
CFLAGS += -std=c11 -Wall -Wextra -Werror -pedantic
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
```

Required entry points:

```make
test: build/tests
	./build/tests

test-sanitize: clean
	$(MAKE) CFLAGS="$(CFLAGS) $(SANITIZE_FLAGS)" LDFLAGS="$(SANITIZE_FLAGS)" test
```

Use `src/libs/acutest/acutest.h`; do not add a package-manager dependency.

### Step 3: Add deterministic allocation failure

Expose only to host tests:

```c
void test_alloc_fail_after(size_t successful_allocations);
void test_alloc_reset(void);
void *test_malloc(size_t size);
void *test_calloc(size_t count, size_t size);
void *test_realloc(void *ptr, size_t size);
void test_free(void *ptr);
```

Tests must be able to fail every allocation site deterministically and verify cleanup.

### Step 4: Wire top-level and CI targets

Root targets:

```make
host-test:
	$(MAKE) -C tests test

host-test-sanitize:
	$(MAKE) -C tests test-sanitize
```

CI adds an Ubuntu native test job independent of the N64 cross-build. Do not remove the existing ROM build.

### Step 5: Verify red/green harness behavior

Run:

```sh
make host-test
make host-test-sanitize
```

Expected: one deliberate smoke test passes under normal and sanitizer builds.

### Step 6: Commit

```sh
git add Makefile .github/workflows/build.yml tests
git commit -m "test: add Phase 3 host harness"
```

---

## 5. Task 2 — Add an upstreamable libdragon directory-close API

**Objective:** Make bounded directory enumeration cancellable without draining to EOF or touching private FatFs state from Aurora.

**Why this blocks the scanner:** FAT `dir_findfirst()` allocates `DIR` into `dir_t.d_cookie`; the pinned API frees it only when enumeration reaches EOF. A truthful `QUIESCED` state requires an explicit supported close.

**Files in submodule:**

- Modify: `libdragon/include/dir.h`
- Modify: `libdragon/include/system.h`
- Modify: `libdragon/src/system.c`
- Modify: `libdragon/src/fat.c` (iterator close plus deterministic FAT `st_mtime` population)
- Test: add and execute the narrowest appropriate libdragon system/filesystem test or example-based target smoke test

**Parent files:**

- Update: `libdragon` submodule pointer only after the submodule commit is reviewed and has a retrievable remote plan
- Create: `docs/research/aurora64_libdragon_dir_close.md`

### Step 1: Create a local submodule feature branch

```sh
git -C libdragon switch -c aurora64-dir-findclose
```

Do not push it yet.

### Step 2: Add the public API

Declare:

```c
int dir_findclose(const char * const path, dir_t *dir);
```

Contract:

- closing an active iterator releases all resources owned by that backend;
- the generic dispatcher never infers active/closed state from `d_cookie`: zero is a valid logical cookie for some backends;
- each backend interprets its own cookie; resource-free backends provide an explicit no-op callback or the documented missing-callback behavior returns success;
- invalid `path` or `dir` returns `-2` and sets `errno = EINVAL`, matching directory-enumeration error convention;
- a backend close failure returns `-2` with translated `errno` after ownership has been cleared;
- after close, caller must use `dir_findfirst()` before `dir_findnext()`;
- no caller may free or inspect private cookie contents.

Add optional filesystem callback:

```c
int (*findclose)(dir_t *dir);
```

`dir_findclose()` dispatches through the filesystem mapping associated with `path` under the existing filesystem lock. It does not short-circuit on `d_cookie == 0`.

### Step 3: Implement FAT cleanup

FAT callback must:

1. interpret a null FAT pointer as already closed and return success;
2. call `f_closedir()` on an active FatFs `DIR`;
3. free the heap allocation exactly once;
4. clear `d_cookie` on success and failure before returning so cleanup is not repeated;
5. translate FatFs errors through existing errno helpers.

Also harden `__fat_findnext()` against a null/closed FAT cookie and make `f_readdir()` error cleanup close/free/clear the active iterator exactly once. Do not apply FAT’s pointer sentinel semantics to BBFS, DragonFS, or Controller Pak cookies.

The pinned `__fat_stat_fill()` currently omits modification time. Populate standard `struct stat.st_mtime` deterministically from FatFs `FILINFO.fdate`/`ftime`, using the project/libdragon time conversion conventions and a documented value for unavailable/invalid FAT dates. This is the only timestamp source used by Aurora; scanner code still may not call private FatFs APIs.

Do not expose `DIR`, `FILINFO`, `f_closedir()`, or `d_cookie` internals to Aurora source.

### Step 4: Execute dispatcher and FAT tests

Required cases:

- mock-filesystem dispatcher invokes the correct backend close exactly once;
- resource-free backend close behavior;
- FAT close after first and several entries;
- FAT close after EOF;
- repeated FAT close;
- `findnext` after FAT close returns the defined error without dereference;
- injected backend close error clears ownership exactly once;
- zero-valued logical cookies in non-FAT backends are not mistaken for closed iterators;
- FAT `stat()` exposes known `fdate`/`ftime` fixtures as deterministic `st_mtime`, including invalid/unavailable-date behavior;
- heap watermark returns after repeated open/one-entry/close cycles on 4 MiB hardware.

Compilation alone is insufficient. Execute a focused libdragon test ROM through the supported emulator or volatile SC64 hardware harness and retain its runtime assertion output. Aurora’s fake/host adapter tests do not substitute for this dependency-level runtime proof.

### Step 5: Build libdragon and Aurora baseline

Run the repository’s pinned container build path. Expected:

- libdragon builds cleanly;
- default Aurora build remains Browser-first;
- no Phase 3 menu source changes yet.

### Step 6: Review and document distribution

`docs/research/aurora64_libdragon_dir_close.md` records:

- pinned base revision;
- API contract;
- exact submodule commit;
- tests/build evidence;
- intended upstream/fork route;
- why Aurora cannot proceed without it.

Escalation gate: Josh reviews before creating/pushing `JSeiv/libdragon` or an upstream PR. Do not leave the parent repository pointing to an unreachable submodule commit.

### Step 7: Publish dependency before parent gitlink

Required sequence:

1. commit the libdragon change inside the submodule;
2. obtain Josh’s review before any push;
3. push the exact commit to a remotely reachable branch;
4. if upstream is not writable, create/use an Aurora64-maintained fork and update `.gitmodules` to that retrievable remote;
5. verify from a clean temporary clone:

   ```sh
   git submodule sync --recursive
   git submodule update --init --recursive
   ```

6. only then commit `.gitmodules` if changed, the parent `libdragon` gitlink, and the research note.

Never leave the parent branch pointing to a local-only submodule commit.

---

## 5A. Task 2A — Run the early 4 MiB discovery spike

**Objective:** Measure the real SD/library and platform costs before production parser, identity, scanner, snapshot, service, or cache interfaces are frozen.

**Files:**

- Create: `tools/phase3_discovery/Makefile`
- Create: `tools/phase3_discovery/main.c`
- Create: `tools/phase3_discovery/README.md`
- Create: `docs/research/aurora64_phase3_discovery.md`

The discovery ROM is a disposable measurement artifact, not production library code. It may use bounded temporary implementations for header recognition and candidate normalized hashing, but Tasks 3 onward must not copy unreviewed spike code by default.

### Step 1: Define read-only measurements

On the actual card, collect aggregate values only:

- top-level directory names and candidate effective roots;
- directory count, maximum depth, path count, maximum/total path bytes;
- candidate ROM count by extension and valid N64 magic;
- `.z64`/`.v64`/`.n64` counts, size distribution, and total ROM bytes;
- duplicate candidates where safely inferable during the benchmark;
- post-init, minimum, and final free heap;
- directory enumeration throughput;
- sequential 4 KiB and larger-buffer read throughput;
- normalized full-stream SHA-256 throughput for representative small/medium/large files, including two complete matching passes;
- Phase 2 Home, static-Library, and Details per-view free-heap watermarks from a temporary instrumented build;
- cache-update primitive evidence (`rename`, overwrite, close/flush semantics, and the absence/presence of fsync-like guarantees);
- conservative cache encode/write peak-memory budget using a bounded synthetic encoder and memory sink—no real SD write in this task;
- input, frame, and USB-service gap while one bounded unit runs;
- directory-close/quiescence latency after early cancellation.

Do not emit ROM payloads, filenames below the level needed to choose roots, or hashes as external project data.

### Step 2: Keep the spike bounded and nonpersistent

- allocate from fixed, reported upper bounds;
- process at most one entry or bounded read chunk per rendered frame;
- use `dir_findclose()` on early stop;
- open files read-only;
- create no menu/cache/config/history file;
- provide a controller stop action that closes all handles before reporting `QUIESCED`;
- fail visibly on any cap rather than silently undercounting.

### Step 3: Build and deploy explicitly

Build the standalone discovery artifact in the pinned container. Record its path, size, and SHA-256. Deploy only to volatile SC64 memory, then reboot with writeback disabled using the exact supported command shape from `docs/aurora64_usb_development.md`:

```sh
./tools/sc64/sc64deployer upload <discovery-rom>
./tools/sc64/sc64deployer debug --no-writeback --init reboot
```

If Docker remote mode is required, add `--remote host.docker.internal:9064` to both commands. Do not use `send-file`, `make run-debug-upload`, or write `/sc64menu.n64`.

### Step 4: Record and review the decision report

`docs/research/aurora64_phase3_discovery.md` records:

- exact source/submodule commits and build flags;
- SummerCart64 firmware and SD initialization status;
- actual measurements and repeat count;
- accepted roots/exclusions;
- accepted root/depth/record/path/string/read budgets;
- normalized SHA-256 decision and measured two-pass cost, or escalation;
- Phase 2 Home/Library/Details heap baseline and conservative cache peak-memory budget;
- supported/unsupported cache update primitives and resulting two-slot constraints;
- minimum heap reserve and maximum service-gap target;
- known measurement limitations.

Revision gate: specification and 4 MiB feasibility reviewers must approve the report. Production Tasks 3–14 remain blocked until this report replaces the provisional hypotheses in Section 3 with frozen, measured values. Phase 4 artwork remains blocked until Task 14 measures the real cache-writer peak on approved disposable media; the synthetic Task 2A budget is only a conservative preimplementation ceiling.

### Step 5: Commit the spike and report

```sh
git add tools/phase3_discovery docs/research/aurora64_phase3_discovery.md
git commit -m "research: measure Phase 3 library constraints"
```

---

## 6. Task 3 — Extract a pure bounded ROM-header parser

**Objective:** Share one byte-order/header implementation between scanner and launch without invoking metadata/config/launch work.

**Files:**

- Create: `src/menu/library/rom_header.c`
- Create: `src/menu/library/rom_header.h`
- Create: `tests/test_rom_header.c`
- Create: `tests/support/rom_fixture_builder.c`
- Create: `tests/support/rom_fixture_builder.h`
- Modify: `src/menu/rom_info.c`
- Modify: `src/menu/rom_info.h` only if the launch interface requires it
- Modify: `Makefile`
- Modify: `tests/Makefile`

### Step 1: Write failing fixtures

Use synthetic, non-copyrighted buffers. Cover:

- valid z64/v64/n64 magic;
- identical parsed title/game code/cartridge ID/country/region/revision/check code across orders;
- table-driven country-code normalization for NTSC-J, NTSC-U, PAL, known-other, and unknown values;
- deterministic `UNKNOWN`/fingerprint-fallback behavior for malformed, homebrew, hack, and prototype header tuples;
- exactly 20 non-NUL title bytes;
- short input below 4 bytes, below the 64-byte metadata region, and below the full `0x1000` header-plus-IPL3 region;
- canonicalization of the complete `0x1000` region used by launch/CIC logic in all three byte orders;
- invalid magic;
- output zeroing on failure.

### Step 2: Define the parser API

```c
typedef enum {
    ROM_BYTE_ORDER_Z64,
    ROM_BYTE_ORDER_V64,
    ROM_BYTE_ORDER_N64,
} rom_byte_order_t;

typedef enum {
    ROM_REGION_UNKNOWN,
    ROM_REGION_NTSC_J,
    ROM_REGION_NTSC_U,
    ROM_REGION_PAL,
    ROM_REGION_OTHER,
} rom_region_t;

typedef struct {
    rom_byte_order_t byte_order;
    char title[21];
    char game_code[5];
    char cartridge_id[3];
    uint8_t country_code;
    rom_region_t region;
    uint8_t revision;
    uint32_t clock_rate;
    uint32_t boot_address;
    uint64_t check_code;
} rom_header_t;

#define ROM_HEADER_METADATA_BYTES 64
#define ROM_HEADER_WITH_IPL3_BYTES 0x1000

bool rom_header_parse(const uint8_t *bytes, size_t length, rom_header_t *out);
bool rom_normalize_prefix(
    rom_byte_order_t order,
    const uint8_t *input,
    size_t input_len,
    uint8_t *output,
    size_t required_len
);
size_t rom_normalize_bytes(
    rom_byte_order_t order,
    const uint8_t *input,
    size_t input_len,
    uint8_t carry[4],
    size_t *carry_len,
    uint8_t *output,
    size_t output_capacity,
    bool final
);
```

`rom_header_parse()` requires at least the 64-byte metadata region for scanner fields. `rom_info_load()` continues to require and read the full `0x1000` header-plus-IPL3 region, canonicalizes that entire region, and passes the canonical IPL3 bytes unchanged to `cic_detect()`. The streaming normalizer preserves 1–3 carry bytes across arbitrary chunk boundaries, never mutates the caller’s input, and fails closed on malformed final tails.

### Step 3: Refactor `rom_info_load()`

Replace only duplicate byte-order and field-extraction logic with the shared parser/normalizer. Preserve the full `0x1000` read contract, CIC detection and boot-address adjustment, and all subsequent database, save, config, metadata, and embedded-metadata behavior exactly.

### Step 4: Verify

```sh
make host-test-sanitize
# build default and Phase 2 enabled artifacts in the pinned container
```

Expected:

- all parser fixtures pass;
- baseline build matrix passes;
- Phase 2 hardware behavior is unchanged before scanner integration.

### Step 5: Commit

```sh
git add src/menu/library src/menu/rom_info.c src/menu/rom_info.h tests Makefile
git commit -m "refactor: share bounded ROM header parsing"
```

---

## 7. Task 4 — Add canonical incremental SHA-256 identity

**Objective:** Produce one stable identity for equivalent z64/v64/n64 byte streams without storing whole ROMs in memory.

**Files:**

- Create: `src/menu/library/sha256.c`
- Create: `src/menu/library/sha256.h`
- Create: `src/menu/library/rom_identity.c`
- Create: `src/menu/library/rom_identity.h`
- Create: `tests/test_sha256.c`
- Create: `tests/test_rom_identity.c`
- Modify: `Makefile`
- Modify: `tests/Makefile`
- Modify: licensing/attribution file if implementation is adapted rather than original

### Step 1: Select and document SHA-256 provenance

Prefer a small, portable implementation with a license compatible with AGPL-3.0. Record origin/license and preserve notices. Do not paste unattributed code.

### Step 2: Test standard SHA-256 vectors

Include empty input, `abc`, multi-block input, and incremental chunking against published vectors.

### Step 3: Test normalized ROM identity

Required:

- identical logical synthetic ROM in z64/v64/n64 -> identical 32-byte fingerprint;
- chunk sizes `1`, `2`, `3`, `4`, `5` through `17`, `4095`, `4096`, and `65536` bytes;
- renamed path -> same fingerprint;
- one payload-byte change -> different fingerprint;
- malformed final length for the detected two-/four-byte encoding -> explicit failure;
- input buffers remain unchanged;
- exactly one copy of every logical byte is hashed;
- no whole-file allocation.

### Step 4: Define API

```c
typedef struct {
    uint8_t bytes[32];
} rom_fingerprint_t;

typedef struct rom_identity_ctx rom_identity_ctx_t;

bool rom_identity_begin(rom_identity_ctx_t *ctx, rom_byte_order_t order);
bool rom_identity_update(rom_identity_ctx_t *ctx, const uint8_t *bytes, size_t length);
bool rom_identity_finish(rom_identity_ctx_t *ctx, rom_fingerprint_t *out);
bool rom_fingerprint_equal(const rom_fingerprint_t *a, const rom_fingerprint_t *b);
```

`rom_identity_begin()` configures byte order only and hashes no payload. The caller must pass the complete file exactly once through `rom_identity_update()`, including the bytes used to detect the header. The context carries 1–3 bytes across chunks, hashes only canonical big-endian output, never mutates caller buffers, and rejects a final length that is invalid for the detected encoding.

### Step 5: Verify and commit

```sh
make host-test-sanitize
git add src/menu/library tests Makefile
git commit -m "feat: add normalized ROM identity"
```

---

## 8. Task 5 — Add bounded root normalization and filesystem abstraction

**Objective:** Make traversal deterministic, testable, and independent of browser/path ownership.

**Files:**

- Create: `src/menu/library/library_fs.h`
- Create: `src/menu/library/library_fs_libdragon.c`
- Create: `src/menu/library/library_roots.c`
- Create: `src/menu/library/library_roots.h`
- Create: `tests/support/fake_library_fs.c`
- Create: `tests/support/fake_library_fs.h`
- Create: `tests/test_library_roots.c`
- Modify: `Makefile`
- Modify: `tests/Makefile`

### Step 1: Define injected filesystem operations

```c
typedef struct {
    void *context;
    int (*dir_open)(void *context, const char *path, void **handle, library_dirent_t *first);
    int (*dir_next)(void *context, void *handle, library_dirent_t *next);
    int (*dir_close)(void *context, void *handle);
    int (*file_open_read)(void *context, const char *path, void **handle);
    int64_t (*file_read)(void *context, void *handle, void *buffer, size_t length);
    int (*file_close)(void *context, void *handle);
    int (*stat)(void *context, const char *path, library_stat_t *out);
    uint32_t (*ticks_now)(void *context);
} library_fs_t;
```

No scanner code may call FatFs internals directly.

### Step 2: Implement root normalization tests

Cover:

- repeated separators;
- trailing slash;
- `.` components;
- `..` escape rejection;
- ASCII case-folded FAT comparison;
- exact duplicate roots;
- ancestor/descendant roots;
- `/rom` does not contain `/roms`;
- max root count/path length;
- bounded exclusions.

### Step 3: Implement libdragon and fake adapters

The adapter owns both its copied normalized directory path and `dir_t`; callers never retain a borrowed path needed by `dir_findnext()`. Use explicit results `LIBRARY_FS_ENTRY`, `LIBRARY_FS_EOF`, and `LIBRARY_FS_ERROR`:

- non-empty open returns `ENTRY` and one owned active handle;
- empty open returns `EOF`, sets `out_handle = NULL`, and requires no close;
- open error returns `ERROR`, sets `out_handle = NULL`, and preserves translated error data;
- EOF from `dir_next` closes/releases internally and invalidates the handle;
- explicit close is required exactly once for an active handle on pause, cancel, or error;
- repeated close through the adapter is safe and reports already closed without touching the backend;
- `dir_next` after EOF/close returns `ERROR`, never dereferences stale state.

The libdragon adapter closes only through `dir_findclose()`. Add fake-backend tests for empty, first-entry, EOF, early close, repeated close, and next-after-close transitions.

### Step 4: Verify and commit

```sh
make host-test-sanitize
# cross-build all currently supported flags
git add src/menu/library tests Makefile
git commit -m "feat: add bounded library filesystem boundary"
```

---

## 9. Task 6 — Implement the cooperative scanner state machine

**Objective:** Enumerate roots, validate candidates, and fingerprint ROMs without unbounded work in a menu frame.

**Files:**

- Create: `src/menu/library/library_scanner.c`
- Create: `src/menu/library/library_scanner.h`
- Create: `tests/test_library_scanner.c`
- Modify: `Makefile`
- Modify: `tests/Makefile`

### Step 1: Write failing state-machine tests

Use fake filesystem/clock/allocator. Cover:

- one directory entry per configured poll;
- max bytes and tick budget;
- nested traversal/depth cap;
- extension candidate followed by magic validation;
- short/failed/slow reads;
- removal and truncation during hashing;
- same-size replacement between scans changes the full fingerprint;
- same-size mutation during hashing outside header and fixed sample ranges, with unchanged timestamp/signatures, causes the two complete canonical passes to differ and rejects or bounded-retries the candidate;
- directory I/O failure is fatal to the candidate generation and retains the old snapshot;
- candidate-file failure is counted, visible, and skipped without being reported as a clean scan;
- open/read/close balance;
- pause reaches `QUIESCED` with no directory/file handle, adapter call, or pending publication;
- resume reopens and deterministically replays from a saved logical checkpoint rather than preserving a FatFs iterator;
- cancel closes resources and discards partial work;
- restart begins a new builder generation;
- OOM at every allocation site;
- any root/depth/queue/record/path/string capacity exhaustion fails the candidate generation visibly and retains the old snapshot; no cap silently publishes a truncated snapshot.

### Step 2: Define lifecycle/state API

```c
typedef enum {
    LIBRARY_SCANNER_IDLE,
    LIBRARY_SCANNER_SCANNING,
    LIBRARY_SCANNER_PAUSE_REQUESTED,
    LIBRARY_SCANNER_QUIESCED,
    LIBRARY_SCANNER_CANCEL_REQUESTED,
    LIBRARY_SCANNER_COMPLETE,
    LIBRARY_SCANNER_FAILED,
} library_scanner_state_t;

typedef struct {
    uint32_t max_directory_entries;
    uint32_t max_read_bytes;
    uint32_t max_ticks;
} library_scan_budget_t;

library_scan_result_t library_scanner_poll(
    library_scanner_t *scanner,
    const library_scan_budget_t *budget
);
```

### Step 3: Implement explicit states

Minimum states:

1. normalize roots;
2. open directory;
3. consume bounded entry;
4. queue child directory;
5. open candidate and capture pre-read `library_source_signature_t`;
6. read/parse header;
7. compute first full canonical SHA-256 in bounded chunks;
8. close/reopen candidate and recompute source signature;
9. reject or bounded-retry when size, FAT modification time, normalized header CRC32, or normalized fixed-sample CRC32 changed;
10. compute a second complete canonical SHA-256 from a fresh open in bounded chunks;
11. accept only when both 256-bit hashes and pre/mid/post source signatures match;
12. add coherent candidate to builder;
13. close directory;
14. complete or fail.

New or changed files require two matching complete canonical passes. This is the coherence rule for mutation during scan; fixed samples and FAT timestamps are early-reject optimizations, not proof. A mismatch gets one bounded retry from a fresh pre-signature; a second mismatch skips the candidate with visible mutation error and never publishes its fingerprint. Task 2A must approve the measured two-pass cost before this contract is frozen.

A tick budget is advisory around potentially blocking filesystem calls; bytes/entries are the hard work caps. On pause/cancel, finish or abort the current adapter call, close every active file/directory, save only logical replay state, and acknowledge `QUIESCED` only after open-handle and in-I/O counters are zero.

### Step 4: Verify and commit

```sh
make host-test-sanitize
git add src/menu/library tests Makefile
git commit -m "feat: add cooperative ROM scanner"
```

---

## 10. Task 7 — Add immutable snapshots and deterministic duplicate handling

**Objective:** Publish complete generations atomically while preserving old readers and selection identity.

**Files:**

- Create: `src/menu/library/library_snapshot.c`
- Create: `src/menu/library/library_snapshot.h`
- Create: `tests/test_library_snapshot.c`
- Modify: `src/menu/library/library_scanner.c`
- Modify: `Makefile`
- Modify: `tests/Makefile`

### Step 1: Define bounded record model

```c
typedef struct {
    uint32_t path_offset;
    uint64_t size;
    int64_t mtime_seconds;
    uint32_t normalized_header_crc32;
    uint32_t normalized_sample_crc32;
    rom_byte_order_t byte_order;
} library_source_t;

typedef enum {
    LIBRARY_LOOKUP_HEADER,
    LIBRARY_LOOKUP_FINGERPRINT,
} library_lookup_kind_t;

typedef struct {
    library_lookup_kind_t kind;
    char cartridge_id[3];
    uint8_t country_code;
    rom_region_t region;
    uint8_t revision;
    rom_fingerprint_t fingerprint;
} library_lookup_key_t;

typedef struct {
    rom_fingerprint_t fingerprint;
    library_lookup_key_t lookup_key;
    uint32_t source_first;
    uint16_t source_count;
    uint16_t primary_source_index;
    char title[21];
    char game_code[5];
    uint8_t country_code;
    rom_region_t region;
    uint8_t revision;
} library_record_t;
```

Byte order is source-specific because one logical record may collapse `.z64`, `.v64`, and `.n64` paths. The lookup key always retains the full fingerprint as a disambiguator: use `LIBRARY_LOOKUP_HEADER` only for a syntactically valid cartridge-ID/country tuple, otherwise use `LIBRARY_LOOKUP_FINGERPRINT`. Unknown country codes map deterministically to `ROM_REGION_UNKNOWN` or `ROM_REGION_OTHER` per the reviewed table; malformed/homebrew/hack/prototype headers never index outside bounds or borrow a retail key without fingerprint disambiguation.

Snapshot owns:

- fixed-width record and per-path source-signature tables;
- packed path/string pool;
- generation and explicit `EMPTY`, `STALE`, `REVALIDATING`, `FRESH`, or `FAILED_STALE` status;
- scan warning/error counts;
- reference count or an equally explicit acquire/release contract.

External metadata dependency signatures are explicitly deferred because this Phase 3 index does not read external metadata files. Adding such dependencies later requires a schema bump and explicit invalidation rule.

### Step 2: Test immutability and failure

Required:

- old acquired generation unchanged during builder work;
- completed builder freezes into new generation;
- OOM/failure leaves old snapshot published;
- z64/v64/n64 copies with compatible source attributes collapse into one record;
- injected equal fingerprint with contradictory size/header attributes fails the candidate generation closed;
- all paths and source signatures retained;
- primary path chosen by normalized case-folded lexical order with original path tie-break;
- directory-order permutations produce identical sort/order;
- status transitions `EMPTY -> STALE -> REVALIDATING -> FRESH` and `REVALIDATING -> FAILED_STALE` are deterministic;
- selection lookup by fingerprint survives additions, removals, same-size replacement, rename, reorder, and snapshot replacement.

### Step 3: Integrate scanner builder/freeze

Partial scans are never published as fresh. Cancelled builders are destroyed. The old published snapshot remains usable until a complete new snapshot is available.

### Step 4: Verify and commit

```sh
make host-test-sanitize
git add src/menu/library tests Makefile
git commit -m "feat: publish immutable library snapshots"
```

---

## 11. Task 8 — Add the menu-owned library service

**Objective:** Own scanner/snapshot lifecycle centrally without embedding records in `menu_t`.

**Files:**

- Create: `src/menu/library/library_service.c`
- Create: `src/menu/library/library_service.h`
- Create: `tests/test_library_service.c`
- Modify: `src/menu/menu_state.h`
- Modify: `src/menu/menu.c`
- Modify: `Makefile`
- Modify: `tests/Makefile`

### Step 1: Define opaque ownership

`menu_t` stores only:

```c
library_service_t *library_service;
```

The service API includes:

```c
bool library_service_init(...);
void library_service_poll(library_service_t *, menu_mode_t mode);
void library_service_request_pause(library_service_t *);
void library_service_resume(library_service_t *);
void library_service_request_cancel(library_service_t *);
void library_service_restart(library_service_t *);
bool library_service_is_quiesced(const library_service_t *);
const library_snapshot_t *library_service_snapshot_acquire(...);
void library_service_snapshot_release(...);
void library_service_free(library_service_t *);
```

### Step 2: Freeze safe-mode and quiescence policy

Discovery, hashing, cache decode/encode, and cache write jobs are allowed only in:

- Home;
- All Games.

Lifecycle drain/close may proceed elsewhere, but Browser, Details, Error/Fault, loaders, USB reboot, and Boot start no new library work.

`QUIESCED` has one exact meaning across scanner and later cache jobs:

- no active directory iterator;
- no open ROM/cache file;
- no adapter callback currently inside I/O;
- no pending snapshot swap/publication;
- no queued write chunk or deferred callback;
- all open-handle/in-I/O counters are zero.

A paused service may retain immutable snapshots and logical replay checkpoints only. It may not retain a FatFs iterator or file handle across views.

### Step 3: Integrate loop ownership

- `menu_init()`: allocate/configure only after storage/settings paths exist; do not synchronously scan.
- first Home/All Games frame: start/restart scanner.
- `menu_run()`: one bounded service poll per rendered frame after current view draw and before transition handling; record poll/USB timing in instrumentation builds.
- every exit from a work-running mode—Home or All Games—routes through the transition coordinator and defers `menu->mode`/`next_mode` change until service quiescence; this includes Home -> Browser, Home -> All Games, and future Home destinations.
- `menu_deinit()`: assert or fail closed unless service is cancelled and quiesced; free before global subsystems close.

### Step 4: Test lifecycle contract

Cover:

- init/poll/pause/quiesce/resume;
- cancel/quiesce/restart;
- no partial publication;
- safe-mode enforcement;
- Home -> Browser, Home -> All Games, All Games A/B, and future work-running-mode exits wait with unchanged mode while I/O is active;
- at most one work budget per rendered frame;
- callbacks/I/O drained before quiesced;
- no free while active;
- old snapshot retained after failure.

### Step 5: Verify and commit

```sh
make host-test-sanitize
# cross-build default and feature combinations
git add src/menu/library src/menu/menu.c src/menu/menu_state.h tests Makefile
git commit -m "feat: own library lifecycle in menu"
```

---

## 12. Task 9 — Replace fixtures with the real All Games view

**Objective:** Render snapshot records, preserve selection by identity, and keep the proven owned-path Details seam.

**Files:**

- Create: `src/menu/views/all_games.c`
- Delete after cutover: `src/menu/views/static_library.c`
- Modify: `src/menu/menu_state.h`
- Modify: `src/menu/menu.c`
- Modify: `src/menu/views/views.h`
- Modify: `src/menu/views/home.c`
- Modify: `src/menu/views/load_rom.c`
- Modify: `src/menu/path.c`
- Modify: `src/menu/path.h`
- Modify: `Makefile`
- Create: `tests/test_library_view_model.c`
- Modify: `tests/Makefile`

### Step 1: Add fallible path construction

Keep stock asserting APIs unchanged. Add:

```c
bool path_try_init(path_t **out, const char *prefix, const char *path);
bool path_try_clone(path_t **out, const path_t *source);
```

All Games uses the fallible API. OOM must remain in All Games with a recoverable message and no stale/pending launch path.

### Step 2: Replace mode/state names

Rename:

- `MENU_MODE_STATIC_LIBRARY` -> `MENU_MODE_LIBRARY`;
- `static_library` menu state -> bounded `library_view` state;
- `view_static_library_*` -> `view_all_games_*`.

Do not retain a second fixture mode.

View state stores by value:

- selected fingerprint and validity;
- last resolved index;
- page/visual offset;
- observed generation;
- pending transition state.

It never stores `library_record_t *`, borrowed title/path pointers, or mutable scanner state across frames.

### Step 3: Preserve the 3x2 text-card presentation

Until Phase 4 artwork:

- show title resolved as header title -> cleaned filename -> original filename;
- display scan/cache/error status without blocking Home;
- page through all records;
- preserve focus by fingerprint after generation changes;
- choose documented nearest-index fallback only if selected fingerprint disappears.

### Step 4: Gate every All Games exit on quiescence

Because the current view table has no teardown callback, no All Games path may assign `menu->next_mode` while service I/O is active.

On A:

1. copy selected fingerprint into pending view state;
2. request service pause;
3. suppress further selection input but continue drawing progress/status;
4. wait across frames for `QUIESCED`;
5. reacquire current snapshot and resolve the same fingerprint;
6. create one owned `path_t` with `path_try_init()`;
7. call existing `view_load_rom_set_pending_path(menu, path, MENU_MODE_LIBRARY)`;
8. only then set `menu->next_mode = MENU_MODE_LOAD_ROM`.

On B or any Library-origin error/navigation exit:

1. store the requested destination;
2. request pause or cancel according to whether return/resume is valid;
3. wait across frames for `QUIESCED`;
4. only then assign `menu->next_mode`.

Tests must prove no mode change occurs on the request frame when a directory/file is active. Do not enter Home, Browser, Details, Error, a loader, USB reboot, or Boot before quiescence. Do not change `resolve_rom_path()` ownership semantics.

### Step 5: Compatibility flag migration

Introduce `AURORA64_LIBRARY`, default `0`. For one transition release, allow `AURORA64_LAUNCH_PROOF` to map to the same real-library route when `AURORA64_LIBRARY` is not explicitly set; it must no longer compile hard-coded fixtures.

Required clean-build commands include at least:

```sh
make clean && make -j2 AURORA64_HOME=0 AURORA64_LIBRARY=0 AURORA64_LAUNCH_PROOF=0
make clean && make -j2 AURORA64_HOME=1 AURORA64_LIBRARY=0 AURORA64_LAUNCH_PROOF=0
make clean && make -j2 AURORA64_HOME=1 AURORA64_LIBRARY=0 AURORA64_LAUNCH_PROOF=1
make clean && make -j2 AURORA64_HOME=1 AURORA64_LIBRARY=1 AURORA64_LAUNCH_PROOF=0
make clean && make -j2 AURORA64_HOME=0 AURORA64_LIBRARY=1 AURORA64_LAUNCH_PROOF=0
```

Also build default, release-equivalent `-DNDEBUG`, and instrumented Home + Library artifacts. The build log and hardware report identify the exact enabled artifact by path and SHA-256; plain `make` is never labeled as Library-enabled.

### Step 6: Verify and commit

```sh
make host-test-sanitize
# run clean container build matrix
git add -A src/menu Makefile tests
git commit -m "feat: replace launch-proof fixtures with All Games"
```

---

## 13. Task 10 — Add contextual Error -> All Games return

**Objective:** Missing indexed ROMs fail closed and B returns to the same All Games identity/page when safe.

**Files:**

- Modify: `src/menu/menu_state.h`
- Modify: `src/menu/views/error.c`
- Modify: `src/menu/views/views.h`
- Modify: `src/menu/views/load_rom.c`
- Modify: `src/menu/views/all_games.c`
- Create: `tests/test_error_context.c`
- Modify: `tests/Makefile`

### Step 1: Add validated one-shot context

Store:

```c
typedef struct {
    bool valid;
    menu_mode_t return_mode;
    bool fingerprint_valid;
    rom_fingerprint_t fingerprint;
    int32_t page_anchor;
} menu_error_context_t;
```

Keep `menu_show_error()` as Browser-default behavior. Add a contextual wrapper that accepts only recognized Library origins and valid data; invalid modes collapse to Browser.

### Step 2: Preserve context before Details cleanup

When a Library-origin Details path fails—missing file, `rom_config_load()` failure, metadata/config failure, or pre-staging validation—capture return context before `clear_active_details()` resets return modes.

No stale `rom_info_t`, pending path, or load flag may survive.

### Step 3: Return and resume safely

Error B:

- consumes/clears the one-shot context;
- returns to All Games without manufacturing Browser state;
- restores selected fingerprint/page anchor;
- resumes paused builder when no launch cancellation occurred;
- falls back to Browser on forged/stale/disabled context.

### Step 4: Test

Cover:

- missing indexed ROM -> Error, no launch callback;
- Error B -> same identity/page;
- selected identity absent after refresh -> deterministic fallback;
- forged mode -> Browser;
- context consumed once;
- ordinary Browser/History/Favorites/autoload/Datel errors remain Browser-default;
- repeated missing-path cycles do not leak.

### Step 5: Verify and commit

```sh
make host-test-sanitize
git add src/menu tests
git commit -m "feat: return library errors to All Games"
```

---

## 14. Task 11 — Gate launch/boot staging on cancellation and quiescence

**Objective:** Ensure no scanner/cache work survives into ROM/save/disk/emulator staging, USB reboot, Boot, or teardown.

**Files:**

- Modify narrowly: `src/menu/views/load_rom.c`
- Modify narrowly: `src/menu/views/load_disk.c`
- Modify narrowly: `src/menu/views/load_emulator.c`
- Modify narrowly: `src/menu/usb_comm.c`
- Modify: `src/menu/menu.c`
- Create: `tests/test_library_transition_guard.c`
- Modify: `tests/Makefile`

### Step 1: Add menu-level transition coordinator

The coordinator records every requested transition out of any work-running mode (Home or All Games) and out of Details when a paused service may resume, then:

1. requests pause for a resumable navigation transition or cancel for staging/reboot/Boot/teardown;
2. continues rendering/polling without changing `menu->mode` until `QUIESCED`;
3. verifies unpublished builder discarded when cancelled, no pending publication/write chunk, and all open I/O/callback counters zero;
4. only then assigns the destination mode or executes the already-existing staging trigger.

Because views have no teardown callback, tests enumerate Home -> Browser, Home -> All Games, A, B, contextual error, Home/Browser fallback, disk/emulator loader, USB reboot, Boot, and deinit paths explicitly.

### Step 2: Distinguish Details B from launch A

- ordinary Details B: return to All Games and resume the paused builder;
- Details A launch request: cancel, quiesce, then set the existing `load_pending.rom_file` path;
- disk/emulator staging and USB reboot: cancel and quiesce before existing behavior.

### Step 3: Prove protected order

Tests/review must show the existing N64 load body still performs:

1. `cart_load_n64_rom_and_save()`;
2. history bookkeeping;
3. Boot selection;
4. boot parameter setup.

No library function may run between staging and boot handoff.

### Step 4: Verify protected diff

```sh
git diff 909f1f30 -- \
  src/menu/cart_load.c \
  src/menu/bookkeeping.c src/menu/bookkeeping.h \
  src/flashcart src/boot src/main.c
```

Expected: empty.

### Step 5: Commit

```sh
git add src/menu/menu.c src/menu/views/load_rom.c \
  src/menu/views/load_disk.c src/menu/views/load_emulator.c \
  src/menu/usb_comm.c tests
git commit -m "fix: quiesce library before load and boot"
```

---

## 15. Task 12 — Instrument and accept the in-memory implementation on 4 MiB hardware

**Objective:** Verify the production implementation stays within the limits frozen by Task 2A before adding cache writes or artwork.

**Files:**

- Create: `src/menu/library/library_metrics.c`
- Create: `src/menu/library/library_metrics.h`
- Modify: `src/menu/library/library_service.c`
- Modify: `src/menu/menu.c`
- Modify: `src/menu/views/home.c`
- Modify: `src/menu/views/all_games.c`
- Create: `docs/verification/aurora64-phase3-in-memory.md`
- Modify: `Makefile`

### Step 1: Record bounded metrics in memory

Use `sys_get_heap_stats()`, `TICKS_READ()`, and short tick deltas. Record:

- post-init free heap;
- Home first-frame free heap;
- All Games and Details entry/minimum free heap;
- minimum free heap;
- Phase 3 owned live/peak allocations;
- max scanner poll duration;
- max gap between `usb_comm_poll()` calls;
- max action-update gap;
- bytes/entries per poll;
- full scan duration;
- pause/cancel request-to-quiesced duration;
- generation, records, paths, errors, open handles.

Do not log every frame. Show a bounded on-screen summary and emit one summary at scan completion/cancel if debug capture is available.

### Step 2: Build and archive the matrix

Every configuration starts with `make clean` because feature flags are not encoded into object dependencies. Archive exact command, warnings, ROM size, SHA-256, and source/submodule commits.

At minimum, run the explicit commands from Task 9, including:

```sh
make clean && make -j2 AURORA64_HOME=0 AURORA64_LIBRARY=0 AURORA64_LAUNCH_PROOF=0
make clean && make -j2 AURORA64_HOME=1 AURORA64_LIBRARY=1 AURORA64_LAUNCH_PROOF=0
```

Also archive default, Home-only, legacy compatibility, release-equivalent Home + Library, and instrumented Home + Library builds. Only the artifact from an explicit `AURORA64_HOME=1 AURORA64_LIBRARY=1` command may be deployed as the Phase 3 enabled acceptance artifact.

### Step 3: Use only volatile SC64 deployment

Allowed sequence for the archived enabled artifact:

```sh
./tools/sc64/sc64deployer list
./tools/sc64/sc64deployer info
./tools/sc64/sc64deployer upload <exact-enabled-artifact>
./tools/sc64/sc64deployer debug --no-writeback --init reboot
```

If Docker remote mode is required, pass `--remote host.docker.internal:9064`. Record ROM-byte readback and SHA comparison where the tooling path supports it.

Forbidden:

- `make run-debug-upload`;
- `send-file` to `/sc64menu.n64`;
- persistent menu replacement;
- game launch/save claims.

### Step 4: Hardware cases on original 4 MiB N64

Run at least three cold-boot attempts per required configuration and repeated scan -> Details -> return cycles. Cover:

- normal current SD collection;
- `.z64` and `.v64` paths;
- empty/no-ROM outcome where practical;
- safe malformed inputs: truncated file, invalid magic, unsupported extension, empty directory, and valid deep/long FAT paths;
- missing file between index and Details;
- repeated pause/resume and cancel/restart;
- selection stability across refresh;
- Browser fallback;
- Error B contextual return;
- no downward heap trend beyond a frozen tolerance.

Unreadable-entry, short-read, transport failure, and mid-read mutation are deterministic fake-backend tests. Do not corrupt or remove the live SD to manufacture them. Any real hardware I/O-failure experiment requires a separately approved sacrificial image/card procedure.

This slice introduces no new Aurora cache write. Existing menu startup may create/load its normal `/menu` state, so report “Phase 3 scanner/cache introduced no write path,” not “the SD was never written.”

### Step 5: Freeze numeric acceptance thresholds

Record measured and approved:

- scan bytes/entries/time budget;
- max input and USB service gaps;
- Home-visible/full-scan latency;
- minimum free heap and leak tolerance;
- max roots/depth/records/paths/string bytes;
- pause/cancel quiescence latency;
- explicit OOM/failure UI behavior.

Revision gate: Phase 3 in-memory must be independently spec/quality reviewed and pass this hardware evidence before Task 13 begins.

### Step 6: Commit evidence

```sh
git add src/menu/library src/menu/menu.c src/menu/views \
  Makefile docs/verification/aurora64-phase3-in-memory.md
git commit -m "test: verify Phase 3 in-memory library"
```

---

## 16. Task 13 — Implement strict cache codec without target writes

**Objective:** Prove cache serialization, bounds, corruption recovery, and cooperative I/O logic entirely through host/fake backends first.

**Files:**

- Create: `src/menu/library/library_cache.c`
- Create: `src/menu/library/library_cache.h`
- Create: `tests/test_library_cache.c`
- Modify: `src/menu/library/library_service.c`
- Modify: `Makefile`
- Modify: `tests/Makefile`

### Step 1: Define explicit wire format

Include:

- magic;
- schema version;
- endian marker;
- header/total lengths;
- generation;
- root-set fingerprint;
- record/source/path/string counts;
- each source path’s size, FAT modification time, normalized header CRC32, and normalized fixed-sample CRC32;
- payload length;
- CRC32 over encoded header/payload;
- completion footer marker.

Encode fixed-width integers explicitly. Never serialize raw C structs, padding, pointers, or host endianness.

### Step 2: Implement strict decoder tests first

Cover:

- round trip;
- wrong magic/version/endian;
- every-byte truncation;
- oversized count/length before allocation;
- integer overflow;
- header/payload/footer corruption;
- unknown future schema;
- OOM at every allocation;
- stale root-set fingerprint;
- structurally valid cache publishes only as `STALE`, then moves to `REVALIDATING`;
- matching source signatures keep cached records visible as stale but do not make them fresh;
- every revalidated source completes the same two matching canonical hash passes before `FRESH` publication;
- size/mtime/header/sample changes—including same-size replacement—force reconciliation and never bypass full identity;
- successful complete rescan publishes `FRESH`;
- refresh failure keeps records as `FAILED_STALE` with visible error status;
- highest valid generation selection;
- valid stale cache retained when refresh fails.

### Step 3: Make codec cooperative and lifecycle-aware

Encoding/decoding uses bounded poll states. A finished scan must not move the frame stall into one large cache allocation/write. Codec jobs register open-handle, in-I/O, pending-publication, and queued-chunk state with `library_service_t`; pause/cancel closes any active read handle and reaches the same exact `QUIESCED` contract as scanning. Codec work starts only in Home/All Games.

### Step 4: Keep target persistence disabled

`AURORA64_LIBRARY_CACHE_WRITE` defaults to `0`. Target builds may decode fixture/cache input for tests, but must not open a writable target cache file yet.

### Step 5: Verify and commit

```sh
make host-test-sanitize
# full cross-build matrix
git add src/menu/library tests Makefile
git commit -m "feat: add strict library cache codec"
```

---

## 17. Task 14 — Add two-slot cache persistence on disposable media

**Objective:** Add recoverable SD persistence without relying on unavailable atomic rename/fsync APIs.

**Prerequisite:** explicit Josh approval after Task 12 hardware evidence and Task 13 review.

**Files:**

- Modify: `src/menu/library/library_fs.h`
- Modify: `src/menu/library/library_fs_libdragon.c`
- Modify: `src/menu/library/library_cache.c`
- Modify: `src/menu/library/library_service.c`
- Modify: `src/menu/menu.c`
- Modify: `Makefile`
- Modify: `tests/support/fake_library_fs.c`
- Modify: `tests/test_library_cache.c`
- Create: `docs/verification/aurora64-phase3-cache.md`

### Step 1: Use two complete slots

Paths under the existing cache directory:

```text
/menu/cache/aurora64-library-a.bin
/menu/cache/aurora64-library-b.bin
```

Protocol:

1. validate both slots;
2. publish highest valid generation as `STALE` immediately;
3. transition to `REVALIDATING` and scan/reconcile source signatures and full identities in background;
4. publish `FRESH` only after a complete successful generation, or `FAILED_STALE` while retaining the old records on failure;
5. write only inactive/older slot in bounded chunks;
6. check every write and close;
7. reopen and fully validate;
8. keep old slot untouched;
9. select highest valid complete generation at next boot.

No selector file, delete-before-write, or assumed atomic rename.

### Step 2: Fault-inject every operation and repeat transition guards

Host fake backend fails after each open/write/short-write/close/reopen/read point. Prior valid slot must remain loadable. Partial new slot must never outrank it.

Extend `tests/test_library_transition_guard.c` with active cache decode/encode/write cases. All Games A/B, Details launch, contextual error, disk/emulator staging, USB reboot, Boot, and teardown must defer mode/staging until the writer has closed its file, cleared queued chunks, and the whole service reports `QUIESCED`. Cache work never starts outside Home/All Games.

### Step 3: Hardware validation only on approved disposable clone

Normal write/readback/reboot recovery first. Do not power-cut during active writes until a separate interruption protocol is explicitly approved.

Record:

- slot/generation selected;
- bytes/writes/polls;
- cache-visible/startup timing;
- cache-write peak memory;
- reopen/validation result;
- SD before/after inventory evidence;
- recovery from an intentionally pre-created corrupt inactive slot.

### Step 4: Enable only after evidence

`AURORA64_LIBRARY_CACHE_WRITE=1` remains opt-in until disposable-media acceptance passes. Default/off and in-memory modes must remain buildable and functional.

### Step 5: Commit

```sh
git add src/menu/library src/menu/menu.c tests Makefile \
  docs/verification/aurora64-phase3-cache.md
git commit -m "feat: persist library index in two slots"
```

---

## 18. Task 15 — Final integration, review, and release-candidate checkpoint

**Objective:** Prove Phase 3 is internally complete without claiming launch/save or Phase 4 artwork readiness prematurely.

### Step 1: Run full host gates

```sh
make host-test
make host-test-sanitize
```

Expected: all tests pass with no sanitizer findings.

### Step 2: Run clean target build matrix

Use `aurora64-dev:v0.3.2`, clean before each flag combination, and archive warnings/size/hash. Expected: all supported default/off/Home/Library/release/instrumented combinations build.

### Step 3: Verify protected boundaries

```sh
git diff 909f1f30 -- \
  src/menu/cart_load.c \
  src/menu/bookkeeping.c src/menu/bookkeeping.h \
  src/flashcart src/boot src/main.c
```

Expected: empty.

Function-level review of modified loader files must confirm only lifecycle gating was added and staging order remains unchanged.

### Step 4: Independent integration reviews

Require:

- specification review against this plan and product roadmap;
- code-quality/ownership review;
- 4 MiB memory/timing review;
- cache/recovery review if Task 14 was approved;
- license/attribution review for SHA-256 and libdragon changes.

All critical/important findings must be fixed and re-reviewed.

### Step 5: Final hardware evidence

- volatile upload only;
- byte-for-byte readback;
- stock SD menu remains next boot;
- three cold boots per required configuration;
- repeated scan/navigation/details/error cycles;
- Browser remains available;
- no game launched and no save behavior claimed unless separately approved.

### Step 6: Commit final integration documentation

```sh
git add docs src tests Makefile .github/workflows/build.yml libdragon
git commit -m "Complete Phase 3 real library foundation"
```

Do not push until Josh reviews the branch and commits.

---

## 19. Required acceptance statement

Before the launch/save gate:

> Aurora64 Phase 3 discovers and indexes the configured ROM library, preserves identity-backed All Games selection, reaches the correct stock Details screen, recovers contextually from missing indexed paths, and keeps the stock SD menu as the persistent next boot. Game launch and save creation/writeback from Aurora remain unaccepted.

If only the in-memory slice is complete:

> Aurora64 Phase 3 in-memory discovery is accepted on original 4 MiB hardware. Library cache persistence remains disabled pending disposable-media validation.

Only after Task 14 passes:

> Aurora64 Phase 3 two-slot library cache persistence is accepted on the tested disposable SD configuration. This does not imply artwork, game launch, or save acceptance.
