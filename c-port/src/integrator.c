#include "integrator.h"

int integrate_one_window(
	const rotated_sample_t *data,
	size_t n_samples,
	gate_window_t window,
	integration_result_t *out) {
	/* Validate the input and output pointers before accessing them. */
	if (!data || !out)
		return -1;
	/* A valid integration window must have a positive duration. */
	if (window.start_ts >= window.end_ts)
		return -2;

	uint64_t start = window.start_ts;
	uint64_t end = window.end_ts;

	/* Accumulate the signal sum and count samples that lie inside
	 * the requested half-open timestamp interval [start, end).
	 */
	double sum = 0.0;
	size_t count = 0;

	for (size_t i = 0; i < n_samples; i++) {
		uint64_t ts = data[i].ts;

		/* Include the starting boundary but exclude the ending
		 * boundary to avoid overlap between adjacent windows.
		 */
		if (ts >= start && ts < end) {
			sum += data[i].sig;
			count++;
		}
	}

	/* Store the values that are defined even when the window
	 * contains no signal samples.
	 */
	out->sum = sum;
	out->n_samples = count;

	/* The arithmetic mean is undefined when no samples were found. */
	if (count == 0) {
		out->mean = 0.0;
		return -3;
	}

	/* Compute the arithmetic mean of all samples in the window. */
	out->mean = sum / (double)count;
	return 0;
}

int integrate_windows(
	const rotated_sample_t *data,
	size_t n_samples,
	const gate_window_t *windows,
	size_t n_windows,
	integration_result_t *out) {
	/* Validate all input and output arrays. */
	if (!data || !out || !windows)
		return -1;
	/* Integrate each helicity window independently and store its
	 * result at the corresponding output index.
	 */
	for (size_t i = 0; i < n_windows; i++) {
		int status = integrate_one_window(
			data, n_samples, windows[i], &out[i]);
		/* Stop immediately if one of the windows is invalid or
		 * contains no samples.
		 */
		if (status != 0) {
			return status;
		}
	}
	return 0;
}
