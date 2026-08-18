/*
 * moller_recv.c
 *
 * MOLLER RFSoC continuous readout receiver.
 *
 * Point-to-point UDP receiver for the RFSoC 40G stream. Uses recvmmsg for
 * batched receive on the main thread and a SPSC ring buffer feeding a writer
 * thread that flushes to disk. Detects seq-number gaps and logs them.
 *
 * Wire format (matches udp_framer.v):
 *   Each UDP payload is exactly 8230 bytes:
 *     bytes  0..7   : uint64  seq          (network byte order)
 *     bytes  8..15  : uint64  adc_ts       (network byte order)
 *     bytes 16..19  : uint32  payload_len  (network byte order; == 8192)
 *     bytes 20..23  : uint32  flags        (network byte order)
 *     bytes 24..37  : 14 bytes zero pad
 *     bytes 38..8229: 8192 bytes of ADC payload
 *
 * Disk output: on-wire UDP payload is written verbatim (headers + payload)
 * so the file is self-describing and a lost packet can be replaced with a
 * zero-filled block of the same size. A separate .meta file records the run
 * parameters, first/last seq/ts, and any seq gaps observed.
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -pthread -std=gnu11 moller_recv.c -o moller_recv
 *
 * Example:
 *   ./moller_recv --bind 192.168.100.2 --port 54321 \
 *                 --out /nvme/run_001.bin --duration 300
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <endian.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ------------------------------------------------------------------ */
/* Wire format constants (must match udp_framer.v)                    */
/* ------------------------------------------------------------------ */
#define APP_HDR_REAL_SIZE   24      /* seq(8) + ts(8) + len(4) + flags(4) */
#define APP_HDR_PAD_SIZE    14      /* zero pad to align payload           */
#define APP_HDR_ON_WIRE     (APP_HDR_REAL_SIZE + APP_HDR_PAD_SIZE)   /* 38 */
#define PAYLOAD_SIZE        8192
#define UDP_PAYLOAD_SIZE    (APP_HDR_ON_WIRE + PAYLOAD_SIZE)         /* 8230 */

/* ------------------------------------------------------------------ */
/* Tuning                                                             */
/* ------------------------------------------------------------------ */
#define RECVMMSG_BATCH      64
#define SOCK_RCVBUF_BYTES   (256 * 1024 * 1024)   /* 256 MiB */
#define RING_SIZE_BYTES     (512UL * 1024 * 1024) /* 512 MiB */
#define WRITE_CHUNK_BYTES   (1UL * 1024 * 1024)   /* 1 MiB */

/* ------------------------------------------------------------------ */
/* Ring buffer (single-producer / single-consumer)                    */
/* Producer writes ring_head; consumer writes ring_tail. All indices  */
/* are byte offsets into ring_buf modulo RING_SIZE_BYTES.             */
/* ------------------------------------------------------------------ */
static uint8_t          *ring_buf;
static _Atomic uint64_t  ring_head = 0;
static _Atomic uint64_t  ring_tail = 0;

/* ------------------------------------------------------------------ */
/* Stats                                                              */
/* ------------------------------------------------------------------ */
static _Atomic uint64_t stat_packets    = 0;
static _Atomic uint64_t stat_bytes      = 0;
static _Atomic uint64_t stat_gaps       = 0;
static _Atomic uint64_t stat_gap_count  = 0;   /* total number of missing pkts */
static _Atomic uint64_t stat_ring_full  = 0;
static _Atomic uint64_t stat_bad_len    = 0;
static _Atomic uint64_t stat_written    = 0;

/* ------------------------------------------------------------------ */
/* Globals                                                            */
/* ------------------------------------------------------------------ */
static volatile sig_atomic_t stop_flag = 0;
static int  out_fd    = -1;
static FILE *meta_fp  = NULL;
static uint64_t first_seq = 0;
static uint64_t first_ts  = 0;
static int have_first     = 0;

static void handle_sig(int sig) { (void)sig; stop_flag = 1; }

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ */
/* Writer thread                                                      */
/* ------------------------------------------------------------------ */
static void *writer_thread(void *arg) {
    (void)arg;
    for (;;) {
        uint64_t head = atomic_load_explicit(&ring_head, memory_order_acquire);
        uint64_t tail = atomic_load_explicit(&ring_tail, memory_order_relaxed);
        uint64_t avail = head - tail;

        if (avail == 0) {
            if (stop_flag) break;
            /* No data; brief sleep to avoid a 100% spin. */
            struct timespec ts = { 0, 200 * 1000 };  /* 200us */
            nanosleep(&ts, NULL);
            continue;
        }

        uint64_t off = tail % RING_SIZE_BYTES;
        uint64_t contig = RING_SIZE_BYTES - off;
        uint64_t to_write = avail;
        if (to_write > WRITE_CHUNK_BYTES) to_write = WRITE_CHUNK_BYTES;
        if (to_write > contig)            to_write = contig;

        ssize_t n = write(out_fd, ring_buf + off, to_write);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "writer: write() failed: %s\n", strerror(errno));
            stop_flag = 1;
            break;
        }
        atomic_fetch_add_explicit(&ring_tail, (uint64_t)n, memory_order_release);
        atomic_fetch_add_explicit(&stat_written, (uint64_t)n, memory_order_relaxed);
    }

    /* Final drain — attempt to flush anything still in the ring. */
    for (;;) {
        uint64_t head = atomic_load_explicit(&ring_head, memory_order_acquire);
        uint64_t tail = atomic_load_explicit(&ring_tail, memory_order_relaxed);
        uint64_t avail = head - tail;
        if (avail == 0) break;
        uint64_t off = tail % RING_SIZE_BYTES;
        uint64_t contig = RING_SIZE_BYTES - off;
        uint64_t to_write = avail;
        if (to_write > WRITE_CHUNK_BYTES) to_write = WRITE_CHUNK_BYTES;
        if (to_write > contig)            to_write = contig;
        ssize_t n = write(out_fd, ring_buf + off, to_write);
        if (n <= 0) break;
        atomic_fetch_add_explicit(&ring_tail, (uint64_t)n, memory_order_release);
        atomic_fetch_add_explicit(&stat_written, (uint64_t)n, memory_order_relaxed);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --bind IP          Bind address       (default 0.0.0.0)\n"
        "  --port N           UDP port           (default 54321)\n"
        "  --out PATH         Output data file   (default moller.bin)\n"
        "  --meta PATH        Metadata sidecar   (default moller.meta)\n"
        "  --duration SEC     Run duration       (default 300)\n"
        "  --help\n", prog);
}

int main(int argc, char **argv) {
    const char *bind_ip   = "0.0.0.0";
    uint16_t    port      = 54321;
    const char *out_path  = "moller.bin";
    const char *meta_path = "moller.meta";
    double      duration  = 300.0;

    static struct option opts[] = {
        {"bind",     required_argument, 0, 'b'},
        {"port",     required_argument, 0, 'p'},
        {"out",      required_argument, 0, 'o'},
        {"meta",     required_argument, 0, 'm'},
        {"duration", required_argument, 0, 'd'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "b:p:o:m:d:h", opts, NULL)) != -1) {
        switch (c) {
            case 'b': bind_ip   = optarg; break;
            case 'p': port      = (uint16_t)atoi(optarg); break;
            case 'o': out_path  = optarg; break;
            case 'm': meta_path = optarg; break;
            case 'd': duration  = atof(optarg); break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 1;
        }
    }

    /* Ring buffer */
    ring_buf = aligned_alloc(4096, RING_SIZE_BYTES);
    if (!ring_buf) { perror("aligned_alloc ring"); return 1; }

    /* Output files */
    out_fd = open(out_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (out_fd < 0) { perror("open out"); return 1; }
    meta_fp = fopen(meta_path, "w");
    if (!meta_fp) { perror("fopen meta"); return 1; }
    setvbuf(meta_fp, NULL, _IOLBF, 0);

    fprintf(meta_fp, "start_wallclock=%ld\n", (long)time(NULL));
    fprintf(meta_fp, "bind=%s:%u\n", bind_ip, port);
    fprintf(meta_fp, "duration=%.3f\n", duration);
    fprintf(meta_fp, "udp_payload_size=%d\n", UDP_PAYLOAD_SIZE);
    fprintf(meta_fp, "adc_payload_size=%d\n", PAYLOAD_SIZE);

    /* Socket */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    int rcvbuf = SOCK_RCVBUF_BYTES;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf, sizeof(rcvbuf)) < 0) {
        /* SO_RCVBUFFORCE needs CAP_NET_ADMIN; fall back to SO_RCVBUF */
        if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
            perror("setsockopt SO_RCVBUF");
        }
    }
    int actual = 0;
    socklen_t alen = sizeof(actual);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &actual, &alen);
    fprintf(stderr, "SO_RCVBUF = %d bytes (requested %d)\n", actual, rcvbuf);
    fprintf(meta_fp, "so_rcvbuf=%d\n", actual);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "bad bind address: %s\n", bind_ip);
        return 1;
    }
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    /* Signal handling */
    signal(SIGINT,  handle_sig);
    signal(SIGTERM, handle_sig);

    /* Writer thread */
    pthread_t writer;
    if (pthread_create(&writer, NULL, writer_thread, NULL) != 0) {
        perror("pthread_create"); return 1;
    }

    /* recvmmsg staging */
    static uint8_t     bufs[RECVMMSG_BATCH][UDP_PAYLOAD_SIZE + 64];
    static struct iovec iovs[RECVMMSG_BATCH];
    static struct mmsghdr msgs[RECVMMSG_BATCH];
    for (int i = 0; i < RECVMMSG_BATCH; i++) {
        iovs[i].iov_base = bufs[i];
        iovs[i].iov_len  = sizeof(bufs[i]);
        memset(&msgs[i], 0, sizeof(msgs[i]));
        msgs[i].msg_hdr.msg_iov    = &iovs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
    }

    uint64_t expected_seq = 0;
    double t_start = now_sec();
    double t_last  = t_start;
    uint64_t last_bytes = 0, last_pkts = 0;

    while (!stop_flag) {
        int n = recvmmsg(sock, msgs, RECVMMSG_BATCH, MSG_WAITFORONE, NULL);
        if (n < 0) {
            if (errno == EINTR) {
                if (stop_flag) break;    /* Ctrl-C / SIGTERM during blocking recv */
                continue;
            }
            perror("recvmmsg");
            break;
        }

        for (int i = 0; i < n; i++) {
            uint32_t len = msgs[i].msg_len;
            if (len != (uint32_t)UDP_PAYLOAD_SIZE) {
                atomic_fetch_add(&stat_bad_len, 1);
                continue;
            }

            /* Parse app header (first 24 real bytes; skip pad). */
            uint64_t seq_n, ts_n;
            uint32_t plen_n, flags_n;
            memcpy(&seq_n,   bufs[i] + 0,  8);
            memcpy(&ts_n,    bufs[i] + 8,  8);
            memcpy(&plen_n,  bufs[i] + 16, 4);
            memcpy(&flags_n, bufs[i] + 20, 4);
            uint64_t seq   = be64toh(seq_n);
            uint64_t ts    = be64toh(ts_n);
            uint32_t plen  = be32toh(plen_n);
            uint32_t flags = be32toh(flags_n);
            (void)flags;

            if (!have_first) {
                first_seq = seq;
                first_ts  = ts;
                expected_seq = seq;
                have_first = 1;
                fprintf(stderr, "first packet: seq=%lu ts=%lu\n",
                        (unsigned long)seq, (unsigned long)ts);
                fprintf(meta_fp, "first_seq=%lu first_ts=%lu first_flags=0x%08x\n",
                        (unsigned long)seq, (unsigned long)ts, flags);
            }

            if (seq != expected_seq) {
                atomic_fetch_add(&stat_gaps, 1);
                int64_t delta = (int64_t)(seq - expected_seq);
                atomic_fetch_add(&stat_gap_count, (uint64_t)(delta > 0 ? delta : 0));
                fprintf(stderr, "SEQ GAP: expected %lu got %lu (delta %ld) ts=%lu\n",
                        (unsigned long)expected_seq, (unsigned long)seq,
                        (long)delta, (unsigned long)ts);
                fprintf(meta_fp, "gap expected=%lu got=%lu delta=%ld ts=%lu\n",
                        (unsigned long)expected_seq, (unsigned long)seq,
                        (long)delta, (unsigned long)ts);
                /* Resync to observed seq so subsequent packets don't re-trigger */
                expected_seq = seq;
            }
            expected_seq++;

            if (plen != (uint32_t)PAYLOAD_SIZE) {
                atomic_fetch_add(&stat_bad_len, 1);
                /* Still write it — the app header + payload go to disk verbatim. */
            }

            /* Enqueue the entire UDP payload (headers + pad + data) verbatim.
             * This keeps the on-disk record self-describing. */
            uint64_t head  = atomic_load_explicit(&ring_head, memory_order_relaxed);
            uint64_t tail  = atomic_load_explicit(&ring_tail, memory_order_acquire);
            uint64_t space = RING_SIZE_BYTES - (head - tail);
            if (space < len) {
                atomic_fetch_add(&stat_ring_full, 1);
                /* Drop this packet. Writer thread can't keep up. */
                continue;
            }
            uint64_t off = head % RING_SIZE_BYTES;
            uint64_t contig = RING_SIZE_BYTES - off;
            if (contig >= len) {
                memcpy(ring_buf + off, bufs[i], len);
            } else {
                memcpy(ring_buf + off, bufs[i], contig);
                memcpy(ring_buf, bufs[i] + contig, len - contig);
            }
            atomic_store_explicit(&ring_head, head + len, memory_order_release);

            atomic_fetch_add(&stat_packets, 1);
            atomic_fetch_add(&stat_bytes, len);
        }

        /* Stats every ~1 second */
        double now = now_sec();
        if (now - t_last >= 1.0) {
            uint64_t p = atomic_load(&stat_packets);
            uint64_t b = atomic_load(&stat_bytes);
            uint64_t g = atomic_load(&stat_gaps);
            uint64_t gc = atomic_load(&stat_gap_count);
            uint64_t rf = atomic_load(&stat_ring_full);
            uint64_t bl = atomic_load(&stat_bad_len);
            uint64_t w = atomic_load(&stat_written);
            double dt = now - t_last;
            fprintf(stderr,
                "[%6.1fs] pkts=%lu (+%lu/s) rx=%.1fMB (%.1fMB/s) "
                "gaps=%lu missed=%lu ring_full=%lu bad_len=%lu "
                "written=%.1fMB backlog=%.1fMB\n",
                now - t_start,
                (unsigned long)p, (unsigned long)((p - last_pkts) / dt),
                b / 1e6, (b - last_bytes) / 1e6 / dt,
                (unsigned long)g, (unsigned long)gc,
                (unsigned long)rf, (unsigned long)bl,
                w / 1e6, (b - w) / 1e6);
            last_pkts  = p;
            last_bytes = b;
            t_last     = now;
        }

        if ((now - t_start) >= duration) {
            fprintf(stderr, "duration reached, stopping\n");
            stop_flag = 1;
        }
    }

    /* Wait for writer to drain (up to a reasonable bound) */
    pthread_join(writer, NULL);

    /* Final summary */
    uint64_t p = atomic_load(&stat_packets);
    uint64_t b = atomic_load(&stat_bytes);
    uint64_t g = atomic_load(&stat_gaps);
    uint64_t gc = atomic_load(&stat_gap_count);
    uint64_t rf = atomic_load(&stat_ring_full);
    uint64_t bl = atomic_load(&stat_bad_len);
    uint64_t w = atomic_load(&stat_written);
    fprintf(stderr,
        "\nDONE: pkts=%lu bytes=%.2fGB gaps=%lu missed_pkts=%lu "
        "ring_full=%lu bad_len=%lu written=%.2fGB\n",
        (unsigned long)p, b / 1e9,
        (unsigned long)g, (unsigned long)gc,
        (unsigned long)rf, (unsigned long)bl, w / 1e9);
    fprintf(meta_fp,
        "final_packets=%lu final_bytes=%lu final_gaps=%lu final_missed=%lu "
        "final_ring_full=%lu final_bad_len=%lu final_written=%lu\n",
        (unsigned long)p, (unsigned long)b, (unsigned long)g, (unsigned long)gc,
        (unsigned long)rf, (unsigned long)bl, (unsigned long)w);
    fprintf(meta_fp, "end_wallclock=%ld\n", (long)time(NULL));

    close(out_fd);
    fclose(meta_fp);
    close(sock);
    free(ring_buf);
    /* Return non-zero if any gaps observed — CI/scripts can key off this. */
    return (g == 0 && rf == 0 && bl == 0) ? 0 : 2;
}
