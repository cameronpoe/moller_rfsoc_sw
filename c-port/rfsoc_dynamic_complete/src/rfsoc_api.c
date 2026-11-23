#include "rfsoc_api.h"

#include "rfsoc_processing.h"
#include "rfsoc_types.h"

#include <limits.h>
#include <string.h>

uint32_t rfsoc_api_version(void)
{
    return RFSOC_API_VERSION;
}

int rfsoc_inspect_capture(
    const uint64_t *adc_words,
    size_t n_adc_words,
    const uint64_t *gate_words,
    size_t n_gate_words,
    size_t words_per_packet,
    rfsoc_capture_info_t *info)
{
    if (!adc_words || !gate_words || !info) {
        return -1;
    }
    if (words_per_packet < 5 || (words_per_packet - 2) % 3 != 0) {
        return -2;
    }
    if (n_adc_words == 0 || n_adc_words % words_per_packet != 0) {
        return -3;
    }
    if (n_adc_words > SIZE_MAX / sizeof(*adc_words)) {
        return -4;
    }

    const size_t packet_count = n_adc_words / words_per_packet;
    const size_t samples_per_packet = (words_per_packet - 2) / 3;
    if (packet_count > SIZE_MAX / samples_per_packet) {
        return -4;
    }

    for (size_t packet = 0; packet < packet_count; ++packet) {
        if (adc_words[packet * words_per_packet] != MAGIC_WORD) {
            return -5;
        }
    }

    memset(info, 0, sizeof(*info));
    info->abi_version = RFSOC_API_VERSION;
    info->adc_words = n_adc_words;
    info->adc_bytes = n_adc_words * sizeof(*adc_words);
    info->words_per_packet = words_per_packet;
    info->packet_count = packet_count;
    info->samples_per_packet = samples_per_packet;
    info->sample_count = packet_count * samples_per_packet;
    info->gate_words = n_gate_words;
    info->first_timestamp = adc_words[1] & UINT64_C(0x7FFFFFFFFFFFFFFF);
    return 0;
}

int rfsoc_process_capture(
    const uint64_t *adc_words,
    size_t n_adc_words,
    const uint64_t *gate_words,
    size_t n_gate_words,
    size_t words_per_packet,
    size_t fft_len,
    bool fft_enabled)
{
    rfsoc_capture_info_t info;
    const int inspect_status = rfsoc_inspect_capture(
        adc_words,
        n_adc_words,
        gate_words,
        n_gate_words,
        words_per_packet,
        &info
    );
    if (inspect_status != 0) {
        return inspect_status;
    }

    return process_dma_buffer(
        adc_words,
        n_adc_words,
        gate_words,
        n_gate_words,
        words_per_packet,
        fft_len,
        fft_enabled
    );
}

int rfsoc_process_capture_ex(
    const uint64_t *adc_words, size_t n_adc_words,
    const uint64_t *gate_words, size_t n_gate_words,
    size_t words_per_packet, size_t fft_len, bool fft_enabled,
    rfsoc_processing_result_t *result)
{
    rfsoc_capture_info_t info;
    const int rc = rfsoc_inspect_capture(adc_words, n_adc_words, gate_words,
                                         n_gate_words, words_per_packet, &info);
    if (rc != 0) return rc;
    return process_dma_buffer_ex(adc_words, n_adc_words, gate_words, n_gate_words,
                                 words_per_packet, fft_len, fft_enabled, result);
}
