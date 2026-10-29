# Metroid Adreno 825 hardware driver

Hardware acceleration is **not working yet**. Overlay `0033-metroid-gpu.patch`
adds the initial firmware/HFI implementation and an optional GPUCC snapshot.
It does not start the GPU or switch CoreAnimation away from its current renderer.

## Public source baseline

Nothing's SM8735 kernel is pinned to
`031e8669eb198d93e69532b01af973a1c2a82710`:

- [tuna-gpu.dtsi](https://github.com/NothingOSS/android_kernel_msm-6.6_nothing_sm8735/blob/031e8669eb198d93e69532b01af973a1c2a82710/vendor/qcom/opensource/graphics-devicetree/gpu/tuna-gpu.dtsi)
  identifies `Adreno825`, chip ID `0x44030000`, compatible
  `qcom,adreno-gpu-gen8-6-0`.
- [adreno-gpulist.h](https://github.com/NothingOSS/android_kernel_msm-6.6_nothing_sm8735/blob/031e8669eb198d93e69532b01af973a1c2a82710/vendor/qcom/opensource/graphics-kernel/adreno-gpulist.h)
  selects `adreno_gpu_core_gen8_6_0` and its hardware scheduler. Marketing name,
  DT chip ID and internal Gen8 revision are distinct; do not derive one from
  the spelling of another.
- [adreno_gen8_gmu.c](https://github.com/NothingOSS/android_kernel_msm-6.6_nothing_sm8735/blob/031e8669eb198d93e69532b01af973a1c2a82710/vendor/qcom/opensource/graphics-kernel/adreno_gen8_gmu.c)
  and `kgsl_gmu_core.h` define the firmware block ABI, memory windows and boot order.
- [adreno_gen8_hwsched_hfi.c](https://github.com/NothingOSS/android_kernel_msm-6.6_nothing_sm8735/blob/031e8669eb198d93e69532b01af973a1c2a82710/vendor/qcom/opensource/graphics-kernel/adreno_gen8_hwsched_hfi.c),
  `adreno_gen8_hfi.c` and `adreno_hfi.h` define the queue layout and messages.
  The hardware scheduler uses priority zero for its three control queues;
  the legacy backend's 10/10/40 priorities are not used here.
- [gpucc-tuna.c](https://github.com/NothingOSS/android_kernel_msm-6.6_nothing_sm8735/blob/031e8669eb198d93e69532b01af973a1c2a82710/drivers/clk/qcom/gpucc-tuna.c)
  defines the clock controller and CX power domain.
- [msm-arm-smmu-tuna.dtsi](https://github.com/NothingOSS/android_kernel_msm-6.6_nothing_sm8735/blob/031e8669eb198d93e69532b01af973a1c2a82710/arch/arm64/boot/dts/vendor/qcom/msm-arm-smmu-tuna.dtsi)
  describes the separate GPU SMMU/TBU and its power/clock dependencies.

The reference checkout used locally is `/private/tmp/metroid-kernel-ufs`.
Sailfish was reviewed at `6f27e41ff249f2680540ebe8ee5ab982c5e641c9` in
`/private/tmp/ios-sailfish-gles-reference`. Its Adreno 530 startup sequence,
firmware and GPU page-table assumptions are not imported into Metroid.

## Resources and firmware

These are **public source values**, not successful XNU register readbacks:

| Resource | Address / identifier |
| --- | --- |
| GPU | `0x03d00000`, size `0x40000` |
| GMU | `0x03d37000`, size `0x68000` |
| GMU AO decode 0 | `0x03d40000`, size `0x10000` |
| GPUCC | `0x03d90000`, size `0x9800` |
| GPU SMMU | `0x03da0000`, size `0x40000` |
| GPU QTB | `0x03de8000`, size `0x1000` |
| GPU/host IRQ | SPI 300 / 80, GIC INTID 332 / 112 |
| HFI/GMU IRQ | SPI 304 / 305, GIC INTID 336 / 337 |
| User/LPAC/secure/GMU stream IDs | `0 / 1 / 2 / 5` |
| GMU firmware | `gen80600_gmu.bin` |
| SQE / AQE firmware | `gen80000_sqe.fw` / `gen80000_aqe.fw` |
| Secure zap firmware | `gen80000_zap.mbn` |

Register constants in `gen8_reg.h` are **dword offsets from the GPU base**.
For example ITCM offset `0x1b400` means PA `0x03d6d000`; it lies in the GMU
region, not in the GPU's first `0x40000` bytes. Do not apply the offset a
second time relative to the GMU resource.

The Adreno 825 GMU image differs from the Adreno 830's `gen80000_gmu.bin`.
The source repository contains the driver and filenames, not these firmware
images. No matching firmware set has been acquired or authenticated. Public
kernel source work can continue without repairing or reading `/vendor`.

## Implemented pieces

`gpu/metroid_gpu_fw.c` parses little-endian 16-byte block records, validates
complete payload bounds and the five Gen8 memory windows, and builds an
allocation/load plan. It rejects overlapping data or reservations, unsupported
records, metadata after payloads, oversized plans and external data without a
preallocation record. These checks are intentionally conservative until tested
against the actual `gen80600_gmu.bin`.

`mg_fw_load` validates the whole image and asks the sink to verify **all**
destinations before copying anything. The future hardware sink must hold CM3
in reset, prepare SMMU-backed external allocations and use MMIO writes for TCM.
A write failure leaves CM3 in reset; the caller must not start partially loaded
firmware. Parsing does not establish firmware authenticity or chip compatibility.

`gpu/metroid_gpu_hfi.c` implements the 840-byte queue table (17 reserved headers),
848-byte aligned queue offset and three 4 KiB control rings. It implements
message headers, 4-dword padding/wrap, free-space accounting, bounded reads and
full echoed-header ACK matching including the 12-bit sequence. Corrupt indices,
truncated packets and stale ACKs fail without advancing the queue. The host
must serialize each queue, supply coherent/uncached DMA memory and ring the
doorbell after publishing data. This is not the complete GMU scheduling protocol.

`gpu/metroid_gpu.c` adds `metroidgpu=1`: a one-shot, read-only GPUCC snapshot,
gated on the ADT's `nothing,metroid` compatibility. It reads CX domain/clock
status only. The default does nothing. It does not read powered-down GMU/GX
registers, assert resets, enable clocks, map DMA or publish an accelerator.
Hardware accessibility of even these GPUCC reads remains untested.

The existing Metroid staging tool automatically selects overlay 0033. After
editing canonical C/H files, regenerate it with:

```sh
python3 tools/sync-metroid-gpu-patch.py
python3 tools/sync-metroid-gpu-patch.py --check
python3 tools/tests/test-metroid-gpu.py
```

An offline firmware inspector uses the same C parser, without accessing a phone:

```sh
python3 tools/metroid-gpu-firmware.py /path/to/gen80600_gmu.bin
```

## Validation and remaining work

Host ASan/UBSan tests pass for synthetic firmware loading, bounds/overflow,
preallocation and sink failure handling; HFI table ABI, ring wrap/full/empty,
padding, corrupt headers/indices, small receive buffers and stale ACKs. Tests
also exercise 10,000 random firmware inputs and 10,000 ring round trips.
The inspector accepts the synthetic fixture and rejects a truncated one.

The overlay applies with `patch --fuzz=0` to the previously validated full
Metroid source stack, and its generated files match canonical sources. ARM64
object compilation passes for the three new C files and modified `bsd_init.c`
in DEVELOPMENT and with DEBUG/DEVELOPMENT disabled. New files have no compiler
diagnostics; `bsd_init.c` retains warnings from existing headers.

No full kernel link, CI run, GPU boot, firmware execution, fence completion,
triangle render, Mesa build or compositor acceleration has been tested. A fresh
end-to-end patch replay attempted from the early cached baseline stopped at
common 0031 because that baseline lacks the staged HFS dependency; it did not
reach the new GPU overlay. GitHub CLI currently has no authenticated account,
so no CI run was dispatched.

Next hardware milestone is **GMU boot and a verified HFI round trip**:

1. Port CX/MX/GX and DDR/CNOC votes, GCC/GPUCC clock sequencing and bounded
   power-state waits from Nothing's Gen8 driver. The public DT requests GMU
   500/650 MHz and the core profile specifies HUB 200 MHz. Do not assume the
   bootloader left the GPU running.
2. Establish the GPU SMMU and GMU stream 5 address space before allocating HFI
   or firmware buffers. Keep the display/storage/touch SMMUs independent. XNU
   16 KiB pages require explicit handling by the GPU's page-table allocator.
3. Acquire the matching firmware set from a trusted stock package, validate it
   with the inspector, add reserved payload transport, implement the hardware
   load sink, secure initialization, GMU register setup and bounded HFI boot.
4. Implement hardware scheduler memory requests, SQE/AQE startup, GPU contexts,
   buffer lifetimes, interrupts, fences and timeout quarantine. Prove a small
   GPU write and a triangle before exposing a userspace service.
5. Integrate an A825-capable Mesa backend and the EAGL/IOSurface bridge. The
   reviewed Mesa 26.0.0 device table (`c10cba7efaa003e11c5c832bb2347e55c619ae7a`)
   lacks chip `0x44030000`; adding an ID alone does not establish compatible
   shader generation or command streams. Verify a suitable backend before
   enabling CoreAnimation's GLES renderer. Metal remains a separate project.

Keep real firmware, device state and generated images in ignored directories.
Only a temporary boot should be used for the first GPUCC/GMU hardware tests;
this implementation performs no phone partition changes.
