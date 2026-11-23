#ifndef RFSOC_PROCESSING_H
#define RFSOC_PROCESSING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSOC_PROCESSING_RESULT_VERSION 1U
#define RFSOC_SAMPLE_FREQUENCY UINT64_C(3072000000)

typedef struct {
    uint32_t abi_version;
    uint32_t reserved;
    size_t packet_count;
    size_t sample_count;
    size_t gate_event_count;
    size_t window_count;
    size_t dc_samples_ch2;
    size_t dc_samples_ch3;
    size_t valid_pair_count;
    double rdf2_resolution_ppm;
    double rdf3_resolution_ppm;
    double ddf_resolution_ppm;
    double allocation_seconds;
    double parser_seconds;
    double gate_seconds;
    double dc_seconds;
    double integration_seconds;
    double rdf_seconds;
    double cleanup_seconds;
    double total_seconds;
} rfsoc_processing_result_t;

int process_dma_buffer_ex(const uint64_t *, size_t, const uint64_t *, size_t,
                          size_t, size_t, bool, rfsoc_processing_result_t *);
int process_dma_buffer(const uint64_t *, size_t, const uint64_t *, size_t,
                       size_t, size_t, bool);

#endif
