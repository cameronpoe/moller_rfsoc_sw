import numpy as np
import matplotlib.pyplot as plt
import argparse
import yaml
from pyfftw.interfaces.numpy_fft import rfftfreq, rfft
from scipy.signal.windows import blackman

SAMPLE_RATE = 5e9
ADC_BITS = 14

INFO_DICT = {}

def read_data(file_path):

    raw = np.fromfile(file_path, dtype=np.uint8)
    raw = np.asarray(raw)                     # dtype=uint8

    n_samples = raw.size // 32
    assert raw.size % 32 == 0, "buffer isn't a whole number of 32-byte samples"

    chunks = raw.reshape(n_samples, 32)

    # real payload: low 24 bytes of each 32-byte chunk
    payload = chunks[:, :24]                      # (n_samples, 24)
    data = np.ascontiguousarray(payload).reshape(-1).view(np.int16)  # 1-D int16
    data = data >> 2 # RFDC packs 14 bit words into 16 bit words. The two LSBs are 0s. Have checked this is true, even with bugs in the 14 data bits.

    # discarded bytes: high 8 bytes of each chunk (should be all zeros)
    discarded = chunks[:, 24:]                    # (n_samples, 8)

    # checks
    all_zero = np.all(discarded == 0)
    print("discarded region all zeros:", all_zero)

    if not all_zero:
        bad_rows = np.where(np.any(discarded != 0, axis=1))[0]
        print(f"{bad_rows.size} of {n_samples} samples have nonzero padding")
        print("first few offending sample indices:", bad_rows[:10])
        print("example offending chunk padding bytes:", discarded[bad_rows[0]])

    return data

def amplitude_diagnostics(data, verbose=False):

    data_max = data.max()
    data_min = data.min()
    data_avg = data.mean()
    rms = np.sqrt(np.mean(data.astype(np.float64)**2))
    rms_hyp = 2**(ADC_BITS - 1) / np.sqrt(2)

    if verbose:
        print(f'Data max: {data_max}')
        print(f'Data min: {data_min}')
        print(f'Data average: {data_avg}')
        print(f'Data RMS value: {rms}')
        print(f'Hypothetical max RMS value: {rms_hyp}')

    INFO_DICT['Amplitude Diagnostics'] = {
        'Data max ADC code': data_max,
        'Data min ADC code': data_min,
        'Data avg ADC code': data_avg,
        'Data RMS value': rms,
        'Data hypothetical max RMS value': rms_hyp
    }

    return data_max, data_min, rms, rms_hyp

def amplitude_hist(data, save_path, verbose=False):

    bins = int(data.max()) - int(data.min()) + 1

    fig, ax = plt.subplots(figsize=(14,10))
    ax.hist(data, bins=bins)
    ax.set_ylabel('Number of samples per 1 ADC code bin', fontdict=dict(size=14))
    ax.set_xlabel('ADC code', fontdict=dict(size=14))
    ax.xaxis.set_ticks_position('both')
    ax.yaxis.set_ticks_position('both')
    ax.xaxis.minorticks_on()
    ax.yaxis.minorticks_on()
    fig.savefig(save_path + 'rfs_amplitude_hist.png')

    return

def compute_fft(data, save_path):

    freq_domain = rfftfreq(data.size, 1/SAMPLE_RATE)
    data_freq = rfft(blackman(data.size)*data, data.size)

    freq_spacing = freq_domain[1] - freq_domain[0]

    carrier_ind = np.abs(data_freq).argmax()
    carrier_power = np.abs(data_freq[carrier_ind])
    carrier_freq = freq_domain[carrier_ind]

    fig, ax = plt.subplots(figsize=(14,10))
    ax.plot(freq_domain*1e-6, 20*np.log10(np.abs(data_freq)/carrier_power))
    ax.set_ylabel('Signal power (dBc)', fontdict=dict(size=14))
    ax.set_xlabel('Frequency (MHz)', fontdict=dict(size=14))
    ax.xaxis.set_ticks_position('both')
    ax.yaxis.set_ticks_position('both')
    ax.xaxis.minorticks_on()
    ax.yaxis.minorticks_on()
    fig.savefig(save_path + 'rfs_fft.png')

    INFO_DICT['Frequency Analysis'] = {
        'Frequency spacing (Hz)': freq_spacing,
        'Carrier frequency (Hz)': carrier_freq
    }

    return freq_spacing, carrier_power, carrier_freq

def sanitize_for_yaml(data):
    """Recursively converts NumPy types and complex numbers to YAML-safe Python types."""
    if isinstance(data, dict):
        return {k: sanitize_for_yaml(v) for k, v in data.items()}
    elif isinstance(data, list):
        return [sanitize_for_yaml(x) for x in data]
    elif isinstance(data, (np.integer, np.int16, np.int32, np.int64)):
        return int(data)
    elif isinstance(data, (np.floating, np.float32, np.float64)):
        return float(data)
    elif isinstance(data, (complex, np.complexfloating, np.complex128)):
        # YAML does not support complex numbers natively, so we convert them to strings
        return str(data).strip("()")
    return data

def main():

    parser = argparse.ArgumentParser(
        description="Process data from the RFSoC4x2 taken at the full sampling rate.")
    parser.add_argument("filename", help="input file path")
    parser.add_argument("-d", "--dir", type=str, default="./tmp/", help="Save directory (default: `./tmp/`")
    parser.add_argument('-v', '--verbose', action='store_true', help='Enable verbose output')
    args = parser.parse_args()

    data = read_data(args.filename)

    amplitude_diagnostics(data, verbose=args.verbose)
    amplitude_hist(data, args.dir, verbose=args.verbose)
    compute_fft(data, args.dir)

    INFO_DICT_yaml_safe = sanitize_for_yaml(INFO_DICT)

    with open(args.dir + 'rfs_info.yaml', "w") as file:
        yaml.dump(INFO_DICT_yaml_safe, file, default_flow_style=False, sort_keys=False)

    print("Done.")

if __name__ == "__main__":
    main()
