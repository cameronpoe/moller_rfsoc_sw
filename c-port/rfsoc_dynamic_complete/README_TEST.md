# PS-DDR to C shared-library test

## Complete files to add to `c-port`

- all files under `include/` and `src/`
- `Makefile.shared`
- `python_test_cell.py`

The included `rfsoc_ring.*` and `rfsoc_stream.*` are the previously prepared
ring modules.

## Implemented processing path

`rfsoc_process_capture_ex()` validates the DMA packet layout and then runs:

1. `adc_packet_parser_v2()` with reconstructed timestamps;
2. sequential rising/falling gate pairing;
3. channel 2 and channel 3 DC conversion in parallel;
4. channel 2 and channel 3 integration in parallel;
5. non-overlapping even/odd RDF pairs and DDF resolution;
6. per-stage timings returned through `rfsoc_processing_result_t`.

The input is zero-copy: Python passes the CPU virtual addresses of the PYNQ
PS-DDR buffers. C owns only temporary parsed/processed arrays and frees them
before returning. The ring and stream modules are included for the next step,
where DMA acquisition and processing overlap across several PS-DDR blocks.

## Build on rfsoc1

```sh
cd /home/xilinx/daniil/moller_rfsoc_sw/c-port
make -f Makefile.shared clean all
```

Expected output:

```text
build/librfsoc_processing.so
```

## Test in Jupyter

Run the PS-DDR acquisition through both DMA `wait()` calls. Then execute:

```python
%run -i /home/xilinx/daniil/moller_rfsoc_sw/c-port/python_test_cell.py
```

For the supplied 200-trigger capture, inspection should report:

```text
ADC words:          4872192
ADC bytes:          38977536
packets:            39
samples/packet:     41642
total samples:      1624038
gate words:         400
```

The pointers passed through ctypes are CPU virtual pointers. The corresponding
PYNQ physical address remains owned by the DMA configuration. No ADC or gate
array is copied before entering C.
