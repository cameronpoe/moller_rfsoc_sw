#ifndef RFSOC_API_H
#define RFSOC_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rfsoc_processing.h"

#define RFSOC_API_VERSION 1U

typedef struct {
    uint32_t abi_version;
    uint32_t reserved;
    size_t adc_words;
    size_t adc_bytes;
    size_t words_per_packet;
    size_t packet_count;
    size_t samples_per_packet;
    size_t sample_count;
    size_t gate_words;
    uint64_t first_timestamp;
} rfsoc_capture_info_t;

uint32_t rfsoc_api_version(void);

/* Validate packet boundaries and MAGIC_WORDs without copying input data. */
int rfsoc_inspect_capture(
    const uint64_t *adc_words,
    size_t n_adc_words,
    const uint64_t *gate_words,
    size_t n_gate_words,
    size_t words_per_packet,
    rfsoc_capture_info_t *info
);

/* Run the existing processing pipeline directly on the PYNQ buffers. */
int rfsoc_process_capture(
    const uint64_t *adc_words,
    size_t n_adc_words,
    const uint64_t *gate_words,
    size_t n_gate_words,
    size_t words_per_packet,
    size_t fft_len,
    bool fft_enabled
);

int rfsoc_process_capture_ex(
    const uint64_t *adc_words, size_t n_adc_words,
    const uint64_t *gate_words, size_t n_gate_words,
    size_t words_per_packet, size_t fft_len, bool fft_enabled,
    rfsoc_processing_result_t *result
);

#endif
