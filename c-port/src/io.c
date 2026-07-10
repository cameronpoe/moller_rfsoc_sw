#include "io.h"

int adc_packet_parser(const uint64_t *raw_data, size_t num_words, adc_sample_t *output) {
    if (!raw_data || !output ) return -1;
    if (num_words % 4 != 0) return -2;

    size_t n_packets = num_words / 4;
    

    for (size_t samp = 0; samp < n_packets; samp++) {
        size_t i = 4 * samp;

        uint64_t ts_word = raw_data[i];
        uint64_t word0   = raw_data[i + 1];
        uint64_t word1   = raw_data[i + 2];
        uint64_t word2   = raw_data[i + 3];

        output[samp].ts = ts_word & 0x7FFFFFFFFFFFFFFFULL;
        output[samp].data = adc_frame_parser(word0, word1, word2);
    }

    return 0;
}

int gate_word_parser(const uint64_t* raw_data, size_t num_words, gate_event_t* output) {
    if (!raw_data || !output) return -1; 

    for (size_t i = 0; i < num_words; i++) {
        gate_event_t event = parse_single_gate_word(raw_data[i]); 
        output[i].edge = event.edge;
        output[i].ts = event.ts;
    }

    return 0;
}

int adc_packet_parser_v2(const uint64_t* raw_data, size_t num_words, size_t words_per_dma_packet, adc_sample_t* output) {
    
    if (!raw_data || !output) return -1;
    if (words_per_dma_packet < 5) return -2;
    if ((words_per_dma_packet - 2) % 3 != 0) return -3;

    
    size_t leftover_words = num_words % words_per_dma_packet;
    if (leftover_words != 0) return -4;

    size_t num_packets = (num_words - leftover_words) / words_per_dma_packet;

    size_t out_idx = 0;

    for (size_t p = 0; p < num_packets; p++) {
        size_t base = p * words_per_dma_packet;

        if (raw_data[base] != MAGIC_WORD) return -5;

        uint64_t packet_ts = raw_data[base + 1] & 0x7FFFFFFFFFFFFFFFULL;

        size_t samples_per_packet = (words_per_dma_packet - 2) / 3;

        for (size_t s = 0; s < samples_per_packet; s++) {
            size_t word_idx = base + 2 + 3 * s;

            uint64_t word0 = raw_data[word_idx + 0];
            uint64_t word1 = raw_data[word_idx + 1];
            uint64_t word2 = raw_data[word_idx + 2];

            output[out_idx].ts = packet_ts + s * TIME_INC;
            output[out_idx].data = adc_frame_parser(word0, word1, word2);

            out_idx++;
        }
    }

    return 0;
}
