// test on rading one dma packer 
#include "test1.h"
#include "rfsoc_types.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "rfsoc_types.h"
#include "io.h"
#include "iq_rot.h"

int dump_first_samples_csv(const char *raw_path, size_t words_per_dma_packet, size_t n_print) {
    if (!raw_path) return -1;
    if (words_per_dma_packet == 0) return -2;

    FILE *fp = fopen(raw_path, "rb");
    if (!fp) {
        perror("fopen");
        return -3;
    }

    uint64_t *raw_words = calloc(words_per_dma_packet, sizeof(uint64_t));
    if (!raw_words) {
        fclose(fp);
        return -4;
    }

    size_t n_read = fread(
        raw_words,
        sizeof(uint64_t),
        words_per_dma_packet,
        fp
    );

    fclose(fp);

    if (n_read != words_per_dma_packet) {
        free(raw_words);
        fprintf(stderr, "Expected %zu words, read %zu words\n",
                words_per_dma_packet, n_read);
        return -5;
    }

    size_t samples_per_packet = (words_per_dma_packet - 2) / 3;

    adc_sample_t *samples =
        calloc(samples_per_packet, sizeof(adc_sample_t));

    if (!samples) {
        free(raw_words);
        return -6;
    }

    int status = adc_packet_parser_v2(
        raw_words,
        words_per_dma_packet,
        words_per_dma_packet,
        samples
    );

    if (status != 0) {
        free(raw_words);
        free(samples);
        fprintf(stderr, "adc_packet_parser_v2 failed: %d\n", status);
        return -7;
    }

    if (n_print > samples_per_packet) {
        n_print = samples_per_packet;
    }

    for (size_t i = 0; i < n_print; i++) {
        printf("%d,%d,%d,%d,%d,%d,%d,%d\n",
            adc24_to_int32(samples[i].data.ch0),
            adc24_to_int32(samples[i].data.ch1),
            adc24_to_int32(samples[i].data.ch2),
            adc24_to_int32(samples[i].data.ch3),
            adc24_to_int32(samples[i].data.ch4),
            adc24_to_int32(samples[i].data.ch5),
            adc24_to_int32(samples[i].data.ch6),
            adc24_to_int32(samples[i].data.ch7)
        );
    }

    free(raw_words);
    free(samples);

    return 0;
}
