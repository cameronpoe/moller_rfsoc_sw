#ifndef RFSOC_STREAM_H
#define RFSOC_STREAM_H

#include "rfsoc_ring.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Called for one completed ring block. The callback may invoke the existing
 * process_dma_buffer() or a test parser. Return zero on success.
 */
typedef int (*rfsoc_block_processor_fn)(
    const uint64_t *words,
    size_t n_words,
    uint64_t sequence,
    void *user_context
);

typedef struct {
    rfsoc_ring_t *ring;
    rfsoc_block_processor_fn process_block;
    void *user_context;
    uint64_t blocks_processed;
    uint64_t words_processed;
    int last_processor_status;
} rfsoc_stream_t;

int rfsoc_stream_init(
    rfsoc_stream_t *stream,
    rfsoc_ring_t *ring,
    rfsoc_block_processor_fn process_block,
    void *user_context
);

/*
 * Processes one READY block.
 * Returns 0 if a block was processed, 1 if none was ready, or <0 on error.
 */
int rfsoc_stream_process_one(rfsoc_stream_t *stream);

/*
 * Processes currently READY blocks. max_blocks == 0 means drain until the
 * next consumer block is not READY. This function never busy-waits.
 */
int rfsoc_stream_drain(
    rfsoc_stream_t *stream,
    size_t max_blocks,
    size_t *processed_now
);

#endif
