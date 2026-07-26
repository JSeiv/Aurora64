# Aurora64 Phase 3 Task 2A — discovery evidence and frozen decisions

**Status:** PASS — hardware evidence, frozen budgets, 4 MiB feasibility, and final independent revision review accepted
**Target:** original N64 with operator-confirmed 4 MiB Jumper Pak, SummerCart64, unchanged working SD card
**Scope:** read-only discovery, identity/cache design constraints, and production budgets; no production scanner or cache writer

## 1. Decision summary

Task 2A collected three complete natural hardware runs, three repeated Phase 2 view-heap cycles, and four phase-specific cancellation runs. The evidence supports the following decisions:

- scan the single effective root `/`;
- recognize `.z64`, `.v64`, `.n64`, and `.rom`, but require valid N64 header magic;
- use canonical full SHA-256 as identity and retain all 256 bits;
- require two complete, fresh-open, matching canonical passes for a new or changed source;
- retain every compatible source path for one logical record and choose its primary source deterministically;
- run discovery as cancellable background work, never synchronously on Library entry;
- keep 4 KiB as the production read/hash unit; 64 KiB was benchmark-only;
- preserve at least 512 KiB free heap and limit total library-owned live heap to 224 KiB;
- use two independently recognizable/validated cache slots if persistence is later enabled;
- keep cache persistence disabled until the separately gated Task 14 disposable-media tests.

The measured card contained 45 valid ROMs totaling 1,107,296,256 bytes. Full uncached two-pass hashing at the observed cooperative rate projects to about 2.51 hours. That cost is explicitly accepted for an initial/new-source background generation; it is not accepted as view-entry latency. Future validated cache reuse is therefore required for normal steady-state startup.

## 2. Hardware and provenance

### 2.1 Environment

- Console: original N64.
- Memory: operator explicitly confirmed the original Jumper Pak, therefore 4 MiB rather than an Expansion Pak.
- Controller: controller 1 used for navigation, stop requests, and Phase 2 measurements.
- Flashcart: SummerCart64 firmware v2.20.2.
- SD: the existing working card and layout were left installed and unchanged during accepted repeats.
- Deployment: volatile ROM upload, save type `None`, ROM shadow disabled, debugger save writeback disabled.
- Post-capture safety state: ROM writes disabled, ROM shadow disabled, save type `None`.
- The discovery ROM performed read-only SD traversal. Its synthetic encode phase performed no filesystem access.

The 4 MiB/Jumper Pak fact is operator-confirmed session evidence. Firmware and SC64 safety settings were captured through `sc64deployer info`; `sd=1` and successful runtime records are ROM evidence.

### 2.2 Accepted artifact

| Fact | Value |
|---|---|
| Source branch | `feature/phase3-library-foundation` |
| Parent source revision used to build | `d6e197ca42a2d00c38e1570408d13986d25545f8` |
| Pinned libdragon revision | `b2544b29e4369747168a9847e48cfc8fafeaaa15` |
| ROM | `tools/phase3_discovery/phase3_discovery.z64` |
| ROM bytes | 229,376 |
| ROM SHA-256 | `a329ad92b860e2b9d880dcb92952900e5e879b2f40d0ad6f58e7bebe2ca1d28b` |
| ELF sections | text 250,424; data 37,448; bss 413,896; total 701,768 bytes |
| Build environment | `aurora64-dev:v0.3.2`, `linux/amd64` |

The accepted normal-clean artifact was built with the discovery Makefile's exact
ROM metadata variables:

- `TARGET := phase3_discovery`
- `N64_ROM_TITLE := "Aurora P3 Discovery"`
- `N64_ROM_SAVETYPE := none`
- `N64_ROM_REGIONFREE := 1`

The build invocation was:

```sh
docker run --platform linux/amd64 --rm -v "$PWD:/work" -w /work \
  aurora64-dev:v0.3.2 bash -lc \
  'set -e; cd libdragon; make install -j2; make tools-install -j2; \
   cd /work; make -C tools/phase3_discovery clean all'
```

The separate strict diagnostic build added `-Wformat=2`,
`-Wformat-signedness`, and `-Werror=format` through `N64_CFLAGS` while
preserving libdragon's required defaults. Both strict and normal builds passed;
the accepted hardware hash above is the normal-clean output.

The earlier one-shot artifact (`5588bf68...`) produced incomplete historical evidence and is rejected for Task 2A measurements. All accepted records below came from the replay-capable `a329ad92...` artifact.

### 2.3 Accepted normalized logs

The logs remain local evidence artifacts rather than production source. Their paths and checksums are frozen here:

| Evidence | SHA-256 |
|---|---|
| `artifacts/phase3_discovery/hardware_run_cycle_1.log` | `659c2e8543be4d24f6f1895a442f160198a2baa41ea5d5267eee26b470ec471a` |
| `artifacts/phase3_discovery/hardware_run2_cycle_1.log` | `c53ec185c3d0937f2fd162e75b8f1702c684ce121249c79aa08ff3f781232bc7` |
| `artifacts/phase3_discovery/hardware_run3_cycle_1.log` | `039e7a5c511744d1b446c7e311b8b937db6d8caa36d375bc98f932d2a57f3066` |
| `artifacts/phase3_discovery/hardware_stop_scan.log` | `a3c5e03af6fa7a64a020f104d5b83dfc73f7bed1aa4d084e774a8769241d3265` |
| `artifacts/phase3_discovery/hardware_stop_read.log` | `edabf28a251ec2174c2818bb4f9a548816460cd5ffde9ee0aeba4318490b0e60` |
| `artifacts/phase3_discovery/hardware_stop_hash.log` | `8bab396223560a4e12d424516a5864557e934bc81369879e962ff58f6d387a4d` |
| `artifacts/phase3_discovery/hardware_stop_synthetic.log` | `5fa7d35fd21fda5f40e1e999b4601e9ca94c76d21e7e56c2f3a083d57c53727f` |

Each accepted complete capture contains one coherent matching `replay_begin` through `replay_end` cycle. Partial historical captures and extra replay cycles are not combined into accepted evidence.

## 3. Card census and root decision

All three natural runs produced identical census results:

| Metric | Accepted result |
|---|---:|
| configured roots used | 1 |
| effective root | `/` |
| top-level directories | 11 |
| total directories | 14 |
| directory calls / returned entries | 85 / 70 |
| searchable / excluded entries | 64 / 6 |
| ROM candidates / valid ROMs | 45 / 45 |
| logical / stored path bytes | 2,071 / 2,135 |
| retained valid-path arena | 1,972 bytes |
| maximum depth | 2 |
| maximum logical path | 71 bytes |
| total valid ROM bytes | 1,107,296,256 (1,056 MiB) |
| valid size range | 8–64 MiB |
| extension counts | 37 z64; 5 v64; 3 n64; 0 rom |
| detected byte order | 43 z64; 2 v64; 0 n64 |
| extension/order match / mismatch | 39 / 6 |
| size buckets | 13 <16 MiB; 13 16–<32; 17 32–<64; 2 >=64 |
| open / read errors | 0 / 0 |

More than four top-level directories contain valid games, so narrowing the root would omit measured content. Phase 3 therefore freezes one configured/effective root: `/`. `settings.default_directory` is not a library-root alias, and Phase 3 will not add a root editor.

### Frozen exclusion policy

At storage root only, exclude these names using ASCII case-folded FAT comparison:

- `menu.bin`
- `menu`
- `N64FlashcartMenu.n64`
- `ED64`
- `ED64P`
- `sc64menu.n64`
- `System Volume Information`
- `.fseventsd`
- `.Spotlight-V100`
- `.Trashes`
- `.VolumeIcon.icns`
- `.metadata_never_index`

At every depth, exclude `desktop.ini`, `Thumbs.db`, and `.DS_Store` under the same ASCII case-folded comparison, plus every basename longer than two bytes beginning `._`.

The discovery spike compared the listed spellings exactly. Production intentionally strengthens this to ASCII case-folding because FAT lookup is case-insensitive; this change prevents casing variants of known metadata from entering the library.

## 4. Throughput and responsiveness

The N64 timer conversion is 46,875,000 ticks/s. Values below are ranges across the three accepted complete runs.

### 4.1 Enumeration

| Metric | Accepted range |
|---|---:|
| wall time | 3.648991–3.649338 s |
| active directory work | 65.915–66.079 ms |
| maximum directory call | 5.402–5.460 ms |
| active enumeration rate | 1,059–1,061 entries/s |
| cooperative enumeration rate | 19 entries/s |

### 4.2 Whole-run responsiveness maxima

These are ranges across the three complete natural runs. They include the
discovery-only 64 KiB sequential benchmark and therefore describe the spike's
worst whole-run observations, not the intended 4 KiB production schedule.

| Metric | Accepted range |
|---|---:|
| input-service gap | 67.680–67.877 ms |
| frame-service gap | 67.681–67.877 ms |
| USB-service-opportunity gap | 67.680–67.877 ms |
| maximum instrumented work duration | 64.516–64.713 ms |
| work cadence | 785.655–785.662 ms |
| explicit close maximum | 78–79 µs |

USB is a service-opportunity measurement; the spike does not execute the
production `usb_comm_poll()` path. The 64 KiB benchmark is the source of the
longest blocking work and is rejected as a production unit.

### 4.3 Sequential read benchmark

Both request sizes read the complete 64 MiB representative in every natural run with exact coverage and no error.

| Request | Calls | Active rate | Cooperative wall rate | Max call |
|---|---:|---:|---:|---:|
| 4 KiB | 16,385 | 1.038369–1.038591 MiB/s | 0.233651 MiB/s | 4.776–4.789 ms |
| 64 KiB | 1,025 | 1.038873–1.039038 MiB/s | 0.986051–0.986305 MiB/s | 64.517–64.713 ms |

The 64 KiB benchmark improves wall throughput only by doing a blocking call that can exceed 64 ms. It is not an accepted production work unit. Production scanning and hashing are frozen at 4 KiB per bounded unit.

### 4.4 Canonical SHA-256

The on-ROM SHA self-test passed. For each available representative class, two complete fresh-open canonical passes succeeded and matched in all three natural runs.

| Class | Size | Active rate | Wall time per pass | Two-pass wall time |
|---|---:|---:|---:|---:|
| small | 8 MiB | 0.451304–0.451409 MiB/s | 34.283046–34.283069 s | 68.566114–68.566123 s |
| medium | 24 MiB | 0.502867–0.503024 MiB/s | 102.748567–102.748600 s | 205.497150–205.497187 s |
| large | 64 MiB | 0.507543–0.507589 MiB/s | 273.912386–273.912392 s | 547.824777–547.824779 s |

Maximum 4 KiB hash-chunk durations were 9.576–9.632 ms (small), 8.676–8.766 ms (medium), and 8.689–8.835 ms (large). Cooperative SHA rate was about 0.233–0.234 MiB/s.

At 1,056 MiB of valid ROM data, two full passes extrapolate to approximately 9,045 seconds, or 2.51 hours. This is accepted only as cancellable initial/new-or-changed-source background work with visible progress. It must not block view entry, navigation, launch, or save paths.

## 5. Identity policy

Production identity is frozen as the complete 256-bit SHA-256 of the canonical logical ROM stream:

- z64 bytes unchanged;
- v64 bytes swapped within each pair;
- n64 bytes reversed within each four-byte unit;
- normalization carry preserved across chunk boundaries;
- incomplete normalization units rejected;
- two complete fresh-open passes required for a new/changed source;
- both full digests and compatible source signatures must agree.

Paths, filenames, size, timestamps, header tuple, and any quick checks are attributes or change detectors, not identity. One compatible fingerprint maps to one logical record. All source paths remain attached. Contradictory equal-fingerprint metadata fails the candidate generation closed and preserves the previous immutable snapshot. Primary-source selection is deterministic and never silently discards alternates.

The discovery evidence is representative-only: it proves canonical two-pass behavior for selected 8, 24, and 64 MiB files, not a complete duplicate census of the card.

## 6. Heap evidence and frozen budgets

### 6.1 Phase 2 production-view baseline

Temporary compile-time instrumentation sampled heap boundaries and rendered an on-screen overlay. Three interactive Home → Library → Details → Library → Home repetitions were recorded on the same 4 MiB system.

| View | Settled free | Lowest observed boundary minimum |
|---|---:|---:|
| Home | 1,134,656 | 1,134,656 |
| Library | 1,134,656 | 1,134,232 |
| Details | 1,134,232 | 1,134,232 |

The heap total reported in each view was 3,362,744 bytes. The values were stable across the three repetitions; no monotonic loss was observed. These are boundary-sampled watermarks and include diagnostic-overlay overhead.

Instrumentation provenance:

- build flag: `FLAGS="-DPHASE2_HEAP_INSTRUMENT=1"`, with `AURORA64_HOME=1 AURORA64_LAUNCH_PROOF=1`;
- instrumented ROM: `artifacts/phase3_discovery/phase2_heap_instrument_v5.n64`;
- SHA-256: `a7a6fba124805f736341e70fa0bed89f2987f16d10864926bf4806752d2fce1e`;
- default-off control ROM SHA-256: `0c4a87af3e63e274403f37957ae63982d0bac42e88754ad462a11f06ec038bb2`;
- default-off ELF contained no `P2HEAP` markers.

The temporary instrumentation source changes were reverted before this Task 2A gate and are excluded from production/commit scope.

### 6.2 Discovery and synthetic memory

All three natural discovery runs matched:

- discovery heap before synthetic allocation: 3,116,808 bytes;
- synthetic minimum: 2,805,488 bytes;
- heap after free: 3,116,808 bytes;
- measured peak allocation delta: 311,320 bytes;
- whole-run post-init/minimum/final: 3,117,240 / 2,805,488 / 3,117,512 bytes;
- allocation success: true; terminal allocations: zero.

The synthetic model allocated 49,152 record bytes, a 196,608-byte path arena, and a 65,536-byte sink. It encoded 243,744 bytes twice with matching private CRC evidence and no filesystem access. This is a conservative encode-overlap ceiling, not a real cache-writer measurement.

### 6.3 Frozen production budgets

| Resource | Frozen production limit |
|---|---:|
| configured/effective roots | 1 (`/`) |
| traversal depth | 8 |
| logical records | 256 |
| retained source paths/searchable paths | 512 |
| packed path/string arena | 32,768 bytes |
| full path including NUL | 512 bytes |
| included top-level directories | 32 |
| header/read unit | 4,096 bytes |
| hash input unit | 4,096 bytes |
| simultaneous read/normalize buffers | 8,192 bytes maximum |
| filesystem/CPU units | 1 per rendered frame |
| intended bounded work unit | 10 ms maximum target |
| total library-owned live heap | 224 KiB maximum |
| minimum free-heap reserve | 512 KiB |
| preferred production input/frame/USB gap | 25 ms maximum target |
| absolute hardware acceptance ceiling | 80 ms |
| synthetic encode-overlap evidence ceiling | 311,320 bytes measured |

Every cap exhaustion or allocation failure must visibly fail the candidate generation, close owned resources, and retain the prior immutable snapshot. It must never silently truncate a library.

The heap arithmetic is conservative:

- worst measured Phase 2 view free heap: 1,134,232 bytes;
- minus synthetic overlap ceiling: 822,912 bytes;
- minus the 224 KiB production library-owned limit: 593,536 bytes;
- 512 KiB reserve: 524,288 bytes;
- remaining margin above reserve: 69,248 bytes.

The standalone discovery ROM's larger free heap is not substituted for the production-view baseline.

The 25 ms scheduling target excludes the rejected 64 KiB production unit. The 80 ms absolute acceptance ceiling derives from the worst accepted observation (70.545 ms) plus margin and exists to detect regressions across full hardware gates. A production implementation using only 4 KiB units should satisfy the stricter 25 ms target.

## 7. Cancellation and ownership

All four required phase-specific stops reached exact `QUIESCED` state with `error=user stop`, zero live handles, zero allocations, released application SDFS ownership, `sdfs_close_attempted=1`, and `cleanup_close_failed=0`.

| Stop phase | In-workload evidence | Stop-to-safe |
|---|---|---:|
| scan/validation | population enumerated, candidate selection incomplete | 66.473 ms |
| sequential 4 KiB read | 40 calls / 163,840 bytes complete | 32.897 ms |
| SHA | small pass 1 stopped after 33 calls / 135,168 bytes | 32.761 ms |
| synthetic | 84 of 2,048 records initialized | 66.171 ms |

Accepted stop-to-safe range was 32.761–66.473 ms. Explicit close maxima were 78–79 µs. The scan stop's partial candidate count is intentional interruption evidence, not a census result. The synthetic stop occurred before its normal phase-local heap finalization; terminal allocation count and final frozen heap confirm cleanup.

Because public `debug_close_sdfs()` returns `void`, successful private unmount cannot be independently observed. The report therefore claims release of application ownership and a close attempt, not proof of private mount state.

## 8. Cache primitive decision

Static review of the pinned filesystem stack found:

- public write, close, and unlink operations;
- no public rename operation suitable for atomic replacement;
- no separate public fsync-like durability guarantee;
- `f_close()` reaches FatFs synchronization behavior;
- the reviewed SDFS `CTRL_SYNC` behavior does not independently prove physical-media durability.

Therefore a future cache writer must not overwrite the only valid cache in place. It must use two independently recognizable slots. Each slot needs its own magic, version, generation, exact length, integrity check, and completion marker. Loading accepts only a fully validated slot and chooses the newest valid generation. An invalid/incomplete peer must not damage the valid slot.

This design is frozen, but persistence remains disabled until Task 14 and explicit disposable-media approval establish real allocation, FAT-update, interruption, and power-loss behavior. The synthetic ring sink is not evidence of atomicity or durability.

## 9. 4 MiB feasibility conclusion

**PASS for the bounded, read-only in-memory library foundation.**

Reasons:

1. The replay artifact completed three natural runs on an operator-confirmed original 4 MiB/Jumper Pak N64.
2. Census, representative selection, read coverage, SHA matches, synthetic results, and heap measurements were stable.
3. The worst production-view watermark minus both the measured synthetic overlap and the frozen production library budget still leaves 69,248 bytes above the 512 KiB reserve.
4. Every required workload cancellation reached `QUIESCED` without leaked application resources.
5. Full canonical two-pass SHA is slow but stable and accepted as visible cancellable background work.

This pass does not approve persistent cache writes, launch/save behavior, artwork, Phase 4, or an implementation that exceeds any frozen budget. Later tasks must measure the real production scanner/service/cache state against the same reserve and service gates.

## 10. Known limitations

- This is one card population, not a universal N64-library census.
- Directory entry sizes were not independently restated for every file.
- Aggregate logs intentionally cannot reconstruct filenames, paths, hashes, or the library.
- Representative hashing is not a whole-card duplicate census.
- The projected 2.51-hour first generation is extrapolated from representative cooperative throughput.
- Phase 2 heap values are boundary-sampled and include temporary overlay overhead.
- USB timing is a service-opportunity metric; the discovery ROM does not run the production `usb_comm_poll()` path.
- SDFS private unmount success is not observable through the public void API.
- The synthetic encoder does not measure real FAT allocation, replacement, persistence, or power-loss safety.
- One natural run reported emulator logging unavailable while USB logging remained available; results remained stable.
- The discovery spike's caps (2,048 records, 4,096 paths, 196,608-byte arena, 4,357-byte path scratch, 64 KiB benchmark buffer) are measurement ceilings, not production budgets.

## 11. Gate checklist

| Requirement | Result |
|---|---|
| replay-capable artifact built and provenance frozen | PASS |
| three complete natural hardware repeats | PASS |
| Phase 2 Home/Library/Details heap repeats | PASS |
| stop during scan, read, hash, and synthetic | PASS |
| exact quiescence and zero application resources | PASS |
| root and exclusion policy frozen | PASS |
| production budgets frozen numerically | PASS |
| canonical identity and SHA cost accepted | PASS |
| cache primitive constraints frozen | PASS |
| 4 MiB feasibility review | PASS |
| temporary instrumentation removed from production source | PASS |
| final independent revision review | PASS |

Task 2A is accepted. Task 3 may start after this measured revision is committed.
