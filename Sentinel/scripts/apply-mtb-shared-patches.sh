#!/bin/sh
##
## apply-mtb-shared-patches.sh
##
## Re-applies Sentinel's local fixes to getlibs-managed libraries under the
## repo-root `mtb_shared/` tree. `mtb_shared/` is .gitignore'd and is re-fetched
## by `make getlibs`, so these patches are NOT tracked by git and MUST be
## re-applied after every getlibs (or a fresh clone + getlibs). The build
## scripts call this automatically; run it by hand after `make getlibs`.
##
## Idempotent: safe to run repeatedly. Each patch checks for its own marker and
## no-ops if already applied.
##
## Canonical patch sources live in `Sentinel/patches/` (committed) for reference
## and manual application; this script does the same edit robustly by anchor so
## it survives trivial whitespace churn.
##
## ---------------------------------------------------------------------------
## PATCH: ota-update v2.0.0 — write_data_to_flash() flash-row-boundary bug (#63)
## ---------------------------------------------------------------------------
## Infineon ota-update release-v2.0.0 `cy_ota_untar.c::write_data_to_flash()`
## clamps a write to the flash-row *size* (CY_FLASH_SIZEOF_ROW) but not the row
## *boundary*. Its read-modify-write path uses a single one-row scratch buffer;
## a chunk that starts mid-row and extends past the row end overflows that
## buffer AND drops the spill-over bytes (they are never written into the next
## row). BLE OTA delivers MTU-sized, row-unaligned chunks, so this corrupted the
## image at nearly every row crossing -> the secondary-slot image hash never
## matched -> MCUBoot rejected every OTA. Fix: clamp each write to the bytes
## remaining in the current row; the loop writes the remainder next iteration.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)

ota_untar="$repo_root/mtb_shared/ota-update/release-v2.0.0/source/cy_ota_untar.c"
marker="#63 FIX (sentinel)"

if [ ! -f "$ota_untar" ]; then
    echo "apply-mtb-shared-patches: ota-update source not found:"
    echo "  $ota_untar"
    echo "  (run 'make getlibs' first)"
    exit 1
fi

if grep -q "$marker" "$ota_untar"; then
    echo "apply-mtb-shared-patches: ota-update row-boundary fix already present."
    exit 0
fi

python3 - "$ota_untar" <<'PY'
import sys

path = sys.argv[1]
src = open(path, encoding="utf-8").read()

anchor = (
    "        uint32_t chunk_size = bytes_to_write;\n"
    "        if (chunk_size > CY_FLASH_SIZEOF_ROW)\n"
    "        {\n"
    "            chunk_size = CY_FLASH_SIZEOF_ROW;\n"
    "        }\n"
)

fix = anchor + (
    "\n"
    "        /* #63 FIX (sentinel): never let a single write cross a flash-row\n"
    "         * boundary. block_buffer is exactly one row; the original code\n"
    "         * clamped only to CY_FLASH_SIZEOF_ROW, so a chunk that starts\n"
    "         * mid-row and extends past the row end overflowed block_buffer in\n"
    "         * the read-modify-write path below AND dropped the spill-over\n"
    "         * bytes (never written into the next row), corrupting the stored\n"
    "         * image. BLE OTA delivers MTU-sized, row-unaligned chunks, so this\n"
    "         * triggered on nearly every row boundary. Clamp to the bytes\n"
    "         * remaining in the current row; the loop writes the remainder on\n"
    "         * the next iteration. */\n"
    "        {\n"
    "            uint32_t row_remaining =\n"
    "                CY_FLASH_SIZEOF_ROW - (curr_offset % CY_FLASH_SIZEOF_ROW);\n"
    "            if (chunk_size > row_remaining)\n"
    "            {\n"
    "                chunk_size = row_remaining;\n"
    "            }\n"
    "        }\n"
)

if anchor not in src:
    sys.exit(
        "apply-mtb-shared-patches: anchor not found in cy_ota_untar.c.\n"
        "  The ota-update library layout may have changed (version bump?).\n"
        "  Re-derive the patch from Sentinel/patches/ and update this script."
    )

open(path, "w", encoding="utf-8").write(src.replace(anchor, fix, 1))
print("apply-mtb-shared-patches: applied ota-update row-boundary fix (#63).")
PY
