#include "rfsoc_ring.h"

#include <stdlib.h>
#include <string.h>

int rfsoc_ring_init(
    rfsoc_ring_t *ring,
    uint64_t *base,
    size_t total_words,
    size_t words_per_block)
{
    if (!ring || !base || total_words == 0 || words_per_block == 0) {
        return -1;
    }

    if (total_words < words_per_block ||
        total_words % words_per_block != 0) {
        return -2;
    }

    const size_t block_count = total_words / words_per_block;
    if (block_count < 2) {
        return -3;
    }

    rfsoc_ring_block_t *blocks = calloc(block_count, sizeof(*blocks));
    if (!blocks) {
        return -4;
    }

    memset(ring, 0, sizeof(*ring));
    ring->base = base;
    ring->total_words = total_words;
    ring->words_per_block = words_per_block;
    ring->block_count = block_count;
    ring->next_sequence = 1;
    ring->blocks = blocks;

    for (size_t i = 0; i < block_count; ++i) {
        blocks[i].data = base + i * words_per_block;
        blocks[i].n_words = words_per_block;
        blocks[i].state = RFSOC_BLOCK_FREE;
        blocks[i].sequence = 0;
    }

    return 0;
}

void rfsoc_ring_destroy(rfsoc_ring_t *ring)
{
    if (!ring) {
        return;
    }

    free(ring->blocks);
    memset(ring, 0, sizeof(*ring));
}

rfsoc_ring_block_t *rfsoc_ring_current_write(rfsoc_ring_t *ring)
{
    if (!ring || !ring->blocks || ring->block_count == 0) {
        return NULL;
    }

    rfsoc_ring_block_t *block = &ring->blocks[ring->producer_index];
    if (block->state != RFSOC_BLOCK_FREE) {
        return NULL;
    }

    block->state = RFSOC_BLOCK_DMA_WRITING;
    return block;
}

int rfsoc_ring_mark_ready(rfsoc_ring_t *ring)
{
    if (!ring || !ring->blocks || ring->block_count == 0) {
        return -1;
    }

    rfsoc_ring_block_t *block = &ring->blocks[ring->producer_index];
    if (block->state != RFSOC_BLOCK_DMA_WRITING) {
        return -2;
    }

    block->sequence = ring->next_sequence++;
    block->state = RFSOC_BLOCK_READY;
    ring->producer_index = (ring->producer_index + 1) % ring->block_count;
    return 0;
}

rfsoc_ring_block_t *rfsoc_ring_current_read(rfsoc_ring_t *ring)
{
    if (!ring || !ring->blocks || ring->block_count == 0) {
        return NULL;
    }

    rfsoc_ring_block_t *block = &ring->blocks[ring->consumer_index];
    if (block->state != RFSOC_BLOCK_READY) {
        return NULL;
    }

    block->state = RFSOC_BLOCK_PROCESSING;
    return block;
}

int rfsoc_ring_release_read(rfsoc_ring_t *ring)
{
    if (!ring || !ring->blocks || ring->block_count == 0) {
        return -1;
    }

    rfsoc_ring_block_t *block = &ring->blocks[ring->consumer_index];
    if (block->state != RFSOC_BLOCK_PROCESSING) {
        return -2;
    }

    block->state = RFSOC_BLOCK_FREE;
    ring->consumer_index = (ring->consumer_index + 1) % ring->block_count;
    return 0;
}
