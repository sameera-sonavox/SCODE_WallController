# MX25R6435F External Flash and LittleFS Middleware Checklist

## Objective

Integrate the 8-MiB MX25R6435F, accessed through the custom LPSPI1
multiphase/quad driver, with Zephyr's Flash API, flash-map layer, LittleFS, and
eventually LVGL image access.

```text
Application / LVGL
        |
Zephyr filesystem API
        |
LittleFS mounted at /lfs
        |
Zephyr flash-map: ext_storage_partition
        |
Custom Zephyr flash_driver_api wrapper
        |
ExtFlash read/write/erase controller
        |
Custom NXP LPSPI1 multiphase driver
        |
MX25R6435F
```

LittleFS must not call the SPI API or flash command functions directly. The
custom Zephyr Flash API wrapper is the boundary between LittleFS and the
validated low-level controller.

## Flash geometry and reserved layout

| Property | Value |
|---|---:|
| Capacity | 8 MiB / 64 Mbit |
| Valid device offsets | `0x000000-0x7FFFFF` |
| Page-program size | 256 bytes |
| Minimum erase size | 4096 bytes |
| Minimum write block reported to Zephyr | 1 byte |
| Erased value | `0xFF` |

| Partition | Start | Size | End | Purpose |
|---|---:|---:|---:|---|
| `ext_fw_partition` | `0x000000` | 1 MiB | `0x0FFFFF` | Reserved for future firmware handling |
| `ext_storage_partition` | `0x100000` | 7 MiB | `0x7FFFFF` | LittleFS application storage |

The firmware partition is only reserved. It is not yet an MCUboot secondary
slot and must not be included in the LittleFS volume.

## Phase 0 - Validated low-level flash controller

- [x] Initialize LPSPI1 through the custom NXP SPI API.
- [x] Apply the required LPSPI1 pin configuration.
- [x] Read and validate JEDEC ID `C2 28 17`.
- [x] Read and validate SFDP.
- [x] Validate status/configuration registers and QE.
- [x] Implement bounded WIP polling.
- [x] Implement write-enable handling.
- [x] Implement 4-KiB sector erase.
- [x] Implement page-split programming.
- [x] Implement bounded normal reads.
- [x] Implement multiphase quad reads.
- [x] Support 1-4 bytes per LPSPI frame.
- [x] Pass unaligned-address tests.
- [x] Pass page-boundary write tests.
- [x] Pass normal-read boundary tests.
- [x] Pass quad transaction-boundary tests.
- [x] Pass partial-frame tests.
- [x] Verify every byte of an erased sector is `0xFF`.
- [x] Reject the currently supported invalid inputs with `-EINVAL`.
- [x] Pass the repeated 64-KiB bulk test using normal and quad reads.

Acceptance criterion: completed from the two successful full validation runs.

## Phase 1 - Disable destructive raw validation

- [x] Set `EXT_FLASH_ENABLE_VALIDATION_TESTS` to `0U` in
      `ExtFlash_ProjDef.h`.
- [x] Keep the individual test switches and test implementation available for
      future bring-up work.
- [x] Rebuild the application.
- [x] Boot the application and confirm that no raw validation-test messages
      appear.
- [x] Confirm that startup no longer erases or programs the final 64 KiB of
      `ext_storage_partition`.
- [x] Keep the LittleFS fstab node disabled during this phase.

Acceptance criterion: the flash initializes successfully, but no destructive
validation operation occurs after initialization.

## Phase 2 - Define the Zephyr Flash API wrapper contract

- [x ] Create a dedicated wrapper module above `ExtFlashReadWrite`.
- [ ] Make wrapper offsets relative to the complete 8-MiB flash device.
- [ ] Validate ranges without integer overflow using the equivalent of
      `offset <= device_size` and `length <= device_size - offset`.
- [ ] Return `0` for valid zero-length Zephyr Flash API operations as required
      by the Zephyr API contract.
- [ ] Reject negative offsets.
- [ ] Reject non-null-required buffers when length is nonzero.
- [ ] Report all errors as negative errno values.
- [ ] Add a wrapper mutex covering the complete read, write, or erase
      operation.
- [ ] Ensure write-enable, program, and WIP polling cannot be interleaved with
      another flash request.
- [ ] Keep the existing lower-level SPI synchronization.

Acceptance criterion: the wrapper contract is documented and every public
callback has deterministic validation and serialization rules.

## Phase 3 - Implement `struct flash_driver_api`

- [ ] Implement `read` using `iRead_DataFromFlash_Quad()`.
- [ ] Implement `write` using `iWrite_DataToFlash()`.
- [ ] Implement `erase`:
  - [ ] Require 4096-byte aligned offset.
  - [ ] Require 4096-byte aligned length.
  - [ ] Erase every sector in the requested range.
- [ ] Implement `get_parameters`:
  - [ ] `write_block_size = 1`.
  - [ ] `erase_value = 0xFF`.
  - [ ] Explicit erase is required.
- [ ] Implement `get_size` and return `8 * 1024 * 1024`.
- [ ] Implement `page_layout`:
  - [ ] Page size is 4096 bytes.
  - [ ] Page count is 2048.
- [ ] Register a static `flash_driver_api` instance.
- [ ] Register the Zephyr device with `DEVICE_DT_DEFINE()` or
      `DEVICE_DT_INST_DEFINE()`.
- [ ] Make the device initialization callback initialize the custom LPSPI1 and
      MX25R6435F controller.
- [ ] Remove the duplicate application-level `bInit_ExtFlash()` call after the
      device initializer owns initialization.
- [ ] Ensure the device init priority is earlier than filesystem mounting.

Acceptance criterion: `device_is_ready()` succeeds for the custom external
flash device and initialization occurs exactly once.

## Phase 4 - Add the custom Devicetree binding and flash device

- [ ] Add an application binding such as
      `sonavox,mx25r64-custom.yaml`.
- [ ] Define the required geometry properties in the binding or wrapper
      configuration.
- [ ] Replace the disabled `compatible = "jedec,spi-nor"` node with the custom
      compatible, for example `compatible = "sonavox,mx25r64-custom"`.
- [ ] Do not bind the device to Zephyr's generic SPI-NOR driver.
- [ ] Keep the Zephyr LPSPI1 controller node disabled because the custom SPI
      API owns it.
- [ ] Keep the LPSPI1 pinctrl definitions available to the custom controller.
- [ ] Put both fixed partitions under the custom flash device node.
- [ ] Confirm generated Devicetree values for:
  - [ ] Custom flash device node.
  - [ ] `ext_fw_partition` offset and size.
  - [ ] `ext_storage_partition` offset and size.
  - [ ] Partition-to-flash-device association.

Acceptance criterion: the generated Devicetree maps both partitions to the
custom Zephyr flash device and no `jedec,spi-nor` device claims LPSPI1.

## Phase 5 - Finalize Kconfig for the custom wrapper

- [ ] Keep `CONFIG_FLASH=y`.
- [ ] Keep `CONFIG_FLASH_MAP=y`.
- [ ] Keep `CONFIG_FLASH_PAGE_LAYOUT=y`.
- [ ] Keep `CONFIG_FILE_SYSTEM=y`.
- [ ] Keep `CONFIG_FILE_SYSTEM_LITTLEFS=y`.
- [ ] Keep `CONFIG_FS_LITTLEFS_FMP_DEV=y`.
- [ ] Remove `CONFIG_SPI_NOR=y`.
- [ ] Remove `CONFIG_SPI_NOR_SFDP_MINIMAL=y`.
- [ ] Remove `CONFIG_SPI_NOR_FLASH_LAYOUT_PAGE_SIZE=4096`.
- [ ] Retain general SPI/LPSPI configurations needed by the display or other
      Zephyr-managed peripherals.
- [ ] Build without Kconfig dependency warnings.

Acceptance criterion: LittleFS and flash-map support are enabled while the
generic SPI-NOR driver is not built for the custom external flash.

## Phase 6 - Validate through Zephyr Flash and flash-map APIs

Do not mount LittleFS while these destructive wrapper tests are active.

- [ ] Verify `device_is_ready()`.
- [ ] Verify `flash_get_size()` returns 8 MiB.
- [ ] Verify `flash_get_parameters()` reports write block 1 and erase value
      `0xFF`.
- [ ] Verify page layout reports 2048 pages of 4096 bytes.
- [ ] Open `ext_storage_partition` with `flash_area_open()`.
- [ ] Confirm the flash-area device is the custom wrapper.
- [ ] Confirm flash-area offset is `0x100000` and size is 7 MiB.
- [ ] Erase a reserved test sector using `flash_area_erase()`.
- [ ] Write using `flash_area_write()`.
- [ ] Read and compare using `flash_area_read()`.
- [ ] Test unaligned reads and page-crossing writes through flash-map.
- [ ] Verify flash-area requests cannot cross partition boundaries.
- [ ] Verify wrapper erase alignment errors.
- [ ] Repeat the wrapper test without calling the raw controller API directly.
- [ ] Disable the wrapper test after it passes.

Acceptance criterion: all access is routed through Zephyr's flash device and
flash-map APIs, and partition boundaries are enforced.

## Phase 7 - Enable LittleFS with manual mounting

- [ ] Set the `ext_littlefs` fstab node to `status = "okay"`.
- [ ] Keep `automount` disabled initially.
- [ ] Reference only `ext_storage_partition`.
- [ ] Retain and validate these initial parameters:
  - [ ] `read-size = 16`.
  - [ ] `prog-size = 16`.
  - [ ] `cache-size = 256`.
  - [ ] `lookahead-size = 32`.
  - [ ] `block-cycles = 512`.
- [ ] Confirm cache size is a multiple of read and program sizes.
- [ ] Confirm cache size divides the 4096-byte erase block.
- [ ] Declare the fstab mount entry in the application.
- [ ] Check that the underlying flash device is ready.
- [ ] Mount `/lfs` manually with `fs_mount()`.
- [ ] Allow the first mount to format blank/unrecognized media.
- [ ] Report mount and format failures without hiding the original errno.

Acceptance criterion: `/lfs` mounts successfully on
`ext_storage_partition`; `ext_fw_partition` remains untouched.

## Phase 8 - Run LittleFS functional and persistence tests

- [ ] Create a directory.
- [ ] Create and write a small file.
- [ ] Create and write a file larger than 256 bytes.
- [ ] Create and write a file larger than 4096 bytes.
- [ ] Call `fs_sync()` before closing test files.
- [ ] Close and reopen each file.
- [ ] Read and compare every byte.
- [ ] Test seek and partial reads.
- [ ] Rename a file.
- [ ] Check file metadata with `fs_stat()`.
- [ ] Enumerate the directory.
- [ ] Delete the test files and directory.
- [ ] Unmount and remount without formatting.
- [ ] Verify files persist across unmount/remount.
- [ ] Reboot and verify files persist across reset.
- [ ] Run repeated create/update/delete cycles.
- [ ] Perform a controlled power-interruption recovery test later.

Acceptance criterion: file contents and filesystem metadata remain correct
after remount and reboot, with no access outside the storage partition.

## Phase 9 - Enable fstab automount

- [ ] Restore the `automount` property.
- [ ] Confirm the custom flash device initializes before filesystem automount.
- [ ] Remove temporary manual-mount code.
- [ ] Verify startup on blank, formatted, and previously used media.
- [ ] Verify application code handles a mount failure gracefully.

Acceptance criterion: `/lfs` is available when application file users start,
without duplicate initialization or duplicate mount attempts.

## Phase 10 - Integrate LVGL image files

- [ ] Select the stored image format.
- [ ] Decide whether to use LVGL's filesystem adapter or an application image
      decoder above Zephyr `fs_*()` calls.
- [ ] Open image files through `/lfs`.
- [ ] Read/decode images in bounded chunks.
- [ ] Use reusable static/aligned buffers rather than request-sized stack
      arrays.
- [ ] Avoid loading complete large images into RAM.
- [ ] Test a small image first.
- [ ] Test full-size assets.
- [ ] Measure file-read time, decode time, render time, RAM, and CPU usage.
- [ ] Verify concurrent filesystem users are serialized safely by the flash
      wrapper.

Acceptance criterion: LVGL repeatedly renders images from LittleFS without
corruption, excessive stack use, or filesystem errors.

## Phase 11 - Production cleanup and robustness

- [ ] Keep `EXT_FLASH_ENABLE_VALIDATION_TESTS=0U` in production builds.
- [ ] Remove or gate verbose bring-up logging.
- [ ] Keep bounded timeouts for SPI, program, and erase operations.
- [ ] Confirm the wrapper mutex is released on every error path.
- [ ] Add flash protocol recovery after a failed transfer when required.
- [ ] Verify watchdog behavior during long erase operations.
- [ ] Document LittleFS format policy and recovery behavior.
- [ ] Record the selected filesystem geometry as an on-disk compatibility
      contract.
- [ ] Run long-duration file update/readback testing.

Acceptance criterion: the filesystem operates without destructive startup
tests, unbounded waits, concurrency races, or excessive diagnostic noise.

## Phase 12 - Integrate the reserved firmware partition later

- [ ] Keep `ext_fw_partition` outside LittleFS.
- [ ] Decide whether it will become an MCUboot secondary slot or an
      application-managed download area.
- [ ] Add the custom external flash driver to the MCUboot build if MCUboot must
      access it.
- [ ] Resolve the current internal secondary-slot strategy before assigning an
      external slot role.
- [ ] Validate upload, signature verification, swap/copy, rollback, and
      recovery independently from LittleFS.

Acceptance criterion: firmware update operations never erase or overwrite
`ext_storage_partition`, and LittleFS never accesses `ext_fw_partition`.

## Progress rule

Complete each phase's acceptance criterion before enabling the next layer.
Update the checkboxes in this document as work is completed and verified on
hardware.
