/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * GMU block firmware loader, adapted from the public Gen8 memory layout and
 * kgsl_gmu_core.h block ABI. No Apple or Qualcomm firmware is embedded here.
 */
#include "metroid_gpu.h"
#ifdef MG_HOST_TEST
#include <string.h>
#else
#include <libkern/libkern.h>
#endif

static uint32_t mg_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int mg_region_for(uint32_t address, uint32_t size)
{
    static const uint32_t starts[] = {0, 0x4000, 0x10004000, 0x60000000, 0xc0000000};
    static const uint32_t lengths[] = {0x4000, 0xffc000, 0x4000, 0x20000000, 0x20000000};
    uint64_t end = (uint64_t)address + size;
    if (!size || (address & 3) || (size & 3)) return MG_INVALID;
    for (unsigned i = 0; i < sizeof(starts) / sizeof(starts[0]); i++)
        if (address >= starts[i] && end <= (uint64_t)starts[i] + lengths[i])
            return (int)i;
    return MG_UNSUPPORTED;
}

static bool mg_overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{
    return (uint64_t)a < (uint64_t)b + m && (uint64_t)b < (uint64_t)a + n;
}

static int mg_parse(const uint8_t *image, size_t bytes, struct mg_fw_plan *p)
{
    size_t offset = 0;
    bool data_seen = false;
    if (!image || !bytes || bytes > MG_FW_MAX_BYTES) return MG_INVALID;
    while (offset < bytes) {
        if (bytes - offset < 16) return MG_INVALID;
        uint32_t address = mg_le32(image + offset), size = mg_le32(image + offset + 4);
        uint32_t type = mg_le32(image + offset + 8), value = mg_le32(image + offset + 12);
        offset += 16;
        if (size > bytes - offset) return MG_INVALID;
        if (size) {
            if (type != 0) return MG_UNSUPPORTED;
            int region = mg_region_for(address, size);
            if (region < 0) return region;
            if (p->count == MG_FW_MAX_BLOCKS) return MG_LIMIT;
            for (uint32_t i = 0; i < p->count; i++)
                if (mg_overlap(address, size, p->blocks[i].address, p->blocks[i].size))
                    return MG_INVALID;
            p->blocks[p->count++] = (struct mg_fw_block){address, size, (uint32_t)offset, (uint32_t)region};
            data_seen = true;
        } else {
            /* Nothing's parser requires metadata to precede load data. */
            if (data_seen) return MG_INVALID;
            if (type == 1 || type == 7) {
                int region = mg_region_for(address, value);
                if (region < 0) return region;
                if (p->reservation_count == MG_FW_MAX_RESERVATIONS) return MG_LIMIT;
                for (uint32_t i = 0; i < p->reservation_count; i++) {
                    const struct mg_fw_reservation *r = &p->reservations[i];
                    if (mg_overlap(address, value, r->address, r->size)) return MG_INVALID;
                }
                p->reservations[p->reservation_count++] = (struct mg_fw_reservation){
                    address, value, type == 7, (uint32_t)region};
            } else if (type >= 2 && type <= 6) {
                if (p->metadata_mask & (1u << (type - 2))) return MG_INVALID;
                p->metadata_mask |= 1u << (type - 2);
                p->versions[type - 2] = value;
            } else if (type != 0) return MG_UNSUPPORTED;
        }
        offset += size;
    }
    if (!p->count) return MG_INVALID;
    /* Match load_fw's requirement that non-TCM data has backing reserved first.
     * Allocation granules/permissions are the SMMU allocator's responsibility.
     */
    for (uint32_t i = 0; i < p->count; i++) {
        const struct mg_fw_block *b = &p->blocks[i];
        if (b->region == MG_ITCM || b->region == MG_DTCM) continue;
        bool backed = false;
        for (uint32_t j = 0; j < p->reservation_count; j++) {
            const struct mg_fw_reservation *r = &p->reservations[j];
            if (b->address >= r->address &&
                (uint64_t)b->address + b->size <= (uint64_t)r->address + r->size)
                backed = true;
        }
        if (!backed) return MG_UNBACKED;
    }
    return MG_OK;
}

int mg_fw_parse(const void *image, size_t bytes, struct mg_fw_plan *plan)
{
    if (!plan) return MG_INVALID;
    memset(plan, 0, sizeof(*plan));
    int result = mg_parse(image, bytes, plan);
    if (result) memset(plan, 0, sizeof(*plan));
    return result;
}

int mg_fw_load(const void *image, size_t bytes, struct mg_fw_plan *plan,
    const struct mg_fw_sink *sink)
{
    if (!sink || !sink->prepare || !sink->write) return MG_INVALID;
    int result = mg_fw_parse(image, bytes, plan);
    if (result) return result;
    for (uint32_t i = 0; i < plan->count; i++)
        if (!sink->prepare(sink->context, &plan->blocks[i])) return MG_UNBACKED;
    for (uint32_t i = 0; i < plan->count; i++) {
        const struct mg_fw_block *b = &plan->blocks[i];
        if (!sink->write(sink->context, b, (const uint8_t *)image + b->offset)) return MG_IO;
    }
    return MG_OK;
}
