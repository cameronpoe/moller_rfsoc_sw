#include "io.h"

int adc_packet_parser(
	const uint64_t *raw_data,
	size_t num_words,
	adc_sample_t *output) {
	/* Validate the input and output array pointers. */
	if (!raw_data || !output)
		return -1;
	/* Each sample in the original format consists of exactly
	 * one timestamp word followed by three ADC data words.
	 */
	if (num_words % 4 != 0)
		return -2;

	size_t n_packets = num_words / 4;

	/* Parse each four-word sample record independently. */
	for (size_t samp = 0; samp < n_packets; samp++) {
		size_t i = 4 * samp;

		/* Read the timestamp and three packed ADC data words. */
		uint64_t ts_word = raw_data[i];
		uint64_t word0 = raw_data[i + 1];
		uint64_t word1 = raw_data[i + 2];
		uint64_t word2 = raw_data[i + 3];

		/* Remove the timestamp word's most-significant control bit. */
		output[samp].ts = ts_word & 0x7FFFFFFFFFFFFFFFULL;

		/* Decode the three ADC words into eight 24-bit values. */
		output[samp].data = adc_frame_parser(word0, word1, word2);
	}

	return 0;
}

int gate_word_parser(
	const uint64_t *raw_data,
	size_t num_words,
	gate_event_t *output) {
	/* Validate the input and output array pointers. */
	if (!raw_data || !output)
		return -1;

	/* Parse every raw word into one timestamped gate event. */
	for (size_t i = 0; i < num_words; i++) {
		gate_event_t event = parse_single_gate_word(raw_data[i]);
		output[i].edge = event.edge;
		output[i].ts = event.ts;
	}

	return 0;
}

int adc_packet_parser_v2(
	const uint64_t *raw_data,
	size_t num_words,
	size_t words_per_dma_packet,
	adc_sample_t *output) {
	/* Validate the input and output array pointers. */
	if (!raw_data || !output)
		return -1;

	/* A valid packet requires one magic word, one timestamp word,
	 * and at least one group of three ADC data words.
	 */
	if (words_per_dma_packet < 5)
		return -2;

	/* After removing the two header words, the packet payload must
	 * consist entirely of three-word ADC samples.
	 */
	if ((words_per_dma_packet - 2) % 3 != 0)
		return -3;

	/* Reject an input buffer that ends with an incomplete DMA packet. */
	size_t leftover_words = num_words % words_per_dma_packet;
	if (leftover_words != 0)
		return -4;

	size_t num_packets =
		(num_words - leftover_words) / words_per_dma_packet;

	size_t out_idx = 0;

	/* Process each complete DMA packet in the input buffer. */
	for (size_t p = 0; p < num_packets; p++) {
		size_t base = p * words_per_dma_packet;

		/* Verify the synchronization word at the start of the packet.
		 */
		if (raw_data[base] != MAGIC_WORD)
			return -5;

		/* Extract the packet timestamp while clearing its
		 * most-significant control bit.
		 */
		uint64_t packet_ts = raw_data[base + 1] & 0x7FFFFFFFFFFFFFFFULL;

		/* Decode every three-word ADC sample in the packet payload. */
		size_t samples_per_packet = (words_per_dma_packet - 2) / 3;

		for (size_t s = 0; s < samples_per_packet; s++) {
			size_t word_idx = base + 2 + 3 * s;

			uint64_t word0 = raw_data[word_idx + 0];
			uint64_t word1 = raw_data[word_idx + 1];
			uint64_t word2 = raw_data[word_idx + 2];

			/* Reconstruct the individual sample timestamp from
			 * the packet timestamp and fixed sample interval.
			 */
			output[out_idx].ts = packet_ts + s * TIME_INC;

			/* Decode the packed ADC words into eight channels. */
			output[out_idx].data =
				adc_frame_parser(word0, word1, word2);

			out_idx++;
		}
	}

	return 0;
}

int adc_packet_parser_v3_beta(
	const uint64_t *raw_data,
	size_t num_words,
	size_t words_per_dma_packet,
	adc_sample_test_t *output) {

	if (!raw_data || !output)
		return -1;

	/* A valid packet requires one magic word, one timestamp word,
	 * and at least one group of three ADC data words.
	 */
	if (words_per_dma_packet < 5)
		return -2;

	/* After removing the two header words, the packet payload must
	 * consist entirely of three-word ADC samples.
	 */
	if ((words_per_dma_packet - 2) % 3 != 0)
		return -3;

	/* Reject an input buffer that ends with an incomplete DMA packet. */
	size_t leftover_words = num_words % words_per_dma_packet;
	if (leftover_words != 0)
		return -4;

	size_t num_packets =
		(num_words - leftover_words) / words_per_dma_packet;

	size_t out_idx = 0;

	/* Process each complete DMA packet in the input buffer. */
	for (size_t p = 0; p < num_packets; p++) {
		size_t base = p * words_per_dma_packet;

		/* Verify the synchronization word at the start of the packet.
		 */
		if (raw_data[base] != MAGIC_WORD)
			return -5;

		/* Extract the packet timestamp while clearing its
		 * most-significant control bit.
		 */
		uint64_t packet_ts = raw_data[base + 1] & 0x7FFFFFFFFFFFFFFFULL;

		/* Decode every three-word ADC sample in the packet payload. */
		size_t samples_per_packet = (words_per_dma_packet - 2) / 3;

		for (size_t s = 0; s < samples_per_packet; s++) {
			size_t word_idx = base + 2 + 3 * s;

			uint64_t word0 = raw_data[word_idx + 0];
			uint64_t word1 = raw_data[word_idx + 1];
			uint64_t word2 = raw_data[word_idx + 2];

			/* Reconstruct the individual sample timestamp from
			 * the packet timestamp and fixed sample interval.
			 */
			// output[out_idx].ts = packet_ts + s * TIME_INC;

			/* Decode the packed ADC words into eight channels. */
			output[out_idx].data =
				adc_frame_parser(word0, word1, word2);

			out_idx++;
		}
	}

	return 0;
}


int adc_packet_parser_v4_beta(
        const uint64_t *raw_data,
        size_t num_words,
        size_t words_per_dma_packet,
        adc_sample_test_t *output)
{
    /*
     * output оставлен в сигнатуре, чтобы функция имела те же аргументы,
     * что и настоящий parser. В этом диагностическом тесте мы намеренно
     * ничего в output не записываем.
     */
    (void)output;

    if (raw_data == NULL)
        return -1;

    /*
     * DMA packet:
     *
     * word 0: MAGIC_WORD
     * word 1: timestamp
     * word 2...: ADC payload
     *
     * Один ADC frame занимает три uint64_t.
     */
    if (words_per_dma_packet < 5)
        return -2;

    if ((words_per_dma_packet - 2) % 3 != 0)
        return -3;

    if (num_words % words_per_dma_packet != 0)
        return -4;

    const size_t num_packets =
        num_words / words_per_dma_packet;

    const size_t samples_per_packet =
        (words_per_dma_packet - 2) / 3;

    /*
     * Независимые аккумуляторы уменьшают последовательную зависимость
     * между итерациями и позволяют процессору обрабатывать несколько
     * загруженных слов параллельно.
     */
    uint64_t checksum0 = 0;
    uint64_t checksum1 = 0;
    uint64_t checksum2 = 0;
    uint64_t checksum3 = 0;

    for (size_t p = 0; p < num_packets; p++) {
        const size_t base = p * words_per_dma_packet;

        if (raw_data[base] != MAGIC_WORD)
            return -5;

        const size_t payload_begin = base + 2;
        const size_t payload_end =
            payload_begin + 3 * samples_per_packet;

        size_t i = payload_begin;

        /*
         * Читаем по четыре слова за итерацию.
         */
        for (; i + 3 < payload_end; i += 4) {
            checksum0 ^= raw_data[i + 0];
            checksum1 ^= raw_data[i + 1];
            checksum2 ^= raw_data[i + 2];
            checksum3 ^= raw_data[i + 3];
        }

        /*
         * Обрабатываем остаток: от нуля до трёх слов.
         */
        for (; i < payload_end; i++) {
            checksum0 ^= raw_data[i];
        }
    }

    const uint64_t checksum =
        checksum0 ^ checksum1 ^ checksum2 ^ checksum3;

    fprintf(stderr,
            "v4_beta diagnostic:\n"
            "  num_words:          %zu\n"
            "  num_packets:        %zu\n"
            "  samples_per_packet: %zu\n"
            "  total_samples:      %zu\n"
            "  input_size:         %.3f GiB\n"
            "  checksum:           %" PRIu64 "\n",
            num_words,
            num_packets,
            samples_per_packet,
            num_packets * samples_per_packet,
            (double)(num_words * sizeof(*raw_data)) /
                (1024.0 * 1024.0 * 1024.0),
            checksum);

    return 0;
}