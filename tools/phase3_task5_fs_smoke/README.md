# Phase 3 Task 5 filesystem smoke test

This is a focused, headless libdragon diagnostic for the production Task 5 filesystem adapter. It is intended for volatile SummerCart64 deployment and a deliberately small temporary SD-card fixture.

## Fixture

```text
/
├── test.z64
├── Games/
│   ├── nested.v64
│   └── ignored.txt
├── System Volume Information/
│   └── excluded.z64
└── aurora64/
    └── internal.z64
```

The ROM files used for the smoke test are 4 KiB placeholders with the appropriate z64 or v64 magic bytes. Do not overwrite existing paths when creating the fixture.

## Coverage

The diagnostic verifies:

- logical `/` through the `sd:/` adapter prefix;
- root and nested directory enumeration through EOF;
- root-only excluded-name policy;
- file stat, open, header read, close, and stale-token rejection;
- explicit early directory close and repeated-close behavior;
- 32 bounded stress cycles, each enumerating `/Games` through EOF and then reopening and closing it early;
- adapter deinitialization.

The diagnostic intentionally initializes no display, so a black screen is expected. It writes one machine-readable result line to `sd:/aurora64/fs-smoke-result.txt` after adapter teardown. The temporary result and fixture must be removed after readback.

## Build

Use the repository's pinned dev image and libdragon submodule:

```sh
docker run --platform linux/amd64 --rm \
  -v "$PWD:/work" -w /work aurora64-dev:v0.3.2 \
  bash -lc 'set -e; cd libdragon; make install -j2; make tools-install -j2; cd /work; make -C tools/phase3_task5_fs_smoke clean all'
```

Upload the resulting ROM only to volatile cart memory, with no save and no ROM shadow:

```sh
./tools/sc64/sc64deployer upload --save-type none --no-shadow \
  tools/phase3_task5_fs_smoke/phase3_task5_fs_smoke.z64
```

A physical N64 reset or power cycle may be required. If the N64 side still owns the SD card after the run, power off the console while retaining SC64 USB power before reading the result.

## Validated result

The focused test ran on a real N64, SummerCart64, and SD card on 2026-07-27. Machine-read result:

```text
P3FS summary result=PASS failures=0 root_entries=20 games_entries=2 cycles=32
```

Afterward, all temporary fixture paths were removed, the pre-existing contents of `System Volume Information` were verified intact, and SC64 state was restored to `Bootloader -> Menu from SD card` with ROM write and ROM shadow disabled.
