#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "rfsoc_types.h"
#include "io.h"
#include "iq_rot.h"

#define WORDS_PER_PACKET 124928ULL
#define SAMP_FREQ 3072000000ULL

struct timespec t0, t1;

int dump_process_to_dc_csv(
	const char *raw_path,
	const char *out_csv,
	int channel,
	size_t fft_len,
	size_t max_print) {

	if (!raw_path || !out_csv)
		return -1;

	FILE *fp = fopen(raw_path, "rb");
	if (!fp) {
		perror("fopen raw_path");
		return -2;
	}

	uint64_t *raw_words = calloc(WORDS_PER_PACKET, sizeof(uint64_t));
	if (!raw_words) {
		fclose(fp);
		return -3;
	}

	size_t n_read =
		fread(raw_words, sizeof(uint64_t), WORDS_PER_PACKET, fp);
	fclose(fp);

	if (n_read != WORDS_PER_PACKET) {
		fprintf(stderr,
			"Expected %llu words, read %zu\n",
			(unsigned long long)WORDS_PER_PACKET,
			n_read);
		free(raw_words);
		return -4;
	}

	size_t n_samples = (WORDS_PER_PACKET - 2) / 3;

	adc_sample_t *samples = calloc(n_samples, sizeof(adc_sample_t));
	rotated_sample_t *dc = calloc(n_samples, sizeof(rotated_sample_t));

	if (!samples || !dc) {
		free(raw_words);
		free(samples);
		free(dc);
		return -5;
	}

	int status = adc_packet_parser_v2(
		raw_words, WORDS_PER_PACKET, WORDS_PER_PACKET, samples);

	if (status != 0) {
		fprintf(stderr, "adc_packet_parser_v2 failed: %d\n", status);
		free(raw_words);
		free(samples);
		free(dc);
		return -6;
	}

	size_t produced = 0;

	clock_gettime(CLOCK_MONOTONIC, &t0);

	status = process_to_dc(
		samples, n_samples, dc, &produced, SAMP_FREQ, channel, fft_len);

	clock_gettime(CLOCK_MONOTONIC, &t1);

	double sec = (double)(t1.tv_sec - t0.tv_sec) +
		     1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);

	printf("process_to_dc time = %.9f sec\n", sec);

	if (status != 0) {
		fprintf(stderr, "process_to_dc failed: %d\n", status);
		free(raw_words);
		free(samples);
		free(dc);
		return -7;
	}

	size_t n_out = n_samples;
	if (fft_len > 0) {
		n_out = n_samples - (n_samples % fft_len);
	}

	if (max_print > 0 && max_print < n_out) {
		n_out = max_print;
	}

	FILE *out = fopen(out_csv, "w");
	if (!out) {
		perror("fopen out_csv");
		free(raw_words);
		free(samples);
		free(dc);
		return -8;
	}

	fprintf(out, "ts,sig\n");

	for (size_t i = 0; i < n_out; i++) {
		fprintf(out,
			"%llu,%.17g\n",
			(unsigned long long)dc[i].ts,
			dc[i].sig);
	}

	fclose(out);
	free(raw_words);

	timestamped_iq_t *iq = calloc(n_samples, sizeof(timestamped_iq_t));
	int stat = get_iq_data_from_packets_ts(samples, n_samples, channel, iq);

	FILE *iq_out = fopen("tests/c_iq_before_dc.csv", "w");
	fprintf(iq_out, "real,imag\n");

	for (size_t i = 0; i < 1000; i++) {
		fprintf(iq_out,
			"%.17g,%.17g\n",
			creal(iq[i].sig),
			cimag(iq[i].sig));
	}

	fclose(iq_out);
	free(iq);
	free(samples);
	free(dc);
	return 0;
}
