#include "rfsoc_stream.h"

#include <string.h>

int rfsoc_stream_init(
    rfsoc_stream_t *stream,
    rfsoc_ring_t *ring,
    rfsoc_block_processor_fn process_block,
    void *user_context)
{
    if (!stream || !ring || !ring->blocks || !process_block) {
        return -1;
    }

    memset(stream, 0, sizeof(*stream));
    stream->ring = ring;
    stream->process_block = process_block;
    stream->user_context = user_context;
    return 0;
}

int rfsoc_stream_process_one(rfsoc_stream_t *stream)
{
    if (!stream || !stream->ring || !stream->process_block) {
        return -1;
    }

    rfsoc_ring_block_t *block = rfsoc_ring_current_read(stream->ring);
    if (!block) {
        return 1;
    }

    const int status = stream->process_block(
        block->data,
        block->n_words,
        block->sequence,
        stream->user_context
    );
    stream->last_processor_status = status;

    if (status != 0) {
        /* Leave the block PROCESSING so the caller cannot silently reuse it. */
        return -2;
    }

    const size_t words = block->n_words;
    if (rfsoc_ring_release_read(stream->ring) != 0) {
        return -3;
    }

    stream->blocks_processed++;
    stream->words_processed += words;
    return 0;
}

int rfsoc_stream_drain(
    rfsoc_stream_t *stream,
    size_t max_blocks,
    size_t *processed_now)
{
    if (!stream) {
        return -1;
    }

    size_t processed = 0;

    while (max_blocks == 0 || processed < max_blocks) {
        const int status = rfsoc_stream_process_one(stream);

        if (status == 1) {
            break;
        }

        if (status != 0) {
            if (processed_now) {
                *processed_now = processed;
            }
            return status;
        }

        processed++;
    }

    if (processed_now) {
        *processed_now = processed;
    }

    return 0;
}
