# Aurora64 libdragon directory-close and FAT timestamp dependency

Status: reviewed, published to the Aurora64-owned libdragon fork, and verified from a clean GitHub clone. The parent gitlink commit is local and intentionally unpushed.

## Dependency revisions

- Upstream repository: `https://github.com/DragonMinded/libdragon`
- Base revision: `5cb976aab11eb30622c33b112c120a1107eedb5e`
- Aurora64 dependency revision: `b2544b29e4369747168a9847e48cfc8fafeaaa15`
- Local branch: `feature/aurora64-dir-findclose-fat-time`
- Commit subject: `add explicit directory iterator cleanup`

The dependency commit is published at `https://github.com/JSeiv/libdragon/tree/feature/aurora64-dir-findclose-fat-time`. The parent repository records the fork URL in `.gitmodules` so fresh recursive clones can fetch the exact gitlink without depending on an unmerged upstream commit.

## Why Aurora64 needs this dependency change

Aurora64's games-first library work needs to scan FAT directories repeatedly without retaining FatFs iterator allocations, and it needs stable modification times for metadata refresh and future cache invalidation.

The previous public directory API offered `dir_findfirst()` and `dir_findnext()` but no explicit close operation. FAT iteration allocated a FatFs `DIR`, which meant callers stopping before EOF had no public way to release it. FAT-backed `stat()` also did not populate `st_mtime`.

These are general libdragon filesystem concerns rather than Aurora64 menu policy, so the fix belongs in the dependency and should be distributed through a libdragon fork branch and parent submodule pin instead of a parent-only workaround.

## Contract and implementation

The dependency commit:

- adds public `dir_findclose(path, dir)`;
- appends an optional `findclose` callback to `filesystem_t`;
- treats a missing callback as a successful resource-free no-op;
- validates paths and filesystem mappings consistently with the existing dispatcher;
- makes FAT cleanup idempotent and relinquishes ownership before `f_closedir()`;
- frees FAT iterator storage on explicit close, EOF, and `f_readdir()` failure;
- preserves the primary read/traversal error when cleanup also fails;
- updates `dir_walk()` to close iterators on callback abort/error/go-up, recursive failure, and iterator errors;
- converts packed FAT dates/times to deterministic Unix UTC seconds without host timezone state;
- returns `0` for invalid packed dates;
- populates FAT `stat.st_mtime` from directory metadata.

`dir_findclose()` returns `0` on success and `-2` with `errno` on error, matching the existing low-level directory dispatcher convention. After close, callers must call `dir_findfirst()` before reusing the iterator with `dir_findnext()`.

## Test coverage

The commit adds:

- dispatcher tests for callback dispatch, zero-valued cookies, optional callbacks, invalid arguments, error translation, and single-consumption ownership;
- an injected close-failure test preserving `EIO` while consuming ownership once;
- a 65,536-byte write-protected RAM FAT12 fixture;
- real FAT tests for close after first entry, several entries, EOF, repeated close, and post-close `findnext()` rejection;
- public `stat()` assertions for the FAT epoch, leap day 2000, the 2107 maximum, and invalid packed dates;
- 20-cycle heap-watermark checks for explicit early close;
- 20-cycle heap-watermark checks for `dir_walk()` abort, callback error with `EIO` preservation, and go-up;
- a focused `testrom_dir.z64` target that runs only the three Task 2 test groups.

Fixture:

- `tests/filesystem/fat12_dir_fixture.img`
- Size: 65,536 bytes
- SHA-256: `4e7645c7666988a30c27c68277e1d8bed6d8655d9cfe8d68df158ec707f22e69`

## Verification evidence

### Test-ROM builds

The following disposable amd64 container gate completed with exit code 0:

```sh
docker run --platform linux/amd64 --rm \
  -v "$PWD:/work" \
  -w /work/libdragon \
  aurora64-dev:v0.3.2 \
  bash -lc 'make install -j2 && make tools-install -j2 && make -C tests clean && make -C tests testrom_emu.z64 testrom_dir.z64 -j2'
```

Both the full emulator-compatible test ROM and focused hardware test ROM linked successfully.

### Published dependency clean clone

A fresh single-branch clone from `https://github.com/JSeiv/libdragon.git` resolved `feature/aurora64-dir-findclose-fat-time` to exactly:

```text
b2544b29e4369747168a9847e48cfc8fafeaaa15
```

The pinned container then rebuilt both targets from that clean clone with exit code 0:

```text
testrom_emu.z64  638,976 bytes
testrom_dir.z64  294,912 bytes
```

The clean-clone ROM hashes were `517971c3db15f57e853e5db0dd89518147d869874700dcddcc5eb19b386d1516` and `fc1a77fbe283883619f0c7ec4612ee5db6eb7528f17ea9059b6f6f84a57b02c7`, respectively. ROM hashes vary across builds because build-time metadata is embedded; the source SHA, successful targets, and output sizes are the reproducibility criteria used here.

### Physical N64

The final focused artifact was:

- Size: 294,912 bytes
- SHA-256: `537f52ebf81234808a68637f5ca31491fe6f891f439393f1d09af1bba097a4c1`

It was uploaded to SummerCart64 volatile ROM memory with save type `None`. A cart-ROM readback from offset `0` matched the built artifact byte-for-byte (`cmp` exit code 0 and matching SHA-256). The N64 required one physical RESET press after staging the ROM.

Josh confirmed all three focused groups displayed PASS on the original 4 MiB N64:

```text
libdragon Task 2 directory/FAT tests (N64)
test_dir_findclose_dispatch PASS
test_dirfindclose_errors PASS
test_fat_dir_integration PASS

Testsuite finished in 00:00
Passed: 3 out of 3 (0 skipped)
```

The final FAT integration PASS includes the added `dir_walk()` early-return heap regressions.

No test ROM was copied to the SD card, save writeback was disabled, and `/sc64menu.n64` was not replaced.

### Full dependency and parent baseline

The exact pinned baseline command from `docs/aurora64_architecture_audit.md` passed after the final fix:

```sh
docker run --platform linux/amd64 --rm \
  -v "$PWD":/work -w /work \
  aurora64-dev:v0.3.2 \
  bash -lc 'set -e; cd libdragon; make clobber -j2; make libdragon tools -j2; make install tools-install -j2; cd /work; make clean; make all'
```

The default Browser-first parent ROM linked successfully. All five parent outputs were 1,671,168 bytes and matched within the build:

```text
def0098f3379dd3aefe573691b0a2c8ec4f792606554e1738e76e71ba2a7896e
```

The hash is build-specific because the ROM embeds a UTC build timestamp.

Parent host gates also passed:

```sh
make host-test
make host-test-sanitize
```

Both reported all five tests successful, including ASan/UBSan execution.

## Review evidence

- Specification review: implementation and verification PASS; publication sequencing intentionally pending.
- Code-quality review initially found `dir_walk()` early-return iterator leaks.
- The leak was fixed and covered with real FAT heap regressions.
- Final code-quality re-review: PASS, ready for local commit.
- `git diff --check`: clean before commit.
- Dependency worktree: clean after commit.

## Distribution route

Completed publication steps:

1. Josh explicitly approved publication.
2. Created the public `JSeiv/libdragon` fork while preserving `DragonMinded/libdragon` as `upstream`.
3. Published `feature/aurora64-dir-findclose-fat-time` without changing fork `trunk` or opening an upstream PR.
4. Verified `b2544b29e4369747168a9847e48cfc8fafeaaa15` through local state, `git ls-remote`, the GitHub branch API, and a clean GitHub clone/build.
5. Updated the parent `.gitmodules` URL and `libdragon` gitlink to the reachable fork commit.
6. Committed this provenance note, `.gitmodules`, and the gitlink together locally, then verified the result from a clean recursive clone.

The parent branch remains unpushed. An upstream libdragon PR can be prepared later as a separate reviewed action.

This keeps Aurora64 reproducible while preserving a clean route for upstream review and avoids relying on an unreachable local-only submodule commit.
