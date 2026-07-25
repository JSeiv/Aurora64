# Aurora64 SummerCart64 USB Development Loop

Status: verified on the Aurora64 Mac mini with SummerCart64 firmware/deployer v2.20.2
Baseline ROM: pinned upstream N64FlashcartMenu V0.3.2
Safety property: this workflow uses volatile SC64 ROM memory and does not replace `/sc64menu.n64` on the SD card

## Verified environment

- macOS host: Apple Silicon (`arm64`)
- SC64 USB device: visible as `Polprzewodnikowy SC64`
- SC64 firmware: v2.20.2
- Host deployer: `tools/sc64/sc64deployer` v2.20.2 (native arm64)
- Container deployer: v2.20.2 in `aurora64-dev:v0.3.2`
- Host proxy: TCP port 9064
- Container target: `host.docker.internal:9064`

## One-time host setup

The official macOS deployer is stored under ignored `tools/sc64/`:

```sh
mkdir -p tools/sc64
curl -fL \
  -o tools/sc64/sc64-deployer-macos-v2.20.2.tgz \
  https://github.com/Polprzewodnikowy/SummerCart64/releases/download/v2.20.2/sc64-deployer-macos-v2.20.2.tgz
tar -xzf tools/sc64/sc64-deployer-macos-v2.20.2.tgz -C tools/sc64
chmod +x tools/sc64/sc64deployer
```

Observed SHA-256 values:

```text
9fe4516d71988d23827635c93a920c1c4ff9c3708a4fbefcb54582e6047d074c  sc64-deployer-macos-v2.20.2.tgz
8afdb0a9b08f25b383cfa3a110577a98b25c4df0539a52b8ffb22457703c2908  sc64deployer
```

## 1. Confirm the device

```sh
./tools/sc64/sc64deployer list
./tools/sc64/sc64deployer info
```

The verified device reported firmware v2.20.2, initialized SD at 50 MHz, 3.208 V, and normal operating temperature.

## 2. Start the host USB proxy

Run this on macOS and leave it active:

```sh
./tools/sc64/sc64deployer server 0.0.0.0:9064
```

`0.0.0.0` is intentional: the Linux container must reach the macOS listener through `host.docker.internal`. Do not expose port 9064 beyond the trusted development network.

Verify from the container:

```sh
docker run --platform linux/amd64 --rm \
  aurora64-dev:v0.3.2 \
  sc64deployer --remote host.docker.internal:9064 info
```

This exact proxy path was verified successfully.

## 3. Build the ROM

Initialize pinned submodules, build the amd64 toolchain image, and build the source as documented in `docs/aurora64_architecture_audit.md`.

Expected artifact:

```text
output/N64FlashcartMenu.n64
```

The verified baseline artifact was 1,671,168 bytes. Its observed build-specific SHA-256 was:

```text
da4e8674bfc38eed039c77b6dfa8269096b3a90d096f565db7c5e12d44692b16
```

The hash changes on clean rebuilds because the ROM embeds a UTC build timestamp.

## 4. Upload to volatile SC64 ROM memory

```sh
docker run --platform linux/amd64 --rm \
  -v "$PWD":/work -w /work \
  aurora64-dev:v0.3.2 \
  sc64deployer --remote host.docker.internal:9064 \
  upload ./output/N64FlashcartMenu.n64
```

Verified result:

```text
Uploading ROM [N64FlashcartMenu.n64]... done
Save type set to [None]
Boot mode set to [Bootloader -> ROM]
```

This changes volatile SC64 state. It does not copy or replace the menu on the SD card.

## 5. Reboot the running menu into the uploaded ROM

When a compatible N64FlashcartMenu is already running:

```sh
docker run --platform linux/amd64 --rm \
  aurora64-dev:v0.3.2 \
  sc64deployer --remote host.docker.internal:9064 \
  debug --no-writeback --init reboot
```

The verified command connected, sent `reboot`, and exited cleanly. After reboot, SC64 state returned to `Bootloader -> Menu from SD card`, consistent with the uploaded menu initializing SC64 and restoring its normal next-boot default.

`--no-writeback` is intentional for menu debugging. Game-save validation requires separate save/writeback tests and must not assume this debug mode persists saves.

## 6. Verify the uploaded bytes

The deployer `dump` address is a cart-ROM offset, so the ROM begins at offset `0`, not CPU physical address `0x10000000`.

```sh
./tools/sc64/sc64deployer --remote 127.0.0.1:9064 \
  dump 0 1671168 /tmp/aurora64-sc64-readback.n64

shasum -a 256 \
  /tmp/aurora64-sc64-readback.n64 \
  output/N64FlashcartMenu.n64

cmp -s \
  /tmp/aurora64-sc64-readback.n64 \
  output/N64FlashcartMenu.n64
```

The verified readback was byte-for-byte identical to the built artifact and had the same SHA-256:

```text
da4e8674bfc38eed039c77b6dfa8269096b3a90d096f565db7c5e12d44692b16
```

## 7. Return to the SD-card menu

The menu restores the SC64 next-boot mode to `Bootloader -> Menu from SD card` during initialization. A normal power cycle therefore returns to the existing SD-card menu. No manual SD-card update is required for this development loop.

Do not use `make run-debug-upload` during early Aurora64 development: that path intentionally sends `/sc64menu.n64` to the SD card. Use volatile `upload` plus `debug --init reboot` until an SD release candidate has been explicitly approved.

## Current limitation

The verified reboot command displayed `[Debug]: Started`, `[Init]: reboot`, and `[Debug]: Stopped`, but did not retain a live log stream. Volatile upload, reboot initiation, SC64 reinitialization, and byte-for-byte ROM readback are verified. Persistent `debugf` capture should be treated as a separate follow-up before relying on USB logs for diagnostics.
