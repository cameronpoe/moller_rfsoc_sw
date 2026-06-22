# MOLLER RFSoC Software

This repo contains software meant to interface with the Berkeley RFSoC-based receiver for MOLLER BCM readout. 

## `rfsoc_moller`

Contains online Python code for use in driving the firmware via PYNQ.

## `rfsoc_moller_c`

Contains online C code for use in analyzing data, specifically doing the I/Q phase rotation and window integration. 

## `arduino_code`

Contains Arduino code to interface with the digital step attenuators (DSAs). 

The receiver box uses an Arduino Uno to set the DSA levels. In the box, there are four ZX76-31R5A-SNS+ DSAs. Each can do a maximum of 31.5 dB attenuation in increments of 0.5 dB. Each DSA receives three signals, clock, data, and a latch. The data signal is fed into a serial-in, parallel-out 6-bit shift register. The bits correspond to [16, 8, 4, 2, 1, 0.5] dB attenuation, and the MSB is fed in first. The latch signal flashes to lock in the values. The four DSAs share the clock and data signals, but each gets its own latch for individual control. The mapping is:

| Signal | Arduino Physical Pin | Receiver Channel |
| --- | --- | --- |
`clk` (shared) | RX | n/a
`data` (shared) | TX | n/a
`LE[0]` (latch) | 2 | TBD
`LE[1]` (latch) | ~3 | TBD
`LE[2]` (latch) | 4 | TBD
`LE[3]` (latch) | ~5 | TBD

## `offline_analysis`
