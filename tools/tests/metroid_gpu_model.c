/* SPDX-License-Identifier: GPL-2.0-only
 * Synthetic Gen8.6.0 wire fixtures. These are not captured hardware results.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "metroid_gpu.h"

/* Independent little-endian fixture: reserve uncached memory, version,
 * ITCM, DTCM and uncached data. No proprietary firmware bytes.
 */
static const uint8_t firmware[] = {
    0,0,0,0x60, 0,0,0,0, 1,0,0,0, 0,0x10,0,0,
    0,0,0,0, 0,0,0,0, 6,0,0,0, 0x34,0x12,0,0,
    0,0,0,0, 4,0,0,0, 0,0,0,0, 0,0,0,0, 0xaa,0xbb,0xcc,0xdd,
    0,0x40,0,0x10, 4,0,0,0, 0,0,0,0, 0,0,0,0, 1,2,3,4,
    0x20,0,0,0x60, 8,0,0,0, 0,0,0,0, 0,0,0,0, 5,6,7,8,9,10,11,12,
};
static struct mg_fw_plan plan;
static uint8_t blob[8192];
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (i * 8));
}
static void block(uint8_t *p, uint32_t address, uint32_t bytes, uint32_t type, uint32_t value)
{
    put32(p, address); put32(p + 4, bytes); put32(p + 8, type); put32(p + 12, value);
}
struct sink_state { unsigned prepares, writes, fail_prepare, fail_write; uint8_t contents[16]; };
static bool prepare(void *context, const struct mg_fw_block *b)
{
    struct sink_state *s = context;
    assert(b->region == MG_ITCM || b->region == MG_DTCM || b->region == MG_UNCACHED);
    return ++s->prepares != s->fail_prepare;
}
static bool write_block(void *context, const struct mg_fw_block *b, const uint8_t *data)
{
    struct sink_state *s = context;
    assert(s->prepares == 3); /* all destinations checked before any write */
    if (++s->writes == s->fail_write) return false;
    unsigned offset = b->region == MG_ITCM ? 0 : b->region == MG_DTCM ? 4 : 8;
    assert(offset + b->size <= sizeof(s->contents));
    memcpy(s->contents + offset, data, b->size);
    return true;
}
static void parse_bad(size_t bytes, int expected)
{
    memset(&plan, 0xa5, sizeof(plan));
    int result = mg_fw_parse(blob, bytes, &plan);
    assert(expected ? result == expected : result < 0);
    static const struct mg_fw_plan zero;
    assert(memcmp(&plan, &zero, sizeof(plan)) == 0);
}
static void firmware_tests(void)
{
    assert(sizeof(firmware) == 96);
    assert(mg_fw_parse(firmware, sizeof(firmware), &plan) == MG_OK);
    assert(plan.count == 3 && plan.reservation_count == 1);
    assert(plan.metadata_mask == 16 && plan.versions[4] == 0x1234);
    assert(plan.blocks[0].offset == 48 && plan.blocks[0].region == MG_ITCM);
    assert(plan.blocks[1].offset == 68 && plan.blocks[1].region == MG_DTCM);
    assert(plan.blocks[2].offset == 88 && plan.blocks[2].region == MG_UNCACHED);
    assert(plan.reservations[0].address == 0x60000000 && plan.reservations[0].size == 4096);
    struct sink_state state = {0};
    struct mg_fw_sink sink = {&state, prepare, write_block};
    assert(mg_fw_load(firmware, sizeof(firmware), &plan, &sink) == MG_OK);
    static const uint8_t want[] = {0xaa,0xbb,0xcc,0xdd,1,2,3,4,5,6,7,8,9,10,11,12};
    assert(state.writes == 3 && memcmp(state.contents, want, sizeof(want)) == 0);
    state = (struct sink_state){.fail_prepare = 3};
    assert(mg_fw_load(firmware, sizeof(firmware), &plan, &sink) == MG_UNBACKED && !state.writes);
    state = (struct sink_state){.fail_write = 2};
    assert(mg_fw_load(firmware, sizeof(firmware), &plan, &sink) == MG_IO && state.writes == 2);
    memcpy(blob, firmware, sizeof(firmware));
    state = (struct sink_state){0};
    assert(mg_fw_load(blob, sizeof(firmware) - 1, &plan, &sink) == MG_INVALID);
    assert(!state.prepares && !state.writes);
    for (size_t n = 0; n < sizeof(firmware); n++)
        if (n != 52 && n != 72) parse_bad(n, 0); /* complete prefix images are legal */
    assert(mg_fw_parse(NULL, 16, &plan) == MG_INVALID);
    assert(mg_fw_parse(blob, MG_FW_MAX_BYTES + 1, &plan) == MG_INVALID);
    assert(mg_fw_parse(blob, 16, NULL) == MG_INVALID);
    memcpy(blob, firmware, sizeof(firmware)); put32(blob + 36, 0xffffffff);
    parse_bad(sizeof(firmware), MG_INVALID);
    memcpy(blob, firmware, sizeof(firmware)); put32(blob + 32, 1);
    parse_bad(sizeof(firmware), MG_INVALID);
    memcpy(blob, firmware, sizeof(firmware)); put32(blob + 32, 0x3ffc); put32(blob + 36, 8);
    parse_bad(56, MG_UNSUPPORTED);
    memcpy(blob, firmware, sizeof(firmware)); put32(blob + 72, 0xfffffff0);
    parse_bad(sizeof(firmware), MG_UNSUPPORTED);
    memcpy(blob, firmware, sizeof(firmware)); put32(blob + 72, 0x60001000);
    parse_bad(sizeof(firmware), MG_UNBACKED);
    memcpy(blob, firmware, sizeof(firmware)); put32(blob + 56, 0);
    parse_bad(sizeof(firmware), MG_INVALID); /* overlapping ITCM */
    memcpy(blob, firmware, sizeof(firmware)); put32(blob + 40, 9);
    parse_bad(sizeof(firmware), MG_UNSUPPORTED);
    memcpy(blob, firmware, sizeof(firmware)); put32(blob + 24, 99);
    parse_bad(sizeof(firmware), MG_UNSUPPORTED);
    memcpy(blob, firmware, sizeof(firmware)); block(blob + 96, 0, 0, 2, 1);
    parse_bad(112, MG_INVALID); /* metadata after data */
    memcpy(blob, firmware, sizeof(firmware)); put32(blob + 12, 0);
    parse_bad(sizeof(firmware), MG_INVALID);
    memcpy(blob, firmware, 32); memcpy(blob + 32, firmware, 16); memcpy(blob + 48, firmware + 32, 64);
    parse_bad(112, MG_INVALID); /* overlapping reservations */
    memcpy(blob, firmware, 32); memcpy(blob + 32, firmware + 16, 16); memcpy(blob + 48, firmware + 32, 64);
    parse_bad(112, MG_INVALID); /* duplicate version metadata */
    memcpy(blob, firmware, sizeof(firmware)); put32(blob + 8, 7);
    assert(mg_fw_parse(blob, sizeof(firmware), &plan) == MG_OK && plan.reservations[0].persistent);
    for (unsigned i = 0; i < MG_FW_MAX_BLOCKS + 1; i++) {
        block(blob + i * 20, i * 4, 4, 0, 0); put32(blob + i * 20 + 16, i);
    }
    parse_bad((MG_FW_MAX_BLOCKS + 1) * 20, MG_LIMIT);
    /* Every random input must terminate without over-reading its true extent. */
    uint32_t rng = 0x12345678;
    for (unsigned t = 0; t < 10000; t++) {
        size_t n = t % 192;
        uint8_t *exact = malloc(n ? n : 1);
        for (size_t i = 0; i < n; i++) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; exact[i] = (uint8_t)rng;
        }
        (void)mg_fw_parse(exact, n, &plan);
        free(exact);
    }
}

static _Alignas(16) uint8_t memory[MG_HFI_BYTES + 16];
static struct mg_hfi_queue q;
static void reset_queue(void)
{
    memset(memory, 0xa5, sizeof(memory));
    assert(mg_hfi_init(memory, sizeof(memory), 0x60000000) == MG_OK);
    assert(mg_hfi_bind(memory, sizeof(memory), 0x60000000, 0, &q) == MG_OK);
}
static void hfi_tests(void)
{
    uint32_t msg[256], out[256], hdr = 0xdeadbeef;
    reset_queue();
    assert(MG_HFI_OFFSET == 848 && MG_HFI_BYTES == 13136);
    struct mg_hfi_table *t = (void *)memory;
    assert(t->size == 210 && t->header_size == 12 && t->count == 3 && t->header_offset == 6);
    for (unsigned i = 0; i < 3; i++) {
        assert(t->queue[i].type == i && t->queue[i].status == 1);
        assert(t->queue[i].address == 0x60000350 + i * 4096);
    }
    for (size_t i = MG_HFI_BYTES; i < sizeof(memory); i++) assert(memory[i] == 0xa5);
    assert(mg_hfi_init(memory + 1, sizeof(memory) - 1, 0x60000000) == MG_INVALID);
    assert(mg_hfi_init(memory, MG_HFI_BYTES - 1, 0x60000000) == MG_INVALID);
    assert(mg_hfi_init(memory, sizeof(memory), 0xfffffff0) == MG_INVALID);
    assert(mg_hfi_init(memory, sizeof(memory), 0x7ffff000) == MG_INVALID);
    assert(mg_hfi_init(memory, sizeof(memory), 0x4000) == MG_INVALID);
    assert(mg_hfi_bind(memory, sizeof(memory), 0x60000000, 3, &q) == MG_INVALID);
    assert(mg_hfi_message(0x42, 0, 0xabc, 3, &hdr) == MG_OK && hdr == 0xabc00342);
    assert(mg_hfi_message(256, 0, 0, 1, &hdr) == MG_INVALID && hdr == 0xabc00342);
    assert(mg_hfi_message(1, 0, 4096, 1, &hdr) == MG_INVALID);
    assert(mg_hfi_message(1, 0, 0, 256, &hdr) == MG_INVALID);
    assert(mg_hfi_message(1, 0, 0, 0, &hdr) == MG_INVALID);
    msg[0] = hdr; msg[1] = 0x1234; msg[2] = 0x5678;
    assert(mg_hfi_read(&q, out, 256) == MG_EMPTY);
    assert(mg_hfi_write(&q, msg, 3) == MG_OK && q.header->write == 4);
    assert(q.ring[3] == 0xfafafafa);
    assert(mg_hfi_read(&q, out, 2) == MG_TOO_SMALL && q.header->read == 0);
    assert(mg_hfi_read(&q, out, 3) == 3 && !memcmp(msg, out, 12));
    assert(q.header->read == 4);
    reset_queue(); q.header->read = q.header->write = 1020;
    assert(mg_hfi_message(1, 0, 17, 5, &msg[0]) == MG_OK);
    for (unsigned i = 1; i < 5; i++) msg[i] = i;
    assert(mg_hfi_write(&q, msg, 5) == MG_OK && q.header->write == 4);
    assert(q.ring[0] == 4 && q.ring[1] == 0xfafafafa);
    assert(mg_hfi_read(&q, out, 256) == 5 && !memcmp(msg, out, 20));
    assert(q.header->read == 4);
    reset_queue(); q.header->write = 1020;
    assert(mg_hfi_write(&q, msg, 5) == MG_FULL && q.header->write == 1020);
    reset_queue(); assert(mg_hfi_message(1, 0, 0, 255, &msg[0]) == MG_OK);
    for (unsigned i = 1; i < 255; i++) msg[i] = i;
    for (unsigned i = 0; i < 3; i++) assert(mg_hfi_write(&q, msg, 255) == MG_OK);
    assert(mg_hfi_write(&q, msg, 255) == MG_FULL);
    for (unsigned i = 0; i < 3; i++) assert(mg_hfi_read(&q, out, 256) == 255 && !memcmp(msg, out, 1020));
    assert(mg_hfi_read(&q, out, 256) == MG_EMPTY);
    for (unsigned bad = 0; bad < 7; bad++) {
        reset_queue();
        if (bad == 0) q.header->read = 1024;
        if (bad == 1) q.header->write = 2;
        if (bad == 2) q.header->words = 0xffffffff;
        if (bad == 3) q.header->status = 0;
        if (bad == 4) q.header->address++;
        if (bad == 5) q.header->type++;
        if (bad == 6) q.header->message_size = 4;
        uint32_t r = q.header->read, w = q.header->write;
        assert(mg_hfi_write(&q, msg, 255) == MG_INVALID);
        assert(mg_hfi_read(&q, out, 256) == MG_INVALID);
        assert(q.header->read == r && q.header->write == w);
    }
    reset_queue(); q.header->write = 4; q.ring[0] = 0;
    assert(mg_hfi_read(&q, out, 256) == MG_INVALID && q.header->read == 0);
    q.ring[0] = 0x2001; /* header advertises more than published */
    assert(mg_hfi_read(&q, out, 256) == MG_INVALID && q.header->read == 0);
    reset_queue();
    for (unsigned i = 0; i < 10000; i++) {
        unsigned n = i % 255 + 1;
        assert(mg_hfi_message(i % 256, 0, i % 4096, n, &msg[0]) == MG_OK);
        for (unsigned j = 1; j < n; j++) msg[j] = i ^ (j * 0x1234);
        out[n] = 0xa5a5a5a5;
        assert(mg_hfi_write(&q, msg, n) == MG_OK);
        assert(mg_hfi_read(&q, out, n) == (int)n && !memcmp(out, msg, n * 4));
        assert(out[n] == 0xa5a5a5a5);
    }
    uint32_t result = 0xdeadbeef;
    const uint32_t ack[] = {0x00010342, 0xabc80342, 0};
    assert(mg_hfi_ack(ack, 3, 0xabc00342, &result) == MG_OK && result == 0);
    result = 0xdeadbeef;
    assert(mg_hfi_ack(ack, 3, 0xabb00342, &result) == MG_STALE && result == 0xdeadbeef);
    assert(mg_hfi_ack(ack, 2, 0xabc00342, &result) == MG_INVALID);
    const uint32_t not_ack[] = {0x00000342, 0xabc00342, 0};
    assert(mg_hfi_ack(not_ack, 3, 0xabc00342, &result) == MG_INVALID);
    assert(mg_hfi_ack(ack, 3, 0xabc00343, &result) == MG_STALE);
}

int main(void)
{
    assert(MG_CHIP_ID == 0x44030000 && MG_GMU_PA == 0x3d37000 && MG_SMMU_PA == 0x3da0000);
    firmware_tests(); hfi_tests();
    puts("PASS: Gen8 firmware bounds/backing/load preflight; HFI ABI, wrap, padding, full/empty, corrupt indices, stale ACKs, random inputs (ASan/UBSan)");
    return 0;
}
