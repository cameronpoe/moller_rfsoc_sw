#ifndef IO_H // Include guard: checks if this token is undefined

#define IO_H // Defines the token to block subsequent inclusions

#include "rfsoc_types.h"

/**
 * @brief Parses three raw 64-bit words into one eight-channel ADC frame.
 *
 * The three input words contain 192 bits of packed ADC data:
 * eight channels, each represented by one signed 24-bit value.
 *
 * Each 64-bit word is byte-swapped before its individual bytes are
 * extracted. The resulting 24 bytes are divided consecutively into
 * eight groups of three bytes:
 *
 * - channel 0: bytes 0-2;
 * - channel 1: bytes 3-5;
 * - channel 2: bytes 6-8;
 * - channel 3: bytes 9-11;
 * - channel 4: bytes 12-14;
 * - channel 5: bytes 15-17;
 * - channel 6: bytes 18-20;
 * - channel 7: bytes 21-23.
 *
 * @param[in] word1 First packed 64-bit ADC data word.
 * @param[in] word2 Second packed 64-bit ADC data word.
 * @param[in] word3 Third packed 64-bit ADC data word
 * @return Parsed ADC frame containing eight 24-bit channel values.
 *
 * @note The function assumes that b0 is the most-significant byte
 * and b2 is the least-significant byte in each adc24_t value.
 */
// static inline adc_frame_t adc_frame_parser(
// 	uint64_t word1,
// 	uint64_t word2,
// 	uint64_t word3) {
// 	/* Reverse the byte order of each incoming 64-bit word to match
// 	 * the byte ordering used by the ADC data format.
// 	 */
// 	uint64_t tmp1 = __builtin_bswap64(word1);
// 	uint64_t tmp2 = __builtin_bswap64(word2);
// 	uint64_t tmp3 = __builtin_bswap64(word3);

// 	/* Extract the eight individual bytes from the first data word,
// 	 * beginning with its least-significant byte after byte swapping.
// 	 */
// 	uint8_t b0 = tmp1 & 0xFF;
// 	uint8_t b1 = (tmp1 >> 8) & 0xFF;
// 	uint8_t b2 = (tmp1 >> 16) & 0xFF;
// 	uint8_t b3 = (tmp1 >> 24) & 0xFF;
// 	uint8_t b4 = (tmp1 >> 32) & 0xFF;
// 	uint8_t b5 = (tmp1 >> 40) & 0xFF;
// 	uint8_t b6 = (tmp1 >> 48) & 0xFF;
// 	uint8_t b7 = (tmp1 >> 56) & 0xFF;

// 	/* Extract the eight individual bytes from the second data word. */
// 	uint8_t b0_s = tmp2 & 0xFF;
// 	uint8_t b1_s = (tmp2 >> 8) & 0xFF;
// 	uint8_t b2_s = (tmp2 >> 16) & 0xFF;
// 	uint8_t b3_s = (tmp2 >> 24) & 0xFF;
// 	uint8_t b4_s = (tmp2 >> 32) & 0xFF;
// 	uint8_t b5_s = (tmp2 >> 40) & 0xFF;
// 	uint8_t b6_s = (tmp2 >> 48) & 0xFF;
// 	uint8_t b7_s = (tmp2 >> 56) & 0xFF;

// 	/* Extract the eight individual bytes from the third data word. */
// 	uint8_t b0_t = tmp3 & 0xFF;
// 	uint8_t b1_t = (tmp3 >> 8) & 0xFF;
// 	uint8_t b2_t = (tmp3 >> 16) & 0xFF;
// 	uint8_t b3_t = (tmp3 >> 24) & 0xFF;
// 	uint8_t b4_t = (tmp3 >> 32) & 0xFF;
// 	uint8_t b5_t = (tmp3 >> 40) & 0xFF;
// 	uint8_t b6_t = (tmp3 >> 48) & 0xFF;
// 	uint8_t b7_t = (tmp3 >> 56) & 0xFF;

// 	/* Reassemble the 24 extracted bytes into eight consecutive
// 	 * three-byte ADC channel values.
// 	 */
// 	adc_frame_t output;

// 	output.ch0 = make_adc24(b0, b1, b2);
// 	output.ch1 = make_adc24(b3, b4, b5);
// 	output.ch2 = make_adc24(b6, b7, b0_s);
// 	output.ch3 = make_adc24(b1_s, b2_s, b3_s);
// 	output.ch4 = make_adc24(b4_s, b5_s, b6_s);
// 	output.ch5 = make_adc24(b7_s, b0_t, b1_t);
// 	output.ch6 = make_adc24(b2_t, b3_t, b4_t);
// 	output.ch7 = make_adc24(b5_t, b6_t, b7_t);

// 	return output;
// }

static inline adc_frame_t adc_frame_parser(
    uint64_t word1,
    uint64_t word2,
    uint64_t word3)
{
    adc_frame_t output;

    output.ch0 = make_adc24(
        word1 >> 56,
        word1 >> 48,
        word1 >> 40);

    output.ch1 = make_adc24(
        word1 >> 32,
        word1 >> 24,
        word1 >> 16);

    output.ch2 = make_adc24(
        word1 >> 8,
        word1,
        word2 >> 56);

    output.ch3 = make_adc24(
        word2 >> 48,
        word2 >> 40,
        word2 >> 32);

    output.ch4 = make_adc24(
        word2 >> 24,
        word2 >> 16,
        word2 >> 8);

    output.ch5 = make_adc24(
        word2,
        word3 >> 56,
        word3 >> 48);

    output.ch6 = make_adc24(
        word3 >> 40,
        word3 >> 32,
        word3 >> 24);

    output.ch7 = make_adc24(
        word3 >> 16,
        word3 >> 8,
        word3);

    return output;
}

/**
 * @brief Parses one raw 64-bit gate-event word.
 *
 * The most-significant bit stores the gate-event (edge) type, while the
 * remaining 63 bits store the gate timestamp:
 *
 * - bit 63: edge type;
 * - bits 62:0: timestamp.
 *
 * In the current representation, false corresponds to a rising edge
 * and true corresponds to a falling edge.
 * @param[in] word Raw 64-bit gate-event word.
 * @return Parsed gate event containing the edge type and timestamp.
 */

static inline gate_event_t parse_single_gate_word(
	uint64_t word) {
	gate_event_t ev;

	/* Extract the edge type from the most-significant bit. */
	ev.edge = (bool)(word >> 63);

	/* Clear the most-significant bit and retain the 63-bit timestamp. */
	ev.ts = word & 0x7FFFFFFFFFFFFFFFULL;
	return ev;
}

/**
 * @brief Parses timestamped ADC samples from the original packet format.
 *
 * The input is interpreted as a sequence of four-word records:
 * - word 0: timestamp;
 * - words 1-3: eight packed 24-bit ADC values.
 *
 * The most-significant bit of the timestamp word is discarded.
 * Every four input words produce one adc_sample_t output element.
 *
 * @param[in] raw_data Array of raw 64-bit input words.
 * @param[in] num_words Number of elements in raw_data.
 * @param[out] output Parsed ADC sample array.
 *
 * @pre output must contain space for at least num_words / 4 elements.
 *
 * @retval 0 All records were parsed successfully.
 * @retval -1 raw_data or output is NULL.
 * @retval -2 num_words is not divisible by four.
 */
int adc_packet_parser(
	const uint64_t *raw_data,
	size_t num_words,
	adc_sample_t *output);

/**
 * @brief Parses timestamped ADC samples from DMA packets.
 *
 * Each DMA packet is expected to contain:
 * - one magic word;
 * - one packet timestamp;
 * - one or more groups of three packed ADC data words.
 *
 * Each three-word data group produces one adc_sample_t element.
 * The timestamp of sample s is reconstructed as:
 *
 * \f[
 * t_s = t_{\mathrm{packet}} + s \cdot TIME\_INC.
 * \f]
 *
 * @param[in] raw_data Array of raw 64-bit DMA packet words.
 * @param[in] num_words Number of elements in raw_data.
 * @param words_per_dma_packet
 * @param[in] words_per_dma_packet Number of 64-bit words in each DMA packet.
 * @param[out] output Parsed ADC sample array.
 *
 * @pre output must contain space for at least
 * num_words / words_per_dma_packet multiplied by
 * (words_per_dma_packet - 2) / 3 elements.
 *
 * @retval 0 All DMA packets were parsed successfully.
 * @retval -1 raw_data or output is NULL.
 * @retval -2 words_per_dma_packet is smaller than five.
 * @retval -3 The packet payload cannot be divided into three-word samples.
 * @retval -4 num_words does not contain an integer number of DMA packets.
 * @retval -5 A packet does not begin with MAGIC_WORD.
 */
int adc_packet_parser_v2(
	const uint64_t *raw_data,
	size_t num_words,
	size_t words_per_dma_packet,
	adc_sample_t *output);


int adc_packet_parser_v3_beta(
    const uint64_t *raw_data, 
    size_t num_words, 
    size_t words_per_dma_packet, 
    adc_sample_test_t *output
);
/**
 * @brief Parses an array of raw gate-event words.
 *
 * Converts every input word into one gate_event_t containing its event (edge)
 * type and 63-bit timestamp.
 *
 * @param[in] raw_data Array of raw 64-bit gate-event words.
 * @param[in] num_words Number of elements in raw_data.
 * @param[out] output Parsed gate-event array.
 *
 * @pre output must contain space for at least num_words elements.
 *
 * @retval 0 All gate words were parsed successfully.
 * @retval -1 raw_data or output is NULL.
 */
int gate_word_parser(
	const uint64_t *raw_data,
	size_t num_words,
	gate_event_t *output);

#endif // IO_H
