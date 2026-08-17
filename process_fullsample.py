import argparse
import sys
from pathlib import Path
import shutil
import yaml


import numpy as np
import matplotlib.pyplot as plt
from matplotlib.offsetbox import AnchoredText
from pyfftw.interfaces.numpy_fft import rfftfreq, rfft
from scipy.signal.windows import blackman

SAMPLE_RATE = 5e9
ADC_BITS = 14

DEFAULT_DIR = "./tmp/"
INFO_DICT = {}

def resolve_output_dir(value: str, default: str = DEFAULT_DIR) -> Path:
    """Resolve the -d/--dir argument to an existing directory.

    Default value: created under the current working directory if missing.
    Explicit value: looked up first relative to the cwd, then as an
    absolute/expanded path. Raises FileNotFoundError if neither exists.
    """
    if value == default:
        d = (Path.cwd() / value).resolve()
        d.mkdir(parents=True, exist_ok=True)
        return d

    candidate = Path(value).expanduser()

    if not candidate.is_absolute():
        local = (Path.cwd() / candidate).resolve()
        if local.is_dir():
            return local

    absolute = candidate.resolve()
    if absolute.is_dir():
        return absolute

    raise FileNotFoundError(
        f"Could not find directory {value!r} relative to {Path.cwd()} or as an absolute path."
    )


def run_directory(filename: str, outdir: Path, clean: bool = False) -> Path:
    """Return <outdir>/<stem>, creating it, optionally emptying it first."""       
    stem = Path(filename).stem
    if stem[0:4] == 'rfs_':
        stem = stem[4:]
    run_dir_string = f'rfs_{stem}'
    run_dir = outdir / run_dir_string

    if clean and run_dir.is_dir():
        clear_directory(run_dir)

    run_dir.mkdir(parents=True, exist_ok=True)
    return run_dir

def clear_directory(target: Path) -> int:
    """Remove everything inside `target`, leaving the directory itself. Returns count."""
    removed = 0
    for entry in target.iterdir():
        if entry.is_dir() and not entry.is_symlink():
            shutil.rmtree(entry)
        else:
            entry.unlink()
        removed += 1
    return removed

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

    data_np64 = data.astype(np.float64)

    data_max = data.max()
    data_min = data.min()
    data_avg = data_np64.mean()
    rms = np.sqrt(np.mean(data_np64**2))
    rms_hyp = (2**(ADC_BITS - 1) - 0.5) / np.sqrt(2) # The -0.5 is here because the max pos value is 2**(N-1) - 1 and min neg value is - 2**(N-1). So the average of the absolute value of these two quantities is 2**(N-1) - 0.5
    scale_dbfs = 20*np.log10(rms / rms_hyp)

    if verbose:
        print(f'Data max: {data_max}')
        print(f'Data min: {data_min}')
        print(f'Data average: {data_avg}')
        print(f'Data RMS value: {rms}')
        print(f'Hypothetical max RMS value: {rms_hyp}')
        print(f'Scale (dBFS): {scale_dbfs}')

    INFO_DICT['Amplitude Diagnostics'] = {
        'Data max ADC code': data_max,
        'Data min ADC code': data_min,
        'Data avg ADC code': data_avg,
        'Data RMS value': rms,
        'Data hypothetical max RMS value': rms_hyp,
        'Data scale (dBFS)': scale_dbfs
    }

    return data_max, data_min, rms, rms_hyp, scale_dbfs

def amplitude_hist(data, data_max, data_min, save_dir, verbose=False):

    bins = np.linspace(data_min - 0.5, data_max + 0.5, data_max - data_min +2)

    fig, ax = plt.subplots(figsize=(12,9))
    ax.hist(data, bins=bins)
    ax.set_ylabel('Number of samples per 1 ADC code bin', fontdict=dict(size=14))
    ax.set_xlabel('ADC code', fontdict=dict(size=14))
    ax.xaxis.set_ticks_position('both')
    ax.yaxis.set_ticks_position('both')
    ax.xaxis.minorticks_on()
    ax.yaxis.minorticks_on()
    fig.savefig(save_dir / '01_amplitude_hist.png')

    fig, ax = plt.subplots(figsize=(12,9))
    ax.hist(data, bins=300)
    ax.set_ylabel('Number of samples per 1 ADC code bin', fontdict=dict(size=14))
    ax.set_xlabel('ADC code', fontdict=dict(size=14))
    ax.xaxis.set_ticks_position('both')
    ax.yaxis.set_ticks_position('both')
    ax.xaxis.minorticks_on()
    ax.yaxis.minorticks_on()
    fig.savefig(save_dir / '02_amplitude_hist_fewer_bins.png')
    
    return

def compute_fft(data, save_dir):

    freq_domain = rfftfreq(data.size, 1/SAMPLE_RATE)
    data_freq = rfft(blackman(data.size)*data, data.size)

    freq_spacing = freq_domain[1] - freq_domain[0]

    carrier_ind = np.abs(data_freq).argmax()
    carrier_power = np.abs(data_freq[carrier_ind])
    carrier_freq = freq_domain[carrier_ind]

    info = "\n".join([
        rf"$f_c$ = {carrier_freq*1e-6:.6f} MHz",
        rf"$\Delta f$ = {freq_spacing:.3f} Hz",
    ])
    at = AnchoredText(info, loc="upper right", prop=dict(size=10, family="monospace"),
                    frameon=True, borderpad=0.5)

    fig, ax = plt.subplots(figsize=(12,7))
    ax.plot(freq_domain*1e-6, 20*np.log10(np.abs(data_freq)/carrier_power))
    ax.set_ylabel('Signal power (dBc)', fontdict=dict(size=14))
    ax.set_xlabel('Frequency (MHz)', fontdict=dict(size=14))
    at.patch.set(alpha=0.8, facecolor="white", edgecolor="0.7")
    ax.add_artist(at)
    ax.xaxis.set_ticks_position('both')
    ax.yaxis.set_ticks_position('both')
    ax.xaxis.minorticks_on()
    ax.yaxis.minorticks_on()
    fig.savefig(save_dir / '03_fft.png')

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

    try:
        outdir = resolve_output_dir(args.dir, parser.get_default("dir"))
    except FileNotFoundError as err:
        parser.error(str(err))
    run_dir = run_directory(args.filename, outdir)


    existing = list(run_dir.iterdir()) if run_dir.is_dir() else []
    if existing:
        if sys.stdin.isatty():
            reply = input(f"Delete {len(existing)} item(s) in {run_dir}? [y/N] ")
            if reply.strip().lower() not in {"y", "yes"}:
                parser.error("aborted")
        clear_directory(run_dir)

    data = read_data(args.filename)

    data_max, data_min, data_rms, rms_hyp, scale_dbfs = amplitude_diagnostics(data, verbose=args.verbose)
    amplitude_hist(data, data_max, data_min, run_dir, verbose=args.verbose)
    compute_fft(data, run_dir)

    INFO_DICT_yaml_safe = sanitize_for_yaml(INFO_DICT)

    with open(run_dir / '00_info.yaml', "w") as file:
        yaml.dump(INFO_DICT_yaml_safe, file, default_flow_style=False, sort_keys=False)

    print("Done.")

if __name__ == "__main__":
    main()
