#include "ddf.h"

int compute_ddf(
	const integration_result_t *ch_a,
	const integration_result_t *ch_b,
	size_t n_windows,
	bool normalize_sqrt2,
	ddf_result_t *out) {
	/* Validate all input and output pointers. */
	if (!ch_a || !ch_b || !out)
		return -1;
	/* At least two windows are required to form one consecutive pair. */
	if (n_windows < 2)
		return -2;

	/* Apply the optional 1 / sqrt(2) normalization to each DDF value. */
	double norm = normalize_sqrt2 ? (1.0 / sqrt(2.0)) : 1.0;

	/* Calculate one result for every pair of consecutive windows.
	 *
	 * Therefore, n_windows input windows produce n_windows - 1
	 * output values.
	 */
	for (size_t k = 0; k + 1 < n_windows; k++) {
		/* Read the channel-A means for the current and next windows. */
		double a0 = ch_a[k].mean;
		double a1 = ch_a[k + 1].mean;

		/* Read the channel-B means for the current and next windows. */
		double b0 = ch_b[k].mean;
		double b1 = ch_b[k + 1].mean;

		/* Compute the denominators of the two relative differences. */
		double den_a = a1 + a0;
		double den_b = b1 + b0;

		/* A zero denominator would make the corresponding relative
		 * difference undefined.
		 */
		if (den_a == 0.0 || den_b == 0.0)
			return -1;
		/* Compute the relative difference independently for each
		 * input channel.
		 */
		double rdf1 = (a1 - a0) / den_a;
		double rdf2 = (b1 - b0) / den_b;

		/* Store both relative differences and their normalized
		 * double difference.
		 */
		out[k].rdf1 = rdf1;
		out[k].rdf2 = rdf2;
		out[k].ddf = norm * (rdf1 - rdf2);
	}

	return 0;
}
