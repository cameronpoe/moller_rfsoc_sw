import numpy as np
from pyfftw.interfaces.numpy_fft import fft, fftfreq
import matplotlib.pyplot as plt
from matplotlib.colors import LogNorm
from scipy.signal.windows import blackman
from scipy.optimize import curve_fit
import sys, os

ACLK_FREQ = 125e6
HEADER_WORDS = 2
WORDS_PER_PACKET = 124928
FULL_FREQ = 5e9
DECIMATION = 320

def format_data(data, data_gate, aclk_freq, words_per_packet, header_words):

    buffer_gate_np = np.array(data_gate)
    buffer_nonzero = buffer_gate_np[buffer_gate_np != 0]
    buffer_gate_np = None
    rising_edge_mask = ((buffer_nonzero & (1 << 63)) >> 63) == 0
    rising_edges = buffer_nonzero[rising_edge_mask]
    falling_edges = buffer_nonzero[~rising_edge_mask] & ((1 << 63) - 1)
    buffer_nonzero = None
    rising_edges = rising_edges.astype(np.float64) / aclk_freq
    falling_edges = falling_edges.astype(np.float64) / aclk_freq
    
    edge_times = np.array([rising_edges, falling_edges]).T
    rising_edges, falling_edges = None, None
    
    buffer_np = np.frombuffer(data, dtype=np.uint64)        
    
    num_words = buffer_np.size
    
    expected_magic_word_indices = np.arange(int(num_words / words_per_packet)) * words_per_packet
    expected_metadata_mask = np.full(buffer_np.size, False)
    expected_metadata_mask[expected_magic_word_indices] = True
    expected_magic_word_indices = None
    for _ in range(header_words-1):
        expected_metadata_mask |= np.roll(expected_metadata_mask, 1)
        
    first_ts = buffer_np[header_words-1]

    buffer_np = buffer_np[~expected_metadata_mask]
    buffer_np = np.frombuffer(buffer_np.byteswap(), dtype=np.uint8).reshape((-1,3)).astype(np.uint32)

    buffer_np = (buffer_np[:,0]<<16) + (buffer_np[:,1]<<8) + (buffer_np[:,2])

    buffer_np = buffer_np << 8
    buffer_np = buffer_np.astype(np.int32)
    buffer_np = buffer_np >> 8

    buffer_np = buffer_np.reshape((-1,8)).T
    
    return buffer_np, first_ts, edge_times

def process_to_dc(iq_data, samp_freq, fft_bins=1):

    num_samp = iq_data.shape[1]

    # Reshape the data so axis 1 indexes the different fft bins
    new_num_samps = num_samp - num_samp%fft_bins
    bin_samps = int(new_num_samps/fft_bins)
    iq_data = iq_data[:,:new_num_samps]
    iq_data = iq_data.reshape((-1,fft_bins,bin_samps))
    print(f'New number of samples (after FFT binning): {new_num_samps}')

    # Get FFT data and freq domain and sort (fftfreq doesn't give freqs back in ascending order)
    freq_domain = fftfreq(bin_samps, 1/samp_freq)
    freq_sort = np.argsort(freq_domain)
    freq_domain = freq_domain[freq_sort]
    iq_data_freq = fft(blackman(bin_samps)*iq_data, axis=2)[:,:,freq_sort]
    freq_sort = None

    if True:
        print('Finishesd FFTing')

    # Finds index of the highest-power signal (i.e. the carrier)
    carrier_indices = np.argmax(np.abs(iq_data_freq), axis=2)

    # Slices of indices immediately (3 away) around carrier index
    slices = carrier_indices[:,:,np.newaxis] + np.arange(-3, 4)[np.newaxis,np.newaxis,:]

    # Frequencies around carrier frequency
    freq_neighborhoods = freq_domain[slices]
  
    # FFT spectrum around carrier frequency
    iq_data_freq_neighborhoods = np.abs(np.take_along_axis(iq_data_freq, slices, axis=2))
    slices = None, None

    # Carrier frequency is found by weighted average of frequencies around the highest-power one. Since DC peak is not a delta, since if the true frequency is shifting, power is shifting among the FFT bins. 
    carrier_freqs = np.sum(freq_neighborhoods * iq_data_freq_neighborhoods/(np.sum(iq_data_freq_neighborhoods, axis=2)[:,:,np.newaxis]), axis=2)
    iq_data_freq_neighborhoods, freq_neighborhoods = None, None

    # Phases of the carrier signal
    carrier_phases = np.unwrap(np.angle(np.take_along_axis(iq_data_freq, carrier_indices[:,:,np.newaxis], axis=2))).squeeze(axis=2)
    iq_data_freq, carrier_indices = None, None

    # Down-mixes and eliminates any the phase due to the carrier
    iq_data = iq_data * np.exp(-1j * (2*np.pi*carrier_freqs[:,:,np.newaxis]/samp_freq*np.arange(bin_samps) + carrier_phases[:,:,np.newaxis]))
    carrier_freqs, carrier_phases = None, None

    # Takes away any remnant phases between I/Q data
    avg_phases = np.average(np.unwrap(np.angle(iq_data)), axis=2) # unwrap is very important here b/c we're averaging. if angle is fluctuating around +/- pi, the average of np.angle() could be ~0, but average of np.unwrap(np.angle()) will be the correct phase
    iq_data *= np.exp(-1j*avg_phases[:,:,np.newaxis])
    avg_phases = None

    iq_data = iq_data.reshape((-1, new_num_samps))

    return np.real(iq_data).astype(np.float64)    

def gate_means(data, times, gates):
    """
    data:  (4, N) detector data
    times: (N,)   sample times (constant spacing, assumed sorted)
    gates: (M, 2) [start, stop] times; uses start <= t < stop
    returns: (4, K) mean of data within each fully-contained gate, K <= M.
             Gates that are not fully covered by the data are dropped.
    """
    starts = gates[:, 0]
    stops = gates[:, 1]

    N = data.shape[1]
    lo = np.searchsorted(times, starts, side='left')
    hi = np.searchsorted(times, stops,  side='left')

    # A gate is fully contained iff:
    #   - its start is within the time range: start >= times[0]   -> lo < N (and start not before data)
    #   - its stop does not run past the data: hi < N (there is a sample at/after stop,
    #     meaning the data extends beyond the gate end)
    keep = (starts >= times[0]) & (hi < N) & (hi > lo)

    csum = np.concatenate(
        [np.zeros((data.shape[0], 1), dtype=np.float64),
         np.cumsum(data, axis=1, dtype=np.float64)],
        axis=1,
    )

    counts = hi - lo
    sums = csum[:, hi[keep]] - csum[:, lo[keep]]
    means = sums / counts[keep]

    return means

def plot_nice_ddf(ddfs, rdf1, rdf2, ch1_name, ch2_name, dir_path):

    fig, axs = plt.subplots(2,2, figsize=(10,8))
    fig.subplots_adjust(hspace=0)
    fig.subplots_adjust(wspace=0)

    n, bins, _ = axs[0,0].hist(rdf1, bins=100, histtype='step', color='purple', orientation='horizontal')
    axs[0,0].text(0.15*np.max(n), 0.85*bins[-1], f'$\\sigma={round(np.std(rdf1), 2)}$ ppm', fontdict=dict(size=14))
    axs[0,0].set_xscale('log')
    axs[0,0].minorticks_on()
    axs[0,0].xaxis.tick_top()
    axs[0,0].set_ylabel(f'{ch2_name} rel. diff. (ppm)')

    h = axs[0,1].hist2d(rdf1, rdf2, bins=100, cmap='turbo', norm=LogNorm())
    axs[0,1].set_xticks([])
    axs[0,1].set_yticks([])
    pos = axs[0, 1].get_position()

    # Create a new axis for the colorbar
    cbar_ax = fig.add_axes([
        pos.x1 + 0.01,  # x: slightly to the right of axs[0,1]
        pos.y0,         # y: same bottom as axs[0,1]
        0.02,           # width of colorbar
        pos.height      # same height as axs[0,1]
    ])
    fig.colorbar(h[3], cax=cbar_ax)

    n, bins, _ = axs[1,0].hist(ddfs, bins=100, histtype='step', color='purple')
    if np.std(ddfs) >= 0.5:
        bin_centers = (bins[:-1] + 0.5*np.diff(bins))
        def mygaussian(x, N, sigma, mu):
            return N/(sigma*np.sqrt(2*np.pi)) * np.exp(-0.5*(x-mu)*(x-mu)/(sigma*sigma))
        p0 = [np.sum(n), np.std(ddfs), 0]
        popt_ddf, pcov = curve_fit(mygaussian, bin_centers, n, p0=p0)
        mygauss_domain = np.linspace(bin_centers[0], bin_centers[-1], 300)
        axs[1,0].plot(mygauss_domain, mygaussian(mygauss_domain, *popt_ddf), color='teal')
    try:
        axs[1,0].text(0.2*bins[-1], 0.75*np.max(n), f'$\\sigma={round(popt_ddf[1], 2)}$ ppm', fontdict=dict(size=14))
        popt_ddf = None
    except:
        axs[1,0].text(0.2*bins[-1], 0.75*np.max(n), f'$\\sigma={round(np.std(ddfs), 2)}$ ppm', fontdict=dict(size=14))
        # axs[1,0].text(0.2*bins[-1], 0.75*np.max(n), f'$\\sigma={round(np.std(ddfs), 2)}$ ppm', fontdict=dict(size=14))
    axs[1,0].set_yscale('log')
    axs[1,0].minorticks_on()
    axs[1,0].set_xlabel(f'{ch1_name} - {ch2_name} ddf (ppm)')

    n, bins, _ = axs[1,1].hist(rdf2, bins=100, histtype='step', color='purple')
    axs[1,1].text(0.2*bins[-1], 0.75*np.max(n), f'$\\sigma={round(np.std(rdf2), 2)}$ ppm', fontdict=dict(size=14))
    axs[1,1].set_yscale('log')
    axs[1,1].minorticks_on()
    axs[1,1].yaxis.tick_right()
    axs[1,1].set_xlabel(f'{ch2_name} rel. diff (ppm)')

    fig.savefig(dir_path + 'ddf_plot')
    
    return

def plot_diff_nonlinearity(rdfs, ddfs, dir_path):

    fig, ax = plt.subplots()
    ax.scatter(rdfs*1e6, ddfs*1e6, marker='.', color='black')
    ax.set_xlabel('RDF (ppm)')
    ax.set_ylabel('DDF (ppm)')
    ax.xaxis.set_ticks_position('both')
    ax.yaxis.set_ticks_position('both')
    ax.xaxis.minorticks_on()
    ax.yaxis.minorticks_on()
    fig.savefig(dir_path + 'ddf_vs_rdf_scatter')

    bins = np.linspace(rdfs.min(), rdfs.max(), 21)
    idx = np.digitize(rdfs, bins)

    centers, means, ses = [], [], []
    for b in range(1, len(bins)):
        sel = idx == b
        n = sel.sum()
        if n > 1:
            m = ddfs[sel].mean()
            se = ddfs[sel].std(ddof=1) / np.sqrt(n)
            center = 0.5 * (bins[b-1] + bins[b])
            centers.append(center)
            means.append(m)
            ses.append(se)
            print(f"{center*1e6:8.0f}  mean={m*1e6:7.3f}  se={se*1e6:6.3f}  ({m/se:+.1f} sigma)")

    centers = np.array(centers)
    means = np.array(means)
    ses = np.array(ses)

    fig, ax = plt.subplots()
    ax.errorbar(centers*1e6, means*1e6, yerr=ses*1e6, fmt='o', capsize=3, color='black')
    ax.axhline(0, color='red', linestyle='--', linewidth=1)
    ax.set_xlabel('RDF (ppm)')
    ax.set_ylabel('Binned mean DDF (ppm)')
    ax.xaxis.set_ticks_position('both')
    ax.yaxis.set_ticks_position('both')
    ax.xaxis.minorticks_on()
    ax.yaxis.minorticks_on()
    fig.savefig(dir_path + 'ddf_vs_rdf_residuals')

    return

if __name__ == '__main__':

    args = sys.argv

    fft_bins = 1

    roll_channel = -1
    roll_amount = 0

    for i, arg in enumerate(args):
        if arg == '-d' or arg == '--dec':
            DECIMATION = int(args[i+1])
        elif arg == '-f' or arg == '--format':
            FORMAT_DATA = 1
        elif arg == '-chs' or arg == '--channels':
            CH1, CH2 = int(args[i+1]), int(args[i+2])
        elif arg == '-fb' or arg == '--fft-bins':
            fft_bins = int(args[i+1])
        elif arg == '-r':
            roll_channel = int(args[i+1])
            roll_amount = int(args[i+2])
        elif arg == '-v' or arg == '--verbose':
            VERBOSE = True
        elif arg == '-h' or arg == '--help':
            print('''Usage: python3 compute_resolution.py <npy_file_path> [-h | --help] [-options]

OPTIONS
    A summary of options is included below.

    [-h | --help]
        Show this summary of options.

    [-d <dec> | --dec <dec>]
        Sets decimation to the integer <dec>. Sampling frequency is then 5e9/<dec>. Default is <dec>=1.
                  
    [-f | --format ]
        Formats data from muxed RFSoC format into 2D numpy array indexed by I/Q channel.
    
    [-chs <ch1> <ch2> | --channels <ch1> <ch2> ]
        Chooses which two channels to compare to compute the resolution.
            
    [-fb <num> | --fft-bins <num> ]
        Sets the number of frequency bins to use to <num>. Default is <num>=1.
                  
    [-r <ch> <samples> | --roll <ch> <samples> ]
        Rolls channel <ch> by number of samples <samples>. Default behavior is no rolling. 
    [-v | --verbose ]
''')
            exit()
    
    if len(args) == 1:
        print('Error: `compute_resolution.py` takes at least 1 argument')
        exit()


    cwd = os.path.dirname(os.path.abspath(__file__))
    tmp_dir_path = cwd + '/' + 'tmp/'
    if not os.path.isdir(tmp_dir_path):
        print('Error: cannot find ./tmp/ directory. Please create it, then try again')
        exit()

    print(f'Data file path: {args[1]}')
    print(f'tmp/ directory path: {tmp_dir_path}')
    # print(f'Channel 1: {CH1}') 
    # print(f'Channel 2: {CH2}')
    # if roll_channel >= 0:
    #     print(f'Roll channel: {roll_channel}')
    #     print(f'Roll amount: {roll_amount}')
    # else:
    #     print(f'Not rolling any channels.')
    
    data_path = args[1]


    data = np.fromfile(data_path, dtype=np.uint64)
    data_gate = np.fromfile(data_path + '_gate', dtype=np.uint64)

    buffer_np, first_ts, edge_times = format_data(data, data_gate, ACLK_FREQ, WORDS_PER_PACKET, HEADER_WORDS)
    data = None
    data_gate = None

    even_mask = np.arange(buffer_np.shape[0])%2 == 0
    buffer_np = buffer_np[even_mask,:] + 1j * buffer_np[~even_mask,:]

    dc_data = process_to_dc(buffer_np, 5e9 / 320, fft_bins=10)
    buffer_np = None

    time_domain = first_ts/ACLK_FREQ + np.arange(dc_data.shape[1]) / (5e9 / 320)
    integrated_data = gate_means(dc_data, time_domain, edge_times)

    if integrated_data.shape[1]%2 != 0:
        integrated_data = integrated_data[:,:-1]

    even_mask = np.arange(integrated_data.shape[1])%2 == 0
    rdfs = (integrated_data[:,even_mask] - integrated_data[:,~even_mask]) / (integrated_data[:,even_mask] + integrated_data[:,~even_mask]) 

    ddfs = rdfs[2] - rdfs[3]

    plot_nice_ddf(ddfs*1e6/np.sqrt(2), rdfs[2]*1e6, rdfs[3]*1e6, 'Ch 2 RDF', 'Ch 1 RDF', tmp_dir_path)

    plot_diff_nonlinearity(rdfs[3], ddfs/np.sqrt(2), tmp_dir_path)



    # if FORMAT_DATA:
    #     data = format_data(data_path)
    # else:
    #     try:
    #         data = np.load(data_path)
    #     except:
    #         print(f'Error: cannot open file: {data_path}')
    #         raise

    # if roll_channel >= 0:
    #     # Rolls I and Q
    #     data[2*roll_channel] = np.roll(data[2*roll_channel], roll_amount)
    #     data[2*roll_channel+1] = np.roll(data[2*roll_channel+1], roll_amount)

    #     # Trims to avoid discontinuities
    #     data = data[:, roll_amount:-roll_amount]

    # iq_data, num_samp = get_iq_data(data, CH1, CH2)

    # print(f'Number of samples: {num_samp}')

    # if fft_bins >= int(SAMP_FREQ/FLIP_FREQ):
    #     print(f'Error: number of FFT bins ({fft_bins}) exceeds number of {FLIP_FREQ} Hz windows ({int(SAMP_FREQ/FLIP_FREQ)}).')
    #     exit()

    # dc_data = process_to_dc(iq_data, SAMP_FREQ, fft_bins=fft_bins)
    # ddfs, rdfs = compute_ddf(dc_data, SAMP_FREQ)

    # np.savez(tmp_dir_path + 'ddfs_rdfs_BINNED', ddfs=ddfs, rdfs=rdfs)