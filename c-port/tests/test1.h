#ifndef TEST1_H
#define TEST1_H

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int dump_first_samples_csv(const char *raw_path, size_t words_per_dma_packet, size_t n_print);

#endif