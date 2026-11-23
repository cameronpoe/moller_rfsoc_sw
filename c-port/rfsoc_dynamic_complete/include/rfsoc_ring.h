#ifndef RFSOC_RING_H
#define RFSOC_RING_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    RFSOC_BLOCK_FREE = 0,
    RFSOC_BLOCK_DMA_WRITING,
    RFSOC_BLOCK_READY,
    RFSOC_BLOCK_PROCESSING
} rfsoc_block_state_t;

typedef struct {
    uint64_t *data;
    size_t n_words;
    rfsoc_block_state_t state;
    uint64_t sequence;
} rfsoc_ring_block_t;

typedef struct {
    uint64_t *base;
    size_t total_words;
    size_t words_per_block;
    size_t block_count;
    size_t producer_index;
    size_t consumer_index;
    uint64_t next_sequence;
    rfsoc_ring_block_t *blocks;
} rfsoc_ring_t;

int rfsoc_ring_init(
    rfsoc_ring_t *ring,
    uint64_t *base,
    size_t total_words,
    size_t words_per_block
);

void rfsoc_ring_destroy(rfsoc_ring_t *ring);

/* Reserves the current FREE producer block for DMA writing. */
rfsoc_ring_block_t *rfsoc_ring_current_write(rfsoc_ring_t *ring);

/* Publishes the current DMA_WRITING block and advances the producer. */
int rfsoc_ring_mark_ready(rfsoc_ring_t *ring);

/* Reserves the current READY consumer block for processing. */
rfsoc_ring_block_t *rfsoc_ring_current_read(rfsoc_ring_t *ring);

/* Releases the current PROCESSING block and advances the consumer. */
int rfsoc_ring_release_read(rfsoc_ring_t *ring);

#endif
