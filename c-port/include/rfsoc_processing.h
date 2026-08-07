#ifndef RFSOC_PROCESSING_H
#define RFSOC_PROCESSING_H

#include "rfsoc_types.h"
#include "io.h"
#include "iq_rot.h"
#include "integrator.h"

#define SAMP_FREQ 3072000000ULL
#define DMA_PACKETS_PER_BLOCK 50U
#define RAW_BUFFER_COUNT 2U

typedef struct {
    const adc_sample_t *samples;
    size_t n_samples;
    rotated_sample_t *out;
    size_t n_out;
    int channel; 
    bool fft_enabled; 
    size_t fft_len;
    int status;
} dc_job_t;

typedef struct {
    const rotated_sample_t *data;
    size_t n_samples;
    const gate_window_t *windows;
    size_t n_windows;
    integration_result_t *out;
    int status;
} integration_job_t;

static double standard_deviation(
    const double *values,
    size_t n_values)
{
    if (!values || n_values == 0) {
        return NAN;
    }

    /*
     * Welford algorithm:
     * numerically stable calculation of mean and variance.
     */
    double mean = 0.0;
    double m2 = 0.0;

    for (size_t i = 0; i < n_values; i++) {
        double delta = values[i] - mean;

        mean += delta / (double)(i + 1);

        double delta_from_new_mean = values[i] - mean;
        m2 += delta * delta_from_new_mean;
    }

    /*
     * Division by n matches np.std(values), whose default ddof is 0.
     */
    double variance = m2 / (double)n_values;

    return sqrt(variance);
}

static void *integration_worker(void *arg)
{
    integration_job_t *job = arg;

    job->status = integrate_windows_sorted(
        job->data,
        job->n_samples,
        job->windows,
        job->n_windows,
        job->out
    );

    return NULL;
}

static void *dc_worker(void *args) {
    dc_job_t *job = args;
    job->status = process_to_dc(
        job->samples,
        job->n_samples,
        job->out, 
        &job->n_out, 
        SAMP_FREQ,
        job->channel, 
        job->fft_enabled, 
        job->fft_len
    );

    return NULL;
}

/**
 * @brief 
 * 
 * @param words 
 * @param n_words 
 * @param output 
 * @param n_output 
 * @return int 
 */
static int zero_padding(
    const uint64_t* words, 
    size_t n_words, 
    uint64_t** output, 
    size_t* n_output
) {
    if (!words || n_words == 0 || !output || !n_output) return -1; 

    *n_output = 0;
    *output = NULL; 

    size_t count = 0; 
    for (size_t i = 0; i < n_words; i++) {
        if (words[i] != 0) count++; 
    }

    uint64_t *nonzero_words = calloc(count, sizeof(*nonzero_words)); 
    if (!nonzero_words && count != 0) {
        free(nonzero_words);
        return -2;
    }
    size_t word_ind = 0; 
    for (size_t i = 0; i < n_words; i++) {
        if (words[i] != 0) {
            nonzero_words[word_ind++] = words[i];
        }
    }

    *n_output = count;
    *output = nonzero_words;

    return 0;
}

/**
 * @brief 
 * 
 * @return int 
 */
static int build_gate_windows(
    gate_event_t* gate_events, 
    size_t n_events,
    gate_window_t** gate_windows, 
    size_t* n_windows
) {
    if (!gate_events || !gate_windows || !n_windows) return -1;

    size_t rising_cnt = 0;
    size_t falling_cnt = 0; 
    for (size_t i = 0; i < n_events; i++) {
        if (gate_events[i].edge) {
            falling_cnt++;
        } else {
            rising_cnt++;
        }
    }

    uint64_t *falling_events = calloc(falling_cnt, sizeof(*falling_events));
    uint64_t* rising_events = calloc(rising_cnt, sizeof(*rising_events));
    if ((!falling_cnt && falling_cnt != 0) || (!rising_cnt && rising_cnt != 0)) {
        free(falling_events);
        free(rising_events);
        return -2;
    }

    size_t falling_ind = 0;
    size_t rising_ind = 0;
    for (size_t i = 0; i < n_events; i++) {
        if (gate_events[i].edge) {
            falling_events[falling_ind++] = gate_events[i].ts;
        } else {
            rising_events[rising_ind++] = gate_events[i].ts;
        }
    }

    size_t n_valid_pairs = falling_cnt < rising_cnt ? falling_cnt : rising_cnt;
    gate_window_t* result = calloc(n_valid_pairs, sizeof(*result));
    if (!result && n_valid_pairs != 0) {
        free(falling_events);
        free(rising_events);
        free(result); 
        return -3; 
    }

    size_t valid_pair_ind = 0;
    for (size_t i = 0; i < n_valid_pairs; i++) {
        if (falling_events[i] <= rising_events[i]) {
            fprintf(stderr,
             "Skipping invalid gate pair %zu: rising=%" PRIu64 
             ", falling=%" PRIu64 "\n", i, rising_events[i], falling_events[i]);
            continue;
        }

        if (valid_pair_ind > 0 && rising_events[i] < result[valid_pair_ind - 1].end_ts) {
            fprintf(stderr, 
            "Gate windows overlap at %zu \n", i);
            free(falling_events);
            free(rising_events);
            free(result);
            return -4;
        }

        result[valid_pair_ind].start_ts = rising_events[i];
        result[valid_pair_ind].end_ts = falling_events[i];
        valid_pair_ind++; 
    }

    *gate_windows = result;
    *n_windows = valid_pair_ind;
    free(falling_events);
    free(rising_events);
    return 0;
}

// static int accumulate_block(
//     const rotated_sample_t *ch2,
//     const rotated_sample_t *ch3,
//     size_t n_samples,
//     const gate_window_t *windows,
//     size_t n_windows,
//     integration_result_t *int_ch2,
//     integration_result_t *int_ch3,
//     size_t *window_index)
// {
//     if (!ch2 || !ch3 || !windows || !int_ch2 || !int_ch3 ||
//         !window_index) {
//         return -1;
//     }

//     size_t w = *window_index;

//     for (size_t i = 0; i < n_samples && w < n_windows; i++) {
//         if (ch2[i].ts != ch3[i].ts) {
//             return -2;
//         }

//         uint64_t ts = ch2[i].ts;

//         while (w < n_windows && ts >= windows[w].end_ts) {
//             w++;
//         }
//         if (w == n_windows) {
//             break;
//         }

//         if (ts >= windows[w].start_ts) {
//             int_ch2[w].sum += ch2[i].sig;
//             int_ch2[w].n_samples++;
//             int_ch3[w].sum += ch3[i].sig;
//             int_ch3[w].n_samples++;
//         }
//     }

//     *window_index = w;
//     return 0;
// }



/**
 * @brief 
 * 
 * @param adc_words 
 * @param n_adc_words 
 * @param gate_words 
 * @param n_gate_words 
 * @param n_words_per_packet 
 * @param fft_len 
 * @param fft_enabled 
 * @return int 
 */
int process_dma_buffer(
    const uint64_t* adc_words,
    size_t n_adc_words,
    const uint64_t* gate_words, 
    size_t n_gate_words,
    size_t n_words_per_packet, 
    size_t fft_len,
    bool fft_enabled
);

#endif //RFSOC_PROCESSING_H