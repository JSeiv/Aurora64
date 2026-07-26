# Aurora64 Phase 3 discovery ROM

This directory builds a standalone, disposable N64 measurement ROM for Phase 3
Task 2A. It is **not production library code**. Its bounds and implementation
choices are measurement hypotheses; Tasks 3–14 must not copy them until the
Task 2A report is completed and approved.

## What the ROM measures

The ROM mounts one configured root, currently `sd:/`, with
`debug_init_sdfs("sd:/", -1)` and performs these phases:

1. an iterative, bounded depth-first scan and 64-byte candidate-header check;
2. deterministic representative selection;
3. complete sequential reads of the largest valid ROM with 4 KiB and 64 KiB
   requests;
4. two complete normalized SHA-256 passes for available small, medium, and
   large representatives;
5. two passes of a bounded synthetic cache encoder into a memory ring sink;
6. natural SDFS close, then final reporting.

Every synchronous filesystem or CPU unit is bounded to one unit per rendered
frame: one directory operation, file open/read/close, representative selection,
hash chunk/finalization, synthetic allocation/fill/encode/free operation, or
SDFS close. USB reporting never emits more than one logical `debugf` record in
a rendered frame. Transition-time read, SHA, synthetic, INIT, warning, and stop
records are retained as metrics/flags rather than emitted immediately. The
logging-service measurement is only an opportunity marker; the ROM does not
call `usb_comm_poll()`.

## Read-only and privacy contract

Actual files are opened only with `fopen(path, "rb")` and read only with
`fread()`. The ROM contains no write/update/append open, `fwrite`, rename,
unlink/`remove`, or `mkdir`; it creates no cache, configuration, history, menu,
or SD-log file. It does not access ROM payloads except through the explicitly
reported full-read and normalized-hash benchmarks. The synthetic phase performs
no real filesystem I/O.

No deep filename, full path, ROM payload, or digest is emitted. Included
**top-level directory** name bytes are emitted only as bounded lowercase hex
(up to 510 hex digits), so control characters cannot alter log framing.
Representative SHA-256 digests remain private in memory.

## Discovery policy and aggregates

Candidate extensions are case-insensitive `.z64`, `.v64`, `.n64`, and `.rom`.
A candidate is valid when its first four bytes are one of:

| Order | Magic |
|---|---|
| z64 | `80 37 12 40` |
| v64 | `37 80 40 12` |
| n64 | `40 12 37 80` |

The scan reports aggregate directory/searchable-path counts, excluded count,
depth and path-byte metrics, payload bytes from directory entries, candidates by
extension, valid files by magic, extension/order matches and mismatches, short
and invalid headers, open/read errors, size buckets (`<16 MiB`, `16–<32 MiB`,
`32–<64 MiB`, `>=64 MiB`), and valid total/minimum/maximum bytes. Directory
entry sizes are used without an extra `stat()` call. A no-valid-ROM population
is explicit.

Exclusion is applied before path construction, top-level recording,
aggregation, classification, descent, and cap consumption. Directly below
`sd:/`, these exact case-sensitive names are excluded whether file or directory:

`menu.bin`, `menu`, `N64FlashcartMenu.n64`, `ED64`, `ED64P`, `sc64menu.n64`,
`System Volume Information`, `.fseventsd`, `.Spotlight-V100`, `.Trashes`,
`.VolumeIcon.icns`, and `.metadata_never_index`.

At every depth, exact basenames `desktop.ini`, `Thumbs.db`, and `.DS_Store` are
excluded, as is every basename longer than two bytes beginning with `._`.
Searchable totals include only non-excluded entries.

### Path accounting and effective roots

Metrics use root-relative logical paths:

- `logical_path_bytes`: sum excluding the `sd:/` prefix and terminating NUL;
- `path_storage_bytes`: logical bytes plus one NUL per included path;
- `max_logical_path_bytes`: maximum excluding prefix and NUL;
- `retained_path_arena_bytes`: separate full-path storage for valid records,
  including each NUL.

Each included top-level directory gets a hex-name aggregate record. Root-direct
candidate/valid totals are in the summary. Effective roots are reported as:

- literal `/` if any valid ROM is directly under `sd:/`;
- literal `/` if more than four top-level directories contain valid ROMs;
- otherwise, each top-level directory with a nonzero valid count, up to four,
  using the same hex encoding.

## Fixed spike caps (not accepted production budgets)

| Resource | Fixed cap |
|---|---:|
| configured roots | 4 (1 currently used) |
| directory depth | 16 |
| retained valid records | 2048 |
| included searchable paths | 4096 |
| retained valid-path arena | 196608 bytes |
| full-path buffer, including NUL | 4357 bytes |
| included top-level directories | 64 × 256-byte slots |
| read buffer | 65536 bytes |
| normalized-hash chunk | 4096 bytes |
| bounded filesystem/CPU units | 1 per rendered frame |

The 4357-byte path buffer covers `sd:/`, sixteen 255-byte directory components,
one 255-byte entry, separators, and NUL. A cap violation is visible, makes
`complete=false`, and enters bounded cleanup rather than silently undercounting.

## Representative reads and normalized SHA-256

Selection is deterministic over retained scan order:

- small: `<16 MiB`, nearest to 8 MiB;
- medium: `16–<32 MiB`, nearest to 24 MiB;
- large: `>=32 MiB`, largest;
- sequential-read target: largest valid ROM overall.

Nearest-size ties choose the smaller size, then lower retained index. Largest
size ties choose the lower retained index. The sequential target is read in
full twice, once with 4096-byte requests and once with 65536-byte requests.
Enumeration and read records separate active operation time from cooperative
wall time and report exact bytes, calls, coverage, maximum call/chunk time, and
errors.

For each available class, SHA-256 hashes two complete canonicalized streams:

- z64 bytes are unchanged;
- v64 bytes are swapped within each pair;
- n64 bytes are reversed within each four-byte unit.

Carry bytes preserve normalization units across 4096-byte chunk boundaries; an
incomplete final unit fails that pass. Each class is independently marked
unavailable when the scan has no member. Missing classes do not fabricate
measurements. Digests are private and only the two-pass match result is emitted.
Matches across the three representatives are aggregate, representative-only
duplicate evidence—not a complete-population duplicate census or proof.

The on-ROM SHA implementation self-tests the standard empty and `abc` vectors
before scanning. Its same implementation was also compiled independently on the
host with `cc -std=c11 -Wall -Wextra -Werror -pedantic`; both host tests passed.

## Synthetic cache ceiling

The synthetic phase models peak encode memory without touching the filesystem:

| Component | Model |
|---|---:|
| records | 2048 × 24 bytes = 49152 bytes |
| path arena | 196608 bytes; 95 bytes + NUL per record |
| ring memory sink | 65536 bytes |
| simultaneous allocation model | 311296 bytes |

The fixed big-endian stream has a 32-byte header, then 2048 records each with
24 bytes of metadata and 95 path bytes. That is exactly **243744 bytes per
pass**. It encodes two passes, uses a private CRC-32 as comparison evidence for
the encoded streams, and emits no CRC value. Equality is CRC-backed rather than
a literal retained byte-for-byte comparison. The CRC implementation self-tests
against
`123456789` before use. A result passes only when allocations, record fill,
overflow checks, CRC self-test, both exact byte counts, and private CRC match all
succeed. This is a synthetic preimplementation ceiling, not evidence of an SD
cache writer, persistence, atomicity, or power-loss safety.

## Stop, cleanup, and completion

B or START requests stop before that frame's work unit. Cleanup releases at
most one owned resource per frame: one synthetic allocation, one `FILE *`, one
directory via `dir_findclose()`, and finally the ROM's SDFS ownership via
`debug_close_sdfs()`. Natural directory exhaustion follows the pinned
close-and-clear contract. A natural run invokes the SDFS close API in its own
frame before `COMPLETE`.

Terminal states are:

- `COMPLETE`: all scan, sequential-read, SHA, synthetic, and SDFS-close-attempt
  gates were evaluated; `complete`/`production_gate_ready` is true only if every
  workload success condition passed and the ROM released its SDFS ownership;
- `ERROR`: failure followed by resource cleanup;
- `QUIESCED`: user stop followed by cleanup;
- `QUIESCE_FAILED_SAFE`: an observable file/directory close failure occurred,
  ownership was cleared under the pinned contract, and remaining resources were
  still processed.

Libdragon's public `debug_close_sdfs()` API returns `void`, even though its
private implementation can retain the mount when `fat_unmount()` fails. The ROM
therefore reports `sdfs_mount_owned`, `sdfs_close_attempted`, and
`sdfs_close_verified=unavailable`; it does not claim independently verified
unmount success. `complete` is a workload/application-ownership result, not
proof of libdragon's private mount state.

Terminal/lifecycle records also report live handle and allocation counts.
Stop-to-safe time is emitted only for an actual stop. `max_work_duration` and
`work_cadence` cover explicitly instrumented filesystem, hash, cleanup, and
synthetic operations; input and frame gaps are the broader stall measurements.
Natural FAT directory EOF-close time is included in the enumeration call timing,
while `close_us` tracks explicit cleanup/file/SDFS close operations.

## USB debug record families and replay capture

Each line begins `P3DISC`. On first observing a terminal state, the reporter waits
one frame so the next frame/input/USB-opportunity samples include the final
terminal-transition work. It then freezes those timing maxima plus work/close
maxima and post-init/minimum/final heap values. The ROM renders a cyclic report
from retained metrics only. It does not repeat SD reads, hashing, synthetic
encoding, allocation, or any other measurement work. Reporting uses fixed static
stage/cursor state and one static 511-byte top-level-name hex buffer; it
allocates no report buffers.

Each cycle begins with `replay_begin cycle=N schema=2 bounded_records_max=99`
and ends with the matching `replay_end cycle=N`. There is exactly one logical
record per rendered frame. A cycle has at most 99 records: 29 unconditional
records, up to two conditional warning/stop records, up to 64 `top` records,
and up to four `effective_root` records. After `replay_end`, the next frame
starts a fresh cycle over the same frozen data. Thus a short-polling
`sc64deployer debug` client may attach at any time: discard records until a
`replay_begin`, then retain through the same-cycle `replay_end`.

Expected families inside each cycle are:

- `event=INIT`, plus conditional `warning=USB_LOG_UNAVAILABLE` and
  `event=STOP_REQUEST` records;
- `sha_selftest`;
- `enumeration` and `representatives`;
- `read request=4096|65536`;
- `sha` for both passes of every class and `sha_class` availability/match
  summaries; unavailable classes still have explicit zero-work pass records;
- `synthetic_encode`, `synthetic_schema`, `synthetic_memory`, and
  `synthetic_result`;
- `duplicate_evidence`;
- `terminal`, `lifecycle`, aggregate `summary`, `extensions`, `magic`, and
  `size_buckets`;
- privacy-bounded `top` and `effective_root` records.

If the scan finds no valid ROM, it emits `effective_root state=none valid=0`
rather than relying on the absence of an effective-root record.

The 4096- and 65536-byte read records are likewise always present and include
explicit `skipped` semantics. Both synthetic-pass records are always present
and identify zero-operation skipped work. Digests and synthetic CRC values
remain private.

Capture one coherent matching begin-to-end cycle; do not splice cycles or treat
a missing `replay_end` (or missing required family within the cycle) as a
successful run.

## Build and verified artifact

The replay-capable source passed both a strict format-diagnostic cross-build and
the normal clean cross-build in `aurora64-dev:v0.3.2` for `linux/amd64`.
`git diff --check` also passed. The strict build added `-Wformat=2`,
`-Wformat-signedness`, and `-Werror=format` through `N64_CFLAGS`, preserving
libdragon's required defaults.

Parent source provenance is branch `feature/phase3-library-foundation` at
`d6e197ca42a2d00c38e1570408d13986d25545f8`, with libdragon
`b2544b29e4369747168a9847e48cfc8fafeaaa15` published on fork branch
`feature/aurora64-dir-findclose-fat-time`.

The normal clean build used:

```sh
docker run --platform linux/amd64 --rm -v "$PWD:/work" -w /work \
  aurora64-dev:v0.3.2 bash -lc \
  'set -e; cd libdragon; make install -j2; make tools-install -j2; \
   cd /work; make -C tools/phase3_discovery clean all'
```

Verified output for the replay-capable artifact:

| Artifact fact | Value |
|---|---|
| ELF sections | text 250424; data 37448; bss 413896; total 701768 bytes |
| ROM path | `tools/phase3_discovery/phase3_discovery.z64` |
| ROM size | 229376 bytes |
| SHA-256 | `a329ad92b860e2b9d880dcb92952900e5e879b2f40d0ad6f58e7bebe2ca1d28b` |
| metadata/assets | no save type; region-free; no DragonFS assets |

The previously deployed one-shot artifact was also 229376 bytes and had SHA-256
`5588bf68eba99d89573e5aef8a31747c382ee0125e8250b0456fcab60cce1eee`.
Its incomplete hardware capture is historical only and must not be attributed to
the replay-capable artifact above.

## Completed approval-gated hardware measurement

The replay-capable ROM was uploaded only to volatile SC64 ROM memory after explicit
approval. The accepted deployment used save type `None`, ROM shadow disabled,
and debugger save writeback disabled. No `send-file`, `make run-debug-upload`,
or `/sc64menu.n64` replacement was used.

Three independent reset-boot runs produced complete matching replay cycles. Four
additional runs stopped during scan/validation, sequential read, SHA, and
synthetic allocation; each reached `QUIESCED` with zero application handles and
allocations. Accepted normalized log paths and SHA-256 checksums, measured
ranges, the 4 MiB feasibility calculation, and frozen production budgets are in:

- `docs/research/aurora64_phase3_discovery.md`

The accepted hardware artifact is the replay-capable ROM documented above, not
the historical one-shot `5588bf68...` artifact. After every temporary upload,
ROM writes were restored to disabled with a same-artifact upload using
`--save-type none --no-shadow`; `sc64deployer info` confirmed ROM shadow off and
save type `None`.

For any future separately approved replay, retain the same safety form:

```sh
./tools/sc64/sc64deployer upload --save-type none --no-shadow \
  tools/phase3_discovery/phase3_discovery.z64
./tools/sc64/sc64deployer debug --no-writeback
```

`--no-shadow` is mandatory: without it, this deployer may copy the last 128 KiB
of the ROM into flash. A volatile upload is not approval to write the SD card or
to generalize this spike into a production cache writer.
