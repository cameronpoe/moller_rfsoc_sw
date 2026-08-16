# qsfp_rx — Möller 40G UDP capture

Server-side receiver for the RFSoC4x2 continuous ADC readout over 40G QSFP28.
Point-to-point link, no switch, ~3 Gbps average, ~375 MB/s sustained.

## Files

| File | Purpose |
|---|---|
| `moller_recv.c` | Receiver: batched `recvmmsg`, SPSC ring, writer thread, seq-gap detection. |
| `setup_nic.sh`  | One-shot NIC config: IP, MTU 9000, PAUSE, large buffers, static ARP, IRQ pinning. |
| `Makefile`      | Build / debug / install / NIC-check helpers. |

## Prerequisites

- Linux, kernel ≥ 3.0 (needs `recvmmsg`); tested on Ubuntu 22.04 / kernel 6.8.
- Mellanox ConnectX-5 (any card with mlx5_core is fine).
- `gcc` (or `clang`), `ethtool`, `iproute2`, `sysctl`, root for setup.

## Build

```
make            # release  (-O2)
make debug      # -O0 -g   for gdb
make sanitize   # -O1 -g   plus ASan+UBSan (slow, catches memory bugs)
make clean
sudo make install PREFIX=/usr/local
```

## Deploy on the server

```
sudo make setup IFACE=enp4s0f1np1        # or edit setup_nic.sh defaults
make check IFACE=enp4s0f1np1             # verify link/mtu/pause/ring/drops
```

Defaults built into `setup_nic.sh`:
- Interface: `enp4s0f1np1`   (ConnectX-5 port 2 at PCI 04:00.1)
- Local IP:  `192.168.100.2/24`
- Remote IP: `192.168.100.1`   (FPGA)
- Remote MAC: `02:00:00:00:00:01`
- MTU: 9000, RX ring: 8192, RX socket buffer: 512 MiB, symmetric PAUSE on.

Override any of them: `sudo ./setup_nic.sh <iface> <local_ip> <remote_ip> <remote_mac>`.

## Run

```
./moller_recv \
    --bind 192.168.100.2 \
    --port 54321 \
    --out  /data/capture.bin \
    --meta /data/capture.meta \
    --duration 300
```

Options:

| Flag | Purpose |
|---|---|
| `--bind ADDR`      | Local IP to bind. |
| `--port N`         | UDP port (must match FPGA config). |
| `--out PATH`       | Raw capture, appended packet-by-packet. |
| `--meta PATH`      | Sidecar text log for anomalies + start/stop stats. |
| `--duration SEC`   | 0 = run forever, else stop after N seconds. |

Exit code `0` on clean run, `2` if any gap / drop / bad-length observed.
Per-second stats print to stdout: pkts/s, MB/s, gaps, dropped, ring_full, backlog.

## Wire format (`--out`)

Each record = one UDP payload = 38-byte app header + 8192-byte ADC payload
(total 8230 bytes). App header in network byte order:

```
offset  size  field           notes
------  ----  --------------  ---------------------------------
 0       8    seq             monotonic, one per packet
 8       8    adc_ts          free-running 64b counter at 312.5 MHz
 16      4    payload_len     always 8192
 20      4    flags           reserved (0)
 24     14    reserved        zero pad (14 bytes)
 38   8192    adc_payload     adc_packetizer output, timestamp+samples
```

The 8192-byte ADC payload starts with `adc_packetizer`'s own timestamp header
words followed by ADC I/Q samples — same layout as the legacy DDR4/DMA capture,
so existing offline tools work unchanged.

## Meta file (`--meta`)

Line-oriented, one event per line:

```
ts=<host_time>  event=START  local=<ip:port>  remote=<ip:port>
ts=<host_time>  event=GAP    expected=<seq>  observed=<seq>  lost=<N>
ts=<host_time>  event=RING_FULL  bytes_dropped=<N>
ts=<host_time>  event=BAD_LEN    observed_len=<N>  expected=8230
ts=<host_time>  event=STOP   pkts=<N>  bytes=<N>  duration=<s>  gaps=<N>
```

## Troubleshooting

**`Link detected: no` after `setup_nic.sh`:**
- FPGA bit loaded and driving idle? (Phase 1 stub with `s_axis_tx_tvalid=0`.)
- Cable fully seated in both cages (twist-lock clicked)?
- `ethtool enp4s0f1np1 | grep Speed` should show `40000Mb/s` once link comes up.

**Seq gaps observed:**
- `ethtool -S enp4s0f1np1 | grep -iE 'drop|out_of_buffer'` — NIC-side counters.
- `nstat -a | grep -i udp` — kernel UDP drops (usually socket buffer overflow).
- Check FPGA framer's `status_pkt_count` vs receiver's count — where did they diverge?

**`RING_FULL` messages:**
- Writer thread can't keep up with disk. `iostat -x 1` — NVMe should sustain ≥400 MB/s.
- Verify `--out` is on the NVMe (not a network mount, not `/tmp` on tmpfs).

**Wrong interface:**
- `sudo lshw -class network -short` — list all NICs and their PCI paths.
- ConnectX-5 shows as `MT27800 Family`, driver `mlx5_core`.
