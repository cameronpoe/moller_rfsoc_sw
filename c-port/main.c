#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rfsoc_types.h"
#include "io.h"
#include "iq_rot.h"
#include "integrator.h"

/**
 * 1) Two becnhamrks for adc_parser and reader 
 * 
 */

#define SAMP_FREQ 3072000000ULL
struct timespec t0, t1;
struct timespec t2, t3; 
struct timespec t4, t5; 
/**
 * @brief Reads an entire binary file as uint64_t words.
 *
 * @param path File path.
 * @param[out] words Newly allocated word buffer.
 * @param[out] n_words Number of uint64_t words read.
 *
 * @return 0 on success, negative value on failure.
 */
static int read_u64_file(
    const char *path,
    uint64_t **words,
    size_t *n_words
) {
    if (!path || !words || !n_words) {
        return -1;
    }

    *words = NULL;
    *n_words = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Could not open %s: %s\n", path, strerror(errno));
        return -2;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -3;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return -4;
    }

    rewind(fp);

    if ((size_t)file_size % sizeof(uint64_t) != 0) {
        fprintf(
            stderr,
            "File size is not divisible by 8 bytes: %s\n",
            path
        );
        fclose(fp);
        return -5;
    }

    size_t count = (size_t)file_size / sizeof(uint64_t);

    uint64_t *buffer = calloc(count, sizeof(uint64_t));
    if (!buffer && count != 0) {
        fclose(fp);
        return -6;
    }

    size_t n_read = fread(buffer, sizeof(uint64_t), count, fp);
    fclose(fp);

    if (n_read != count) {
        fprintf(
            stderr,
            "Expected %zu words from %s, read %zu\n",
            count,
            path,
            n_read
        );
        free(buffer);
        return -7;
    }

    *words = buffer;
    *n_words = count;

    return 0;
}

/**
 * @brief Removes zero padding from a gate-data buffer.
 */
static int remove_zero_gate_words(
    const uint64_t *raw,
    size_t n_raw,
    uint64_t **nonzero,
    size_t *n_nonzero
) {
    if (!raw || !nonzero || !n_nonzero) {
        return -1;
    }

    *nonzero = NULL;
    *n_nonzero = 0;

    size_t count = 0;

    for (size_t i = 0; i < n_raw; i++) {
        if (raw[i] != 0) {
            count++;
        }
    }

    uint64_t *filtered = calloc(count, sizeof(uint64_t));
    if (!filtered && count != 0) {
        return -2;
    }

    size_t j = 0;

    for (size_t i = 0; i < n_raw; i++) {
        if (raw[i] != 0) {
            filtered[j++] = raw[i];
        }
    }

    *nonzero = filtered;
    *n_nonzero = count;

    return 0;
}

/**
 * @brief Builds integration windows from parsed rising and falling edges.
 *
 * This reproduces the Python convention:
 *
 * rising[k] is paired with falling[k].
 *
 * Each resulting interval is half-open:
 *
 *     [rising_timestamp, falling_timestamp)
 */
static int build_gate_windows(
    const gate_event_t *events,
    size_t n_events,
    gate_window_t **windows,
    size_t *n_windows
) {
    if (!events || !windows || !n_windows) {
        return -1;
    }

    *windows = NULL;
    *n_windows = 0;

    size_t n_rising = 0;
    size_t n_falling = 0;

    for (size_t i = 0; i < n_events; i++) {
        if (events[i].edge) {
            n_falling++;
        } else {
            n_rising++;
        }
    }

    uint64_t *rising = calloc(n_rising, sizeof(uint64_t));
    uint64_t *falling = calloc(n_falling, sizeof(uint64_t));

    if ((!rising && n_rising != 0) ||
        (!falling && n_falling != 0)) {
        free(rising);
        free(falling);
        return -2;
    }

    size_t r = 0;
    size_t f = 0;

    for (size_t i = 0; i < n_events; i++) {
        if (events[i].edge) {
            falling[f++] = events[i].ts;
        } else {
            rising[r++] = events[i].ts;
        }
    }

    size_t max_pairs =
        n_rising < n_falling ? n_rising : n_falling;

    gate_window_t *result =
        calloc(max_pairs, sizeof(gate_window_t));

    if (!result && max_pairs != 0) {
        free(rising);
        free(falling);
        return -3;
    }

    size_t valid = 0;

    for (size_t i = 0; i < max_pairs; i++) {
        /*
         * Reject an invalid pair rather than allowing unsigned
         * subtraction or an empty interval later.
         */
        if (falling[i] <= rising[i]) {
            fprintf(
                stderr,
                "Skipping invalid gate pair %zu: "
                "rising=%" PRIu64 ", falling=%" PRIu64 "\n",
                i,
                rising[i],
                falling[i]
            );
            continue;
        }

        result[valid].start_ts = rising[i];
        result[valid].end_ts = falling[i];
        valid++;
    }

    free(rising);
    free(falling);

    *windows = result;
    *n_windows = valid;

    return 0;
}

int main(int argc, char **argv) {
    clock_gettime(CLOCK_MONOTONIC, &t4);
    /*
     * Usage:
     *
     * ./resolution_analysis ADC_FILE GATE_FILE OUTPUT_CSV \
     *                       WORDS_PER_PACKET FFT_LEN
     */
    if (argc != 6) {
        fprintf(
            stderr,
            "Usage:\n"
            "  %s ADC_FILE GATE_FILE OUTPUT_CSV "
            "WORDS_PER_PACKET FFT_LEN\n\n"
            "Example:\n"
            "  %s tests/20260625-09_data "
            "tests/20260625-09_data_gate "
            "results/20260625-09.csv "
            "124928 4096\n",
            argv[0],
            argv[0]
        );

        return EXIT_FAILURE;
    }

    const char *adc_path = argv[1];
    const char *gate_path = argv[2];
    const char *csv_path = argv[3];

    char *endptr = NULL;

    unsigned long long words_per_packet_input =
        strtoull(argv[4], &endptr, 10);

    if (*argv[4] == '\0' || *endptr != '\0') {
        fprintf(stderr, "Invalid WORDS_PER_PACKET: %s\n", argv[4]);
        return EXIT_FAILURE;
    }

    size_t words_per_packet =
        (size_t)words_per_packet_input;

    endptr = NULL;

    unsigned long long fft_len_input =
        strtoull(argv[5], &endptr, 10);

    if (*argv[5] == '\0' || *endptr != '\0') {
        fprintf(stderr, "Invalid FFT_LEN: %s\n", argv[5]);
        return EXIT_FAILURE;
    }

    size_t fft_len = (size_t)fft_len_input;

    if (words_per_packet < 5 ||
        (words_per_packet - 2) % 3 != 0) {
        fprintf(
            stderr,
            "Invalid words_per_packet=%zu\n",
            words_per_packet
        );
        return EXIT_FAILURE;
    }

    if (fft_len == 0 ||
        (fft_len & (fft_len - 1)) != 0) {
        fprintf(
            stderr,
            "FFT_LEN must be a nonzero power of two.\n"
        );
        return EXIT_FAILURE;
    }

    int status = 0;

    uint64_t *raw_adc = NULL;
    size_t n_adc_words = 0;

    uint64_t *raw_gate = NULL;
    size_t n_gate_words = 0;

    uint64_t *nonzero_gate = NULL;
    size_t n_nonzero_gate = 0;

    gate_event_t *gate_events = NULL;
    gate_window_t *windows = NULL;
    size_t n_windows = 0;

    adc_sample_t *samples = NULL;
    rotated_sample_t *dc_ch2 = NULL;
    rotated_sample_t *dc_ch3 = NULL;

    integration_result_t *int_ch2 = NULL;
    integration_result_t *int_ch3 = NULL;

    bool *valid_ch2 = NULL;
    bool *valid_ch3 = NULL;

    FILE *csv = NULL;

    /*
     * ------------------------------------------------------------
     * 1. Read ADC data.
     * ------------------------------------------------------------
     */

    clock_gettime(CLOCK_MONOTONIC, &t2);
    status = read_u64_file(
        adc_path,
        &raw_adc,
        &n_adc_words
    );

    if (status != 0) {
        fprintf(stderr, "Failed to read ADC file: %d\n", status);
        goto cleanup;
    }

    if (n_adc_words % words_per_packet != 0) {
        fprintf(
            stderr,
            "ADC file contains %zu words, which is not divisible "
            "by words_per_packet=%zu\n",
            n_adc_words,
            words_per_packet
        );
        status = -1;
        goto cleanup;
    }

    

    size_t n_dma_packets =
        n_adc_words / words_per_packet;

    size_t samples_per_packet =
        (words_per_packet - 2) / 3;

    size_t n_samples =
        n_dma_packets * samples_per_packet;

    printf("ADC words:            %zu\n", n_adc_words);
    printf("DMA packets:          %zu\n", n_dma_packets);
    printf("Samples per packet:   %zu\n", samples_per_packet);
    printf("Total ADC samples:    %zu\n", n_samples);

    samples = calloc(n_samples, sizeof(adc_sample_t));

    if (!samples) {
        status = -2;
        goto cleanup;
    }

    status = adc_packet_parser_v2(
        raw_adc,
        n_adc_words,
        words_per_packet,
        samples
    );

    if (status != 0) {
        fprintf(
            stderr,
            "adc_packet_parser_v2 failed: %d\n",
            status
        );
        goto cleanup;
    }

    clock_gettime(CLOCK_MONOTONIC, &t3);

	double sec1 = (double)(t3.tv_sec - t2.tv_sec) +
		     1e-9 * (double)(t3.tv_nsec - t2.tv_nsec);

	printf("parser time = %.9f sec\n", sec1);
    /*
     * ------------------------------------------------------------
     * 2. Process physical channels 2 and 3 to DC.
     * ------------------------------------------------------------
     */
    dc_ch2 = calloc(n_samples, sizeof(rotated_sample_t));
    dc_ch3 = calloc(n_samples, sizeof(rotated_sample_t));

    if (!dc_ch2 || !dc_ch3) {
        status = -3;
        goto cleanup;
    }

    size_t n_dc_ch2 = 0;
    size_t n_dc_ch3 = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    status = process_to_dc(
        samples,
        n_samples,
        dc_ch2,
        &n_dc_ch2,
        SAMP_FREQ,
        2,
        fft_len
    );

    clock_gettime(CLOCK_MONOTONIC, &t1);

	double sec = (double)(t1.tv_sec - t0.tv_sec) +
		     1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);

	printf("process_to_dc time = %.9f sec\n", sec);

    if (status != 0) {
        fprintf(
            stderr,
            "process_to_dc(channel=2) failed: %d\n",
            status
        );
        goto cleanup;
    }

    status = process_to_dc(
        samples,
        n_samples,
        dc_ch3,
        &n_dc_ch3,
        SAMP_FREQ,
        3,
        fft_len
    );

    if (status != 0) {
        fprintf(
            stderr,
            "process_to_dc(channel=3) failed: %d\n",
            status
        );
        goto cleanup;
    }

    if (n_dc_ch2 != n_dc_ch3) {
        fprintf(
            stderr,
            "Channel output-size mismatch: ch2=%zu, ch3=%zu\n",
            n_dc_ch2,
            n_dc_ch3
        );
        status = -4;
        goto cleanup;
    }

    size_t n_dc_samples = n_dc_ch2;

    if (n_dc_samples == 0) {
        fprintf(stderr, "process_to_dc produced no samples.\n");
        status = -5;
        goto cleanup;
    }

    printf("DC output samples:    %zu\n", n_dc_samples);
    printf("First DC timestamp:   %" PRIu64 "\n", dc_ch2[0].ts);
    printf(
        "Last DC timestamp:    %" PRIu64 "\n",
        dc_ch2[n_dc_samples - 1].ts
    );

    /*
     * ------------------------------------------------------------
     * 3. Read and decode gate data.
     * ------------------------------------------------------------
     */
    status = read_u64_file(
        gate_path,
        &raw_gate,
        &n_gate_words
    );

    if (status != 0) {
        fprintf(stderr, "Failed to read gate file: %d\n", status);
        goto cleanup;
    }

    status = remove_zero_gate_words(
        raw_gate,
        n_gate_words,
        &nonzero_gate,
        &n_nonzero_gate
    );

    if (status != 0) {
        fprintf(stderr, "Gate zero filtering failed: %d\n", status);
        goto cleanup;
    }

    if (n_nonzero_gate == 0) {
        fprintf(stderr, "Gate file contains no nonzero events.\n");
        status = -6;
        goto cleanup;
    }

    gate_events =
        calloc(n_nonzero_gate, sizeof(gate_event_t));

    if (!gate_events) {
        status = -7;
        goto cleanup;
    }

    status = gate_word_parser(
        nonzero_gate,
        n_nonzero_gate,
        gate_events
    );

    if (status != 0) {
        fprintf(stderr, "gate_word_parser failed: %d\n", status);
        goto cleanup;
    }

    status = build_gate_windows(
        gate_events,
        n_nonzero_gate,
        &windows,
        &n_windows
    );

    if (status != 0) {
        fprintf(stderr, "build_gate_windows failed: %d\n", status);
        goto cleanup;
    }

    if (n_windows < 2) {
        fprintf(
            stderr,
            "At least two gate windows are required; found %zu\n",
            n_windows
        );
        status = -8;
        goto cleanup;
    }

    printf("Raw gate words:       %zu\n", n_gate_words);
    printf("Nonzero gate events:  %zu\n", n_nonzero_gate);
    printf("Valid gate windows:   %zu\n", n_windows);

    /*
     * ------------------------------------------------------------
     * 4. Integrate each gate window.
     *
     * We call integrate_one_window separately so a gate outside the
     * available ADC timestamp range can be skipped without aborting
     * the entire run.
     * ------------------------------------------------------------
     */
    int_ch2 =
        calloc(n_windows, sizeof(integration_result_t));

    int_ch3 =
        calloc(n_windows, sizeof(integration_result_t));

    valid_ch2 = calloc(n_windows, sizeof(bool));
    valid_ch3 = calloc(n_windows, sizeof(bool));

    if (!int_ch2 || !int_ch3 || !valid_ch2 || !valid_ch3) {
        status = -9;
        goto cleanup;
    }

    
	status = integrate_windows_sorted(
    	dc_ch2,
    	n_dc_samples,
    	windows,
    	n_windows,
    	int_ch2
	);

	if (status != 0) {
    	fprintf(
        	stderr,
        	"integrate_windows_sorted(channel 2) failed: %d\n",
        	status
    	);
    	goto cleanup;
	}

	status = integrate_windows_sorted(
    	dc_ch3,
    	n_dc_samples,
    	windows,
    	n_windows,
    	int_ch3
	);

	if (status != 0) {
    	fprintf(
        	stderr,
        	"integrate_windows_sorted(channel 3) failed: %d\n",
        	status
    	);
    	goto cleanup;
	}

	size_t valid_window_count = 0;

	for (size_t w = 0; w < n_windows; w++) {
    	valid_ch2[w] = int_ch2[w].n_samples > 0;
    	valid_ch3[w] = int_ch3[w].n_samples > 0;

	    if (valid_ch2[w] && valid_ch3[w]) {
    	    valid_window_count++;
    	}
}	

    printf("Integrated windows:   %zu\n", valid_window_count);

    /*
     * ------------------------------------------------------------
     * 5. Pair adjacent windows and save RDF/DDF values.
     *
     * Python convention:
     *
     *     RDF = (even_window - odd_window)
     *           / (even_window + odd_window)
     *
     * No sqrt(2) normalization is applied to each CSV DDF value.
     * The plotting script later computes:
     *
     *     resolution = std(DDF) / sqrt(2)
     * ------------------------------------------------------------
     */
    csv = fopen(csv_path, "w");

    if (!csv) {
        fprintf(
            stderr,
            "Could not open output CSV %s: %s\n",
            csv_path,
            strerror(errno)
        );
        status = -10;
        goto cleanup;
    }

    fprintf(
        csv,
        "pair_index,"
        "window_even_index,window_odd_index,"
        "even_start_ts,even_end_ts,"
        "odd_start_ts,odd_end_ts,"
        "mean_ch2_even,mean_ch2_odd,"
        "mean_ch3_even,mean_ch3_odd,"
        "n_ch2_even,n_ch2_odd,"
        "n_ch3_even,n_ch3_odd,"
        "rdf_ch2,rdf_ch3,ddf\n"
    );

    size_t pairs_written = 0;
    size_t pairs_skipped = 0;

    for (size_t even = 0;
         even + 1 < n_windows;
         even += 2) {

        size_t odd = even + 1;

        if (!valid_ch2[even] ||
            !valid_ch2[odd] ||
            !valid_ch3[even] ||
            !valid_ch3[odd]) {
            pairs_skipped++;
            continue;
        }

        double ch2_even = int_ch2[even].mean;
        double ch2_odd = int_ch2[odd].mean;

        double ch3_even = int_ch3[even].mean;
        double ch3_odd = int_ch3[odd].mean;

        double denominator_ch2 =
            ch2_even + ch2_odd;

        double denominator_ch3 =
            ch3_even + ch3_odd;

        if (denominator_ch2 == 0.0 ||
            denominator_ch3 == 0.0) {
            pairs_skipped++;
            continue;
        }

        double rdf_ch2 =
            (ch2_even - ch2_odd) /
            denominator_ch2;

        double rdf_ch3 =
            (ch3_even - ch3_odd) /
            denominator_ch3;

        double ddf =
            rdf_ch2 - rdf_ch3;

        fprintf(
            csv,
            "%zu,%zu,%zu,"
            "%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ","
            "%.17g,%.17g,"
            "%.17g,%.17g,"
            "%zu,%zu,%zu,%zu,"
            "%.17g,%.17g,%.17g\n",
            pairs_written,
            even,
            odd,
            windows[even].start_ts,
            windows[even].end_ts,
            windows[odd].start_ts,
            windows[odd].end_ts,
            ch2_even,
            ch2_odd,
            ch3_even,
            ch3_odd,
            int_ch2[even].n_samples,
            int_ch2[odd].n_samples,
            int_ch3[even].n_samples,
            int_ch3[odd].n_samples,
            rdf_ch2,
            rdf_ch3,
            ddf
        );

        pairs_written++;
    }

    printf("CSV pairs written:    %zu\n", pairs_written);
    printf("CSV pairs skipped:    %zu\n", pairs_skipped);
    printf("Output CSV:           %s\n", csv_path);

    status = 0;

cleanup:
    if (csv) {
        fclose(csv);
    }

    free(raw_adc);
    free(raw_gate);
    free(nonzero_gate);

    free(gate_events);
    free(windows);

    free(samples);
    free(dc_ch2);
    free(dc_ch3);

    free(int_ch2);
    free(int_ch3);

    free(valid_ch2);
    free(valid_ch3);
    clock_gettime(CLOCK_MONOTONIC, &t5);
	double sec2 = (double)(t5.tv_sec - t4.tv_sec) +
		     1e-9 * (double)(t5.tv_nsec - t4.tv_nsec);
    printf("run time = %.9f sec\n", sec2);
    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}