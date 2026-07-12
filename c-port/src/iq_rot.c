#include "iq_rot.h"

int get_iq_data_from_packets_ts(
	const adc_sample_t *packets,
	size_t n_samples,
	int channel,
	timestamped_iq_t *out) {
	/* Validate the input and output arrays. */
	if (!packets || !out)
		return -1;

	/* Four physical ADC channels are available. Each channel uses
	 * two streams: one for I and one for Q.
	 */
	if (channel < 0 || channel > 3)
		return -2;

	/* Extract one complex I/Q value from every timestamped ADC frame. */
	for (size_t i = 0; i < n_samples; i++) {
		const adc_frame_t *frame = &packets[i].data;

		/* Select the consecutive I and Q streams belonging to the
		 * requested physical channel.
		 */
		const adc24_t *ch1_i_adc =
			get_adc24_channel(frame, 2 * channel);
		const adc24_t *ch1_q_adc =
			get_adc24_channel(frame, 2 * channel + 1);

		/* These pointers should always be valid because the channel
		 * range was checked above.
		 */
		if (!ch1_i_adc || !ch1_q_adc)
			return -2;

		/* Sign-extend both 24-bit ADC values to signed 32-bit values.
		 */
		int32_t ch1_i = adc24_to_int32(*ch1_i_adc);
		int32_t ch1_q = adc24_to_int32(*ch1_q_adc);

		/* Preserve the original sample timestamp and combine the
		 * two components into the complex value I + iQ.
		 */
		out[i].ts = packets[i].ts;
		out[i].sig = (double)ch1_i + I * (double)ch1_q;
	}

	return 0;
}

int process_to_dc(
	const adc_sample_t *packets,
	size_t n_samples,
	rotated_sample_t *out,
	size_t *n_out,
	uint64_t samp_freq,
	int channel,
	size_t fft_len) {
	/* Validate all input and output pointers. The output sample count is
	 * initialized to zero so that the caller never observes an
	 * uninitialized value if an error occurs.
	 */
	if (!packets || !out || !n_out || n_samples == 0)
		return -1;

	*n_out = 0;
	/* Allocate temporary storage for the timestamped I/Q data and
	 * a separate contiguous complex signal array.
	 */
	timestamped_iq_t *iq_data =
		(timestamped_iq_t *)calloc(n_samples, sizeof(timestamped_iq_t));
	double complex *sig =
		(double complex *)calloc(n_samples, sizeof(double complex));

	if (!iq_data || !sig) {
		free(iq_data);
		free(sig);
		return -2;
	}

	/* Extract the selected physical ADC channel as complex I/Q data. */
	if (get_iq_data_from_packets_ts(packets, n_samples, channel, iq_data) !=
		0) {
		free(iq_data);
		free(sig);
		return -3;
	}

	double complex mean = 0.0 + 0.0 * I;

	/* Copy the complex samples into the working array, preserve all
	 * timestamps, and accumulate the complex signal mean.
	 */
	for (size_t i = 0; i < n_samples; i++) {
		sig[i] = iq_data[i].sig;
		out[i].ts = iq_data[i].ts;

		mean += sig[i];
	}

	/* Determine whether the extracted signal is already sufficiently
	 * constant in amplitude and phase to be treated as DC.
	 */
	mean /= (double)n_samples;

	bool sig_dc = check_if_dc(sig, n_samples, PHASE_LIM, REL_LIM);
	if (!sig_dc) {
		/* FFT down-conversion requires a nonzero power-of-two
		 * block length.
		 */
		if (fft_len == 0 || (fft_len & (fft_len - 1)) != 0) {
			free(iq_data);
			free(sig);
			return -4;
		}

		/* At least one complete FFT block must fit in the signal. */
		if (fft_len > n_samples) {
			free(iq_data);
			free(sig);
			return -5;
		}

		/* Allocate storage for the real-valued down-converted signal.
		 */
		double *real = (double *)calloc(n_samples, sizeof(double));

		if (!real) {
			free(real);
			free(iq_data);
			free(sig);
			return -6;
		}

		/* Ignore any incomplete final block shorter than fft_len. */
		size_t usable = n_samples - (n_samples % fft_len);

		/* Down-convert each complete FFT block independently. */
		for (size_t start = 0; start + fft_len <= usable;
			start += fft_len) {
			int s = process_fft_block(
				sig, start, fft_len, (double)samp_freq, real);

			if (s != 0) {
				free(real);
				free(iq_data);
				free(sig);
				return -7;
			}
		}

		/* Copy all successfully processed FFT samples into the output
		 * array. Only complete FFT blocks are processed, therefore any
		 * incomplete trailing block is discarded.
		 */
		for (size_t i = 0; i < usable; i++) {
			out[i].ts = iq_data[i].ts;
			out[i].sig = real[i];
		}

		/* Report the number of valid output samples. */
		*n_out = usable;

		free(real);
		free(iq_data);
		free(sig);

		return 0;
	}

	/* The signal is already at DC. Remove its residual constant phase
	 * so that the complex mean lies on the positive real axis.
	 */
	double complex rot = cexp(-I * carg(mean));

	/* Rotate the signal so that its mean lies on the positive real axis,
	 * then retain only the real component.
	 */
	for (size_t i = 0; i < n_samples; i++) {
		out[i].sig = creal(sig[i] * rot);
	}

	/* Every input sample produced one output sample. */
	*n_out = n_samples;
	free(iq_data);
	free(sig);
	return 0;
}

int process_to_dc_window(
	const adc_sample_t *packets,
	size_t n_samples,
	uint64_t start_ts,
	uint64_t end_ts,
	rotated_sample_t *out,
	size_t *n_out,
	uint64_t samp_freq,
	int channel,
	size_t fft_len) {
	/* Validate all required input and output pointers. */
	if (!packets || !out || !n_out)
		return -1;

	/* A valid timestamp window must have a positive duration. */
	if (start_ts >= end_ts)
		return -2;

	/* Ensure that the caller never observes an uninitialized count
	 * when the function exits with an error.
	 */
	*n_out = 0;

	size_t count = 0;

	/* Count samples in the half-open interval [start_ts, end_ts). */
	for (size_t i = 0; i < n_samples; i++) {
		uint64_t ts = packets[i].ts;

		if (ts >= start_ts && ts < end_ts) {
			count++;
		}
	}

	/* The selected window cannot be processed if it contains no samples. */
	if (count == 0)
		return -3;

	/* Allocate a contiguous array containing only samples from the
	 * requested timestamp window.
	 */
	adc_sample_t *window_packets =
		(adc_sample_t *)calloc(count, sizeof(adc_sample_t));

	if (!window_packets)
		return -4;

	/* Copy all selected samples into the temporary window array. */
	size_t j = 0;

	for (size_t i = 0; i < n_samples; i++) {
		uint64_t ts = packets[i].ts;

		if (ts >= start_ts && ts < end_ts) {
			window_packets[j] = packets[i];
			j++;
		}
	}

	/* Convert the selected signal window to DC. The function also reports
	 * the number of valid output samples through n_out.
	 */
	size_t produced = 0;

	int status = process_to_dc(
		window_packets,
		count,
		out,
		&produced,
		samp_freq,
		channel,
		fft_len);

	*n_out = produced;
	/* Stop if the DC conversion failed. n_out remains zero, as initialized
	 * at the beginning of this function.
	 */
	if (status != 0) {
		free(window_packets);
		return -5;
	}

	/* Publish the number of valid output samples only after successful
	 * processing.
	 */
	*n_out = produced;

	free(window_packets);
	return 0;
}
