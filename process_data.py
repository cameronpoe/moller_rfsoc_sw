import sys
import shutil
from pathlib import Path
import yaml
import argparse

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.offsetbox import AnchoredText
from pyfftw.interfaces.numpy_fft import fftfreq, fft, rfftfreq, rfft
from scipy.optimize import curve_fit
from scipy.signal.windows import blackman
from sigfig import round

FULL_SAMPLE_FREQ = 5e9
DECIMATION = 320
SAMPLE_FREQ = FULL_SAMPLE_FREQ / DECIMATION
ACLK_FREQ = 125e6
HEADER_WORDS = 2
WORDS_PER_PACKET = 124928

DEFAULT_DIR = "./tmp/"
DEFAULT_PLOT_CONFIG = "./plots.yaml"

INFO_DICT = {}

# The RFSoC SMAs are hooked up backwards to the FPGA pins, so the index of the data
#       does NOT correspond to the actual channel number. This dictionary converts
#       between the two schemes.
CH_NAME_DICT = {
    '0': 'ch4',
    '1': 'ch3',
    '2': 'ch2',
    '3': 'ch1'
}

# Inverse: human-facing channel number (1-4) -> array index (0-3)
CH_INDEX_DICT = {1: 3, 2: 2, 3: 1, 4: 0}


def gaussian(x, A, mu, sigma):
    return A * np.exp(-0.5 * ((x - mu) / sigma) ** 2)


def fit_gaussian_to_hist(counts, centers, verbose=True):
    """Chi-square fit of a Gaussian to histogram counts with sqrt(N) errors
    (empty bins get an error of 1).

    Returns (popt, perr, chi2_red); all zeros if the fit fails.
    """
    counts = np.asarray(counts, dtype=float)

    err = np.sqrt(counts)
    err[counts == 0] = 1.0

    fail = (np.zeros(3), np.zeros(3), 0.0)

    if counts.sum() <= 0:
        return fail

    # Initial guess from the moments of the histogram
    mu0 = np.average(centers, weights=counts)
    var0 = np.average((centers - mu0) ** 2, weights=counts)
    sigma0 = np.sqrt(var0) if var0 > 0 else (centers[-1] - centers[0]) / 10.0
    A0 = counts.max()

    try:
        popt, pcov = curve_fit(
            gaussian, centers, counts,
            p0=[A0, mu0, sigma0],
            sigma=err,
            absolute_sigma=True,
            maxfev=10000,
        )
        perr = np.sqrt(np.diag(pcov))
        if not np.all(np.isfinite(popt)) or not np.all(np.isfinite(perr)):
            raise RuntimeError("fit returned non-finite parameters/covariance")
    except (RuntimeError, ValueError, TypeError, np.linalg.LinAlgError) as e:
        if verbose:
            print(f"Gaussian fit failed: {e}")
        return fail

    popt[2] = abs(popt[2])  # sigma is degenerate under sign flip

    resid = (counts - gaussian(centers, *popt)) / err
    ndf = len(centers) - 3
    chi2_red = float(np.sum(resid ** 2) / ndf) if ndf > 0 else 0.0

    return popt, perr, chi2_red


def freedman_diaconis_rule(binned_data):
    # Freedman-Diaconis rule for optimal bin count
    q75, q25 = np.percentile(binned_data, [75, 25])
    iqr = q75 - q25
    bin_width = 2 * iqr * binned_data.size**(-1/3)
    num_bins = int(np.round((binned_data.max() - binned_data.min())/bin_width))
    if num_bins < 10:
        num_bins = 10
    return num_bins


def style_ax(ax):
    ax.xaxis.set_ticks_position('both')
    ax.yaxis.set_ticks_position('both')
    ax.xaxis.minorticks_on()
    ax.yaxis.minorticks_on()


def load_plot_config(path: Path) -> dict:
    """Load plot-enable flags from a YAML file.

    If the file does not exist, write a template with all plots enabled and
    return a dict with all plots enabled.
    """
    all_plots = {
        '03_down-mixed_iq_phase':    True,
        '04_15-625MHz_time-series':  True,
        '05_15-625MHz_fft':          True,
        '06_asymmetry_time-series':  True,
        '07_asymmetry_hist':         True,
        '08_asymmetry_fft':          True,
        '09_ddf_hist':               True,
        '10_dnl_scatter':            True,
        '11_dnl_binned':             True,
    }
    if not path.exists():
        with open(path, 'w') as f:
            yaml.dump({'plots': all_plots}, f, default_flow_style=False)
        return all_plots
    with open(path) as f:
        config = yaml.safe_load(f)
    return config.get('plots', all_plots)


def plot_enabled(config: dict, name: str) -> bool:
    return config.get(name, True)


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
    if stem[0:4] == 'mrf_':
        stem = stem[0:4]
    run_dir_string = f'mrf_{stem}'
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


def format_data(filename):

    data = np.fromfile(filename, dtype=np.uint64)
    data_gate = np.fromfile(filename + '_gate', dtype=np.uint64)

    buffer_gate_np = np.array(data_gate)
    buffer_nonzero = buffer_gate_np[buffer_gate_np != 0]
    buffer_gate_np = None
    rising_edge_mask = ((buffer_nonzero & (1 << 63)) >> 63) == 0
    rising_edges = buffer_nonzero[rising_edge_mask]
    falling_edges = buffer_nonzero[~rising_edge_mask] & ((1 << 63) - 1)
    buffer_nonzero = None
    rising_edges = rising_edges.astype(np.float64) / ACLK_FREQ
    falling_edges = falling_edges.astype(np.float64) / ACLK_FREQ

    avg_gate_freq = 0.5 * ((1/np.diff(rising_edges)).mean() + (1/np.diff(falling_edges)).mean())

    edge_times = np.array([rising_edges, falling_edges]).T
    rising_edges, falling_edges = None, None

    buffer_np = np.frombuffer(data, dtype=np.uint64)

    num_words = buffer_np.size

    expected_magic_word_indices = np.arange(int(num_words / WORDS_PER_PACKET)) * WORDS_PER_PACKET
    expected_metadata_mask = np.full(buffer_np.size, False)
    expected_metadata_mask[expected_magic_word_indices] = True
    expected_magic_word_indices = None
    for _ in range(HEADER_WORDS-1):
        expected_metadata_mask |= np.roll(expected_metadata_mask, 1)

    first_ts = buffer_np[HEADER_WORDS-1]

    buffer_np = buffer_np[~expected_metadata_mask]
    buffer_np = np.frombuffer(buffer_np.byteswap(), dtype=np.uint8).reshape((-1,3)).astype(np.uint32)

    buffer_np = (buffer_np[:,0]<<16) + (buffer_np[:,1]<<8) + (buffer_np[:,2])

    buffer_np = buffer_np << 8
    buffer_np = buffer_np.astype(np.int32)
    buffer_np = buffer_np >> 8

    buffer_np = buffer_np.reshape((-1,8)).T

    even_mask = np.arange(buffer_np.shape[0])%2 == 0
    buffer_np = buffer_np[even_mask,:] + 1j * buffer_np[~even_mask,:]

    return buffer_np, first_ts, edge_times, avg_gate_freq


def process_to_dc(iq_data, downmix_carrier, mode, save_dir, ch_names, plot_config, verbose=False, fft_bins=1):

    num_samp = iq_data.shape[1]
    new_num_samps = num_samp

    if downmix_carrier:

        # Reshape the data so axis 1 indexes the different fft bins
        new_num_samps = num_samp - num_samp%fft_bins
        bin_samps = int(new_num_samps/fft_bins)
        iq_data = iq_data[:,:new_num_samps]
        iq_data = iq_data.reshape((-1,fft_bins,bin_samps))
        if verbose:
            print(f'New number of samples (after FFT binning): {new_num_samps}')

        # Get FFT data and freq domain and sort (fftfreq doesn't give freqs back in ascending order)
        freq_domain = fftfreq(bin_samps, 1/SAMPLE_FREQ)
        freq_sort = np.argsort(freq_domain)
        freq_domain = freq_domain[freq_sort]
        iq_data_freq = fft(blackman(bin_samps)*iq_data, axis=2)[:,:,freq_sort]
        freq_sort = None

        if verbose:
            print('Finished FFTing')

        # Finds index of the highest-power signal (i.e. the carrier)
        carrier_indices = np.argmax(np.abs(iq_data_freq), axis=2)

        # Slices of indices immediately (3 away) around carrier index
        slices = carrier_indices[:,:,np.newaxis] + np.arange(-3, 4)[np.newaxis,np.newaxis,:]

        # Frequencies around carrier frequency
        freq_neighborhoods = freq_domain[slices]

        # FFT spectrum around carrier frequency
        iq_data_freq_neighborhoods = np.abs(np.take_along_axis(iq_data_freq, slices, axis=2))
        slices = None

        # Carrier frequency is found by weighted average of frequencies around the highest-power one.
        # Since the DC peak is not a delta function, power shifts among FFT bins as the true frequency shifts.
        carrier_freqs = np.sum(freq_neighborhoods * iq_data_freq_neighborhoods/(np.sum(iq_data_freq_neighborhoods, axis=2)[:,:,np.newaxis]), axis=2)
        iq_data_freq_neighborhoods, freq_neighborhoods = None, None

        # Downmixes data
        iq_data = iq_data * np.exp(-1j * (2*np.pi*carrier_freqs[:,:,np.newaxis]/SAMPLE_FREQ*np.arange(bin_samps)))
        carrier_freqs = None

    if plot_enabled(plot_config, '03_down-mixed_iq_phase'):
        save_name = '03_down-mixed_iq_phase'
        for i in range(iq_data.shape[0]):
            plot_title = f'{save_dir.stem}, {save_name[3:]}_{ch_names[i]}'
            particular_save_str = save_name + f'_{ch_names[i]}.png'

            fig, ax = plt.subplots(figsize=(12,7))
            ax.plot(np.unwrap(np.angle(iq_data[i].flatten())))
            ax.set_title(plot_title, fontdict=dict(size=14))
            ax.set_ylabel('I/Q phase (rad.)', fontdict=dict(size=12.5))
            ax.set_xlabel('Time', fontdict=dict(size=12.5))
            fig.subplots_adjust(bottom=0.13)
            style_ax(ax)
            fig.savefig(save_dir / particular_save_str)
            plt.close(fig)

    if mode == 'quad':
        if verbose:
            print('Phase rotation: Using quadrature sum.')
        iq_data = np.abs(iq_data)
    elif mode == 'running':
        if verbose:
            print('Phase rotation: Rotating by running I/Q phase.')

        # Phases of the carrier signal
        carrier_phases = np.unwrap(np.angle(np.take_along_axis(iq_data_freq, carrier_indices[:,:,np.newaxis], axis=2))).squeeze(axis=2)
        iq_data_freq, carrier_indices = None, None

        # Eliminates any phase due to the carrier
        iq_data = iq_data * np.exp(-1j * carrier_phases[:,:,np.newaxis])
        carrier_phases = None

        # Takes away any remnant phases between I/Q data
        # unwrap is critical: if phase fluctuates around +/- pi, np.angle() average ~= 0
        # but np.unwrap(np.angle()) average gives the correct phase
        avg_phases = np.average(np.unwrap(np.angle(iq_data)), axis=2)
        iq_data *= np.exp(-1j*avg_phases[:,:,np.newaxis])
        avg_phases = None

    iq_data = np.real(iq_data.reshape((-1, new_num_samps))).astype(np.float64)

    if plot_enabled(plot_config, '04_15-625MHz_time-series'):
        save_name = '04_15-625MHz_time-series'
        for i in range(iq_data.shape[0]):
            plot_title = f'{save_dir.stem}, {save_name[3:]}_{ch_names[i]}'
            particular_save_str = save_name + f'_{ch_names[i]}.png'

            fig, ax = plt.subplots(figsize=(12,7))
            ax.plot(iq_data[i])
            ax.set_title(plot_title, fontdict=dict(size=14))
            ax.set_ylabel('Amplitude (arb.)', fontdict=dict(size=12.5))
            ax.set_xlabel('Time', fontdict=dict(size=12.5))
            fig.subplots_adjust(bottom=0.13)
            style_ax(ax)
            fig.savefig(save_dir / particular_save_str)
            plt.close(fig)

    return iq_data


def fft_time_series(data, save_dir, save_name, sample_freq, truncate_for_speed, ch_names, plot_config, units='Hz'):

    if not plot_enabled(plot_config, save_name):
        return

    unit_norm = 1
    if units == 'kHz':
        unit_norm = 1e-3
    elif units == 'MHz':
        unit_norm = 1e-6
    elif units == 'GHz':
        unit_norm = 1e-9

    stop_ind = None
    data_size = data.shape[-1]
    if truncate_for_speed:
        stop_ind = 1 << int(np.log2(data_size))
        data_size = stop_ind

    freq_domain = rfftfreq(data_size, d=1/sample_freq)
    data_freq = rfft(blackman(data_size)*data[:,:stop_ind], axis=1)

    freq_spacing = freq_domain[1] - freq_domain[0]

    carrier_ind = np.abs(data_freq).argmax(axis=1)
    carrier_power = np.take_along_axis(np.abs(data_freq), carrier_ind[:,np.newaxis], axis=1)
    carrier_freq = freq_domain[carrier_ind]

    for i in range(data_freq.shape[0]):

        plot_title = f'{save_dir.stem}, {save_name[3:]}_{ch_names[i]}'
        particular_save_str = save_name + f'_{ch_names[i]}.png'

        info = "\n".join([
            rf"$f_c$ = {carrier_freq[i]*1e-3:.4f} kHz",
            rf"$\Delta f$ = {freq_spacing:.3f} Hz",
        ])
        at = AnchoredText(info, loc="upper right", prop=dict(size=10, family="monospace"),
                        frameon=True, borderpad=0.5)

        fig, ax = plt.subplots(figsize=(12,7))
        ax.plot(freq_domain*unit_norm, 20*np.log10(np.abs(data_freq[i])/carrier_power[i]))
        ax.set_title(plot_title, fontdict=dict(size=14))
        ax.set_ylabel('Signal power (dBc)', fontdict=dict(size=12.5))
        ax.set_xlabel(f'Frequency ({units})', fontdict=dict(size=12.5))
        at.patch.set(alpha=0.8, facecolor="white", edgecolor="0.7")
        ax.add_artist(at)
        fig.subplots_adjust(bottom=0.13)
        style_ax(ax)
        fig.savefig(save_dir / particular_save_str)
        plt.close(fig)


def gate_means(data, first_ts, gates):
    """
    data:  (C, N) detector data
    first_ts: first sample timestamp (ACLK ticks)
    gates: (M, 2) [start, stop] times; uses start <= t < stop
    returns: (C, K) mean of data within each fully-contained gate, K <= M.
             Gates not fully covered by the data are dropped.
    """

    # first_ts is the time stamp associated with the first sample in the buffer
    time_domain = first_ts/ACLK_FREQ + np.arange(data.shape[-1]) / SAMPLE_FREQ

    starts = gates[:, 0]
    stops = gates[:, 1]

    N = data.shape[1]
    # np.searchsorted returns indices of first array such that an element of the second array,
    #       if placed in that index, would keep the array sorted. In this case, it's used because
    #       time_domain increments in units of 1/15.625MHz, but the time stamp granularity is
    #       actually 1/125 MHz.
    # Both are `side='left'` because we want to include the start index, but exclude the end index.
    #       Alternatively, this is saying `start <= sample_time < end`
    start_inds = np.searchsorted(time_domain, starts, side='left')
    end_inds = np.searchsorted(time_domain, stops,  side='left')

    # Some gates may have start times before the `first_ts`, or have end times that exceed the last
    #       time of recorded data. We need to throw away those gates.
    # A gate is fully contained iff:
    #   - its start is within the time range: start >= times[0]
    #   - its stop does not run past the data: hi < N (there is a sample at/after stop,
    #     meaning the data extends beyond the gate end)
    # We also throw away any gates where the end time is smaller than start time (in other words,
    #       keep all gates with end time larger than start time, `hi > lo`)
    keep = (starts >= time_domain[0]) & (end_inds < N) & (end_inds > start_inds)

    csum = np.concatenate(
        [np.zeros((data.shape[0], 1), dtype=np.float64),
         np.cumsum(data, axis=1, dtype=np.float64)],
        axis=1,
    )

    counts = end_inds - start_inds
    sums = csum[:, end_inds[keep]] - csum[:, start_inds[keep]]
    means = sums / counts[keep]

    window_starts = starts[keep]

    return means, window_starts


def construct_asymmetries(means, times, save_dir, ch_names, plot_config, verbose=False):

    start_times = np.copy(times)
    if means.shape[-1] % 2 == 1:
        if verbose:
            print('Number of integrated windows is odd. Dropping last window.')
        means = means[:,:-1]
        start_times = start_times[:-1]
    even_mask = np.arange(means.shape[-1]) % 2 == 0

    asymmetries_ppm = 1e6 * (means[:,even_mask] - means[:,~even_mask]) / (means[:,even_mask] + means[:,~even_mask])
    window_pair_times = start_times[even_mask]
    even_mask, start_times = None, None

    window_pair_times -= window_pair_times[0]

    for i in range(means.shape[0]):

        if plot_enabled(plot_config, '06_asymmetry_time-series'):
            save_name = '06_asymmetry_time-series'
            plot_title = f'{save_dir.stem}, {save_name[3:]}_{ch_names[i]}'
            particular_save_str = save_name + f'_{ch_names[i]}.png'

            fig, ax = plt.subplots(figsize=(12,7))
            ax.plot(window_pair_times, asymmetries_ppm[i])
            ax.set_title(plot_title, fontdict=dict(size=14))
            ax.set_ylabel('Asymmetry (ppm)', fontdict=dict(size=12.5))
            ax.set_xlabel('Time (sec)', fontdict=dict(size=12.5))
            style_ax(ax)
            fig.savefig(save_dir / particular_save_str)
            plt.close(fig)

        if plot_enabled(plot_config, '07_asymmetry_hist'):
            save_name = '07_asymmetry_hist'
            plot_title = f'{save_dir.stem}, {save_name[3:]}_{ch_names[i]}'
            particular_save_str = save_name + f'_{ch_names[i]}.png'

            num_bins = freedman_diaconis_rule(asymmetries_ppm[i])

            fig, ax = plt.subplots(figsize=(10,7))
            n, bin_edges = np.histogram(asymmetries_ppm[i], bins=num_bins)
            yerr = np.sqrt(n)
            yerr[n == 0] = 1.0
            bin_centers = bin_edges[:-1] + 0.5*(bin_edges[1] - bin_edges[0])
            ax.errorbar(bin_centers, n, yerr=yerr, fmt='.', ecolor='black', elinewidth=1.5, capsize=4)
            popt, perr, chi2_red = fit_gaussian_to_hist(n, bin_centers, verbose=verbose)
            fit_ok = np.any(popt)

            if fit_ok:
                data_mean, mean_err = popt[1], perr[1]
                std_dev,  std_err   = popt[2], perr[2]
                chi2_line = rf"$\chi^2/\nu$ = {chi2_red:.3f}"
                curve_domain = np.linspace(bin_centers[0], bin_centers[-1], 500)
                ax.plot(curve_domain, gaussian(curve_domain, *popt), color='red')
            else:
                data_mean = float(np.mean(asymmetries_ppm[i]))
                std_dev   = float(np.std(asymmetries_ppm[i], ddof=1))
                mean_err  = std_dev / np.sqrt(asymmetries_ppm[i].size)
                std_err   = std_dev / np.sqrt(2 * (asymmetries_ppm[i].size - 1))
                chi2_line = r"$\chi^2/\nu$ = n/a"

            info = "\n".join([
                rf"$\mu$ = {round(data_mean, mean_err)} ppm",
                rf"$\sigma$ = {round(std_dev, std_err)} ppm",
                chi2_line,
                rf"No. of bins = {num_bins:d}",
            ])
            at = AnchoredText(info, loc="upper right", prop=dict(size=10, family="monospace"),
                            frameon=True, borderpad=0.5)
            at.patch.set(alpha=0.8, facecolor="white", edgecolor="0.7")
            ax.add_artist(at)
            ax.set_yscale('log')
            ax.set_title(plot_title, fontdict=dict(size=14))
            ax.set_ylabel('Counts per bin', fontdict=dict(size=12.5))
            ax.set_xlabel('Asymmetry (ppm)', fontdict=dict(size=12.5))
            style_ax(ax)
            fig.savefig(save_dir / particular_save_str)
            plt.close(fig)

    return asymmetries_ppm * 1e-6


def compute_resolution(ddf, save_dir, data1_name, data2_name, plot_config, verbose=False):

    if not plot_enabled(plot_config, '09_ddf_hist'):
        return

    ddf_ppm = ddf*1e6
    save_name = f'09_ddf_hist_{data1_name}-{data2_name}'
    plot_title = f'{save_dir.stem}, {save_name[3:]}'
    particular_save_str = save_name + '.png'

    num_bins = freedman_diaconis_rule(ddf_ppm)

    fig, ax = plt.subplots(figsize=(10,7))
    n, bin_edges = np.histogram(ddf_ppm, bins=num_bins)
    yerr = np.sqrt(n)
    yerr[n == 0] = 1.0
    bin_centers = bin_edges[:-1] + 0.5*(bin_edges[1] - bin_edges[0])
    ax.errorbar(bin_centers, n, yerr=yerr, fmt='.', color='black', ecolor='black', elinewidth=1.5, capsize=4)
    popt, perr, chi2_red = fit_gaussian_to_hist(n, bin_centers, verbose=verbose)
    fit_ok = np.any(popt)

    if fit_ok:
        data_mean, mean_err = popt[1], perr[1]
        std_dev,  std_err   = popt[2], perr[2]
        chi2_line = rf"$\chi^2/\nu$ = {chi2_red:.3f}"
        curve_domain = np.linspace(bin_centers[0], bin_centers[-1], 500)
        ax.plot(curve_domain, gaussian(curve_domain, *popt), color='red')
    else:
        data_mean = float(np.mean(ddf_ppm))
        std_dev   = float(np.std(ddf_ppm, ddof=1))
        mean_err  = std_dev / np.sqrt(ddf_ppm.size)
        std_err   = std_dev / np.sqrt(2 * (ddf_ppm.size - 1))
        chi2_line = r"$\chi^2/\nu$ = n/a"

    info = "\n".join([
        rf"$\mu$ = {round(data_mean, mean_err)} ppm",
        rf"$\sigma$ = {round(std_dev, std_err)} ppm",
        chi2_line,
        rf"No. of bins = {num_bins:d}",
        rf"Resolution = {round(1/np.sqrt(2)*std_dev, 1/np.sqrt(2)*std_err)} ppm",
    ])
    at = AnchoredText(info, loc="upper right", prop=dict(size=10, family="monospace"),
                    frameon=True, borderpad=0.5)
    at.patch.set(alpha=0.8, facecolor="white", edgecolor="0.7")
    ax.add_artist(at)
    ax.set_yscale('log')
    ax.set_title(plot_title, fontdict=dict(size=14))
    ax.set_ylabel('Counts per bin', fontdict=dict(size=12.5))
    ax.set_xlabel(f'asym_{data1_name}-asym_{data2_name} (ppm)', fontdict=dict(size=12.5))
    style_ax(ax)
    fig.savefig(save_dir / particular_save_str)
    plt.close(fig)

    return (data_mean, mean_err), (std_dev, std_err), num_bins, chi2_red, fit_ok


def plot_diff_nonlinearity(rdf, ddf, save_dir, ch1_name, ch2_name, plot_config):
    """Scatter and binned-mean plots of DDF vs RDF to diagnose differential nonlinearity."""

    rdf_ppm = rdf * 1e6
    ddf_ppm = ddf * 1e6

    if plot_enabled(plot_config, '10_dnl_scatter'):
        save_name = f'10_dnl_scatter_{ch1_name}-{ch2_name}'
        fig, ax = plt.subplots(figsize=(10, 7))
        ax.scatter(rdf_ppm, ddf_ppm, marker='.', color='black', s=4)
        ax.set_title(f'{save_dir.stem}, DNL scatter {ch1_name}-{ch2_name}', fontdict=dict(size=14))
        ax.set_xlabel('RDF (ppm)', fontdict=dict(size=12.5))
        ax.set_ylabel('DDF (ppm)', fontdict=dict(size=12.5))
        style_ax(ax)
        fig.savefig(save_dir / (save_name + '.png'))
        plt.close(fig)

    # Bin RDF and compute mean DDF per bin
    bins = np.linspace(rdf_ppm.min(), rdf_ppm.max(), 21)
    idx = np.digitize(rdf_ppm, bins)

    centers, means, ses = [], [], []
    for b in range(1, len(bins)):
        sel = idx == b
        n = sel.sum()
        if n > 1:
            m = ddf_ppm[sel].mean()
            se = ddf_ppm[sel].std(ddof=1) / np.sqrt(n)
            centers.append(0.5 * (bins[b-1] + bins[b]))
            means.append(m)
            ses.append(se)

    if len(centers) < 2:
        return

    centers = np.array(centers)
    means = np.array(means)
    ses = np.array(ses)

    # Weighted linear fit (np.polyfit weights are 1/sigma, not 1/sigma^2)
    coeffs, cov = np.polyfit(centers, means, deg=1, w=1.0/ses, cov=True)
    slope, intercept = coeffs
    slope_err = np.sqrt(cov[0, 0])
    intercept_err = np.sqrt(cov[1, 1])

    model = np.polyval(coeffs, centers)
    chi2 = np.sum(((means - model) / ses)**2)
    dof = len(centers) - 2
    print(f"DNL {ch1_name}-{ch2_name}: slope = {slope:+.3e} +/- {slope_err:.3e}  ({slope/slope_err:+.1f}σ)")
    print(f"    intercept = {intercept:+.3f} +/- {intercept_err:.3f} ppm,  χ²/dof = {chi2:.1f}/{dof} = {chi2/dof:.2f}")

    if plot_enabled(plot_config, '11_dnl_binned'):
        save_name = f'11_dnl_binned_{ch1_name}-{ch2_name}'
        xfit = np.linspace(centers.min(), centers.max(), 200)
        fig, ax = plt.subplots(figsize=(10, 7))
        ax.errorbar(centers, means, yerr=ses, fmt='o', capsize=3, color='black')
        ax.axhline(0, color='red', linestyle='--', linewidth=1)
        ax.plot(xfit, np.polyval(coeffs, xfit), color='blue',
                label=f'linear fit (slope {slope:+.2e})')
        ax.set_title(f'{save_dir.stem}, DNL binned {ch1_name}-{ch2_name}', fontdict=dict(size=14))
        ax.set_xlabel('RDF (ppm)', fontdict=dict(size=12.5))
        ax.set_ylabel('Binned mean DDF (ppm)', fontdict=dict(size=12.5))
        ax.legend()
        style_ax(ax)
        fig.savefig(save_dir / (save_name + '.png'))
        plt.close(fig)


def main():

    parser = argparse.ArgumentParser(
        description="Offline tool for processing and analyzing moller_rfsoc_fw data.")
    parser.add_argument("filename", help="File path of binary data. Assumes gate data has same name with `_gate` appended.")
    parser.add_argument("-d", "--dir", type=str, default="./tmp/", help="Directory to save outputs (default: `./tmp/`")
    parser.add_argument('-v', '--verbose', action='store_true', help='Enable verbose output. (default: False)')
    parser.add_argument('--fft-dc', action='store_true', help='Enables finding carrier frequency via FFT during digital down conversion. Useful if the data from the FPGA is not at DC. (default: False)')
    parser.add_argument("-iq", "--iq-rotation-mode", type=str.lower, default="quad", choices=['quad', 'running'],
                        help='Sets the mode for doing the IQ rotation. "quad" simply adds the I and Q in quadrature. '
                             '"running" tracks and rotates by the running I/Q phase. Requires --fft-dc. (default: quad)')
    parser.add_argument('-chs', '--channels', nargs='+', default=[1,2,3,4], type=int,
                        help='Sets which physical channels (1-4) to process. format_data always decodes all channels; '
                             'this selects which are analyzed downstream. (default: all)')
    parser.add_argument('--plot-config', type=str, default=DEFAULT_PLOT_CONFIG,
                        help=f'Path to YAML file controlling which plots are generated. '
                             f'Written as a template with all plots enabled if the file does not exist. '
                             f'(default: {DEFAULT_PLOT_CONFIG})')
    args = parser.parse_args()

    if args.iq_rotation_mode == 'running' and not args.fft_dc:
        parser.error("--iq-rotation-mode running requires --fft-dc")

    invalid_chs = [ch for ch in args.channels if ch not in CH_INDEX_DICT]
    if invalid_chs:
        parser.error(f"Invalid channel(s): {invalid_chs}. Valid values are 1-4.")

    try:
        outdir = resolve_output_dir(args.dir, parser.get_default("dir"))
    except FileNotFoundError as err:
        parser.error(str(err))

    plot_config = load_plot_config(Path(args.plot_config))

    run_dir = run_directory(args.filename, outdir)

    existing = list(run_dir.iterdir()) if run_dir.is_dir() else []
    if existing:
        if sys.stdin.isatty():
            reply = input(f"Delete {len(existing)} item(s) in {run_dir}? [y/N] ")
            if reply.strip().lower() not in {"y", "yes"}:
                parser.error("aborted")
        clear_directory(run_dir)

    if args.verbose:
        print(f"Writing outputs to {run_dir}")

    buffer_np, first_ts, edge_times, avg_gate_freq = format_data(args.filename)

    # Select only the requested physical channels, maintaining sorted array order
    ch_indices = sorted(CH_INDEX_DICT[ch] for ch in args.channels)
    ch_names = [CH_NAME_DICT[str(idx)] for idx in ch_indices]
    buffer_np = buffer_np[ch_indices, :]

    data_dc = process_to_dc(buffer_np, args.fft_dc, args.iq_rotation_mode, run_dir, ch_names, plot_config, args.verbose)
    buffer_np = None

    fft_time_series(data_dc, run_dir, '05_15-625MHz_fft', sample_freq=SAMPLE_FREQ,
                    truncate_for_speed=True, ch_names=ch_names, plot_config=plot_config, units='MHz')

    data_integrated, window_start_times = gate_means(data_dc, first_ts, edge_times)

    save_dict = {}

    asymmetries = construct_asymmetries(data_integrated, window_start_times, run_dir, ch_names, plot_config, verbose=args.verbose)

    for i in range(asymmetries.shape[0]):
        save_dict[f'asym_{CH_INDEX_DICT[str(i)]}'] = asymmetries[i]

    # Figure 08: FFT of asymmetry time series.
    # Each asymmetry spans one window pair (two gate periods), so the asymmetry rate is avg_gate_freq/2.
    fft_time_series(asymmetries * 1e6, run_dir, '08_asymmetry_fft', sample_freq=avg_gate_freq/2,
                    truncate_for_speed=False, ch_names=ch_names, plot_config=plot_config, units='Hz')

    i = 0
    while i < asymmetries.shape[0]:
        j = i + 1
        while j < asymmetries.shape[0]:
            ddf = asymmetries[i] - asymmetries[j]
            ddf_mean, ddf_stddev, num_bins, chi2_red, fit_ok = compute_resolution(ddf, run_dir, ch_names[j], ch_names[i], plot_config, verbose=args.verbose)
            plot_diff_nonlinearity(asymmetries[i], ddf, run_dir, ch_names[i], ch_names[j], plot_config)
            save_dict.update({
                f'ddf_{CH_NAME_DICT[str(i)]}-{CH_NAME_DICT[str(j)]}_mean': ddf_mean,
                f'ddf_{CH_NAME_DICT[str(i)]}-{CH_NAME_DICT[str(j)]}_stddev': ddf_stddev,
                f'ddf_{CH_NAME_DICT[str(i)]}-{CH_NAME_DICT[str(j)]}_nbins': num_bins,
                f'ddf_{CH_NAME_DICT[str(i)]}-{CH_NAME_DICT[str(j)]}_chi2-red': chi2_red,
                f'ddf_{CH_NAME_DICT[str(i)]}-{CH_NAME_DICT[str(j)]}_fit-ok': fit_ok
            })
            j += 1
        i += 1

    np.savez(run_dir / f'00_data.npz', **save_dict)


if __name__ == "__main__":
    main()
