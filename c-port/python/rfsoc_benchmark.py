"""ctypes wrapper and timing benchmark for librfsoc_processing.so.

Run this file from Jupyter with:
    %run ./rfsoc_benchmark.py

Then call benchmark_rfsoc(ol.buffer, ol.buffer_gate, ...).
"""

from __future__ import annotations

import ctypes
import os
import platform
import statistics
import time
from pathlib import Path
from typing import Any

import numpy as np


U64_PTR = ctypes.POINTER(ctypes.c_uint64)


def _u64_view(buffer: Any, name: str) -> np.ndarray:
    """Return a zero-copy, flat, contiguous uint64 view of a NumPy/PYNQ buffer."""
    array = np.asarray(buffer)
    if array.dtype != np.uint64:
        raise TypeError(f"{name}.dtype must be uint64, got {array.dtype}")
    if not array.flags.c_contiguous:
        raise ValueError(f"{name} must be C-contiguous; refusing to make a timed copy")
    if not array.flags.aligned:
        raise ValueError(f"{name} is not suitably aligned")
    if array.size == 0:
        raise ValueError(f"{name} is empty")
    return array.reshape(-1)


def load_library(path: str | os.PathLike = "./build/librfsoc_processing.so"):
    """Load the shared library and declare the exact C ABI."""
    library_path = Path(path).expanduser().resolve()
    if not library_path.is_file():
        raise FileNotFoundError(f"Shared library not found: {library_path}")

    lib = ctypes.CDLL(str(library_path))
    lib.process_dma_buffer.argtypes = [
        U64_PTR,              # adc_words
        ctypes.c_size_t,      # n_adc_words
        U64_PTR,              # gate_words
        ctypes.c_size_t,      # n_gate_words
        ctypes.c_size_t,      # n_words_per_packet
        ctypes.c_size_t,      # fft_len
        ctypes.c_bool,        # fft_enabled
    ]
    lib.process_dma_buffer.restype = ctypes.c_int
    return lib


def benchmark_rfsoc(
    adc_buffer: Any,
    gate_buffer: Any,
    *,
    library_path: str | os.PathLike = "./build/librfsoc_processing.so",
    words_per_packet: int = 124_928,
    fft_len: int = 4_096,
    fft_enabled: bool = False,
    repeats: int = 1,
    max_packets: int | None = None,
) -> dict[str, Any]:
    """Time C processing of already-filled DDR4 buffers, without copying.

    max_packets is intended for the first smoke test. Set it to None for the
    full 8-second acquisition. Gate data are currently passed in full because
    gate words are timestamp-selected by the C analysis.
    """
    if words_per_packet < 5 or (words_per_packet - 2) % 3:
        raise ValueError("words_per_packet must equal 2 + 3*N")
    if repeats < 1:
        raise ValueError("repeats must be at least 1")
    if max_packets is not None and max_packets < 1:
        raise ValueError("max_packets must be positive or None")

    adc = _u64_view(adc_buffer, "adc_buffer")
    gate = _u64_view(gate_buffer, "gate_buffer")

    complete_packets = adc.size // words_per_packet
    if complete_packets == 0:
        raise ValueError("ADC buffer does not contain one complete DMA packet")
    packets_used = complete_packets
    if max_packets is not None:
        packets_used = min(packets_used, max_packets)
    adc_words_used = packets_used * words_per_packet

    # For PYNQ buffers, make CPU reads observe the completed DMA writes.
    for buffer in (adc_buffer, gate_buffer):
        invalidate = getattr(buffer, "invalidate", None)
        if callable(invalidate):
            invalidate()

    lib = load_library(library_path)
    adc_ptr = adc.ctypes.data_as(U64_PTR)
    gate_ptr = gate.ctypes.data_as(U64_PTR)

    elapsed = []
    return_codes = []
    for _ in range(repeats):
        started = time.perf_counter()
        rc = lib.process_dma_buffer(
            adc_ptr,
            adc_words_used,
            gate_ptr,
            gate.size,
            words_per_packet,
            fft_len,
            fft_enabled,
        )
        elapsed.append(time.perf_counter() - started)
        return_codes.append(rc)
        if rc != 0:
            raise RuntimeError(
                f"process_dma_buffer returned {rc}; see the C stderr output above"
            )

    summary = {
        "machine": platform.machine(),
        "adc_virtual_address": hex(adc.ctypes.data),
        "gate_virtual_address": hex(gate.ctypes.data),
        "packets_used": packets_used,
        "adc_words_used": adc_words_used,
        "adc_gib_used": adc_words_used * 8 / 1024**3,
        "gate_words_used": int(gate.size),
        "fft_enabled": bool(fft_enabled),
        "repeats": repeats,
        "seconds_each": elapsed,
        "seconds_min": min(elapsed),
        "seconds_median": statistics.median(elapsed),
        "return_codes": return_codes,
    }

    print("RFSoC C benchmark")
    print(f"  architecture:    {summary['machine']}")
    print(f"  DMA packets:     {packets_used:,}")
    print(f"  ADC input:       {summary['adc_gib_used']:.3f} GiB")
    print(f"  gate words:      {gate.size:,}")
    print(f"  FFT enabled:     {bool(fft_enabled)}")
    print(f"  times:           {[round(x, 6) for x in elapsed]} s")
    print(f"  best / median:   {min(elapsed):.6f} / {statistics.median(elapsed):.6f} s")
    return summary

