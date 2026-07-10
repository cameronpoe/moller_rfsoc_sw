#ifndef INTEGRATOR_H

#define INTEGRATOR_H

#include "rfsoc_types.h"

// int integrate_one_window(const rotated_sample_t* data1, const rotated_sample_t* data2, size_t n_samples1, size_t n_samples2, gate_window_t window, integration_result_t *out);

int integrate_one_window(
    const rotated_sample_t* data,
    size_t n_samples,
    gate_window_t window,
    integration_result_t *out
);

int integrate_windows(const rotated_sample_t* data, size_t n_samples, const gate_window_t* windows, size_t n_windows, integration_result_t *out);

#endif // INTEGRATOR_H
