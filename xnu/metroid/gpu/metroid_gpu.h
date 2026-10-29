/* SPDX-License-Identifier: GPL-2.0-only
 * Metroid Adreno825 / Gen8.6.0 driver foundation.
 * Wire formats follow Nothing's public KGSL source at 031e8669eb19.
 * This is not an IOAccelerator implementation. See ../GPU.md.
 */
#ifndef METROID_GPU_H
#define METROID_GPU_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MG_CHIP_ID 0x44030000u
#define MG_GPU_PA 0x03d00000ull
#define MG_GPUCC_PA 0x03d90000ull
#define MG_GPUCC_SIZE 0x9800u
#define MG_GMU_PA 0x03d37000ull
#define MG_GMU_SIZE 0x68000u
#define MG_SMMU_PA 0x03da0000ull
#define MG_SMMU_SIZE 0x40000u
#define MG_FW_GMU "gen80600_gmu.bin"
#define MG_FW_SQE "gen80000_sqe.fw"
#define MG_FW_AQE "gen80000_aqe.fw"
#define MG_FW_ZAP "gen80000_zap.mbn"

enum mg_result {
    MG_OK = 0, MG_INVALID = -1, MG_LIMIT = -2, MG_UNSUPPORTED = -3,
    MG_UNBACKED = -4, MG_IO = -5, MG_EMPTY = -6, MG_FULL = -7,
    MG_TOO_SMALL = -8, MG_STALE = -9
};

enum mg_region { MG_ITCM, MG_CACHE, MG_DTCM, MG_UNCACHED, MG_UNCACHED_EXT };
#define MG_FW_MAX_BLOCKS 128u
#define MG_FW_MAX_RESERVATIONS 64u
#define MG_FW_MAX_BYTES (16u * 1024u * 1024u)
struct mg_fw_block {
    uint32_t address, size, offset, region;
};
struct mg_fw_reservation {
    uint32_t address, size, persistent, region;
};
struct mg_fw_plan {
    uint32_t count, reservation_count, metadata_mask;
    uint32_t versions[5]; /* core, core-dev, power, power-dev, HFI */
    struct mg_fw_block blocks[MG_FW_MAX_BLOCKS];
    struct mg_fw_reservation reservations[MG_FW_MAX_RESERVATIONS];
};

/* Parse the entire immutable image before allocation/MMIO. On failure clear plan.
 * Format validation does not authenticate firmware or establish its GPU model.
 */
int mg_fw_parse(const void *image, size_t bytes, struct mg_fw_plan *plan);
/* Caller holds the GMU in reset. prepare must check backing, without writing
 * firmware. write failure can leave partial contents: do not release GMU reset.
 * Image must remain immutable throughout. Plan is caller-owned (not stack-small).
 */
struct mg_fw_sink {
    void *context;
    bool (*prepare)(void *, const struct mg_fw_block *);
    bool (*write)(void *, const struct mg_fw_block *, const uint8_t *);
};
int mg_fw_load(const void *image, size_t bytes, struct mg_fw_plan *plan,
    const struct mg_fw_sink *sink);

/* HFI shared memory must be coherent or mapped uncached to both CPU and GMU.
 * No cache maintenance or doorbell is hidden here. Serialize each queue's host
 * operations externally. Never tear down/reuse its memory before DMA is stopped.
 */
#define MG_HFI_QUEUES 3u
#define MG_HFI_HEADERS 17u
#define MG_HFI_WORDS 1024u
#define MG_HFI_MAX_MESSAGE 255u /* 8-bit wire size, including header */
struct mg_hfi_header {
    uint32_t status, address, type, words, message_size, unused[5];
    volatile uint32_t read, write;
};
struct mg_hfi_table {
    uint32_t version, size, header_offset, header_size, count, active;
    struct mg_hfi_header queue[MG_HFI_HEADERS];
};
#define MG_HFI_OFFSET ((sizeof(struct mg_hfi_table) + 15u) & ~(size_t)15u)
#define MG_HFI_BYTES (MG_HFI_OFFSET + MG_HFI_QUEUES * MG_HFI_WORDS * 4u)
struct mg_hfi_queue {
    volatile struct mg_hfi_header *header;
    volatile uint32_t *ring;
    uint32_t address, type;
};

int mg_hfi_init(void *memory, size_t bytes, uint32_t gmu_address);
int mg_hfi_bind(void *memory, size_t bytes, uint32_t gmu_address,
    unsigned queue, struct mg_hfi_queue *out);
int mg_hfi_message(uint32_t id, uint32_t type, uint32_t sequence,
    uint32_t words, uint32_t *out);
int mg_hfi_write(struct mg_hfi_queue *, const uint32_t *message, size_t words);
/* Success returns the positive word count. Errors do not advance the queue.
 * MG_INVALID means corrupt firmware state: caller must quarantine the transport.
 */
int mg_hfi_read(struct mg_hfi_queue *, uint32_t *out, size_t capacity);
int mg_hfi_ack(const uint32_t *message, size_t words, uint32_t sent_header,
    uint32_t *firmware_result);
#endif
