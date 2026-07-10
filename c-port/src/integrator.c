#include "integrator.h"

// int integrate_one_window(const rotated_sample_t* data1, const rotated_sample_t* data2, size_t n_samples1, size_t n_samples2, gate_window_t window, integration_result_t *out) {


//     if (!data || !out) return -1;
//     if (window.start_ts >= window.end_ts) return -2; 
//     if (n_samples1 != n_samples2) return -3;

//     double sum = 0.0;
//     // double sum2 = 0.0;
//     size_t count = 0;


//     uint64_t start = window.start_ts;
//     uint64_t end = window.end_ts;

//     for (size_t i = 0; i < n_samples1; i++) {
//         uint64_t ts = data1[i].ts; 
//         uint64_t ts = data2[i].ts; 
//         if (ts >= start && ts < end) {
//             count++; 
//             sum += data[i].sig;
//             // sum2 += data[i].sig2;
//         }
//     }

//     out->sum = sum;
//     // out->sum2 = sum2;
//     out->n_samples = count;

//     if (count > 0) {
//         double avr = sum / (double) count; 
//         // double avr2 = sum2 / (double) count;
        
//         out->mean = avr;
//         // out->mean2 = avr2;
//     } 
//     else {
//         out->mean = 0.0;
//         // out->mean2 = 0.0;

//         return -3;
//     }

//     return 0;
// }

int integrate_one_window(
    const rotated_sample_t* data,
    size_t n_samples,
    gate_window_t window,
    integration_result_t *out
) {
    if (!data || !out) return -1;
    if (window.start_ts >= window.end_ts) return -2;

    uint64_t start = window.start_ts;
    uint64_t end = window.end_ts;

    double sum = 0.0;
    size_t count = 0;

    for (size_t i = 0; i < n_samples; i++) {
        uint64_t ts = data[i].ts;

        if (ts >= start && ts < end) {
            sum += data[i].sig;
            count++;
        }
    }

    out->sum = sum;
    out->n_samples = count;

    if (count == 0) {
        out->mean = 0.0;
        return -3;
    }

    out->mean = sum / (double)count;
    return 0;
}

int integrate_windows(const rotated_sample_t* data, size_t n_samples, const gate_window_t* windows, size_t n_windows, integration_result_t *out) {

    if (!data || !out || !windows) return -1;
    for (size_t i = 0; i < n_windows; i++) { 
        int status = integrate_one_window(data, n_samples, windows[i], &out[i]);

        if (status != 0) {
            return status;
        }
    }
    return 0;
}
