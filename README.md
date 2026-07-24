# MOLLER RFSoC Software

This repo contains software meant to interface with the Berkeley RFSoC-based receiver for MOLLER BCM readout. 

## Moving data from the RFSoC to the server

The easiest way to get data off the RFSoC with the current v1.0.X firmware is by streaming to a TCP socket on the `moller` server. To do this, one first sets up the server to receive by running `receive_data.py` (use the `-h` flag to figure out options). The server will then wait until it receives data on `localhost`/`127.0.0.1` on a specific port. To link the two localhosts, we need a tunnel. I find it nicer to have the server being the one to set up the tunnel, so that means we need a reverse tunnel. This is because TCP ports only allow one listener, and `receive_data.py` is the listener on the server's port. The default `-L` flag to SSH creates a listener on the client (in this case the server) port, wherease `-R` puts the listener on the remote port. Since a TCP port can have multiple connectors (things streaming data to it), this is okay. Next, let's say we have a PYNQ buffer that we want to stream to a binary file on the server. One would import the function via `from rfsoc_moller import stream_tcp`. Then, with `receive_data.py` running on the server and the SSH tunnel up, just run that function with the argument the buffer you want to stream. 

## `rfsoc_moller`

Contains online Python code for use in driving the firmware via PYNQ.

### Updating the bitstream

When a new bitstream file is created, one can update the version the `mollerOverlay` uses by moving the bitstream and hardware handoff files into `~/site-packages/rfsoc_moller/`. Make sure they are named `moller.bit` and `moller.hwh`. 

### Updating the Python package

When new updates are pushed to the `rfsoc_moller` subdirectory, it is necessary to update the Python package. 

First, make sure you update the version of the code in the `__init__.py`. Next, you may still need to update the `__init__.py` file depending on if you added a new function/class and how you want to call it. If you added a new function in `moller.py`, for example, but you want to be able to do `from rfsoc_moller import new_function`, then you need to add `new_function` from the `import...` line in `__init__.py`, then add that function name to the list at the bottom. Otherwise, you would need to do `from rfsoc_moller.moller import new_function`. Same goes for classes, etc. 

Then, from the root directory, run `sudo pip install .`. Make sure your `pip` is the PYNQ virtual environment pip, so running `which pip` should give `/usr/local/share/pynq-venv/bin/pip` (since it is in `/usr`, this is why we must use `sudo` with `pip`). After that, the package should be updated for use.

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
