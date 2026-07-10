#ifndef RFSOC_TYPES_H    
#define RFSOC_TYPES_H    

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>
#include <complex.h>
#include <string.h>

// typedef struct {
//     uint32_t ch0 : 24;
//     uint32_t ch1 : 24;
//     uint32_t ch2 : 24;
//     uint32_t ch3 : 24;
//     uint32_t ch4 : 24;
//     uint32_t ch5 : 24;
//     uint32_t ch6 : 24;
//     uint32_t ch7 : 24;
// } __attribute__((packed)) adc_frame_t;
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAGIC_WORD 0xFFFFFFFFFFFFFFFFULL
#define TIME_INC 8
#define REL_LIM 1e-3
#define PHASE_LIM 1e-3

typedef struct {
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
} adc24_t;

typedef struct {
    adc24_t ch0;
    adc24_t ch1;
    adc24_t ch2;
    adc24_t ch3;
    adc24_t ch4;
    adc24_t ch5;
    adc24_t ch6;
    adc24_t ch7;
} adc_frame_t;


// typedef struct {
//     double complex sig1;
//     double complex sig2;
// } pair_t;


typedef struct {
    bool edge;  // falling = true, rising = false 
    uint64_t ts; 
} gate_event_t;

typedef struct {
    uint64_t ts; 
    adc_frame_t data;
} adc_sample_t;

typedef struct { 
    uint64_t start_ts; 
    uint64_t end_ts; 
} gate_window_t; 

typedef struct {
    double sig; 
    uint64_t ts; 
} rotated_sample_t;

typedef struct {
    double sum;
    double mean;
    size_t n_samples;
} integration_result_t;

typedef struct {
    double complex sig;
    uint64_t ts;
} timestamped_iq_t;

typedef struct {
    double rdf1;
    double rdf2;
    double ddf;
} ddf_result_t;

static inline int get_even(double complex* arr, size_t n, double complex* out) {
    for (size_t i = 0; 2 * i < n; i++) {
        out[i] = arr[2 * i];
    }

    return 0;
}

static inline int get_odd(double complex* arr, size_t n, double complex* out) {
    for (size_t i = 0; 2 * i + 1 < n; i++) {
        out[i] = arr[2 * i + 1];
    }

    return 0;
}

static inline int fft_Cooley_Tukey(double complex* arr, size_t fft_len, double complex* out) {
    if (!arr || !out) return -1;

    if (fft_len == 0 || (fft_len & (fft_len - 1)) != 0) return -2;

    if (fft_len == 1) {
        out[0] = arr[0];
        return 0;
    }

    size_t half = fft_len / 2;

    double complex* even = (double complex*) calloc(half, sizeof(double complex));
    double complex* odd  = (double complex*) calloc(half, sizeof(double complex));
    double complex* even_fft = (double complex*) calloc(half, sizeof(double complex));
    double complex* odd_fft =(double complex*) calloc(half, sizeof(double complex));

    if (!odd || !even || !even_fft || !odd_fft) {
        free(odd);
        free(even);
        free(even_fft);
        free(odd_fft);
        return -2;
    }

    int status1 = get_odd(arr, fft_len, odd);
    int status2 = get_even(arr, fft_len, even);

    if ((status1 != 0) || (status2 != 0)) {
        free(odd);
        free(even);
        free(even_fft);
        free(odd_fft);
        return -3;
    }

    int status3 = fft_Cooley_Tukey(even, half, even_fft);
    int status4 = fft_Cooley_Tukey(odd, half, odd_fft);

    if (status3 != 0 || status4 != 0) {
        free(even);
        free(odd);
        free(even_fft);
        free(odd_fft);
        return -4;
    }

    for (size_t k = 0; k < half; k++) {
        double angle = -2.0 * M_PI * k / fft_len;
        double complex w = cos(angle) + I * sin(angle);

        out[k] = even_fft[k] + w * odd_fft[k];
        out[k + half] = even_fft[k] - w * odd_fft[k];
    }

    free(odd); 
    free(even);
    free(even_fft);
    free(odd_fft);

    return 0;
}


static inline double fft_bin_to_freq(double sampl_freq, size_t n, size_t k) {
    if (k <= n / 2) {
        return (double) k * sampl_freq / (double) n;
    } else {
        return -((double) (n - k) * sampl_freq) / (double) n;
    }
}

static inline size_t fft_find_argmax(double complex* out, size_t n) {
    double max = cabs(out[0]);
    size_t best_ind = 0;

    if (!out || n == 0) return 0;

    for (size_t i = 1; i < n; i++) {
        double tmp = cabs(out[i]);

        if (tmp > max) {
            max = tmp;
            best_ind = i;
        }
    }

    return best_ind;
}

static inline int process_fft_block(const double complex* sig, size_t start, size_t fft_len, double samp_freq, double *out) {
    if (!sig || !out) return -1; 
    double complex* fft_out = calloc(fft_len, sizeof(double complex));
    double complex* dc = calloc(fft_len, sizeof(double complex));

    if (!fft_out || !dc) {
        free(fft_out);
        free(dc);
        return -2;
    }

    int status = fft_Cooley_Tukey((double complex *)&sig[start], fft_len, fft_out);

    if (status != 0) {
        free(fft_out);
        free(dc);
        return -2;
    }

    size_t kmax = fft_find_argmax(fft_out, fft_len);
    double freq = fft_bin_to_freq(samp_freq, fft_len, kmax);

    double carrier_phase = carg(fft_out[kmax]);

    printf(
        "C DEBUG: start=%zu fft_len=%zu kmax=%zu carrier_freq=%.17g carrier_phase=%.17g\n",
        start,
        fft_len,
        kmax,
        freq,
        carrier_phase
    );

    // double carrier_mag = cabs(fft_out[kmax]);

    // printf(
    //     "C DEBUG: start=%zu fft_len=%zu kmax=%zu freq=%.17g phase=%.17g mag=%.17g\n",
    //     start,
    //     fft_len,
    //     kmax,
    //     freq,
    //     carrier_phase,
    //     carrier_mag
    // );
    
    for (size_t i = 0; i < fft_len; i++) {
        double phase = 2.0 * M_PI * freq * (double)i / samp_freq;
        dc[i] = sig[start + i] * cexp(-I * phase);
    }

    double complex mean = 0.0 + 0.0 * I;
    for (size_t i = 0; i < fft_len; i++) {
        mean += dc[i];
    }
    mean /= (double)fft_len;

    double complex rot = cexp(-I * carg(mean));

    for (size_t j = 0; j < fft_len; j++) {
        out[start + j] = creal(dc[j] * rot);
    }

    
    free(fft_out);
    free(dc);

    return 0;

}

static inline size_t get_max(size_t a, size_t b) {
    return (a > b) ? a : b; 
}

static inline adc24_t make_adc24(uint8_t b0, uint8_t b1, uint8_t b2) {
    adc24_t x;
    x.b0 = b0;
    x.b1 = b1;
    x.b2 = b2;
    return x;
}


static inline int32_t adc24_to_int32(adc24_t x) {
    uint32_t raw =
        ((uint32_t)x.b0 << 16) |
        ((uint32_t)x.b1 << 8)  |
        ((uint32_t)x.b2);

    return ((int32_t)(raw << 8)) >> 8;
}

static inline const adc24_t* get_adc24_channel(const adc_frame_t* frames, int stream) {
    switch (stream){
        case 0: return &frames->ch0;
        case 1: return &frames->ch1;
        case 2: return &frames->ch2;
        case 3: return &frames->ch3;
        case 4: return &frames->ch4;
        case 5: return &frames->ch5;
        case 6: return &frames->ch6; 
        case 7: return &frames->ch7;
    
        default: 
            return NULL;
    }
}



#endif // RFSOC_TYPES_H
