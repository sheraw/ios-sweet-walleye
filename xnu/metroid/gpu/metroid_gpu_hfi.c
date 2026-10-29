/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Gen8 HFI host transport. Matches adreno_hfi.h/adreno_gen8_hfi.c wire layout.
 * No live GMU is started by this file. See the memory/locking contract in .h.
 */
#include "metroid_gpu.h"
#ifdef MG_HOST_TEST
#include <string.h>
#define MG_MB() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#else
#include <libkern/libkern.h>
#define MG_MB() __asm__ volatile("dmb osh" ::: "memory")
#endif

_Static_assert(sizeof(struct mg_hfi_header) == 48, "HFI queue ABI");
_Static_assert(offsetof(struct mg_hfi_header, read) == 40, "HFI read ABI");
_Static_assert(offsetof(struct mg_hfi_header, write) == 44, "HFI write ABI");
_Static_assert(sizeof(struct mg_hfi_table) == 840, "HFI table ABI");

static uint32_t mg_queue_type(unsigned queue)
{
    /* Gen8.6.0 selects the hwsched backend: all three control queues have
     * priority 0 (adreno_gen8_hwsched_hfi.c), unlike the legacy 10/10/40.
     */
    return queue;
}

static bool mg_memory_valid(void *memory, size_t bytes, uint32_t address)
{
    uint64_t end = (uint64_t)address + MG_HFI_BYTES;
    return memory && !((uintptr_t)memory & 15) && bytes >= MG_HFI_BYTES && !(address & 15) &&
        ((address >= 0x60000000 && end <= 0x80000000ull) ||
         (address >= 0xc0000000 && end <= 0xe0000000ull));
}

int mg_hfi_init(void *memory, size_t bytes, uint32_t address)
{
    if (!mg_memory_valid(memory, bytes, address)) return MG_INVALID;
    memset(memory, 0, MG_HFI_BYTES);
    struct mg_hfi_table *t = memory;
    t->size = sizeof(*t) / 4;
    t->header_offset = 6;
    t->header_size = 12;
    t->count = t->active = MG_HFI_QUEUES;
    for (unsigned i = 0; i < MG_HFI_QUEUES; i++) {
        struct mg_hfi_header *h = &t->queue[i];
        h->status = 1;
        h->address = address + (uint32_t)MG_HFI_OFFSET + i * MG_HFI_WORDS * 4;
        h->type = mg_queue_type(i);
        h->words = MG_HFI_WORDS;
    }
    MG_MB();
    return MG_OK;
}

int mg_hfi_bind(void *memory, size_t bytes, uint32_t address,
    unsigned queue, struct mg_hfi_queue *out)
{
    if (!out || queue >= MG_HFI_QUEUES || !mg_memory_valid(memory, bytes, address))
        return MG_INVALID;
    volatile struct mg_hfi_table *t = memory;
    if (t->version || t->size != sizeof(*t) / 4 || t->header_offset != 6 ||
        t->header_size != 12 || t->count != MG_HFI_QUEUES || t->active != MG_HFI_QUEUES)
        return MG_INVALID;
    *out = (struct mg_hfi_queue){
        .header = &t->queue[queue],
        .ring = (volatile uint32_t *)((uint8_t *)memory + MG_HFI_OFFSET) + queue * MG_HFI_WORDS,
        .address = address + (uint32_t)MG_HFI_OFFSET + queue * MG_HFI_WORDS * 4,
        .type = mg_queue_type(queue),
    };
    return MG_OK;
}

int mg_hfi_message(uint32_t id, uint32_t type, uint32_t sequence,
    uint32_t words, uint32_t *out)
{
    if (!out || id > 255 || type > 1 || sequence > 4095 ||
        !words || words > MG_HFI_MAX_MESSAGE) return MG_INVALID;
    *out = id | words << 8 | type << 16 | sequence << 20;
    return MG_OK;
}

static int mg_queue_indices(struct mg_hfi_queue *q, uint32_t *read, uint32_t *write)
{
    if (!q || !q->header || !q->ring) return MG_INVALID;
    volatile struct mg_hfi_header *h = q->header;
    if (h->status != 1 || h->address != q->address || h->type != q->type ||
        h->words != MG_HFI_WORDS || h->message_size) return MG_INVALID;
    *read = h->read;
    *write = h->write;
    MG_MB();
    if (*read >= MG_HFI_WORDS || *write >= MG_HFI_WORDS || ((*read | *write) & 3))
        return MG_INVALID;
    return MG_OK;
}

int mg_hfi_write(struct mg_hfi_queue *q, const uint32_t *message, size_t words)
{
    uint32_t read, write;
    if (!message || !words || words > MG_HFI_MAX_MESSAGE ||
        ((message[0] >> 8) & 255) != words) return MG_INVALID;
    int result = mg_queue_indices(q, &read, &write);
    if (result) return result;
    uint32_t padded = ((uint32_t)words + 3) & ~3u;
    uint32_t used = (write + MG_HFI_WORDS - read) % MG_HFI_WORDS;
    if (MG_HFI_WORDS - used <= padded) return MG_FULL;
    for (uint32_t i = 0; i < padded; i++)
        q->ring[(write + i) % MG_HFI_WORDS] = i < words ? message[i] : 0xfafafafa;
    MG_MB();
    q->header->write = (write + padded) % MG_HFI_WORDS;
    MG_MB(); /* publish before caller rings the GMU doorbell */
    return MG_OK;
}

int mg_hfi_read(struct mg_hfi_queue *q, uint32_t *out, size_t capacity)
{
    uint32_t read, write;
    if (!out || !capacity) return MG_INVALID;
    int result = mg_queue_indices(q, &read, &write);
    if (result) return result;
    if (read == write) return MG_EMPTY;
    uint32_t words = (q->ring[read] >> 8) & 255;
    uint32_t padded = (words + 3) & ~3u;
    uint32_t used = (write + MG_HFI_WORDS - read) % MG_HFI_WORDS;
    if (!words || used < padded) return MG_INVALID;
    if (words > capacity) return MG_TOO_SMALL;
    for (uint32_t i = 0; i < words; i++) out[i] = q->ring[(read + i) % MG_HFI_WORDS];
    MG_MB(); /* consume the payload before returning its space to the GMU */
    q->header->read = (read + padded) % MG_HFI_WORDS;
    MG_MB();
    return (int)words;
}

int mg_hfi_ack(const uint32_t *message, size_t words, uint32_t sent, uint32_t *result)
{
    if (!message || !result || words < 3 || words > MG_HFI_MAX_MESSAGE ||
        ((message[0] >> 8) & 255) != words || ((message[0] >> 16) & 15) != 1)
        return MG_INVALID;
    /* Full echoed header, including original size and sequence; ignore RECORD
     * exactly as Nothing's CMP_HFI_ACK_HDR does. Never mutate the input headers.
     */
    if ((message[1] & ~(1u << 19)) != (sent & ~(1u << 19))) return MG_STALE;
    *result = message[2];
    return MG_OK;
}
