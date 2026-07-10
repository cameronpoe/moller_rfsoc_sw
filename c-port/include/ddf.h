#ifndef DDF_H 
#define DDF_H

#include "rfsoc_types.h"

// int compute_ddf(const integration_result_t* windows, size_t n_windows, ddf_result_t* out);

int compute_ddf(
    const integration_result_t *ch_a,
    const integration_result_t *ch_b,
    size_t n_windows,
    bool normalize_sqrt2,
    ddf_result_t *out
);

#endif // DDF_H
