#!/bin/bash
# setup_nic.sh
#
# Configure the Mellanox ConnectX-5 for a lossless 40G point-to-point UDP
# receive from the RFSoC4x2. Idempotent — safe to re-run.
#
# Usage:
#   sudo ./setup_nic.sh [IFACE] [LOCAL_IP] [REMOTE_IP] [REMOTE_MAC]
#
# Defaults match the point-to-point plan we agreed on:
#   IFACE       enp4s0f1np1
#   LOCAL_IP    192.168.100.2
#   REMOTE_IP   192.168.100.1  (RFSoC side)
#   REMOTE_MAC  02:00:00:00:00:01
#
# The remote MAC / IP must match what PYNQ writes into the FPGA's
# cfg_src_mac / cfg_src_ip registers. Update both sides together.

set -euo pipefail

IFACE="${1:-enp4s0f1np1}"
LOCAL_IP="${2:-192.168.100.2}"
REMOTE_IP="${3:-192.168.100.1}"
REMOTE_MAC="${4:-02:00:00:00:00:01}"
NETMASK="24"
MTU="9000"

if [[ $EUID -ne 0 ]]; then
    echo "must run as root (sudo)" >&2
    exit 1
fi

echo "=== configuring $IFACE ==="
echo "    local  = $LOCAL_IP/$NETMASK"
echo "    remote = $REMOTE_IP ($REMOTE_MAC)"

# --- IP addressing ------------------------------------------------------
ip addr flush dev "$IFACE" || true
ip link set "$IFACE" down
ip link set "$IFACE" mtu "$MTU"
ip addr add "${LOCAL_IP}/${NETMASK}" dev "$IFACE"
ip link set "$IFACE" up

# --- Ethernet flow control (802.3x PAUSE) -------------------------------
# Symmetric PAUSE lets the FPGA back-pressure the FPGA -> host direction
# if the NIC RX ring fills. Required for lossless behavior at line rate.
ethtool -A "$IFACE" rx on tx on || true

# --- Speed / autoneg ----------------------------------------------------
# Let the NIC autonegotiate with the FPGA to 40G. If negotiation
# refuses to settle, uncomment the forced-speed lines below. Note that
# ConnectX-5 sometimes needs `ethtool -s ... autoneg off` after a forced
# speed to actually latch it.
#
# ethtool -s "$IFACE" speed 40000 duplex full autoneg off || true

# --- RX ring size -------------------------------------------------------
# Larger ring = more headroom for interrupt-service jitter. 8192 is the
# ConnectX-5 max.
ethtool -G "$IFACE" rx 8192 || true

# --- Static ARP for the point-to-point link -----------------------------
# The FPGA never sends ARP; the server needs a manual entry to know where
# to send if you ever want to originate traffic toward it. Not required
# for pure RX, but harmless and useful for pings/tests.
arp -s "$REMOTE_IP" "$REMOTE_MAC"

# --- Kernel network buffers ---------------------------------------------
# Big enough to absorb ~1 second of RX at 5 GB/s worst-case burst.
sysctl -w net.core.rmem_max=536870912          >/dev/null
sysctl -w net.core.rmem_default=268435456      >/dev/null
sysctl -w net.core.netdev_max_backlog=500000   >/dev/null
sysctl -w net.ipv4.udp_rmem_min=8192           >/dev/null
sysctl -w net.ipv4.udp_mem="1048576 8388608 33554432" >/dev/null

# --- IRQ affinity (best-effort) -----------------------------------------
# Concentrate the NIC's MSI-X vectors on cores 2..15. The IRQ list comes
# from sysfs (works for any PCI NIC regardless of naming in /proc/interrupts).
# The whole block is best-effort; any failure is non-fatal.
IRQ_DIR="/sys/class/net/${IFACE}/device/msi_irqs"
IRQS=""
if [[ -d "$IRQ_DIR" ]]; then
    IRQS=$(ls "$IRQ_DIR" 2>/dev/null || true)
fi
if [[ -n "$IRQS" ]]; then
    CPU=2
    for irq in $IRQS; do
        printf "%x" $((1 << CPU)) > "/proc/irq/${irq}/smp_affinity" 2>/dev/null || true
        CPU=$((CPU + 1))
        [[ $CPU -gt 15 ]] && CPU=2
    done
    echo "    IRQs pinned to cores 2..15 ($(echo $IRQS | wc -w) IRQs)"
else
    echo "    no MSI-X IRQs found via sysfs, skipping affinity"
fi

# --- Optional: disable IRQ coalescing tuning ----------------------------
# Default coalescing is usually fine on ConnectX-5. If you see high
# latency jitter, try:
# ethtool -C "$IFACE" adaptive-rx off rx-usecs 8 rx-frames 32 || true

# --- Verify --------------------------------------------------------------
echo
echo "=== link state ==="
sleep 1
ethtool "$IFACE" | grep -E "Speed|Link detected|Duplex"
echo
echo "=== address ==="
ip -brief addr show "$IFACE"
echo
echo "=== ring ==="
ethtool -g "$IFACE" | grep -E "RX:|Current"
echo
echo "=== pause ==="
ethtool -a "$IFACE" | grep -E "RX|TX"
echo
echo "setup complete."
