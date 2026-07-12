#ifndef DDF_H
#define DDF_H

#include "rfsoc_types.h"

/**
 * @brief Computes the relative differences and double differences for
 * consecutive helicity windows.
 *
 * For each pair of consecutive windows k and k + 1, the function
 * computes the relative difference for channels A and B:
 *
 * \f[
 * RDF_A =
 * \frac{A_{k+1} - A_k}{A_{k+1} + A_k},
 * \qquad * RDF_B =
 * \frac{B_{k+1} - B_k}{B_{k+1} + B_k}.
 * \f]
 *
 * The double difference is then computed as
 *
 * \f[
 * DDF = N(RDF_A - RDF_B),
 * \f]
 *
 * where N is either 1 or \f$1 / \sqrt{2}\f$, depending on the value
 * of normalize_sqrt2.
 *
 * Because each result is calculated from two consecutive windows,
 * the function writes n_windows - 1 elements to out.
 *
 * @param[in] ch_a Integration results for the first signal channel.
 * @param[in] ch_b Integration results for the second signal channel.
 * @param[in] n_windows Number of integration windows in each input array.
 * @param[in] normalize_sqrt2 If true, multiply each double difference
 * by \f$1 / \sqrt{2}\f$.
 * @param[out] out Output array containing n_windows - 1 DDF results.
 *
 * @pre ch_a must contain at least n_windows elements.
 * @pre ch_b must contain at least n_windows elements.
 * @pre out must contain space for at least n_windows - 1 elements.
 *
 * @retval 0 All double differences were computed successfully.
 * @retval -1 ch_a, ch_b, or out is NULL.
 * @retval -2 Fewer than two helicity windows were provided.
 * @retval -3 The relative-difference denominator is zero for at least
 * one pair of windows.
 */
int compute_ddf(
	const integration_result_t *ch_a,
	const integration_result_t *ch_b,
	size_t n_windows,
	bool normalize_sqrt2,
	ddf_result_t *out);

#endif // DDF_H
