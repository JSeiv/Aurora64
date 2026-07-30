# Aurora64 Phase 0 Regression Test Plan

Status: test specification; **no results are recorded here**

Upstream reference: `Polprzewodnikowy/N64FlashcartMenu` tag `V0.3.2`, commit `6407ab15f6c19d1bf9ded5104c1c8b3c36471379`

Primary target: original N64 + SummerCart64 (SC64). Emulator coverage is menu/UI smoke only.

## 1. Purpose and safety boundaries

Establish a reproducible menu, launch, save, media, and recovery baseline before Aurora64 UI work. Real SC64 hardware is required for launch, save/writeback, and byte-order acceptance.

Treat these upstream paths as **protected boundaries**:

- **Launch:** ROM inspection/configuration, `cart_load_n64_rom_and_save()`, `boot_params_t`, `boot()`, CIC/region handling, and boot handoff.
- **Save:** `flashcart_load_save()` creation, size validation, initialization, loading, and writeback registration.
- **SC64:** firmware checks, initialization order, address maps, commands/config IDs, save mappings, writeback sectors, lock, and deinitialization.

Phase 0 must not alter these boundaries. Stop and escalate a failure involving boot timing, CIC, anti-piracy, save writeback, or low-level SC64 behavior; compare the identical fixture against unchanged V0.3.2 before proposing a fix.

### Deployment prohibition

Use only a **volatile SC64 ROM upload** for early builds. Do not run `make run-debug-upload`, use `send-file` against the root menu, or replace SD `/sc64menu.n64`. No Phase 0 gate authorizes SD-root replacement.

Follow `docs/aurora64_usb_development.md` only after the manifest cites an authoritative, validated, **save-capable** volatile upload/reboot procedure: upload `output/N64FlashcartMenu.n64` to volatile ROM memory, reboot into it, and power-cycle back to the known-good SD menu. The validated procedure must preserve normal save/writeback behavior and must not use the debug `--no-writeback` path. `--no-writeback` is for menu-only debugging and is forbidden for save cases.

## 2. Two regression modes and exact identity

Results from the following modes are separate; never combine their attempts into one frequency.

### B — baseline characterization

B runs **unchanged** V0.3.2 at exactly commit `6407ab15f6c19d1bf9ded5104c1c8b3c36471379`, with recorded submodule commits and a clean tree. Record the build command/toolchain and SHA-256 of the resulting artifact. The artifact hash, not its filename, identifies the binary. Embedded timestamps may make separate builds differ, so select and archive one baseline artifact and use that exact hash for all paired B comparisons.

B characterizes upstream behavior; it is not expected to contain Aurora Home, library, index, or Aurora-config UI. A candidate-only screen appearing in B is an identity failure, not a pass.

### C — Aurora candidate regression

C runs one named Aurora commit plus recorded dirty state, submodule commits, feature flags, build command/toolchain, and one archived artifact SHA-256. A dirty build is allowed only when its patch is archived and approved by the test owner. Rebuilding creates a new candidate identity unless the artifact hash is identical.

Only C expects **Home** and Aurora-specific library/index/config behavior. Candidate acceptance requires the C expectation and, where specified, a paired B observation made with the same console, SC64, media image, ROM/save fixture, and effective upstream configuration. If any identity field or fixture hash differs unexpectedly, invalidate the pair and rerun it.

No result may be relabeled from B to C, inferred across artifacts, or inferred across emulator/hardware, memory size, ROM revision, byte order, or save type.

## 3. Result, repetition, and applicability policy

### Independent attempts versus transition coverage

Every **required applicable** case must PASS in three independent attempts per mode/configuration (`3/3`):

1. restore the fixture and effective configuration to the case's recorded starting hashes; power off before hardware/media changes; then cold boot;
2. restore the same starting hashes again; cold boot and run the case independently;
3. restore the same starting hashes again; cold boot after at least 10 seconds powered off and run independently.

The ten- or twenty-cycle navigation loops within an endurance case are **transition coverage inside each attempt**, not substitutes for three attempts. Reset/re-entry transitions requested by a case are also performed inside each attempt. Do not seed attempt 2 from attempt 1.

For stateful/save cases, clone the pristine save/media image for each attempt, verify its starting SHA-256, and archive its ending hash separately. Restore by re-imaging or byte-for-byte copy while the console and SC64 are powered off. Expected output changes (the unique in-game marker) must be listed; every unrelated file must retain its starting hash. A failed or interrupted attempt is preserved for evidence, then a fresh clone is used. Never restore over the only evidence or personal save.

Use `PASS`, `FAIL`, `BLOCKED`, or `NOT APPLICABLE`:

- required applicable cases gate and require `3/3 PASS`; `BLOCKED`/missing data fail the gate;
- optional/informational cases are explicitly labeled and never gate; `NOT TESTED` may be recorded for them;
- `NOT APPLICABLE` requires a named applicability owner, dated rationale, and evidence (for example, unsupported hardware). The test lead owns applicability unless the session manifest delegates it;
- do not use `SKIP`. It cannot resolve a required gate.

Any crash, hang, wrong game, corrupted display, launch mismatch, unexplained save/media change, or intermittent result is FAIL.

### Per-attempt evidence record

| Field | Required entry |
|---|---|
| Test / mode / attempt | ID, B or C, and independent attempt 1–3 |
| Date/time / people | Local timestamp, tester, test lead/applicability owner |
| Build identity | Commit/tag, dirty state and patch, submodules, menu version, feature flags, toolchain/build command |
| Artifact | Filename, byte size, SHA-256; archive path |
| Target | Emulator exact name/version/build or N64 board/region/serial notes |
| Emulator pin | Core/plugins, renderer, settings/profile hash, region, memory, command line |
| Memory/controllers | Jumper or Expansion Pak; controller and Controller Pak identities |
| SC64 | Hardware revision, firmware, deployer exact version, boot mode |
| Media/config | SD identity/filesystem/free bytes; image and effective config hashes |
| Fixture manifest | Manifest revision/hash and case fixture IDs |
| ROM identity | Legal dump filename, game code/region/revision, byte order, byte size, SHA-256 |
| Save precondition | Type, filename, exact size, SHA-256, clone/backup, expected marker |
| Procedure source | Product/SC64 document title, version/URL or repository path and section |
| Steps/actual | Exact navigation, launch, game save, reset/power sequence and observed results |
| Timing | Named start/end events and measured values; paired B/C delta where applicable |
| Evidence | Photo/video, logs, screenshots, hashes, SC64 output |
| Result/frequency | `PASS`, `FAIL`, `BLOCKED`, or `NOT APPLICABLE`; cumulative `n/3` |
| Comparison/recovery | Paired B result, defect, first bad build, recovery performed |

Keep raw evidence under a dated path such as `test-results/YYYY-MM-DD_<commit>/`; do not commit ROMs, saves, or copyrighted artwork.

## 4. Session manifest and consolidated preflight

Before any test session, create and approve a fixture/session manifest outside the repository. Gate A remains BLOCKED until every required value is concrete; do not invent a ROM hash, expected size, timing limit, or product procedure.

The manifest must contain:

- B and C source/submodule identities, dirty state/patch, build commands, toolchain/container image, warnings, artifact filenames/sizes/SHA-256, and archives;
- emulator exact version/build and pinned core/plugins/renderer/settings/profile hash, region, memory size, and command line;
- N64/SC64/SD/controller identities, firmware/deployer versions, power/video path, filesystem/free bytes, and effective config hashes;
- a fixture table mapping each test ID to legal ROM SHA-256, byte size/order, header/game code, region/revision, expected database save type, exact expected save-file size, pristine save/media hash, and backup/recovery path;
- the mandatory **The Legend of Zelda: Ocarina of Time, USA v1.2, byteswapped `.v64`** identity and hash;
- an authoritative, validated save-capable volatile upload/reboot procedure, with a versioned source or controlled procedure ID, exact deployment commands, expected SC64 state transitions, and validation evidence that normal writeback remains enabled. It must not use `make run-debug-upload`, `--no-writeback`, `send-file` against the root menu, or SD-root replacement;
- authoritative product/SC64 procedure citations for ordinary launch, in-game save completion, reset/return, power-off, pending writeback completion, and recovery. Record steps verbatim or by controlled procedure ID; the plan supplies no guessed delay;
- explicit measured acceptance values for fixture-dependent timing/free-space cases, including named measurement events, B sample values, permitted candidate delta, rationale, approver, and approval date. If not established before C runs, the affected required case is BLOCKED;
- owner/rationale for each `NOT APPLICABLE` decision and labels for required versus optional cases.

Perform this consolidated preflight once before each session and reference its record from every attempt:

1. Confirm B identity is clean V0.3.2 and C identity is exact; build/archive both and record hashes. Verify the deployed/read-back artifact where practical.
2. Verify readable, hash-matching backups of the complete known-good SD, `/sc64menu.n64`, `/config.ini`, `/menu`, sidecars, configs, indexes, and saves.
3. Inventory and hash every ROM/save/fault fixture. Use only legal known-good dumps and disposable save/media clones.
4. Record hardware, emulator, firmware, deployer, media, controller, memory, and effective configuration pins. Disable cheats, patches, autoload, and fast reboot unless a case enables one.
5. Run `sc64deployer list` and `info`; record state, SD initialization, voltage, temperature, firmware, and deployer. Do not upgrade/downgrade firmware during a matrix.
6. Boot and verify the known-good SD menu; use the approved save-capable volatile upload/reboot procedure for the selected artifact and capture deployer output; verify a power cycle returns to the unchanged root menu.
7. **Power the N64 and SC64 fully off before swapping Jumper/Expansion Paks, Controller Paks, SD cards, adapters, or write-lock state.** Re-run relevant identity/hash checks after a swap.

Stop before testing if any required identity, fixture, backup, recovery image, exact expected value, or authoritative safe procedure is absent.

## 5. Required environments

| ID | Environment | Requirement |
|---|---|---|
| E1 | Primary emulator | Manifest-pinned ares version/build and settings; 4 MiB, NTSC-U |
| E2 | Secondary emulator | Manifest-pinned Gopher64 or independent implementation; optional/informational |
| H1 | Original N64 | NTSC-U console, **Jumper Pak (4 MiB)**, SC64, known-good power/video/controller |
| H2 | Original N64 | Same console with **Expansion Pak (8 MiB)**; optional in Phase 0 unless an accepted feature claims it |

**Emulator limitation:** in this upstream configuration, an emulator can smoke-test only menu rendering/input/navigation and deterministic fixture presentation. It cannot validate SC64 ROM handoff/launch, save creation or writeback, SC64 reset/power behavior, or hardware byte-order loading. No emulator result gates or substitutes for those H1 cases.

## 6. Emulator menu/UI smoke suite

Run required EM cases in both B and C on E1. E2 observations are optional/informational and non-gating.

| ID | Mode | Action | Expected result/evidence |
|---|---|---|---|
| EM-01 | B+C | Boot the exact artifact with the pinned clean profile. | Menu becomes interactive; stable video/input; no crash/assert/hang. |
| EM-02 | B+C | Navigate fixture directories, open ROM details, cancel, enter settings, return, revisit. | Focus and filenames match the manifest; B and C each follow their documented screen model. |
| EM-03 | C only | Exercise Home → Browser → details → cancel → Home. | Home is present and transitions preserve the recorded selection. This case is not applicable to B by design. |
| EM-04 | B+C | Present manifest `.z64`, `.v64`, and `.n64` entries and inspect menu metadata/config without launching. | Displayed identity/order-dependent metadata matches each fixture; source hashes remain unchanged. This does not validate hardware loading. |
| EM-05 | B+C | Boot without optional art/metadata, then with a copied corrupt PNG. | Selection/navigation remain usable; documented fallback/error appears; no crash. Launch is not asserted. |
| EM-06 | B+C | Perform 20 Browser/details/cancel transitions per independent attempt on 4 MiB emulation. | All 20 transitions complete with valid focus. Record named-event duration and B/C delta; C passes timing only against the pre-approved manifest limit. |
| EM-07 | C only | Perform 20 Home/Browser/Home transitions per independent attempt. | All transitions complete without state loss; timing passes only against the pre-approved manifest value. |

## 7. Hardware launch and byte-order matrix

Run on H1 in B and C. Reach controllable gameplay, play at least two minutes, exercise Start/B/game input, and use only the manifest's cited normal product return/power procedure. Hash source ROMs before/after.

| ID | Game/fixture | Required focus | Expected result |
|---|---|---|---|
| CORE-01 | Mario Kart 64 (USA) | Fast boot, input, race start | Correct title and race load; controls/audio remain stable. |
| CORE-02 | Banjo-Kazooie (USA) | 3D gameplay and EEPROM route | Existing slot intact; controllable level; persistence tested separately. |
| CORE-03 | **Ocarina of Time, USA v1.2, byteswapped `.v64`** | V0.3.2 anti-piracy concern, SRAM, byte order | Manifest header/revision/hash match; title/file select/gameplay run without anti-piracy crash. Evidence explicitly shows `.v64`, USA, v1.2. |
| CORE-04 | Tony Hawk's Pro Skater 2 (USA) | Gameplay/controller timing | Correct selection and playable run load; controls/audio stable. |

| ID | Byte-order case | Expected result on H1 |
|---|---|---|
| BO-01 | Manifest `.z64` fixture | Correct game/config/save-type selection and controllable gameplay; source unchanged. |
| BO-02 | Mandatory Ocarina USA v1.2 `.v64` | Correct revision reaches gameplay; source unchanged. |
| BO-03 | Manifest `.n64` fixture | Correct game reaches gameplay; source unchanged. |
| BO-04 | Same legal dump in two available orders | Equivalent title/game code/config/save path and gameplay; no conflicting save; record paths/hashes. |

For any C failure, rerun B with the same restored fixture and environment. Do not call it an Aurora regression without a valid pair.

## 8. Memory, Expansion Pak, endurance, multiplayer, and accessories

| ID | Requirement/action | Expected result | Phase 0 status |
|---|---|---|---|
| MEM-01 | H1: browse all fixtures, open/cancel details 20 times, launch all core titles. | Every transition completes; no allocation/corruption/hang. Timings and B/C deltas meet pre-approved manifest values. | Required |
| MEM-02 | H2: repeat CORE suite. | No functional regression from the recorded H1 expectation. | Optional unless claimed |
| MEM-03 | H2: Majora's Mask. | Expansion-required behavior reaches gameplay; existing save intact. | Optional unless claimed |
| MEM-04 | H2: Perfect Dark or inventoried equivalent. | Enhanced-memory content reaches gameplay. | Optional unless claimed |
| MEM-05 | H1 negative check for Expansion-required fixture. | Documented base-memory behavior; no crash/media change/false 8 MiB claim. | Optional unless claimed |
| MP-01 | H1: Mario Kart 64, two-player race. | Both controllers independently control expected players. | Required |
| MP-02 | H1/H2: inventoried 3–4 player fixture. | Every available port works through game start; behavior matches B. | Optional |
| MP-03 | Menu navigation with controllers in ports 1–4 and recorded controller mix. | Port 1 controls menu; no phantom action or blocked selection. | Optional |

Power off before every Pak/accessory swap. Optional cases never gate Phase 0 and cannot support a compatibility claim until they pass their own acceptance gate.

## 9. Save integrity suite

### Safe procedure and scope

SAVE-01 through SAVE-08 run on H1 only, from per-attempt disposable clones. Before they may run, the manifest must document both (1) the authoritative, validated save-capable volatile upload/reboot procedure required by Gate A, without the debug `--no-writeback` path, and (2) an authoritative exact SC64/product procedure for normal launch/play, game-reported save completion, return/reset if required, completion of writeback, and permitted power-off. Follow both procedures exactly; do not add a guessed wait, assume LED meaning, or power-cycle merely to force writeback. If either procedure is not documented, all save tests are **BLOCKED**, Gate A cannot pass, and Gate C fails pending resolution.

For each persistence attempt: verify pristine clone hashes; launch normally (never `--no-writeback`); create a unique observable marker; invoke the game's documented save action; observe its documented completion indication; follow the cited normal reset/return/power sequence; relaunch; verify old data and marker; power off by the same procedure; archive file/media hashes. No deliberate interruption is part of this sequence.

Phase 0 samples EEPROM 4 Kbit, EEPROM 16 Kbit, SRAM 256 Kbit, ordinary FlashRAM 1 Mbit, no-save, and Controller Pak behavior. It **does not establish full save compatibility**. The later full matrix must independently cover every canonical menu type, plus no-save and Controller Pak paths:

| Canonical menu type | Upstream V0.3.2 allocation/validation size |
|---|---:|
| EEPROM 4 Kbit | 512 bytes |
| EEPROM 16 Kbit | 2 KiB |
| SRAM 256 Kbit | 32 KiB |
| SRAM banked | 96 KiB |
| SRAM 1 Mbit | 128 KiB |
| FlashRAM 1 Mbit | 128 KiB |
| Pokémon Stadium 2 (`FLASHRAM_PKST2`) | 128 KiB |

These values come from the protected V0.3.2 `SAVE_SIZE` table in `src/flashcart/flashcart.c`; record the applicable value and fixture/product evidence in the manifest. A later matrix must also verify that each type is supported on the target SC64 firmware rather than inferring support from file allocation alone.

| ID | Technology/scenario | Required checks | Expected result |
|---|---|---|---|
| SAVE-01A | EEPROM 4 Kbit fixture | Existing load, normal update/writeback/relaunch | Old data remains; marker persists; manifest size unchanged; unrelated hashes unchanged. |
| SAVE-01B | EEPROM 16 Kbit fixture (Banjo-Kazooie only if manifest verifies it) | Same | Same acceptance for the distinct 16 Kbit type. |
| SAVE-02 | SRAM 256 Kbit; mandatory Ocarina USA v1.2 `.v64` | Existing slot, update, normal return/relaunch | Slot/marker persist; no zeroing, replacement, collision, or size change. |
| SAVE-03 | Ordinary FlashRAM 1 Mbit fixture | Existing load, update, normal return/relaunch | Marker persists at manifest size; no stale/cross-title data. |
| SAVE-04 | Controller Pak (THPS2 only if verified; otherwise manifest fixture) | Read existing note, update/create note, normal return/relaunch | Expected note alone changes; Pak remains readable. |
| SAVE-05 | Missing save file on cloned media | Launch normally; separately observe file creation/size | File is created at manifest size and game can initialize/use it; later normal persistence passes. |
| SAVE-05I | Code-level initialization instrumentation | Unit/harness or temporary instrumented observation of the protected creation path, with no hardware acceptance claim | Evidence shows initialization behavior without requiring an unsafe/unobservable black-box pre-game `0xFF` capture. Informational, non-gating; instrumentation is not the release artifact and must not alter protected code. |
| SAVE-06 | Disposable incorrect-size save | Attempt launch | Explicit recoverable rejection; file size/content hash unchanged. |
| SAVE-07 | No-save ROM/homebrew | Launch fixture | No save created; correct title reaches gameplay. |
| SAVE-08 | Isolation across two save types | Save/relaunch each consecutively using normal procedure | Each gets only its own save; markers persist; unrelated hashes unchanged. |
| SAVE-09 | Forced reset/power interruption | Do not execute in Phase 0 | **Deferred, optional, non-gating.** See below. |

SAVE-05 separates observable file creation/size from persistence. It does not require black-box observation of all-`0xFF` contents; initialization may be verified at code level by SAVE-05I.

### Deferred interruption testing (SAVE-09)

Do **not** deliberately reset, unplug, remove media, or power off during possible writeback. SAVE-09 remains prohibited until an authoritative, version-specific SC64 procedure identifies exact states/signals, interruption method, expected failure bounds, and recovery. A future approved protocol must use a cloned disposable SD and saves, never personal/sole copies; identify an operator and abort authority; image/hash before and after; stop on unexpected device state, heat/voltage, write activity, or recovery mismatch; and prove restoration to a known-good image before another attempt. Lack of this protocol is `NOT TESTED`, not a Gate C failure.

**Immediate stop:** on loss or unexplained modification, do not retry or reboot repeatedly. Preserve/write-protect evidence, image the media, restore only to a separate clone, and compare with B after recovery is verified.

## 10. Homebrew and ROM-hack compatibility

| ID | Fixture | Expected result | Status |
|---|---|---|---|
| HB-01 | Known-good open-source homebrew with documented CIC/memory/save expectations | Identified; gameplay/input work; documented save behavior; no commercial metadata misapplied. | Required |
| HB-02 | Legally produced patch/hack such as an identified Smash Remix build | Base/patch/build recorded; gameplay; clean fallback; no base-game save collision. | Optional |
| HB-03 | Filename with spaces, punctuation, and manifest length | Correct file/details/launch; no neighboring selection; path unchanged. | Optional |

## 11. Media, index, artwork, and path failures

Run only after the normal matrix on a freshly restored expendable clone. Hash critical files before/after; power off before removing or swapping media.

| ID | Fault injection | Expected result/recovery | Status |
|---|---|---|---|
| ERR-01 | Remove copied artwork/metadata for one game. | Visible/selectable fallback and correct hardware launch; restore files. | Gate D required in each applicable B/C mode; applicability owner decides from documented mode support |
| ERR-02 | Truncate copied PNG. | No crash; documented fallback/error; other art works; restore. | Gate D required in each applicable B/C mode; applicability owner decides from documented mode support |
| ERR-03 | Move/rename ROM after path/index/history entry. | Never launches neighbor; stale target fails closed; manual browser finds moved ROM. | Gate D required in each applicable B/C mode; applicability owner decides from documented path/index/history support |
| ERR-04 | Remove optional Aurora index. | C boots to documented rebuild/fallback; browser works; ROMs/saves unchanged. | Gate D: C required; B not applicable by design |
| ERR-05 | Corrupt disposable Aurora index. | Validation rejects it; no wrong launch; approved rebuild restores. | Gate D: C required; B not applicable by design |
| ERR-06 | Manifest slow-SD fixture. | No wrong selection/corruption; boot/browse/launch values and B/C deltas meet pre-approved manifest limits. | Optional unless limits/fixture are approved |
| ERR-07 | Nearly-full disposable SD at exact manifest free-byte value. | Reads/launch safe; writes complete or fail explicitly without truncation. | Optional until value approved |
| ERR-08 | Full disposable SD during non-save config/index write only. | Explicit recoverable failure; previous valid file or backup recovers. Never exhaust space during save writeback. | Optional |
| ERR-09 | Remove/corrupt copied Aurora config while upstream backup is preserved. | C uses approved defaults/fallback; no loop; restore recovers. | Gate D: C required; B not applicable by design |

A required case lacking an explicit fixture value, expected fallback, applicability decision, or approved timing delta is BLOCKED; subjective terms such as “obvious slowdown,” “small margin,” or “sane” are not acceptance criteria.

## 12. SC64 configuration and launch checks

| ID | Action | Expected result | Status |
|---|---|---|---|
| SC-01 | Boot with manifest firmware/media/config. | Exact expected firmware decision; SD/menu state matches manifest; recorded telemetry remains within manufacturer limits cited there. | Required |
| SC-02 | Launch core games with automatic CIC/save/TV and no override. | Selection matches manifest and paired B; gameplay reached. | Required |
| SC-03 | Apply copied known-valid per-ROM override, then remove it. | Limited to that ROM; removal restores automatic manifest behavior. | Required |
| SC-04 | Enable supported fast reboot only after ordinary path passes; test disabled/unsupported path. | Both paths match cited product behavior; save procedure remains the approved one. | Required only if applicability owner confirms support; otherwise N/A with rationale |
| SC-05 | Complete approved normal save/return sequence, then launch second title. | Procedure confirms writeback complete before reconfiguration; both saves pass hashes/markers. | Required; BLOCKED without authoritative procedure |
| SC-06 | C only: approved power-off after volatile candidate use, then boot. | `Bootloader -> Menu from SD card`; root-menu hash unchanged. | C required; B not applicable by design |
| SC-07 | Unsupported firmware rejection. | Do not downgrade or induce an unsafe firmware state. | **Deferred, optional, non-gating** unless a safe vendor-supported simulator/fixture and expected rejection are approved |

SC-07 may be reported `NOT TESTED`; it never gates Phase 0 and supports no unsupported-firmware claim.

## 13. Phase gates

### Gate A — identity and readiness

PASS only when B/C identities and archives, fixture/session manifest, exact emulator settings, hardware/SC64/media/config, ROM/save clones, an authoritative validated save-capable volatile upload/reboot procedure that does not use the debug `--no-writeback` path, an authoritative normal SC64 launch/save/writeback/power procedure, explicit required fixture values, owners, backups, and recovery are recorded. All save tests remain BLOCKED until both procedures are documented. Any required unknown is BLOCKED; do not invent it.

### Gate B — emulator UI/menu smoke

PASS only when required EM cases applicable to each mode are `3/3 PASS` on pinned E1. E2 is informational. This gate makes no launch, persistence, SC64, or byte-order hardware claim.

### Gate C — original N64 + SC64

PASS only when H1 preflight; CORE-01–04; BO-01–03; MEM-01; MP-01; SAVE-01A, SAVE-01B, and SAVE-02–08; HB-01; and required applicable SC-01–05 are `3/3 PASS` in both B characterization and C candidate regression, with valid paired comparisons where required, and C-only SC-06 is `3/3 PASS` in C. There must be no unexplained save/media mutation, and an approved normal power cycle must return to the known-good SD menu. Required `BLOCKED`, `NOT APPLICABLE` without approved rationale, or fewer than three independent passes fails this gate.

SAVE-05I, SAVE-09, SC-07, E2, H2, BO-04, expanded multiplayer, HB-02/03, and cases explicitly marked optional/informational do not gate A–C. Their absence cannot be used as evidence of support.

### Gate D — resilience and optional-hardware claims

The Phase 0 resilience gate PASSes only when ERR-01–ERR-05 and ERR-09 are `3/3 PASS` in every mode where their table status makes them required. Every applicability decision must be explicit; `BLOCKED`, missing results, or `NOT APPLICABLE` without the required approved rationale fails Gate D. ERR-06–ERR-08 and optional-hardware cases do not gate Phase 0, but any separate named claim that selects them passes only when every selected case has explicit applicability, fixtures/thresholds, and `3/3 PASS`; untested optional cases must be reported as `NOT TESTED` and excluded from claims.

### Phase 0 exit

Phase 0 exits only after Gates A–D pass, all C failures have valid B comparisons, evidence/recovery are archived, and protected launch/save/SC64 boundaries remain unchanged. Gate D and its required applicable ERR cases cannot be waived by treating resilience as an unclaimed feature. A known upstream failure needs an owner, reproduction, impact, rollback, and explicit exception approval. Do not promote C to SD-root `/sc64menu.n64`.

## 14. Rollback and recovery

1. Stop on save loss, boot hang, SC64 anomaly, wrong-ROM launch, unexpected filesystem write, or any departure from the approved procedure.
2. Capture screen/logs and exact power/reset state. Do not repeatedly reboot a possibly writing card.
3. Follow the manifest's authoritative abort step. If it permits removal, power fully off before removing SD/Paks; write-lock where available and image/hash before repair.
4. Preserve artifact, configs, affected saves/indexes, deployer output, and pre/post hashes. Never overwrite the only evidence.
5. Restore cloned saves or full image to separate disposable media from the verified backup.
6. Restore known-good V0.3.2 `/sc64menu.n64` only from its hash-matching copy; volatile Phase 0 testing should not alter it.
7. Boot the known-good SD menu, verify SC64 identity/state, then validate affected saves using disposable copies and the approved normal procedure.
8. Reproduce once in B with identical restored fixture/environment. If hardware/media is unstable, stop and isolate it.
9. File the defect with per-attempt records. Resume only after recovery verification and test-owner approval.

A cycle is complete only when evidence, failures, blocked/N/A decisions, paired comparisons, and recovery are recorded. This plan specifies tests; it asserts none have run.
