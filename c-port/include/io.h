#ifndef IO_H    // Include guard: checks if this token is undefined

#define IO_H    // Defines the token to block subsequent inclusions

#include "rfsoc_types.h"

static inline adc_frame_t adc_frame_parser(uint64_t word1, uint64_t word2, uint64_t word3) {
    uint64_t tmp1 = __builtin_bswap64(word1);
    uint64_t tmp2 = __builtin_bswap64(word2);
    uint64_t tmp3 = __builtin_bswap64(word3);
    
    // uint8_t b0 = (tmp1 >> 56) & 0xFF;
    // uint8_t b1 = (tmp1 >> 48) & 0xFF;
    // uint8_t b2 = (tmp1 >> 40) & 0xFF;
    // uint8_t b3 = (tmp1 >> 32) & 0xFF;
    // uint8_t b4 = (tmp1 >> 24) & 0xFF;
    // uint8_t b5 = (tmp1 >> 16) & 0xFF;
    // uint8_t b6 = (tmp1 >> 8)  & 0xFF;
    // uint8_t b7 =  tmp1        & 0xFF;
    uint8_t b0 =  tmp1        & 0xFF;
    uint8_t b1 = (tmp1 >> 8)  & 0xFF;
    uint8_t b2 = (tmp1 >> 16) & 0xFF;
    uint8_t b3 = (tmp1 >> 24) & 0xFF;
    uint8_t b4 = (tmp1 >> 32) & 0xFF;
    uint8_t b5 = (tmp1 >> 40) & 0xFF;
    uint8_t b6 = (tmp1 >> 48) & 0xFF;
    uint8_t b7 = (tmp1 >> 56) & 0xFF;

    // uint8_t b0_s = (tmp2 >> 56) & 0xFF;
    // uint8_t b1_s = (tmp2 >> 48) & 0xFF;
    // uint8_t b2_s = (tmp2 >> 40) & 0xFF;
    // uint8_t b3_s = (tmp2 >> 32) & 0xFF;
    // uint8_t b4_s = (tmp2 >> 24) & 0xFF;
    // uint8_t b5_s = (tmp2 >> 16) & 0xFF;
    // uint8_t b6_s = (tmp2 >> 8)  & 0xFF;
    // uint8_t b7_s =  tmp2        & 0xFF;

    uint8_t b0_s =  tmp2        & 0xFF;
    uint8_t b1_s = (tmp2 >> 8)  & 0xFF;
    uint8_t b2_s = (tmp2 >> 16) & 0xFF;
    uint8_t b3_s = (tmp2 >> 24) & 0xFF;
    uint8_t b4_s = (tmp2 >> 32) & 0xFF;
    uint8_t b5_s = (tmp2 >> 40) & 0xFF;
    uint8_t b6_s = (tmp2 >> 48) & 0xFF;
    uint8_t b7_s = (tmp2 >> 56) & 0xFF;

    // uint8_t b0_t = (tmp3 >> 56) & 0xFF;
    // uint8_t b1_t = (tmp3 >> 48) & 0xFF;
    // uint8_t b2_t = (tmp3 >> 40) & 0xFF;
    // uint8_t b3_t = (tmp3 >> 32) & 0xFF;
    // uint8_t b4_t = (tmp3 >> 24) & 0xFF;
    // uint8_t b5_t = (tmp3 >> 16) & 0xFF;
    // uint8_t b6_t = (tmp3 >> 8)  & 0xFF;
    // uint8_t b7_t =  tmp3        & 0xFF;

    uint8_t b0_t =  tmp3        & 0xFF;
    uint8_t b1_t = (tmp3 >> 8)  & 0xFF;
    uint8_t b2_t = (tmp3 >> 16) & 0xFF;
    uint8_t b3_t = (tmp3 >> 24) & 0xFF;
    uint8_t b4_t = (tmp3 >> 32) & 0xFF;
    uint8_t b5_t = (tmp3 >> 40) & 0xFF;
    uint8_t b6_t = (tmp3 >> 48) & 0xFF;
    uint8_t b7_t = (tmp3 >> 56) & 0xFF;

    adc_frame_t output; 
    output.ch0 = make_adc24(b0,   b1,   b2);
    output.ch1 = make_adc24(b3,   b4,   b5);
    output.ch2 = make_adc24(b6,   b7,   b0_s);
    output.ch3 = make_adc24(b1_s, b2_s, b3_s);
    output.ch4 = make_adc24(b4_s, b5_s, b6_s);
    output.ch5 = make_adc24(b7_s, b0_t, b1_t);
    output.ch6 = make_adc24(b2_t, b3_t, b4_t);
    output.ch7 = make_adc24(b5_t, b6_t, b7_t);

    return output;

} 
static inline gate_event_t parse_single_gate_word(uint64_t word) {
    gate_event_t ev;
    ev.edge = (bool)(word >> 63);
    ev.ts = word & 0x7FFFFFFFFFFFFFFFULL;
    return ev;
}


int adc_packet_parser(const uint64_t* raw_data, size_t num_words, adc_sample_t* output); 

int adc_packet_parser_v2(const uint64_t* raw_data, size_t num_words, size_t words_per_dma_packet, adc_sample_t* output);

int gate_word_parser(const uint64_t* raw_data, size_t num_words, gate_event_t* output);


#endif // IO_H
