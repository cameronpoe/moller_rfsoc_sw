#ifndef IQ_ROT_H
#define IQ_ROT_H

#include "rfsoc_types.h"

/**
 * @brief Determines whether a complex signal is approximately at DC.
 *
 * Estimates the average relative change in amplitude and the average
 * absolute phase difference between consecutive complex samples.
 *
 * The signal is classified as DC when both of the following conditions
 * are satisfied:
 *
 * \f[
 * \frac{\langle |x_{i+1} - x_i| \rangle}
 * {\langle |x_i| \rangle}
 * < rel\_lim,
 * \f]
 *
 * and
 *
 * \f[
 * \left\langle
 * \left|
 * \arg(x_{i+1}x_i^*)
 * \right|
 * \right\rangle
 * < phase\_lim.
 * \f]
 *
 * @param[in] sig Array of complex signal samples.
 * @param[in] n Number of samples in sig.
 * @param[in] phase_lim Maximum allowed mean phase change, in radians.
 * @param[in] rel_lim Maximum allowed mean relative sample-to-sample change.
 *
 * @return true if the signal is approximately constant in amplitude
 * and phase.
 * @return false if the signal is not at DC, sig is NULL, fewer than
 * two samples are provided, or the mean signal amplitude is zero.
 */
static inline bool check_if_dc(
	double complex *sig,
	size_t n,
	double phase_lim,
	double rel_lim) {

	/* At least two samples are required to calculate a
	 * sample-to-sample difference.
	 */
	if (!sig || n < 2)
		return false;

	double sum_abs_dphi = 0.0;
	double sum_amp = 0.0;
	double accum = 0.0;

	/* Accumulate the amplitude, sample-to-sample change, and phase
	 * difference for every consecutive pair of signal samples.
	 */
	for (size_t i = 0; i + 1 < n; i++) {
		double complex diff = sig[i + 1] - sig[i];

		sum_amp += cabs(sig[i]);
		accum += cabs(diff);

		/* x[i + 1] * conj(x[i]) has phase equal to the phase
		 * difference between the two consecutive samples.
		 */
		double complex phase_ratio = sig[i + 1] * conj(sig[i]);
		sum_abs_dphi += fabs(carg(phase_ratio));
	}

	/* Compute averages over the n - 1 consecutive sample pairs. */
	double mean_amp = sum_amp / (double)(n - 1);
	double mean_diff = accum / (double)(n - 1);
	double mean_phase_diff = sum_abs_dphi / (double)(n - 1);

	/* The relative variation is undefined for a zero-amplitude signal. */
	if (mean_amp == 0)
		return false;

	double rel_diff = mean_diff / mean_amp;

	/* Classify the signal as DC only when both amplitude and phase
	 * variation remain below their respective limits.
	 */
	return (rel_diff < rel_lim) && (mean_phase_diff < phase_lim);
}

/**
 * @brief Extracts timestamped complex I/Q samples from parsed ADC packets.
 *
 *
 * Each physical ADC channel is represented by two consecutive 24-bit
 * streams:
 *
 * - stream 2 * channel: I component;
 * - stream 2 * channel + 1: Q component.
 *
 * The two signed 24-bit values are converted to double and combined as
 *
 * \f[
 * x = I + iQ.
 * \f]
 *
 * The timestamp of each input ADC sample is copied to the corresponding
 * output sample.
 *
 *
 * @param[in] packets Array of parsed timestamped ADC samples.
 * @param[in] n_samples Number of elements in packets.
 * @param[in] channel Physical ADC channel index in the range 0 through 3.
 * @param[out] out Output array of timestamped complex I/Q samples.
 *
 * @pre out must contain space for at least n_samples elements.
 *
 *
 * @retval 0 I/Q samples were extracted successfully.
 * @retval -1 packets or out is NULL.
 * @retval -2 channel is outside the range 0 through 3.
 */
int get_iq_data_from_packets_ts(
	const adc_sample_t *packets,
	size_t n_samples,
	int channel,
	timestamped_iq_t *out);

/**
 * @brief Converts one complete ADC channel signal to a real-valued DC signal.
 *
 * First extracts the complex I/Q signal from the selected ADC channel.
 * The function then determines whether the signal is already approximately
 * at DC.
 *
 * If the signal is already at DC, it is phase-rotated so that its mean
 * lies on the positive real axis.
 *
 * Otherwise, the signal is divided into complete blocks of fft_len samples.
 * Each complete block is independently shifted to DC using
 * process_fft_block().
 *
 * @param[in] packets Array of parsed timestamped ADC samples.
 * @param[in] n_samples Number of elements in packets.
 * @param[out] out Output array of timestamped real-valued DC samples.
 * @param[out] n_out Number of elements in out.
 * @param[in] samp_freq Sampling frequency, in hertz.
 * @param[in] channel Physical ADC channel index in the range 0 through 3.
 * @param[in] fft_len Number of samples in each FFT processing block.
 *
 * @pre out must contain space for at least n_samples elements.
 *
 * @retval 0 Signal conversion completed successfully.
 * @retval -1 packets or out is NULL, or n_samples is zero.
 * @retval -2 Temporary memory allocation failed.
 * @retval -3 I/Q extraction failed.
 * @retval -4 fft_len is zero or is not a power of two.
 * @retval -5 fft_len is greater than n_samples.
 * @retval -6 Allocation of the real-valued FFT output buffer failed.
 * @retval -7 Processing of an FFT block failed.
 *
 * @note When FFT processing is required, only complete blocks of fft_len
 * samples are written. A final incomplete block is ignored.
 */
int process_to_dc(
	const adc_sample_t *packets,
	size_t n_samples,
	rotated_sample_t *out,
	size_t *n_out,
	uint64_t samp_freq,
	int channel,
	bool fft_flag,
	size_t fft_len);

/**
 * @brief Converts samples within one timestamp window to a DC signal.
 *
 * Selects all ADC samples satisfying
 *
 * \f[
 * start\_ts \leq ts < end\_ts
 * \f]
 *
 * and passes the selected samples to process_to_dc().
 *
 * @param[in] packets Array of parsed timestamped ADC samples.
 * @param[in] n_samples Number of elements in packets.
 * @param[in] start_ts Inclusive timestamp at the beginning of the window.
 * @param[in] end_ts Exclusive timestamp at the end of the window.
 * @param[out] out Output array of timestamped real-valued DC samples. 
 * @param[out] n_out Number of valid samples written to out.
 * @param[in] samp_freq Sampling frequency, in hertz.
 * @param[in] channel Physical ADC channel index in the range 0 through 3.
 * @param[in] fft_len Number of samples in each FFT processing block.
 *
 * @pre out must contain enough space for every input sample within
 * the requested timestamp window.
 *
 * @retval 0 Window was processed successfully.
 * @retval -1 packets, out, or n_out is NULL.
 * @retval -2 start_ts is greater than or equal to end_ts.
 * @retval -3 No samples were found inside the requested window.
 * @retval -4 Allocation of the temporary window buffer failed.
 * @retval -5 Conversion of the selected samples to DC failed.
 */
int process_to_dc_window(
	const adc_sample_t *packets,
	size_t n_samples,
	uint64_t start_ts,
	uint64_t end_ts,
	rotated_sample_t *out,
	size_t *n_out,
	uint64_t samp_freq,
	int channel,
	size_t fft_len);

#endif // IQ_ROT_H
