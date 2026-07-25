# Aurora64 Phase 1 Home Verification

Date: 2026-07-25 (EDT)
Branch: `aurora64-v0.3.2`

## Build verification

The pinned `aurora64-dev:v0.3.2` linux/amd64 container completed clean builds for:

- the unoverridden default (`AURORA64_HOME=0` by default);
- explicit `AURORA64_HOME=0`;
- explicit `AURORA64_HOME=1`.

Final one-build artifact observations:

| Configuration | SHA-256 |
| --- | --- |
| Default | `7fb745a64a6b9abd82d50611d388028f1122f1e5aff1df9a36d3a16af7e4e1bf` |
| Explicit off | `8fa9ee4ca127781d61b9f5b6886f498444b6648e5c8148759ef570bfb4823286` |
| Explicit on | `4b183b17c7560f8bc48489cf88caf1fc588a356ab38022d0894463a3ad495201` |

Each artifact was 1,671,168 bytes. These hashes identify these specific builds; the build embeds generated data, so they are not permanent reproducibility identities.

## Static and review gates

- `git diff --check` passed.
- The protected boot, flashcart, ROM launch, and save paths had no diff.
- `home.c` contains no Home-owned allocation, filesystem, settings, bookkeeping, decoder, launch, save, or SC64 calls.
- Independent task-level specification and quality reviews passed.
- The full-slice quality review found and verified fixes for Credits caller preservation, diagonal boundary input, and the Browser action-bar style/format type.
- Final independent quality re-review returned `APPROVED`.

## Hardware baseline

- Original N64 with the original Jumper Pak (4 MiB).
- SummerCart64 firmware v2.20.2.
- Controller in port 1.
- macOS host proxy on TCP port 9064.
- Persistent boot mode returned to `Bootloader -> Menu from SD card` after testing.

## Volatile deployment integrity

No candidate was copied to `/sc64menu.n64` or otherwise installed as the persistent SD-card menu.

The enabled and explicit-off candidates were uploaded to volatile SC64 ROM memory. Before each first execution, the complete 1,671,168-byte ROM region was read back and compared with the source artifact:

- Enabled readback: byte-for-byte match, SHA-256 `4b183b17c7560f8bc48489cf88caf1fc588a356ab38022d0894463a3ad495201`.
- Explicit-off readback: byte-for-byte match, SHA-256 `8fa9ee4ca127781d61b9f5b6886f498444b6648e5c8148759ef570bfb4823286`.
- Debug execution used `--no-writeback`.
- No ROM was launched and no intentional settings, delete, extract, default-directory, save, or persistent menu replacement action was performed.

This does not claim byte identity of the SD card or prove that unchanged menu initialization caused zero incidental filesystem writes.

## Physical acceptance results

Enabled build:

- PASS: normal startup displayed the static six-card 3x2 Home grid.
- PASS: all six cards were reachable and focus remained clamped to the grid.
- PASS: placeholder A input was inert.
- PASS: `Browse Files` A input entered the stock Browser.
- PASS: Browser root B returned Home.
- PASS: the selected-card focus accent rendered as a short inset vertical bar beside the left border; the user considered it acceptable.
- PASS: 20 repeated Home -> Browser -> Home transitions remained responsive, with no observed hang, corruption, focus loss, or slowdown.

Explicit-off build:

- PASS: startup entered the normal Browser with no Home grid.
- PASS: B at filesystem root remained a no-op.

Recovery:

- PASS: full power-off and normal power-on returned to the untouched persistent SD-card menu.
- PASS: stock-menu controller input worked after recovery.

## Outcome

Phase 1 static Home passed its build, review, volatile-deployment, original-4-MiB-hardware, feature-on, feature-off, stability, and recovery gates. Analogue 3D validation remains future platform testing and is not part of this result.
