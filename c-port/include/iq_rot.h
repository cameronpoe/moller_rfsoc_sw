#ifndef IQ_ROT_H
#define IQ_ROT_H

#include "rfsoc_types.h"

static inline bool check_if_dc(double complex* sig, size_t n, double phase_lim, double rel_lim) {
    double sum_abs_dphi = 0.0; 
    double sum_amp = 0.0;
    double accum = 0.0; 

    if (!sig || n < 2) return false;

    for (size_t i = 0; i + 1 < n; i++) {
        double complex diff = sig[i + 1] - sig[i];

        sum_amp += cabs(sig[i]);
        accum += cabs(diff);

        double complex phase_ratio = sig[i + 1] * conj(sig[i]); 
        sum_abs_dphi += fabs(carg(phase_ratio));
    }

    double mean_amp = sum_amp / (double) (n - 1); 
    double mean_diff = accum / (double) (n - 1); 
    double mean_phase_diff = sum_abs_dphi / (double) (n - 1); 

    if (mean_amp == 0) return false;

    double rel_diff = mean_diff / mean_amp; 

    return (rel_diff < rel_lim) && (mean_phase_diff < phase_lim);

}

int get_iq_data_from_packets_ts(
    const adc_sample_t *packets,
    size_t n_samples,
    int channel,
    timestamped_iq_t *out
);

int process_to_dc(const adc_sample_t* packets, size_t n_samples,  rotated_sample_t* out,  uint64_t samp_freq, int channel, size_t fft_len); 

int process_to_dc_window(const adc_sample_t* packets, size_t n_samples, uint64_t start_ts, uint64_t end_ts, rotated_sample_t* out, size_t* n_out, uint64_t samp_freq, int channel, size_t fft_len);

#endif //IQ_ROT_H
