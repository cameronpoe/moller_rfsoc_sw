#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rfsoc_types.h"
#include "io.h"
#include "iq_rot.h"
#include "integrator.h"

#define SAMP_FREQ 3072000000ULL
#define DMA_PACKETS_PER_BLOCK 50U
#define RAW_BUFFER_COUNT 2U

typedef enum {
    RAW_EMPTY = 0,
    RAW_FULL
} raw_buffer_state_t;

typedef struct {
    uint64_t *data;
    size_t words_read;
    raw_buffer_state_t state;
} raw_buffer_t;

typedef struct {
    FILE *fp;
    raw_buffer_t buffers[RAW_BUFFER_COUNT];
    size_t block_words;
    pthread_mutex_t mutex;
    pthread_cond_t can_read;
    pthread_cond_t can_process;
    bool eof;
    bool stop;
    int error_number;
    double read_seconds;
} reader_context_t;

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

static double elapsed_seconds(
    const struct timespec *start,
    const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           1e-9 * (double)(end->tv_nsec - start->tv_nsec);
}

static int parse_positive_size(const char *text, size_t *value)
{
    if (!text || !value || *text == '\0' || *text == '-') {
        return -1;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);

    if (errno == ERANGE || *end != '\0' || parsed == 0 ||
        parsed > SIZE_MAX) {
        return -1;
    }

    *value = (size_t)parsed;
    return 0;
}

static int parse_bool(const char *text, bool *value)
{
    if (!text || !value) {
        return -1;
    }

    if (strcmp(text, "true") == 0 || strcmp(text, "True") == 0) {
        *value = true;
        return 0;
    }

    if (strcmp(text, "false") == 0 || strcmp(text, "False") == 0) {
        *value = false;
        return 0;
    }

    return -1;
}

static int read_u64_file(
    const char *path,
    uint64_t **words,
    size_t *n_words)
{
    if (!path || !words || !n_words) {
        return -1;
    }

    *words = NULL;
    *n_words = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Could not open %s: %s\n", path, strerror(errno));
        return -2;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -3;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return -4;
    }
    rewind(fp);

    if ((size_t)file_size % sizeof(uint64_t) != 0) {
        fprintf(stderr, "File size is not divisible by 8 bytes: %s\n", path);
        fclose(fp);
        return -5;
    }

    size_t count = (size_t)file_size / sizeof(uint64_t);
    uint64_t *buffer = calloc(count, sizeof(*buffer));
    if (!buffer && count != 0) {
        fclose(fp);
        return -6;
    }

    size_t n_read = fread(buffer, sizeof(*buffer), count, fp);
    int read_failed = ferror(fp);
    fclose(fp);

    if (read_failed || n_read != count) {
        fprintf(stderr, "Expected %zu words from %s, read %zu\n",
                count, path, n_read);
        free(buffer);
        return -7;
    }

    *words = buffer;
    *n_words = count;
    return 0;
}

static int remove_zero_gate_words(
    const uint64_t *raw,
    size_t n_raw,
    uint64_t **nonzero,
    size_t *n_nonzero)
{
    if (!raw || !nonzero || !n_nonzero) {
        return -1;
    }

    *nonzero = NULL;
    *n_nonzero = 0;

    size_t count = 0;
    for (size_t i = 0; i < n_raw; i++) {
        if (raw[i] != 0) {
            count++;
        }
    }

    uint64_t *filtered = calloc(count, sizeof(*filtered));
    if (!filtered && count != 0) {
        return -2;
    }

    size_t j = 0;
    for (size_t i = 0; i < n_raw; i++) {
        if (raw[i] != 0) {
            filtered[j++] = raw[i];
        }
    }

    *nonzero = filtered;
    *n_nonzero = count;
    return 0;
}

static int build_gate_windows(
    const gate_event_t *events,
    size_t n_events,
    gate_window_t **windows,
    size_t *n_windows)
{
    if (!events || !windows || !n_windows) {
        return -1;
    }

    *windows = NULL;
    *n_windows = 0;

    size_t n_rising = 0;
    size_t n_falling = 0;
    for (size_t i = 0; i < n_events; i++) {
        if (events[i].edge) {
            n_falling++;
        } else {
            n_rising++;
        }
    }

    uint64_t *rising = calloc(n_rising, sizeof(*rising));
    uint64_t *falling = calloc(n_falling, sizeof(*falling));
    if ((!rising && n_rising != 0) || (!falling && n_falling != 0)) {
        free(rising);
        free(falling);
        return -2;
    }

    size_t r = 0;
    size_t f = 0;
    for (size_t i = 0; i < n_events; i++) {
        if (events[i].edge) {
            falling[f++] = events[i].ts;
        } else {
            rising[r++] = events[i].ts;
        }
    }

    size_t max_pairs = n_rising < n_falling ? n_rising : n_falling;
    gate_window_t *result = calloc(max_pairs, sizeof(*result));
    if (!result && max_pairs != 0) {
        free(rising);
        free(falling);
        return -3;
    }

    size_t valid = 0;
    for (size_t i = 0; i < max_pairs; i++) {
        if (falling[i] <= rising[i]) {
            fprintf(stderr,
                    "Skipping invalid gate pair %zu: rising=%" PRIu64
                    ", falling=%" PRIu64 "\n",
                    i, rising[i], falling[i]);
            continue;
        }

        if (valid > 0 && rising[i] < result[valid - 1].end_ts) {
            fprintf(stderr, "Gate windows overlap or are not sorted at %zu\n", i);
            free(rising);
            free(falling);
            free(result);
            return -4;
        }

        result[valid].start_ts = rising[i];
        result[valid].end_ts = falling[i];
        valid++;
    }

    free(rising);
    free(falling);
    *windows = result;
    *n_windows = valid;
    return 0;
}

static void *dc_worker(void *argument)
{
    dc_job_t *job = argument;
    job->status = process_to_dc(
        job->samples,
        job->n_samples,
        job->out,
        &job->n_out,
        SAMP_FREQ,
        job->channel,
        job->fft_enabled,
        job->fft_len);
    return NULL;
}

/*
 * Fill the two raw buffers in strict sequence.  The processing thread returns
 * a slot to RAW_EMPTY only after it has finished parsing that slot, so fread()
 * can never overwrite data still in use.
 */
static void *reader_worker(void *argument)
{
    reader_context_t *reader = argument;
    size_t slot = 0;

    for (;;) {
        pthread_mutex_lock(&reader->mutex);
        while (!reader->stop &&
               reader->buffers[slot].state != RAW_EMPTY) {
            pthread_cond_wait(&reader->can_read, &reader->mutex);
        }
        bool should_stop = reader->stop;
        pthread_mutex_unlock(&reader->mutex);

        if (should_stop) {
            break;
        }

        struct timespec start;
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        size_t words_read = fread(
            reader->buffers[slot].data,
            sizeof(*reader->buffers[slot].data),
            reader->block_words,
            reader->fp);
        clock_gettime(CLOCK_MONOTONIC, &end);

        pthread_mutex_lock(&reader->mutex);
        reader->read_seconds += elapsed_seconds(&start, &end);

        if (words_read == 0) {
            if (ferror(reader->fp)) {
                reader->error_number = errno != 0 ? errno : EIO;
            }
            reader->eof = true;
            pthread_cond_broadcast(&reader->can_process);
            pthread_mutex_unlock(&reader->mutex);
            break;
        }

        reader->buffers[slot].words_read = words_read;
        reader->buffers[slot].state = RAW_FULL;

        /*
         * A short fread is the final block.  Publish it first, then mark EOF;
         * the processing thread will still consume this slot.
         */
        if (words_read < reader->block_words) {
            if (ferror(reader->fp)) {
                reader->error_number = errno != 0 ? errno : EIO;
            }
            reader->eof = true;
        }

        pthread_cond_broadcast(&reader->can_process);
        bool reached_end = reader->eof;
        pthread_mutex_unlock(&reader->mutex);

        if (reached_end) {
            break;
        }
        slot = (slot + 1U) % RAW_BUFFER_COUNT;
    }

    return NULL;
}

/*
 * Accumulate both channels in a single pass. window_index survives between
 * ADC blocks, so a gate window may begin in one block and end in the next.
 */
static int accumulate_block(
    const rotated_sample_t *ch2,
    const rotated_sample_t *ch3,
    size_t n_samples,
    const gate_window_t *windows,
    size_t n_windows,
    integration_result_t *int_ch2,
    integration_result_t *int_ch3,
    size_t *window_index)
{
    if (!ch2 || !ch3 || !windows || !int_ch2 || !int_ch3 ||
        !window_index) {
        return -1;
    }

    size_t w = *window_index;

    for (size_t i = 0; i < n_samples && w < n_windows; i++) {
        if (ch2[i].ts != ch3[i].ts) {
            return -2;
        }

        uint64_t ts = ch2[i].ts;

        while (w < n_windows && ts >= windows[w].end_ts) {
            w++;
        }
        if (w == n_windows) {
            break;
        }

        if (ts >= windows[w].start_ts) {
            int_ch2[w].sum += ch2[i].sig;
            int_ch2[w].n_samples++;
            int_ch3[w].sum += ch3[i].sig;
            int_ch3[w].n_samples++;
        }
    }

    *window_index = w;
    return 0;
}

static int write_csv(
    const char *path,
    const gate_window_t *windows,
    size_t n_windows,
    integration_result_t *ch2,
    integration_result_t *ch3,
    size_t *pairs_written,
    size_t *pairs_skipped)
{
    FILE *csv = fopen(path, "w");
    if (!csv) {
        fprintf(stderr, "Could not open output CSV %s: %s\n",
                path, strerror(errno));
        return -1;
    }

    fprintf(csv,
            "pair_index,window_even_index,window_odd_index,"
            "even_start_ts,even_end_ts,odd_start_ts,odd_end_ts,"
            "mean_ch2_even,mean_ch2_odd,mean_ch3_even,mean_ch3_odd,"
            "n_ch2_even,n_ch2_odd,n_ch3_even,n_ch3_odd,"
            "rdf_ch2,rdf_ch3,ddf\n");

    *pairs_written = 0;
    *pairs_skipped = 0;

    for (size_t even = 0; even + 1 < n_windows; even += 2) {
        size_t odd = even + 1;

        if (ch2[even].n_samples == 0 || ch2[odd].n_samples == 0 ||
            ch3[even].n_samples == 0 || ch3[odd].n_samples == 0) {
            (*pairs_skipped)++;
            continue;
        }

        ch2[even].mean = ch2[even].sum / (double)ch2[even].n_samples;
        ch2[odd].mean = ch2[odd].sum / (double)ch2[odd].n_samples;
        ch3[even].mean = ch3[even].sum / (double)ch3[even].n_samples;
        ch3[odd].mean = ch3[odd].sum / (double)ch3[odd].n_samples;

        double den2 = ch2[even].mean + ch2[odd].mean;
        double den3 = ch3[even].mean + ch3[odd].mean;
        if (den2 == 0.0 || den3 == 0.0) {
            (*pairs_skipped)++;
            continue;
        }

        double rdf2 = (ch2[even].mean - ch2[odd].mean) / den2;
        double rdf3 = (ch3[even].mean - ch3[odd].mean) / den3;
        double ddf = rdf2 - rdf3;

        fprintf(csv,
                "%zu,%zu,%zu,"
                "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
                "%.17g,%.17g,%.17g,%.17g,"
                "%zu,%zu,%zu,%zu,%.17g,%.17g,%.17g\n",
                *pairs_written, even, odd,
                windows[even].start_ts, windows[even].end_ts,
                windows[odd].start_ts, windows[odd].end_ts,
                ch2[even].mean, ch2[odd].mean,
                ch3[even].mean, ch3[odd].mean,
                ch2[even].n_samples, ch2[odd].n_samples,
                ch3[even].n_samples, ch3[odd].n_samples,
                rdf2, rdf3, ddf);
        (*pairs_written)++;
    }

    if (fclose(csv) != 0) {
        fprintf(stderr, "Failed to close output CSV %s\n", path);
        return -2;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr,
                "Usage:\n"
                "  %s ADC_FILE GATE_FILE OUTPUT_CSV "
                "WORDS_PER_PACKET FFT_LEN FFT_ON\n\n"
                "Example:\n"
                "  %s tests/data tests/data_gate results/out.csv "
                "124928 4096 false\n",
                argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    const char *adc_path = argv[1];
    const char *gate_path = argv[2];
    const char *csv_path = argv[3];

    size_t words_per_packet = 0;
    size_t fft_len = 0;
    bool fft_enabled = false;

    if (parse_positive_size(argv[4], &words_per_packet) != 0) {
        fprintf(stderr, "Invalid WORDS_PER_PACKET: %s\n", argv[4]);
        return EXIT_FAILURE;
    }
    if (parse_positive_size(argv[5], &fft_len) != 0) {
        fprintf(stderr, "Invalid FFT_LEN: %s\n", argv[5]);
        return EXIT_FAILURE;
    }
    if (parse_bool(argv[6], &fft_enabled) != 0) {
        fprintf(stderr,
                "Invalid FFT_ON: %s (expected true, True, false, or False)\n",
                argv[6]);
        return EXIT_FAILURE;
    }
    if (words_per_packet < 5 || (words_per_packet - 2) % 3 != 0) {
        fprintf(stderr, "Invalid WORDS_PER_PACKET=%zu\n", words_per_packet);
        return EXIT_FAILURE;
    }
    if (fft_len == 0 || (fft_len & (fft_len - 1)) != 0) {
        fprintf(stderr, "FFT_LEN must be a nonzero power of two.\n");
        return EXIT_FAILURE;
    }
    if (words_per_packet > SIZE_MAX / DMA_PACKETS_PER_BLOCK) {
        fprintf(stderr, "ADC block size overflows size_t.\n");
        return EXIT_FAILURE;
    }

    int status = -1;
    FILE *adc = NULL;
    reader_context_t reader;
    memset(&reader, 0, sizeof(reader));
    pthread_t reader_thread;
    bool reader_mutex_initialized = false;
    bool reader_can_read_initialized = false;
    bool reader_can_process_initialized = false;
    bool reader_thread_started = false;
    uint64_t *raw_gate = NULL;
    uint64_t *nonzero_gate = NULL;
    gate_event_t *gate_events = NULL;
    gate_window_t *windows = NULL;
    adc_sample_t *samples = NULL;
    rotated_sample_t *dc_ch2 = NULL;
    rotated_sample_t *dc_ch3 = NULL;
    integration_result_t *int_ch2 = NULL;
    integration_result_t *int_ch3 = NULL;

    size_t n_gate_words = 0;
    size_t n_nonzero_gate = 0;
    size_t n_windows = 0;
    size_t window_index = 0;
    size_t total_packets = 0;
    size_t total_samples = 0;
    size_t block_number = 0;

    struct timespec total_start;
    struct timespec total_end;
    struct timespec gate_start;
    struct timespec gate_end;
    double read_seconds = 0.0;
    double parse_seconds = 0.0;
    double dc_seconds = 0.0;
    double integrate_seconds = 0.0;

    clock_gettime(CLOCK_MONOTONIC, &total_start);

    /* Gate data is small enough to decode once before streaming ADC data. */
    clock_gettime(CLOCK_MONOTONIC, &gate_start);
    status = read_u64_file(gate_path, &raw_gate, &n_gate_words);
    if (status != 0) {
        fprintf(stderr, "Failed to read gate file: %d\n", status);
        goto cleanup;
    }
    status = remove_zero_gate_words(
        raw_gate, n_gate_words, &nonzero_gate, &n_nonzero_gate);
    if (status != 0 || n_nonzero_gate == 0) {
        fprintf(stderr, "Gate file contains no usable events.\n");
        goto cleanup;
    }

    gate_events = calloc(n_nonzero_gate, sizeof(*gate_events));
    if (!gate_events) {
        goto cleanup;
    }
    status = gate_word_parser(nonzero_gate, n_nonzero_gate, gate_events);
    if (status != 0) {
        fprintf(stderr, "gate_word_parser failed: %d\n", status);
        goto cleanup;
    }
    status = build_gate_windows(
        gate_events, n_nonzero_gate, &windows, &n_windows);
    if (status != 0 || n_windows < 2) {
        fprintf(stderr, "At least two valid gate windows are required.\n");
        goto cleanup;
    }

    int_ch2 = calloc(n_windows, sizeof(*int_ch2));
    int_ch3 = calloc(n_windows, sizeof(*int_ch3));
    if (!int_ch2 || !int_ch3) {
        goto cleanup;
    }
    clock_gettime(CLOCK_MONOTONIC, &gate_end);

    const size_t block_words =
        DMA_PACKETS_PER_BLOCK * words_per_packet;
    const size_t samples_per_packet =
        (words_per_packet - 2) / 3;
    if (samples_per_packet > SIZE_MAX / DMA_PACKETS_PER_BLOCK) {
        fprintf(stderr, "Sample block size overflows size_t.\n");
        goto cleanup;
    }
    const size_t max_block_samples =
        DMA_PACKETS_PER_BLOCK * samples_per_packet;

    if (block_words > SIZE_MAX / sizeof(uint64_t) ||
        max_block_samples > SIZE_MAX / sizeof(*samples) ||
        max_block_samples > SIZE_MAX / sizeof(*dc_ch2)) {
        fprintf(stderr, "Block-buffer byte size overflows size_t.\n");
        goto cleanup;
    }

    for (size_t i = 0; i < RAW_BUFFER_COUNT; i++) {
        reader.buffers[i].data =
            malloc(block_words * sizeof(*reader.buffers[i].data));
        reader.buffers[i].state = RAW_EMPTY;
    }
    samples = malloc(max_block_samples * sizeof(*samples));
    dc_ch2 = malloc(max_block_samples * sizeof(*dc_ch2));
    dc_ch3 = malloc(max_block_samples * sizeof(*dc_ch3));
    if (!reader.buffers[0].data || !reader.buffers[1].data ||
        !samples || !dc_ch2 || !dc_ch3) {
        fprintf(stderr, "Failed to allocate block buffers.\n");
        goto cleanup;
    }

    adc = fopen(adc_path, "rb");
    if (!adc) {
        fprintf(stderr, "Could not open %s: %s\n", adc_path, strerror(errno));
        goto cleanup;
    }

    reader.fp = adc;
    reader.block_words = block_words;

    int thread_error = pthread_mutex_init(&reader.mutex, NULL);
    if (thread_error != 0) {
        fprintf(stderr, "pthread_mutex_init(reader): %s\n",
                strerror(thread_error));
        goto cleanup;
    }
    reader_mutex_initialized = true;

    thread_error = pthread_cond_init(&reader.can_read, NULL);
    if (thread_error != 0) {
        fprintf(stderr, "pthread_cond_init(can_read): %s\n",
                strerror(thread_error));
        goto cleanup;
    }
    reader_can_read_initialized = true;

    thread_error = pthread_cond_init(&reader.can_process, NULL);
    if (thread_error != 0) {
        fprintf(stderr, "pthread_cond_init(can_process): %s\n",
                strerror(thread_error));
        goto cleanup;
    }
    reader_can_process_initialized = true;

    thread_error = pthread_create(
        &reader_thread, NULL, reader_worker, &reader);
    if (thread_error != 0) {
        fprintf(stderr, "pthread_create(reader): %s\n",
                strerror(thread_error));
        goto cleanup;
    }
    reader_thread_started = true;

    size_t consume_slot = 0;
    for (;;) {
        struct timespec start;
        struct timespec end;

        pthread_mutex_lock(&reader.mutex);
        while (reader.buffers[consume_slot].state != RAW_FULL &&
               !reader.eof && reader.error_number == 0) {
            pthread_cond_wait(&reader.can_process, &reader.mutex);
        }

        if (reader.buffers[consume_slot].state != RAW_FULL) {
            int read_error = reader.error_number;
            pthread_mutex_unlock(&reader.mutex);
            if (read_error != 0) {
                fprintf(stderr, "Error while reading ADC file: %s\n",
                        strerror(read_error));
                goto cleanup;
            }
            break;
        }

        uint64_t *raw_block = reader.buffers[consume_slot].data;
        size_t words_read = reader.buffers[consume_slot].words_read;
        pthread_mutex_unlock(&reader.mutex);

        size_t complete_packets = words_read / words_per_packet;
        size_t complete_words = complete_packets * words_per_packet;
        if (complete_packets == 0) {
            fprintf(stderr, "ADC file ends with no complete DMA packet.\n");
            goto cleanup;
        }
        if (complete_words != words_read) {
            fprintf(stderr,
                    "Ignoring %zu trailing ADC words after the last "
                    "complete DMA packet.\n",
                    words_read - complete_words);
        }

        size_t n_samples = complete_packets * samples_per_packet;

        clock_gettime(CLOCK_MONOTONIC, &start);
        status = adc_packet_parser_v2(
            raw_block, complete_words, words_per_packet, samples);
        clock_gettime(CLOCK_MONOTONIC, &end);
        parse_seconds += elapsed_seconds(&start, &end);
        if (status != 0) {
            fprintf(stderr,
                    "adc_packet_parser_v2 failed in block %zu: %d\n",
                    block_number, status);
            goto cleanup;
        }

        dc_job_t job2 = {
            .samples = samples,
            .n_samples = n_samples,
            .out = dc_ch2,
            .n_out = 0,
            .channel = 2,
            .fft_enabled = fft_enabled,
            .fft_len = fft_len,
            .status = -1
        };
        dc_job_t job3 = {
            .samples = samples,
            .n_samples = n_samples,
            .out = dc_ch3,
            .n_out = 0,
            .channel = 3,
            .fft_enabled = fft_enabled,
            .fft_len = fft_len,
            .status = -1
        };

        pthread_t thread2;
        pthread_t thread3;
        bool thread2_started = false;
        bool thread3_started = false;

        clock_gettime(CLOCK_MONOTONIC, &start);
        thread_error = pthread_create(&thread2, NULL, dc_worker, &job2);
        if (thread_error == 0) {
            thread2_started = true;
        } else {
            fprintf(stderr, "pthread_create(channel 2): %s\n",
                    strerror(thread_error));
        }

        thread_error = pthread_create(&thread3, NULL, dc_worker, &job3);
        if (thread_error == 0) {
            thread3_started = true;
        } else {
            fprintf(stderr, "pthread_create(channel 3): %s\n",
                    strerror(thread_error));
        }

        int join2_error = 0;
        int join3_error = 0;
        if (thread2_started) {
            join2_error = pthread_join(thread2, NULL);
            if (join2_error != 0) {
                fprintf(stderr, "pthread_join(channel 2): %s\n",
                        strerror(join2_error));
            }
        }
        if (thread3_started) {
            join3_error = pthread_join(thread3, NULL);
            if (join3_error != 0) {
                fprintf(stderr, "pthread_join(channel 3): %s\n",
                        strerror(join3_error));
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        dc_seconds += elapsed_seconds(&start, &end);

        if (!thread2_started || !thread3_started ||
            join2_error != 0 || join3_error != 0) {
            goto cleanup;
        }
        if (job2.status != 0 || job3.status != 0) {
            fprintf(stderr,
                    "process_to_dc failed in block %zu: ch2=%d, ch3=%d\n",
                    block_number, job2.status, job3.status);
            goto cleanup;
        }
        if (job2.n_out != job3.n_out) {
            fprintf(stderr,
                    "DC output mismatch in block %zu: ch2=%zu, ch3=%zu\n",
                    block_number, job2.n_out, job3.n_out);
            goto cleanup;
        }

        clock_gettime(CLOCK_MONOTONIC, &start);
        status = accumulate_block(
            dc_ch2, dc_ch3, job2.n_out,
            windows, n_windows, int_ch2, int_ch3, &window_index);
        clock_gettime(CLOCK_MONOTONIC, &end);
        integrate_seconds += elapsed_seconds(&start, &end);
        if (status != 0) {
            fprintf(stderr, "Integration failed in block %zu: %d\n",
                    block_number, status);
            goto cleanup;
        }

        total_packets += complete_packets;
        total_samples += job2.n_out;
        block_number++;

        pthread_mutex_lock(&reader.mutex);
        reader.buffers[consume_slot].words_read = 0;
        reader.buffers[consume_slot].state = RAW_EMPTY;
        pthread_cond_signal(&reader.can_read);
        pthread_mutex_unlock(&reader.mutex);

        consume_slot = (consume_slot + 1U) % RAW_BUFFER_COUNT;
        if (words_read < block_words) {
            break;
        }
    }

    thread_error = pthread_join(reader_thread, NULL);
    reader_thread_started = false;
    if (thread_error != 0) {
        fprintf(stderr, "pthread_join(reader): %s\n",
                strerror(thread_error));
        goto cleanup;
    }
    read_seconds = reader.read_seconds;

    if (total_packets == 0) {
        fprintf(stderr, "ADC file contains no complete DMA packets.\n");
        goto cleanup;
    }

    size_t pairs_written = 0;
    size_t pairs_skipped = 0;
    status = write_csv(
        csv_path, windows, n_windows, int_ch2, int_ch3,
        &pairs_written, &pairs_skipped);
    if (status != 0) {
        goto cleanup;
    }

    clock_gettime(CLOCK_MONOTONIC, &total_end);
    printf("Raw gate words:       %zu\n", n_gate_words);
    printf("Nonzero gate events:  %zu\n", n_nonzero_gate);
    printf("Valid gate windows:   %zu\n", n_windows);
    printf("ADC blocks:           %zu\n", block_number);
    printf("DMA packets:          %zu\n", total_packets);
    printf("DC output samples:    %zu\n", total_samples);
    printf("Gate setup time:      %.9f sec\n",
           elapsed_seconds(&gate_start, &gate_end));
    printf("ADC read time:        %.9f sec\n", read_seconds);
    printf("Parser time:          %.9f sec\n", parse_seconds);
    printf("Parallel DC time:     %.9f sec\n", dc_seconds);
    printf("Integration time:     %.9f sec\n", integrate_seconds);
    printf("Total time:           %.9f sec\n",
           elapsed_seconds(&total_start, &total_end));
    printf("CSV pairs written:    %zu\n", pairs_written);
    printf("CSV pairs skipped:    %zu\n", pairs_skipped);
    printf("Output CSV:           %s\n", csv_path);

    status = 0;

cleanup:
    if (reader_thread_started) {
        pthread_mutex_lock(&reader.mutex);
        reader.stop = true;
        pthread_cond_broadcast(&reader.can_read);
        pthread_cond_broadcast(&reader.can_process);
        pthread_mutex_unlock(&reader.mutex);
        int join_error = pthread_join(reader_thread, NULL);
        if (join_error != 0) {
            fprintf(stderr, "pthread_join(reader): %s\n",
                    strerror(join_error));
        }
    }
    if (adc) {
        fclose(adc);
    }
    if (reader_can_process_initialized) {
        pthread_cond_destroy(&reader.can_process);
    }
    if (reader_can_read_initialized) {
        pthread_cond_destroy(&reader.can_read);
    }
    if (reader_mutex_initialized) {
        pthread_mutex_destroy(&reader.mutex);
    }
    for (size_t i = 0; i < RAW_BUFFER_COUNT; i++) {
        free(reader.buffers[i].data);
    }
    free(raw_gate);
    free(nonzero_gate);
    free(gate_events);
    free(windows);
    free(samples);
    free(dc_ch2);
    free(dc_ch3);
    free(int_ch2);
    free(int_ch3);

    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
