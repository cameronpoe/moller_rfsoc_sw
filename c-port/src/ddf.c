#include "ddf.h"

// int compute_ddf(const integration_result_t *windows, size_t n_windows, ddf_result_t *out) {
//     if (!windows || !out) return -1;

//     double norm = 1 / sqrt(2);
//     // size_t count = n_windows - 1;

//     for (size_t i = 0; i < n_windows; i++) {
//         double a0 = windows[i].mean1;
//         double a1 = windows[i + 1].mean1;

//         double b0 = windows[i].mean2;
//         double b1 = windows[i + 1].mean2;

//         double denum1 = a0 + a1;
//         double denum2 = b0 + b1;

//         if (denum1 == 0 || denum2 == 0) {
//             return -2; 
//         }

//         double rdf1 = (double) (a1 - a0) / denum1;
//         double rdf2 = (double) (b1 - b0) / denum2;

//         out[i].rdf1 = rdf1;
//         out[i].rdf2 = rdf2;
//         out[i].ddf = norm * (rdf1 - rdf2);
//     }

//     return 0;
// }


int compute_ddf(
    const integration_result_t *ch_a,
    const integration_result_t *ch_b,
    size_t n_windows,
    bool normalize_sqrt2,
    ddf_result_t *out
) {
    if (!ch_a || !ch_b || !out) return -1;
    if (n_windows < 2) return -2;

    double norm = normalize_sqrt2 ? (1.0 / sqrt(2.0)) : 1.0;

    for (size_t k = 0; k + 1 < n_windows; k++) {
        double a0 = ch_a[k].mean;
        double a1 = ch_a[k + 1].mean;

        double b0 = ch_b[k].mean;
        double b1 = ch_b[k + 1].mean;

        double den_a = a1 + a0;
        double den_b = b1 + b0;

        if (den_a == 0.0 || den_b == 0.0) return -3;

        double rdf1 = (a1 - a0) / den_a;
        double rdf2 = (b1 - b0) / den_b;

        out[k].rdf1 = rdf1;
        out[k].rdf2 = rdf2;
        out[k].ddf = norm * (rdf1 - rdf2);
    }

    return 0;
}
