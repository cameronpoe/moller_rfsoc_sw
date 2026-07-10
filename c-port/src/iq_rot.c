#include "iq_rot.h"

int get_iq_data_from_packets_ts(
    const adc_sample_t *packets,
    size_t n_samples,
    int channel,
    timestamped_iq_t *out
) {
    if (!packets || !out) return -1;
    if (channel < 0 || channel > 3) return -2;
    // if (channel2 < 0 || channel2 > 3) return -3;

    for (size_t i = 0; i < n_samples; i++) {
        const adc_frame_t *frame = &packets[i].data;

        const adc24_t *ch1_i_adc = get_adc24_channel(frame, 2 * channel);
        const adc24_t *ch1_q_adc = get_adc24_channel(frame, 2 * channel + 1);

        // const adc24_t *ch2_i_adc = get_adc24_channel(frame, 2 * channel2);
        // const adc24_t *ch2_q_adc = get_adc24_channel(frame, 2 * channel2 + 1);

        int32_t ch1_i = adc24_to_int32(*ch1_i_adc);
        int32_t ch1_q = adc24_to_int32(*ch1_q_adc);

        // int32_t ch2_i = adc24_to_int32(*ch2_i_adc);
        // int32_t ch2_q = adc24_to_int32(*ch2_q_adc);

        out[i].ts = packets[i].ts;
        out[i].sig = (double)ch1_i + I * (double)ch1_q;
        // out[i].sig2 = (double)ch2_i + I * (double)ch2_q;
    }

    return 0;
}


int process_to_dc(const adc_sample_t* packets, size_t n_samples,  rotated_sample_t* out,  uint64_t samp_freq, int channel, size_t fft_len) {

    if (!packets || !out) return -1;

    timestamped_iq_t* iq_data = (timestamped_iq_t*) calloc(n_samples, sizeof(timestamped_iq_t));
    double complex* sig = (double complex*) calloc(n_samples, sizeof(double complex));
    // double complex* sig2 = (double complex*) calloc(n_samples, sizeof(double complex));

    if (!iq_data || !sig) {
        free(iq_data);
        free(sig);
        return -2;
    }
    if (get_iq_data_from_packets_ts(packets, n_samples, channel, iq_data) !=0 ) {
        free(iq_data);
        free(sig);
        return -3;
    }
    

    double complex mean = 0.0 + 0.0 * I;
    // double complex mean2 = 0.0 + 0.0 * I;

    for (size_t i = 0; i < n_samples; i++) {
        sig[i] = iq_data[i].sig;
        out[i].ts = iq_data[i].ts;

        mean += sig[i];
    }
    
    mean /= (double) n_samples;
    
    bool sig_dc = check_if_dc(sig, n_samples, PHASE_LIM, REL_LIM);
    // bool sig2_dc = check_if_dc(sig2, n_samples, PHASE_LIM, REL_LIM);


    // if (!sig1_dc || !sig2_dc) {
    //     // passing only even ones 
    //     if (fft_len == 0 || (fft_len & (fft_len - 1)) != 0) return -4;
    //     if (fft_len > n_samples) return -5; 

        
    //     if (n_samples % fft_len == 0) {
    //         double freq_max1 = 0.0; 
    //         double freq_max2 = 0.0; 
    //         for (size_t start = 0; start + fft_len <= n_samples; start += fft_len) {
    //             double complex* fft_sig1 = (double complex*) calloc(fft_len, sizeof(double complex));
    //             double complex* fft_sig2 = (double complex*) calloc(fft_len, sizeof(double complex));

    //             if (!fft_sig1 || !fft_sig2) return -6;

    //             int status1 = fft_Cooley_Tukey(&sig1[start], fft_len, fft_sig1);
    //             int status2 = fft_Cooley_Tukey(&sig2[start], fft_len, fft_sig2);

    //             double freq_tmp1 = fft_bin_to_freq(samp_freq, fft_len, fft_find_argmax(fft_sig1, fft_len));
    //             double freq_tmp2 = fft_bin_to_freq(samp_freq, fft_len, fft_find_argmax(fft_sig2, fft_len));

    //             freq_max1 = get_max(freq_max1, freq_tmp1);
    //             freq_max2 = get_max(freq_max2, freq_tmp2);
    //         }
    //     } else {
    //         size_t remainder = n_samples % fft_len;
    //         size_t full_bins = (size_t) (n_samples - remainder) / (size_t) fft_len;

    //     }


    // }
    if (!sig_dc) {
        if (fft_len == 0 || (fft_len & (fft_len - 1)) != 0) return -4;
        if (fft_len > n_samples) return -5;

        double* real = (double*) calloc(n_samples, sizeof(double));
        // double* real2 = (double*) calloc(n_samples, sizeof(double));

        if (!real ) {
            free(real);
            free(iq_data);
            free(sig);
            return -6;
        }

        size_t usable = n_samples - (n_samples % fft_len);

        for (size_t start = 0; start + fft_len <= usable; start += fft_len) {
            int s = process_fft_block(sig, start, fft_len, (double)samp_freq, real);
            // int s2 = process_fft_block(sig2, start, fft_len, (double)samp_freq, real2);

            if (s != 0) {
                free(real);
                free(iq_data);
                free(sig);
                return -7;
            }
        }

        for (size_t i = 0; i < usable; i++) {
            out[i].ts = iq_data[i].ts;
            out[i].sig = real[i];
            // out[i].sig2 = real2[i];
        }

        free(real);
        free(iq_data);
        free(sig);

        return 0;
    }


    double complex rot = cexp(-I * carg(mean / (double) n_samples));
    // double complex rot2 = cexp(-I * carg(mean2 / (double) n_samples));

    for (size_t i = 0; i < n_samples; i++) {
        out[i].sig = creal(sig[i] * rot);
        // out[i].sig2 = creal(sig2[i] * rot2);
    }

    free(iq_data);
    free(sig);
    return 0;
}

int process_to_dc_window(const adc_sample_t* packets, size_t n_samples, uint64_t start_ts, uint64_t end_ts, rotated_sample_t* out, size_t* n_out, uint64_t samp_freq, int channel, size_t fft_len) {
    if (!packets || !out || !n_out) return -1;
    *n_out = 0;

    size_t count = 0;

    for (size_t i = 0; i < n_samples; i++) {
        uint64_t ts = packets[i].ts;

        if (ts >= start_ts && ts < end_ts) {
            count++;
        }
    }

    if (count == 0) return -4;

    adc_sample_t* window_packets = (adc_sample_t*) calloc(count, sizeof(adc_sample_t));

    if (!window_packets) return -5;

    size_t j = 0;

    for (size_t i = 0; i < n_samples; i++) {
        uint64_t ts = packets[i].ts;

        if (ts >= start_ts && ts < end_ts) {
            window_packets[j] = packets[i];
            j++;
        }
    }

    int status = process_to_dc(
        window_packets,
        count,
        out,
        samp_freq,
        channel,
        fft_len
    );

    if (status != 0) {
        free(window_packets);
        return -6;
    }

    if (fft_len > 0 && count >= fft_len && (fft_len & (fft_len - 1)) == 0) {
        *n_out = count - (count % fft_len);
    } else {
        *n_out = count;
    }

    free(window_packets);
    return 0;
}