#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "rfsoc_types.h"
#include "io.h"
#include "test3.h"




int dump_gate_parser_csv(const char *gate_path, const char *events_csv, const char *windows_csv) {
    if (!gate_path || !events_csv || !windows_csv) return -1;

    FILE *fp = fopen(gate_path, "rb");
    if (!fp) {
        perror("fopen gate_path");
        return -2;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    if (file_size < 0 || file_size % sizeof(uint64_t) != 0) {
        fclose(fp);
        return -3;
    }

    size_t n_words = (size_t)file_size / sizeof(uint64_t);

    uint64_t *raw = calloc(n_words, sizeof(uint64_t));
    if (!raw) {
        fclose(fp);
        return -4;
    }

    size_t n_read = fread(raw, sizeof(uint64_t), n_words, fp);
    fclose(fp);

    if (n_read != n_words) {
        free(raw);
        return -5;
    }

    size_t n_nonzero = 0;
    for (size_t i = 0; i < n_words; i++) {
        if (raw[i] != 0) n_nonzero++;
    }

    uint64_t *nonzero = calloc(n_nonzero, sizeof(uint64_t));
    gate_event_t *events = calloc(n_nonzero, sizeof(gate_event_t));

    if (!nonzero || !events) {
        free(raw);
        free(nonzero);
        free(events);
        return -6;
    }

    size_t j = 0;
    for (size_t i = 0; i < n_words; i++) {
        if (raw[i] != 0) {
            nonzero[j++] = raw[i];
        }
    }

    int status = gate_word_parser(nonzero, n_nonzero, events);
    if (status != 0) {
        free(raw);
        free(nonzero);
        free(events);
        return -7;
    }

    FILE *ev_out = fopen(events_csv, "w");
    if (!ev_out) {
        perror("fopen events_csv");
        free(raw);
        free(nonzero);
        free(events);
        return -8;
    }

    fprintf(ev_out, "idx,raw_word,msb,edge_name,ts_ticks,time_sec\n");

    for (size_t i = 0; i < n_nonzero; i++) {
        int msb = events[i].edge ? 1 : 0;

        const char *edge_name = events[i].edge ? "falling" : "rising";

        fprintf(ev_out, "%zu,%llu,%d,%s,%llu,%.17g\n",
                i,
                (unsigned long long)nonzero[i],
                msb,
                edge_name,
                (unsigned long long)events[i].ts,
                (double)events[i].ts / ACLK_FREQ);
    }

    fclose(ev_out);

    /*
        Match PI convention:

        rising_edges = all MSB=0
        falling_edges = all MSB=1 with MSB removed
        edge_times = np.array([rising_edges, falling_edges]).T

        So pair rising[k] with falling[k].
    */

    size_t n_rising = 0;
    size_t n_falling = 0;

    for (size_t i = 0; i < n_nonzero; i++) {
        if (events[i].edge == false) {
            n_rising++;
        } else {
            n_falling++;
        }
    }

    size_t n_windows = n_rising < n_falling ? n_rising : n_falling;

    FILE *win_out = fopen(windows_csv, "w");
    if (!win_out) {
        perror("fopen windows_csv");
        free(raw);
        free(nonzero);
        free(events);
        return -9;
    }

    fprintf(win_out, "idx,rising_ticks,falling_ticks,rising_sec,falling_sec,width_ticks,width_sec\n");

    size_t r = 0;
    size_t f = 0;

    uint64_t *rising = calloc(n_rising, sizeof(uint64_t));
    uint64_t *falling = calloc(n_falling, sizeof(uint64_t));

    if (!rising || !falling) {
        fclose(win_out);
        free(raw);
        free(nonzero);
        free(events);
        free(rising);
        free(falling);
        return -10;
    }

    for (size_t i = 0; i < n_nonzero; i++) {
        if (events[i].edge == false) {
            rising[r++] = events[i].ts;
        } else {
            falling[f++] = events[i].ts;
        }
    }

    for (size_t i = 0; i < n_windows; i++) {
        uint64_t width_ticks = falling[i] - rising[i];

        fprintf(win_out, "%zu,%llu,%llu,%.17g,%.17g,%llu,%.17g\n",
                i,
                (unsigned long long)rising[i],
                (unsigned long long)falling[i],
                (double)rising[i] / ACLK_FREQ,
                (double)falling[i] / ACLK_FREQ,
                (unsigned long long)width_ticks,
                (double)width_ticks / ACLK_FREQ);
    }

    fclose(win_out);

    printf("gate words total: %zu\n", n_words);
    printf("gate words nonzero: %zu\n", n_nonzero);
    printf("rising: %zu\n", n_rising);
    printf("falling: %zu\n", n_falling);
    printf("windows: %zu\n", n_windows);

    free(raw);
    free(nonzero);
    free(events);
    free(rising);
    free(falling);

    return 0;
}