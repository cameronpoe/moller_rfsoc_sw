#include "rfsoc_processing.h"

// int process_dma_buffer(
//     const uint64_t* adc_words,
//     size_t n_adc_words,
//     const uint64_t* gate_words,
//     size_t n_gate_words,
//     size_t n_words_per_packet,
//     size_t fft_len,
//     bool fft_enabled
// ) {
//     if (!adc_words || !gate_words) return -1;
//     if (n_words_per_packet == 0 || n_adc_words == 0) return -2;

//     adc_sample_t *samples = NULL;
//     gate_event_t *gate_events = NULL;
//     gate_window_t *windows = NULL;
//     uint64_t *nonzero_gate_events = NULL;
//     rotated_sample_t *dc_ch2 = NULL;
//     rotated_sample_t *dc_ch3 = NULL;
//     integration_result_t *int_ch2 = NULL;
//     integration_result_t *int_ch3 = NULL;

//     size_t n_samples = 0;
//     size_t n_gate_events = 0;
//     size_t n_windows = 0;
//     size_t window_ind = 0;
//     int status = -1;
//     int thread_stat = -1;
//     int thread2_join_stat = -1;
//     int thread3_join_stat = -1;

//     size_t samples_per_packet = (n_words_per_packet - 2) / 3 ;
//     if (samples_per_packet )
//     samples = calloc(samples_per_packet, sizeof(adc_sample_t));
//     if (!samples) {
//         goto cleanup;
//     }

//     status = adc_packet_parser_v2(adc_words, n_adc_words, n_words_per_packet,
//     samples); if (status != 0) {
//         fprintf(stderr,
//                 "Failed to parse ADC data \n");
//         goto cleanup;
//     }

//     status = gate_word_parser(gate_words, n_gate_words, gate_events);
//     if (status != 0) {
//         fprintf(stderr,
//                 "Failed to parse gate data \n");
//         goto cleanup;
//     }

//     gate_events = calloc(n_gate_words, sizeof(*gate_events));
//     if (!gate_events) {
//         goto cleanup;
//     }

//     status = zero_padding(gate_words, n_gate_words, &gate_events,
//     &n_gate_events); if (status != 0) {
//         fprintf(stderr,
//                 "Failed to zero padding of gate dat \n");
//         goto cleanup;
//     }

//     status = build_gate_windows(gate_events, n_gate_events, &windows,
//     &n_windows); if (status != 0) {
//         fprintf(stderr,
//         "Failed to build gate windows \n");
//         goto cleanup;
//     }

//     pthread_t thread2;
//     pthread_t thread3;

//     dc_job_t job2 = {
//         .samples = samples,
//         .n_samples = n_samples,
//         .channel = 2,
//         .fft_enabled = fft_enabled,
//         .fft_len = fft_len,
//         .n_out = 0,
//         .out = dc_ch2,
//         .status = -1
//     };

//     dc_job_t job3 = {
//         .samples = samples,
//         .n_samples = n_samples,
//         .channel = 3,
//         .fft_enabled = fft_enabled,
//         .fft_len = fft_len,
//         .n_out = 0,
//         .out = dc_ch3,
//         .status = -1
//     };

//     thread_stat = pthread_create(&thread2, NULL, dc_worker, &job2);
//     if (thread_stat != 0) {
//         fprintf(stderr,
//                 "pthread_create(thread2) %s\n", strerror(thread_stat));
//         goto cleanup;
//     }

//     thread_stat = pthread_create(&thread3, NULL, dc_worker, &job3);
//     if (thread_stat != 0) {
//         fprintf(stderr,
//                 "pthread_create(thread2) %s\n", strerror(thread_stat));
//         goto cleanup;
//     }

//     thread2_join_stat = pthread_join(thread2, NULL);
//     if (thread2_join_stat != 0) {
//         fprintf(stderr,
//                 "pthread_join(thread2) %s\n", strerror(thread2_join_stat));
//         goto cleanup;
//     }

//     thread3_join_stat = pthread_join(thread3, NULL);
//     if (thread3_join_stat != 0) {
//         fprintf(stderr,
//                 "pthread_join(thread3) %s\n", strerror(thread3_join_stat));
//         goto cleanup;
//     }

//     status = accumulate_block(
//             dc_ch2, dc_ch3, job2.n_out,
//             windows, n_windows, int_ch2, int_ch3, &window_ind);
//     if (status != 0) {
//         fprintf(stderr,
//             "Integration has failed \n");
//         goto cleanup;
//     }

// cleanup:
//     free(samples);
//     free(gate_events);
//     free(windows);
//     free(nonzero_gate_events);
//     free(dc_ch2);
//     free(dc_ch3);
//     free(int_ch2);
//     free(int_ch3);
//     return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
// }

static double elapsed_seconds(
	const struct timespec *start,
	const struct timespec *end) {
	return (double)(end->tv_sec - start->tv_sec) +
	       1e-9 * (double)(end->tv_nsec - start->tv_nsec);
}

int process_dma_buffer(
	const uint64_t *adc_words,
	size_t n_adc_words,
	const uint64_t *gate_words,
	size_t n_gate_words,
	size_t n_words_per_packet,
	size_t fft_len,
	bool fft_enabled) {
	if (!adc_words || !gate_words) {
		return -1;
	}

	/*
	 * Packet must contain:
	 *     2 header words
	 *     groups of 3 ADC words
	 */
	if (n_words_per_packet < 2 || n_adc_words == 0) {
		return -2;
	}

	adc_sample_t *samples = NULL;
	uint64_t *nonzero_gate_words = NULL;
	gate_event_t *gate_events = NULL;
	gate_window_t *windows = NULL;
	double *rdf2_ppm = NULL;
	double *rdf3_ppm = NULL;
	double *ddf_ppm = NULL;

	rotated_sample_t *dc_ch2 = NULL;
	rotated_sample_t *dc_ch3 = NULL;

	integration_result_t *int_ch2 = NULL;
	integration_result_t *int_ch3 = NULL;

	size_t n_samples = 0;
	size_t n_nonzero_gate_words = 0;
	size_t n_gate_events = 0;
	size_t n_windows = 0;

	pthread_t thread2;
	pthread_t thread3;

	bool thread2_started = false;
	bool thread3_started = false;

	int status = -1;

	struct timespec dc_start;
    struct timespec dc_end;

	/*
	 * Calculate how many complete DMA packets are present.
	 */
	const size_t n_complete_packets = n_adc_words / n_words_per_packet;

	if (n_complete_packets == 0) {
		fprintf(stderr, "No complete DMA packets were provided.\n");
		status = -2;
		goto cleanup;
	}

	const size_t complete_adc_words =
		n_complete_packets * n_words_per_packet;

	if (complete_adc_words != n_adc_words) {
		fprintf(stderr,
			"Ignoring %zu ADC words after the last complete "
			"packet.\n",
			n_adc_words - complete_adc_words);
	}

	/*
	 * Every three data words produce one adc_sample_t.
	 */
	const size_t samples_per_packet = (n_words_per_packet - 2) / 3;

	if (samples_per_packet == 0) {
		fprintf(stderr, "DMA packet contains no ADC samples.\n");
		status = -2;
		goto cleanup;
	}

	/*
	 * Check multiplication before calculating n_samples.
	 */
	if (n_complete_packets > SIZE_MAX / samples_per_packet) {
		fprintf(stderr, "ADC sample count overflows size_t.\n");
		status = -2;
		goto cleanup;
	}

	n_samples = n_complete_packets * samples_per_packet;

	/*
	 * Check all buffer-size multiplications.
	 */
	if (n_samples > SIZE_MAX / sizeof(*samples) ||
		n_samples > SIZE_MAX / sizeof(*dc_ch2) ||
		n_samples > SIZE_MAX / sizeof(*dc_ch3)) {
		fprintf(stderr, "ADC buffer size overflows size_t.\n");
		status = -2;
		goto cleanup;
	}

	/*
	 * Allocate buffers for parsing and DC conversion.
	 *
	 * malloc is enough because all meaningful elements will be
	 * written by the processing functions.
	 */
	samples = malloc(n_samples * sizeof(*samples));
	dc_ch2 = malloc(n_samples * sizeof(*dc_ch2));
	dc_ch3 = malloc(n_samples * sizeof(*dc_ch3));

	if (!samples || !dc_ch2 || !dc_ch3) {
		fprintf(stderr, "Failed to allocate ADC/DC buffers.\n");
		status = -3;
		goto cleanup;
	}

	status = adc_packet_parser_v2(
		adc_words, complete_adc_words, n_words_per_packet, samples);

	if (status != 0) {
		fprintf(stderr, "adc_packet_parser_v2 failed: %d\n", status);
		goto cleanup;
	}

	/*
	 * Despite its current name, zero_padding() is assumed here to
	 * remove zero gate words and allocate nonzero_gate_words.
	 *
	 * Its expected interface is:
	 *
	 * int zero_padding(
	 *     const uint64_t *raw,
	 *     size_t n_raw,
	 *     uint64_t **nonzero,
	 *     size_t *n_nonzero);
	 */
	status = zero_padding(
		gate_words,
		n_gate_words,
		&nonzero_gate_words,
		&n_nonzero_gate_words);

	if (status != 0) {
		fprintf(stderr,
			"Failed to remove zero gate words: %d\n",
			status);
		goto cleanup;
	}

	if (n_nonzero_gate_words == 0) {
		fprintf(stderr, "No nonzero gate words were found.\n");
		status = -4;
		goto cleanup;
	}

	/*
	 * In this data format one nonzero gate word is assumed to produce
	 * one gate_event_t.
	 */
	n_gate_events = n_nonzero_gate_words;

	if (n_gate_events > SIZE_MAX / sizeof(*gate_events)) {
		fprintf(stderr, "Gate-event buffer size overflows size_t.\n");
		status = -4;
		goto cleanup;
	}

	gate_events = malloc(n_gate_events * sizeof(*gate_events));

	if (!gate_events) {
		fprintf(stderr, "Failed to allocate gate-event buffer.\n");
		status = -4;
		goto cleanup;
	}

	status = gate_word_parser(
		nonzero_gate_words, n_nonzero_gate_words, gate_events);

	if (status != 0) {
		fprintf(stderr, "gate_word_parser failed: %d\n", status);
		goto cleanup;
	}

	status = build_gate_windows(
		gate_events, n_gate_events, &windows, &n_windows);

	if (status != 0) {
		fprintf(stderr, "build_gate_windows failed: %d\n", status);
		goto cleanup;
	}

	if (n_windows == 0) {
		fprintf(stderr, "No valid gate windows were built.\n");
		status = -5;
		goto cleanup;
	}

	if (n_windows > SIZE_MAX / sizeof(*int_ch2) ||
		n_windows > SIZE_MAX / sizeof(*int_ch3)) {
		fprintf(stderr, "Integration buffer size overflows size_t.\n");
		status = -5;
		goto cleanup;
	}

	int_ch2 = malloc(n_windows * sizeof(*int_ch2));
	int_ch3 = malloc(n_windows * sizeof(*int_ch3));

	if (!int_ch2 || !int_ch3) {
		fprintf(stderr, "Failed to allocate integration buffers.\n");
		status = -5;
		goto cleanup;
	}

	/*
	 * Each worker reads the shared samples array, but writes to its
	 * own output array and its own job structure.
	 */
	dc_job_t job2 = {.samples = samples,
		.n_samples = n_samples,
		.channel = 2,
		.fft_enabled = fft_enabled,
		.fft_len = fft_len,
		.n_out = 0,
		.out = dc_ch2,
		.status = -1};

	dc_job_t job3 = {.samples = samples,
		.n_samples = n_samples,
		.channel = 3,
		.fft_enabled = fft_enabled,
		.fft_len = fft_len,
		.n_out = 0,
		.out = dc_ch3,
		.status = -1};
	
	clock_gettime(CLOCK_MONOTONIC, &dc_start);

	int thread_status = pthread_create(&thread2, NULL, dc_worker, &job2);

	if (thread_status != 0) {
		fprintf(stderr,
			"pthread_create(channel 2): %s\n",
			strerror(thread_status));
		status = -6;
		goto cleanup;
	}

	thread2_started = true;

	thread_status = pthread_create(&thread3, NULL, dc_worker, &job3);

	if (thread_status != 0) {
		fprintf(stderr,
			"pthread_create(channel 3): %s\n",
			strerror(thread_status));

		status = -6;

		/*
		 * Channel 2 is already running. We must wait for it before
		 * freeing samples, dc_ch2 or job2.
		 */
		goto cleanup;
	}

	thread3_started = true;

	thread_status = pthread_join(thread2, NULL);

	if (thread_status != 0) {
		fprintf(stderr,
			"pthread_join(channel 2): %s\n",
			strerror(thread_status));
		status = -7;
		goto cleanup;
	}

	thread2_started = false;

	thread_status = pthread_join(thread3, NULL);

	if (thread_status != 0) {
		fprintf(stderr,
			"pthread_join(channel 3): %s\n",
			strerror(thread_status));
		status = -7;
		goto cleanup;
	}

	thread3_started = false;
	clock_gettime(CLOCK_MONOTONIC, &dc_end);
	double dc_seconds = 0.0;
	
	dc_seconds += elapsed_seconds(&dc_start, &dc_end);
	printf("Parallel DC time:     %.9f sec\n", dc_seconds);

	if (job2.status != 0 || job3.status != 0) {
		fprintf(stderr,
			"process_to_dc failed: channel 2=%d, channel 3=%d\n",
			job2.status,
			job3.status);
		status = -8;
		goto cleanup;
	}

	if (job2.n_out != job3.n_out) {
		fprintf(stderr,
			"DC output sizes differ: channel 2=%zu, channel "
			"3=%zu\n",
			job2.n_out,
			job3.n_out);
		status = -8;
		goto cleanup;
	}

	integration_job_t int_job2 = {.data = dc_ch2,
		.n_samples = job2.n_out,
		.windows = windows,
		.n_windows = n_windows,
		.out = int_ch2,
		.status = -1};

	integration_job_t int_job3 = {.data = dc_ch3,
		.n_samples = job3.n_out,
		.windows = windows,
		.n_windows = n_windows,
		.out = int_ch3,
		.status = -1};

	pthread_t int_thread2;
	pthread_t int_thread3;

	pthread_create(&int_thread2, NULL, integration_worker, &int_job2);
	pthread_create(&int_thread3, NULL, integration_worker, &int_job3);

	pthread_join(int_thread2, NULL);
	pthread_join(int_thread3, NULL);

	// status = integrate_windows_sorted(
	// 	dc_ch2, job2.n_out, windows, n_windows, int_ch2);

	// if (status != 0) {
	// 	fprintf(stderr, "Channel 2 integration failed: %d\n", status);
	// 	goto cleanup;
	// }

	// status = integrate_windows_sorted(
	// 	dc_ch3, job3.n_out, windows, n_windows, int_ch3);

	// if (status != 0) {
	// 	fprintf(stderr, "Channel 3 integration failed: %d\n", status);
	// 	goto cleanup;
	// }
	size_t max_pairs = n_windows / 2;

	rdf2_ppm = malloc(max_pairs * sizeof(*rdf2_ppm));
	rdf3_ppm = malloc(max_pairs * sizeof(*rdf3_ppm));
	ddf_ppm = malloc(max_pairs * sizeof(*ddf_ppm));

	if ((!rdf2_ppm || !rdf3_ppm || !ddf_ppm) && max_pairs != 0) {
		fprintf(stderr, "Failed to allocate RDF/DDF arrays.\n");
		status = -9;
		goto cleanup;
	}

	size_t n_valid_pairs = 0;

	for (size_t even = 0; even + 1 < n_windows; even += 2) {
		size_t odd = even + 1;

		/*
		 * integrate_windows_sorted() has already calculated mean for
		 * every window.
		 */
		if (int_ch2[even].n_samples == 0 ||
			int_ch2[odd].n_samples == 0 ||
			int_ch3[even].n_samples == 0 ||
			int_ch3[odd].n_samples == 0) {
			continue;
		}

		double ch2_even = int_ch2[even].mean;
		double ch2_odd = int_ch2[odd].mean;
		double ch3_even = int_ch3[even].mean;
		double ch3_odd = int_ch3[odd].mean;

		double denominator2 = ch2_even + ch2_odd;
		double denominator3 = ch3_even + ch3_odd;

		if (denominator2 == 0.0 || denominator3 == 0.0) {
			continue;
		}

		double rdf2 = (ch2_even - ch2_odd) / denominator2;

		double rdf3 = (ch3_even - ch3_odd) / denominator3;

		double ddf = rdf2 - rdf3;

		/*
		 * Convert the dimensionless values to parts per million.
		 */
		rdf2_ppm[n_valid_pairs] = rdf2 * 1e6;
		rdf3_ppm[n_valid_pairs] = rdf3 * 1e6;
		ddf_ppm[n_valid_pairs] = ddf * 1e6;

		// printf("pair=%zu rdf2_ppm=%.9f rdf3_ppm=%.9f ddf_ppm=%.9f\n",
		// 	n_valid_pairs,
		// 	rdf2_ppm[n_valid_pairs],
		// 	rdf3_ppm[n_valid_pairs],
		// 	ddf_ppm[n_valid_pairs]);

		n_valid_pairs++;
	}

	if (n_valid_pairs == 0) {
		fprintf(stderr,
			"No valid pairs available for resolution "
			"calculation.\n");
		status = -10;
		goto cleanup;
	}

	double rdf2_resolution = standard_deviation(rdf2_ppm, n_valid_pairs);

	double rdf3_resolution = standard_deviation(rdf3_ppm, n_valid_pairs);

	double ddf_resolution =
		standard_deviation(ddf_ppm, n_valid_pairs) / sqrt(2.0);

	printf("\nResolution results:\n");
	printf("Valid pairs:     %zu\n", n_valid_pairs);
	printf("RDF2 resolution: %.9f ppm\n", rdf2_resolution);
	printf("RDF3 resolution: %.9f ppm\n", rdf3_resolution);
	printf("DDF resolution:  %.9f ppm\n", ddf_resolution);

	/*
	 * Temporary terminal output instead of CSV.
	 */
	// printf(
	//     "window,start_ts,end_ts,"
	//     "ch2_sum,ch2_mean,ch2_samples,"
	//     "ch3_sum,ch3_mean,ch3_samples\n"
	// );

	// for (size_t i = 0; i < n_windows; i++) {
	//     printf(
	//         "%zu,%" PRIu64 ",%" PRIu64 ","
	//         "%.17g,%.17g,%zu,"
	//         "%.17g,%.17g,%zu\n",
	//         i,
	//         windows[i].start_ts,
	//         windows[i].end_ts,
	//         int_ch2[i].sum,
	//         int_ch2[i].mean,
	//         int_ch2[i].n_samples,
	//         int_ch3[i].sum,
	//         int_ch3[i].mean,
	//         int_ch3[i].n_samples
	//     );
	// }

	status = 0;

cleanup:
	/*
	 * If an error happened after only one thread was created, wait
	 * for that thread before releasing the buffers it uses.
	 */
	if (thread2_started) {
		const int join_status = pthread_join(thread2, NULL);

		if (join_status != 0) {
			fprintf(stderr,
				"cleanup pthread_join(channel 2): %s\n",
				strerror(join_status));
		}
	}

	if (thread3_started) {
		const int join_status = pthread_join(thread3, NULL);

		if (join_status != 0) {
			fprintf(stderr,
				"cleanup pthread_join(channel 3): %s\n",
				strerror(join_status));
		}
	}

	free(samples);
	free(nonzero_gate_words);
	free(gate_events);
	free(windows);
	free(dc_ch2);
	free(dc_ch3);
	free(int_ch2);
	free(int_ch3);
	free(rdf2_ppm);
	free(rdf3_ppm);
	free(ddf_ppm);

	return status;
}
