import numpy as np
from numpy.fft import fft, fftfreq
import matplotlib.pyplot as plt 
from scipy.signal.windows import blackman
import sys, os

# Default global variables
FULL_FREQ = 5e9
DECIMATION = 1
FLIP_FREQ = 1.92e3
FORMAT_DATA = 0
CH1, CH2 = 0, 1

def format_data(data_path):
    """Reformat muxed data from the RFSoC.

    I/Q Data from all 4 channels of the RFSoC comes in a single buffer. A single I or Q
        channel produces 24 bit words, and the 8 words from the 4 I/Q channels are then
        muxed into 3 64 bit words. 
    This function takes a 1D numpy array of type np.uint64 and reformats it into a 2D 
        array of shape (8, # of samples) where each element is a 24 bit word stored as 
        an np.int32 element. 
    
    Parameters
    ----------
    data_path : String
        A string representing the location of the saved np.uint64 array to be converted.

    """

    data = np.load(data_path)

    # Performs the formatting. First, changes the endianness, then interprets the data as
    #       single bytes. Reshapes so each element i (e.g. array[i,:]) is a list of each
    #       byte in the 24 bit word. Combines them into a 24 bit word, but the sign bit 
    #       is in the 24th bit location instead of the 32nd bit location for an int32
    #       container, so bitshifts so the sign bit is in the correct position, casts
    #       to the signed np.int32 format, and bitshifts back, which keeps the sign bit
    #       in the MSB position. 
    data = np.frombuffer(data.byteswap(), dtype=np.uint8).reshape((-1,3)).astype(np.uint32)
    data = (data[:,0] << 16) + (data[:,1] << 8) + (data[:,2])
    data = data << 8
    data = data.astype(np.int32)
    data = data >> 8
    data = data.reshape((-1,8)).T

    return data 

def get_iq_data(data, ch1, ch2):

    num_samp = data.shape[1]

    iq_data = np.zeros((2, num_samp), dtype=np.complex128)
    iq_data[0] = data[2*ch1,:] + 1j*data[2*ch1 + 1,:]
    iq_data[1] = data[2*ch2,:] + 1j*data[2*ch2 + 1,:]
    
    return iq_data, num_samp

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

    # Finds index of the highest-power signal (i.e. the carrier)
    carrier_indices = np.argmax(np.abs(iq_data_freq), axis=2)

    # Slices of indices immediately (3 away) around carrier index
    slices = carrier_indices[:,:,np.newaxis] + np.arange(-3, 4)[np.newaxis,np.newaxis,:]

    # Frequencies around carrier frequency
    freq_neighborhoods = freq_domain[slices]
  
    # FFT spectrum around carrier frequency
    iq_data_freq_neighborhoods = np.abs(np.take_along_axis(iq_data_freq, slices, axis=2))
    slices = None

    # Carrier frequency is found by weighted average of frequencies around the highest-power one. Since DC peak is not a delta, since if the true frequency is shifting, power is shifting among the FFT bins. 
    carrier_freqs = np.sum(freq_neighborhoods * iq_data_freq_neighborhoods/(np.sum(iq_data_freq_neighborhoods, axis=2)[:,:,np.newaxis]), axis=2)
    iq_data_freq_neighborhoods, freq_neighborhoods = None, None

    # Phases of the carrier signal
    carrier_phases = np.unwrap(np.angle(np.take_along_axis(iq_data_freq, carrier_indices[:,:,np.newaxis], axis=2))).squeeze(axis=2)

    # Down-mixes and eliminates any the phase due to the carrier
    iq_data = iq_data * np.exp(-1j * (2*np.pi*carrier_freqs[:,:,np.newaxis]/samp_freq*np.arange(bin_samps) + carrier_phases[:,:,np.newaxis]))

    # Takes away any remnant phases between I/Q data
    avg_phases = np.average(np.unwrap(np.angle(iq_data)), axis=2) # unwrap is very important here b/c we're averaging. if angle is fluctuating around +/- pi, the average of np.angle() could be ~0, but average of np.unwrap(np.angle()) will be the correct phase
    iq_data *= np.exp(-1j*avg_phases[:,:,np.newaxis])

    iq_data = iq_data.reshape((-1, new_num_samps))

    return np.real(iq_data)    

def compute_ddf(dc_data, samp_freq):

    # We want to trim the DC data so that only an integer number of helicity window pairs can fit inside it
    num_samples_per_helicity_window = int(np.round(samp_freq / FLIP_FREQ))
    num_samples_per_window_pair = num_samples_per_helicity_window * 2
    extra_samples = dc_data.shape[-1]%num_samples_per_window_pair
    dc_data = dc_data[:,:-extra_samples]
    num_windows = dc_data.shape[1] // num_samples_per_window_pair

    dc_data = dc_data.reshape((-1,num_windows,num_samples_per_window_pair))

    ldata = np.mean(dc_data[:,:,:num_samples_per_helicity_window], axis=2)
    rdata = np.mean(dc_data[:,:,num_samples_per_helicity_window:], axis=2)

    rdfs = (rdata - ldata)/(rdata+ldata)
    ddfs = rdfs[0,:] - rdfs[1,:]

    return ddfs, rdfs

if __name__ == '__main__':

    args = sys.argv

    fft_bins = 1

    for i, arg in enumerate(args):
        if arg == '-d' or arg == '--dec':
            DECIMATION = int(args[i+1])
        elif arg == '-f' or arg == '--format':
            FORMAT_DATA = 1
        elif arg == '-chs' or arg == '--channels':
            CH1, CH2 = int(args[i+1]), int(args[i+2])
        elif arg == '--fft-bins':
            fft_bins = int(args[i+1])
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

    SAMP_FREQ = FULL_FREQ/DECIMATION

    print(f'Data file path: {args[1]}')
    print(f'tmp/ directory path: {tmp_dir_path}')
    print(f'Sampling frequency: {round(SAMP_FREQ*1e-6, 3)} MHz')   
    print(f'Channel 1: {CH1}') 
    print(f'Channel 2: {CH2}')
    
    data_path = args[1]

    if FORMAT_DATA:
        data = format_data(data_path)
    else:
        try:
            data = np.load(data_path)
        except:
            print(f'Error: cannot open file: {data_path}')
            raise


    iq_data, num_samp = get_iq_data(data, CH1, CH2)

    print(f'Number of samples: {num_samp}')

    if fft_bins >= int(SAMP_FREQ/FLIP_FREQ):
        print(f'Error: number of FFT bins ({fft_bins}) exceeds number of {FLIP_FREQ} Hz windows ({int(SAMP_FREQ/FLIP_FREQ)}).')
        exit()

    dc_data = process_to_dc(iq_data, SAMP_FREQ, fft_bins=fft_bins)
    ddfs, rdfs = compute_ddf(dc_data, SAMP_FREQ)

    np.savez(tmp_dir_path + 'ddfs_rdfs_BINNED', ddfs=ddfs, rdfs=rdfs)


