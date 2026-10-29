/* SPDX-License-Identifier: GPL-2.0-only
 * Metroid Adreno825 hardware bring-up entry point. metroidgpu=1 reads ONLY
 * the GPU clock-controller registers below. Default off, no GPU MMIO writes,
 * no GMU execution, no SMMU changes, and no published accelerator service.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include "metroid_gpu.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern void ml_io_unmap(vm_offset_t, vm_size_t);
void metroid_gpu_init(void);

static bool mg_is_metroid(void)
{
    DTEntry root;
    const void *value;
    unsigned size = 0;
    if (SecureDTLookupEntry(NULL, "/", &root) != kSuccess ||
        SecureDTGetProperty(root, "compatible", &value, &size) != kSuccess)
        return false;
    const char *list = value;
    /* DT properties are bounded byte arrays, not necessarily C strings. */
    for (unsigned offset = 0; offset < size;) {
        size_t length = strnlen(list + offset, size - offset);
        if (length == size - offset) return false;
        if (length == sizeof("nothing,metroid") - 1 &&
            memcmp(list + offset, "nothing,metroid", length) == 0) return true;
        offset += (unsigned)length + 1;
    }
    return false;
}

void metroid_gpu_init(void)
{
    int mode = 0;
    if (!PE_parse_boot_argn("metroidgpu", &mode, sizeof(mode)) || !mode) return;
    if (mode != 1 || !mg_is_metroid()) {
        printf("METROID GPU: unsupported mode/platform; disabled\n");
        return;
    }
    /* Each offset is from Nothing's drivers/clk/qcom/gpucc-tuna.c.
     * Do not probe GX, CM3, TCM or SMMU registers before power/clock bring-up:
     * reads of a powered-down domain can themselves produce a bus fault.
     */
    static const struct { const char *name; uint32_t offset; } registers[] = {
        {"CX_GDSCR", 0x9080}, {"CX_GDS_HW_CTRL", 0x9094},
        {"AHB_CBCR", 0x90bc}, {"CX_GMU_CBCR", 0x90d4},
        {"CXO_CBCR", 0x90e4}, {"HUB_CX_CBCR", 0x90e8},
        {"SMMU_VOTE", 0x7000},
    };
    uint32_t snapshot[sizeof(registers) / sizeof(registers[0])];
    /* ml_io_map's PA argument need not be page aligned: preserve its returned
     * byte bias by mapping the containing 16K pages explicitly.
     */
    uint64_t base = MG_GPUCC_PA & ~(uint64_t)PAGE_MASK;
    vm_size_t bias = (vm_size_t)(MG_GPUCC_PA - base);
    vm_size_t span = (vm_size_t)round_page(bias + MG_GPUCC_SIZE);
    vm_offset_t mapping = ml_io_map((vm_offset_t)base, span);
    if (!mapping) {
        printf("METROID GPU: GPUCC mapping failed\n");
        return;
    }
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); i++) {
        __asm__ volatile("dmb oshld" ::: "memory");
        snapshot[i] = *(volatile uint32_t *)(mapping + bias + registers[i].offset);
    }
    __asm__ volatile("dmb oshld" ::: "memory");
    ml_io_unmap(mapping, span);
    printf("METROID GPU: target Adreno825 Gen8.6.0 chip=%#x (source profile, not hardware ID)\n", MG_CHIP_ID);
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); i++)
        printf("METROID GPU: GPUCC %s +%#x = %#x\n",
            registers[i].name, registers[i].offset, snapshot[i]);
    printf("METROID GPU: snapshot only; GMU/SMMU/submission not started; acceleration unavailable\n");
}
#endif
