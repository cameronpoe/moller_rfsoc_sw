#ifndef RFSOC_TYPES_H
#define RFSOC_TYPES_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>
#include <complex.h>
#include <string.h>
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAGIC_WORD 0xFFFFFFFFFFFFFFFFULL
#define TIME_INC 8
#define REL_LIM 1e-3
#define PHASE_LIM 1e-3

/**
 * @brief One signed 24-bit ADC value stored as three bytes.
 *
 * The ADC data stream stores each sample using exactly 24 bits.
 * The bytes are kept separately to avoid relying on compiler-specific
 * packed bit fields.
 */
typedef struct {
	uint8_t b0; /**< Least-significant byte. */
	uint8_t b1; /**< Middle byte. */
	uint8_t b2; /**< Most-significant byte. */
} adc24_t;

/**
 * @brief One frame of the raw data input stream as eight 24-bit ADC values.
 *
 * One frame contains eight ADC values extracted from the raw input stream.
 * In the current data format, these values correspond to the I and Q
 * components of four ADC channels.
 *
 * The mapping is:
 * - ch0: channel 0, I component;
 * - ch1: channel 0, Q component;
 * - ch2: channel 1, I component;
 * - ch3: channel 1, Q component;
 * - ch4: channel 2, I component;
 * - ch5: channel 2, Q component;
 * - ch6: channel 3, I component;
 * - ch7: channel 3, Q component;
 *
 */
typedef struct {
	adc24_t ch0; /**< ADC channel 0, I component. */
	adc24_t ch1; /**< ADC channel 0, Q component. */
	adc24_t ch2; /**< ADC channel 1, I component. */
	adc24_t ch3; /**< ADC channel 1, Q component. */
	adc24_t ch4; /**< ADC channel 2, I component. */
	adc24_t ch5; /**< ADC channel 2, Q component. */
	adc24_t ch6; /**< ADC channel 3, I component. */
	adc24_t ch7; /**< ADC channel 3, Q component. */
} adc_frame_t;

/**
 * @brief One gate transiotion event.
 *
 * A gate event contains state of the gate and the timestep
 * at which transition has occured.
 *
 * The timestamp is expressed in gate-clock ticks.
 *
 */
typedef struct {
	bool edge;   /**< Gate state parameter. Possible states: falling = true,
			rising = false  . */
	uint64_t ts; /**< Timestamp of the gate transtion in clock ticks. */
} gate_event_t;

/**
 * @brief One timestamped ADC frame.
 *
 * Timestamped ADC frame contains ADC data frame and the timestamp
 * at which the frame was recorded.
 *
 * The timestamp is expressed in clock ticks.
 */
typedef struct {
	uint64_t ts;	  /**< Timestamp of ADC frame*/
	adc_frame_t data; /**< ADC frame recorded at this timestamp */
} adc_sample_t;

/**
 * @brief Helicity window.
 *
 * Contains two timestamps corresponding to the beginning and
 * end of a helicity window.
 *
 * Timestamps are expressed in gate-clock ticks.
 */
typedef struct {
	uint64_t
		start_ts; /**< Timestamp of the beggining of helicity window. */
	uint64_t end_ts;  /**< Timestamp of the end of helicity window. */
} gate_window_t;

/**
 * @brief One DC-converted sample.
 *
 * Contains amplitude of signal after down-conversion to DC and the timestamp
 * at which the orginal signal sample was recorded.
 *
 * Timestamp is expresses in clock ticks.
 */
typedef struct {
	double sig; /**< Amplitude of the signal after down-conversion to DC. */
	uint64_t ts; /**< Timestamp of the original signal sample.*/
} rotated_sample_t;

/**
 * @brief Integration result for one helicity window.
 *
 * Contatins the sum and mean of DC-converted signal samples within a
 * specific helicity window. It also stores the number of samples which where
 * used for the integration.
 *
 * @note The sample count is primarily stored for debugging and
 * validation purposes.
 */
typedef struct {
	double sum;  /**< Sum of the signal samples in the helicity window. */
	double mean; /**< Mean of the signal samples in the helicity window. */
	size_t n_samples; /**< Number of samples included in the integration. */
} integration_result_t;

/**
 * @brief One timestamped complex I/Q sample.
 *
 * Stores one complex signal sample in the form
 * \f$ I + iQ \f$ and timestamp at which the orignial
 * sample was recorded.
 *
 * Timestamp is expressed in clock ticks.
 */
typedef struct {
	double complex sig; /**< Complex signal value in the form I + iQ. */
	uint64_t ts;	    /**< Timestamp of the original signal sample. */
} timestamped_iq_t;

/**
 * @brief DDF calculation result for one pair of helicity windows.
 *
 * Stores the relative differences calculated independently for the
 * two input channels and their final combined double difference.
 *
 */
typedef struct {
	double rdf1; /**< Relative difference for the first input channel. */
	double rdf2; /**< Relative difference for the second input channel. */
	double ddf;  /**< Combined double-difference value. */
} ddf_result_t;

/**
 * @brief Extracts the even-indexed elements of a complex array.
 *
 * Copies elemets arr[0], arr[2], arr[4], and so on
 * into the output array.
 *
 * @param[in] arr Input complex array.
 * @param[in] n Number of elements in the input array.
 * @param[out] out Output array for the even-indexed elements.
 *
 * @pre arr must point to an array containing at least n elements.
 * @pre out must point to an array containing at lease (n + 1) / 2 elements.
 *
 * @return This function does not return an error code.
 */
static inline int get_even(
	double complex *arr,
	size_t n,
	double complex *out) {
	for (size_t i = 0; 2 * i < n; i++) {
		out[i] = arr[2 * i];
	}

	return 0;
}

/**
 * @brief Extracts the odd-indexed elements of a complex array.
 *
 * @param[in] arr Input complex array.
 * @param[in] n Number of elements in the input array.
 * @param[out] out Output array for the odd-indexed elements.
 *
 * @pre arr must point to an array containing at least n elements.
 * @pre out must point to an array containing at least (n + 1) / 2 elements.
 *
 * @return This function does not return an error code.
 */
static inline int get_odd(
	double complex *arr,
	size_t n,
	double complex *out) {
	for (size_t i = 0; 2 * i + 1 < n; i++) {
		out[i] = arr[2 * i + 1];
	}

	return 0;
}

/**
 * @brief Implementation of discrete Fourier transform using
 * the recursive Cooley-Tukey FFT algorithm.
 *
 * The input length must be a nonzero power of two. The transform
 * uses the negative exponential convention.
 *
 * \f[
 * X_k = \sum_{j=0}^{N-1} x_j e^{-2\pi i jk/N}.
 * \f]
 *
 * @param[in] arr Input complex array.
 * @param[in] fft_len Number of input samples and FFT output bins.
 * @param[out] out Output array containing the FFT coefficients.
 *
 *
 * @return 0 FFT completed successfully.
 * @return -1 sig or out is NULL.
 * @return -2 fft_len = 0 or isn't a power of 2.
 * @return -3 Memory allocation has failed.
 * @return -4 Issues with selecting odd or even elemetns of orginal array.
 * @return -5 A recursive FFT call failed.
 */
static inline int fft_Cooley_Tukey(
	double complex *arr,
	size_t fft_len,
	double complex *out) {
	/* Validate input pointers. */
	if (!arr || !out)
		return -1;
	/* The Cooley-Tukey algorithm requires the input length to be non-zero
	 * and a powewr of two. */
	if (fft_len == 0 || (fft_len & (fft_len - 1)) != 0)
		return -2;
	/* Base case of recursion. The FFT of a single elemet is an element
	 * itself. */
	if (fft_len == 1) {
		out[0] = arr[0];
		return 0;
	}

	size_t half = fft_len / 2;

	/* Allocate temporary buffers for the even and odd subsequences
	 * together with their corresponding FFT outputs.
	 */
	double complex *even =
		(double complex *)calloc(half, sizeof(double complex));
	double complex *odd =
		(double complex *)calloc(half, sizeof(double complex));
	double complex *even_fft =
		(double complex *)calloc(half, sizeof(double complex));
	double complex *odd_fft =
		(double complex *)calloc(half, sizeof(double complex));

	/* Abort if memory allocation failed. */
	if (!odd || !even || !even_fft || !odd_fft) {
		free(odd);
		free(even);
		free(even_fft);
		free(odd_fft);
		return -3;
	}

	/* Split the input sequence into its even- and odd-indexed
	 * subsequences.
	 */
	int status1 = get_odd(arr, fft_len, odd);
	int status2 = get_even(arr, fft_len, even);

	if ((status1 != 0) || (status2 != 0)) {
		free(odd);
		free(even);
		free(even_fft);
		free(odd_fft);
		return -4;
	}

	/* Recursively compute the FFT of the two subsequences. */
	int status3 = fft_Cooley_Tukey(even, half, even_fft);
	int status4 = fft_Cooley_Tukey(odd, half, odd_fft);

	if (status3 != 0 || status4 != 0) {
		free(even);
		free(odd);
		free(even_fft);
		free(odd_fft);
		return -5;
	}

	/* Combine the two half-size FFTs into the FFT of the full
	 * sequence using the Cooley-Tukey butterfly operation.
	 */
	for (size_t k = 0; k < half; k++) {
		double angle = -2.0 * M_PI * k / fft_len;
		double complex w = cos(angle) + I * sin(angle);

		out[k] = even_fft[k] + w * odd_fft[k];
		out[k + half] = even_fft[k] - w * odd_fft[k];
	}

	/*Releases all temporay buffers.*/
	free(odd);
	free(even);
	free(even_fft);
	free(odd_fft);

	return 0;
}

/**
 * @brief Converts an FFT bin index to its corresponding signed frequency.
 *
 * FFT bin from 0 to n / 2 corresponds to nonnegative.
 * frequencies. Bins above n / 2 correspond to negative frequencies.
 *
 * @param[in] sampl_freq Sampling frequency in hertz.
 * @param[in] n Total number of FFT bins.
 * @param[in] k Index of the FFT bin.
 *
 * @pre n must be greater than zero.
 * @pre k must be less than n.
 *
 * @return Signed frequency corresponding to FFT bin k, in hertz.
 */
static inline double fft_bin_to_freq(
	double sampl_freq,
	size_t n,
	size_t k) {
	if (k <= n / 2) {
		return (double)k * sampl_freq / (double)n;
	} else {
		return -((double)(n - k) * sampl_freq) / (double)n;
	}
}

/**
 * @brief Finds the index of the FFT coefficient with the largest magnitude.
 *
 * Computes the magnitude of each complex FFT coefficient using cabs()
 * and returns the index of the coefficient with the greatest magnitude.
 *
 * If multiple coefficients have the same maximum magnitude, the index
 * of the first such coefficient is returned.
 *
 *
 *
 * @param[in] out Array of complex FFT coefficients.
 * @param[in] n Number of elements in the FFT output array.
 *
 * @return Index of the coefficient with the largest magnitude.
 * @retval SIZE_MAX out is NULL or n is zero.
 */
static inline size_t fft_find_argmax(
	double complex *out,
	size_t n) {
	double max = cabs(out[0]);
	size_t best_ind = 0;

	if (!out || n == 0)
		return 0;

	for (size_t i = 1; i < n; i++) {
		double tmp = cabs(out[i]);

		if (tmp > max) {
			max = tmp;
			best_ind = i;
		}
	}

	return best_ind;
}
/**
 * @brief Processes one complex signal block and shifts its carrier to DC.
 *
 * Computes the FFT of the selected signal block, determines the dominant
 * frequency bin, and mixes the signal with the negative of that frequency.
 * The resulting complex signal is then phase-rotated so that its average
 * lies on the real axis.
 *
 * The real part of the final rotated signal is written to out using the
 * same indices as the original input block.
 *
 * @param[in] sig Complete input array of complex I/Q samples.
 * @param[in] start Index of the first sample in the block.
 * @param[in] fft_len Number of samples processed in the block.
 * @param[in] samp_freq Sampling frequency, in hertz.
 * @param[out] out Output array for the real-valued DC signal.
 *
 * @pre sig must contain at least start + fft_len samples.
 * @pre out must contain at least start + fft_len elements.
 * @pre fft_len must be a nonzero power of two.
 * @pre samp_freq must be greater than zero.
 *
 * @retval 0 Signal block was processed successfully.
 * @retval -1 sig or out is NULL.
 * @retval -2 Memory allocation failed.
 * @retval -3 FFT calculation failed.
 * @retval -4 No valid maximum FFT bin was found.
 *
 */
static inline int process_fft_block(
	const double complex *sig,
	size_t start,
	size_t fft_len,
	double samp_freq,
	double *out) {
	/* Validate the input and output pointers before accessing
	 * either array.
	 */
	if (!sig || !out)
		return -1;
	/* Allocate temporary buffers for the FFT coefficients and the
	 * complex signal after down-conversion to DC.
	 */
	double complex *fft_out = calloc(fft_len, sizeof(double complex));
	double complex *dc = calloc(fft_len, sizeof(double complex));

	/* Release any successfully allocated buffer if either
	 * allocation failed.
	 */
	if (!fft_out || !dc) {
		free(fft_out);
		free(dc);
		return -2;
	}

	/* Compute the FFT of the selected input block.
	 *
	 * The cast can be removed if fft_Cooley_Tukey() accepts
	 * const double complex
	 * as its input argument.
	 */
	int status = fft_Cooley_Tukey(
		(double complex *)&sig[start], fft_len, fft_out);

	/* Abort if the FFT calculation failed. */
	if (status != 0) {
		free(fft_out);
		free(dc);
		return -3;
	}

	/* Find the FFT bin containing the largest signal magnitude. */
	size_t kmax = fft_find_argmax(fft_out, fft_len);

	/* SIZE_MAX indicates that the FFT output was invalid or empty. */
	if (kmax == SIZE_MAX) {
		free(fft_out);
		free(dc);
		return -4;
	}

	/* Convert the dominant FFT-bin index to its signed physical
	 * frequency.
	 */
	double freq = fft_bin_to_freq(samp_freq, fft_len, kmax);
	/* Extract the phase of the dominant carrier component.
	 *
	 * This value is currently used only for diagnostic output.
	 */
	double carrier_phase = carg(fft_out[kmax]);

	printf("C DEBUG: start=%zu fft_len=%zu kmax=%zu carrier_freq=%.17g "
	       "carrier_phase=%.17g\n",
		start,
		fft_len,
		kmax,
		freq,
		carrier_phase);
	/* Mix the dominant carrier frequency down to DC.
	 *
	 * Multiplication by exp(-i * phase) removes the estimated
	 * carrier oscillation from each complex input sample.
	 */
	for (size_t i = 0; i < fft_len; i++) {
		double phase = 2.0 * M_PI * freq * (double)i / samp_freq;
		dc[i] = sig[start + i] * cexp(-I * phase);
	}

	/* Compute the mean complex value of the down-converted block. */
	double complex mean = 0.0 + 0.0 * I;
	for (size_t i = 0; i < fft_len; i++) {
		mean += dc[i];
	}
	mean /= (double)fft_len;

	/* Remove the residual phase offset so that the mean signal
	 * lies on the positive real axis.
	 */
	double complex rot = cexp(-I * carg(mean));

	/* Apply the phase rotation and store only the real component
	 * of each final DC sample.
	 */
	for (size_t j = 0; j < fft_len; j++) {
		out[start + j] = creal(dc[j] * rot);
	}

	/* Release all temporary working buffers. */
	free(fft_out);
	free(dc);

	return 0;
}

/**
 * @brief Constructs a 24-bit ADC value from three individual bytes.
 *
 * Stores the supplied bytes in an adc24_t structure without performing
 * byte swapping, sign extension, or numerical conversion.
 *
 * @param[in] b0 Most-significant byte, bits 23:16.
 * @param[in] b1 Middle byte, bits 15:8.
 * @param[in] b2 Least-significant byte, bits 7:0.
 *
 * @return Constructed 24-bit ADC value.
 */
static inline adc24_t make_adc24(
	uint8_t b0,
	uint8_t b1,
	uint8_t b2) {
	adc24_t x;
	/* Store each byte in its corresponding structure field. */
	x.b0 = b0;
	x.b1 = b1;
	x.b2 = b2;
	return x;
}

/**
 * @brief Converts a signed 24-bit ADC value to a signed 32-bit integer.
 *
 * Reconstructs the 24-bit two's-complement value from its three bytes
 * and sign-extends it to int32_t.
 *
 * The byte order is interpreted as:
 * - b0: bits 23:16;
 * - b1: bits 15:8;
 * - b2: bits 7:0.
 *
 * @param[in] x Input 24-bit ADC value.
 * @return Sign-extended 32-bit representation of the ADC value.
 */
static inline int32_t adc24_to_int32(
	adc24_t x) {
	/* Combine the three bytes into one unsigned 24-bit value.
	 *
	 * b0 becomes the most-significant byte and b2 becomes the
	 * least-significant byte.
	 */
	uint32_t raw = ((uint32_t)x.b0 << 16) | ((uint32_t)x.b1 << 8) |
		       ((uint32_t)x.b2);

	/* Move the original sign bit from bit 23 to bit 31, convert
	 * to a signed integer, and shift it back with sign extension.
	 */
	return ((int32_t)(raw << 8)) >> 8;
}

/**
 * @brief Get the adc24 channel object.
 *
 * Selects one of the eight 24-bit values stored in an ADC frame using
 * a stream index from zero through seven.
 *
 * @param[in] frames ADC frame containing eight 24-bit signal values.
 * @param[in] stream Index of the requested stream.
 *
 * @return Pointer to the selected adc24_t value.
 * @retval NULL frames is NULL or stream is outside the range 0 through 7.
 */
static inline const adc24_t *get_adc24_channel(
	const adc_frame_t *frames,
	int stream) {
	/* A channel cannot be selected from an invalid frame pointer. */
	if (!frames)
		return NULL;
	/* Map the numerical stream index to the corresponding field
	 * of the ADC frame.
	 */
	switch (stream) {
	case 0:
		return &frames->ch0;
	case 1:
		return &frames->ch1;
	case 2:
		return &frames->ch2;
	case 3:
		return &frames->ch3;
	case 4:
		return &frames->ch4;
	case 5:
		return &frames->ch5;
	case 6:
		return &frames->ch6;
	case 7:
		return &frames->ch7;
	/* Reject stream indices outside the valid range. */
	default:
		return NULL;
	}
}

#endif // RFSOC_TYPES_H
