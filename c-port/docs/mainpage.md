# MOLLER RFSoC C Analysis

This project is a C port of the Python RFSoC analysis pipeline used for
precision beam-monitor studies.

## Processing pipeline

1. Read raw DMA packets.
2. Verify the packet magic word.
3. Decode the packet timestamp.
4. Unpack eight signed 24-bit ADC streams.
5. Combine I/Q streams into four complex channels.
6. Rotate each selected channel to DC.
7. Decode helicity gate timestamps.
8. Integrate samples inside helicity windows.
9. Compute RDF and DDF observables.

## Main modules

- `io`: DMA and gate parsing.
- `iq_rot`: IQ construction and rotation to DC.
- `integrator`: helicity-window integration.
- `ddf`: calculation of relative differences.