#ifndef INTEGRATOR_H

#define INTEGRATOR_H

#include "rfsoc_types.h"
/**
 * @brief Integrates DC-converted signal samples within one helicity window.
 *
 * Selects all samples whose timestamps satisfy
 * \f[
 * start\_ts \leq ts < end\_ts
 * \f]
 * and computes their sum, arithmetic mean, and total count.
 *
 * The interval is half-open: a sample at start_ts is included, while
 * a sample at end_ts is excluded. This prevents a boundary sample from
 * being included in two adjacent windows.
 *
 * @param[in] data Array of timestamped DC-converted signal samples.
 * @param[in] n_samples Number of elements in data.
 * @param[in] window Helicity integration window.
 * @param[out] out Integration result for the requested window.
 *
 * @pre data must contain at least n_samples elements.
 *
 * @retval 0 Integration completed successfully.
 * @retval -1 data or out is NULL.
 * @retval -2 The window is invalid because start_ts is greater than
 * or equal to end_ts
 * @retval -3 No samples were found inside the requested window.
 *
 * @note If no samples are found, out->sum and out->n_samples are set
 * to zero, and out->mean is also set to zero.
 */
int integrate_one_window(
	const rotated_sample_t *data,
	size_t n_samples,
	gate_window_t window,
	integration_result_t *out);

/**
 * @brief Integrates DC-converted signal samples over multiple
 * helicity windows.
 *
 * Calls integrate_one_window() for each element of windows and stores
 * the corresponding result in the matching element of out.
 *
 * @param[in] data Array of timestamped DC-converted signal samples.
 * @param[in] n_samples Number of elements in data.
 * @param[in] windows Array of helicity integration windows
 * @param[in] n_windows Number of elements in windows.
 * @param[out] out Output array containing one integration result
 * for each helicity window.
 *
 * @pre data must contain at least n_samples elements.
 * @pre windows must contain at least n_windows elements.
 * @pre out must contain space for at least n_windows elements.
 *
 * @retval 0 All windows were integrated successfully.
 * @retval -1 data, windows, or out is NULL.
 * @return Any nonzero error code returned by integrate_one_window()
 * if integration of an individual window fails.
 *
 * @note Processing stops at the first window that produces an error.
 * Results for earlier windows remain stored in out.
 */
int integrate_windows(
	const rotated_sample_t *data,
	size_t n_samples,
	const gate_window_t *windows,
	size_t n_windows,
	integration_result_t *out);



int integrate_windows_sorted(
    const rotated_sample_t *data,
    size_t n_samples,
    const gate_window_t *windows,
    size_t n_windows,
    integration_result_t *out
);

#endif // INTEGRATOR_H



