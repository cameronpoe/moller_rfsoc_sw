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

def get_iq_data(data_path):

    try:
        data = np.load(data_path)
    except:
        print('Error: cannot open numpy file.')
        raise

    num_pairs = data.shape[0]
    num_samp = data.shape[2]

    iq_data = np.zeros((2, num_pairs, num_samp), dtype=np.complex128)
    iq_data[0] = data[:,0,:] + 1j*data[:,1,:]
    iq_data[1] = data[:,2,:] + 1j*data[:,3,:]
    
    return iq_data, num_pairs, num_samp

def process_to_dc(iq_data, num_pairs, num_samp, samp_freq):

    freq_domain = fftfreq(num_samp, 1/samp_freq)
    iq_data_freq = fft(blackman(num_samp)*iq_data, axis=2)

    # Finds index of the highest-power signal (carrier)
    carrier_indices = np.argmax(np.abs(iq_data_freq), axis=2)

    # Slices of indices immediately (3 away) around carrier index
    slices = carrier_indices[:,:,np.newaxis] + np.arange(-3, 4)[np.newaxis,np.newaxis,:]

    # Frequencies around carrier frequency
    freq_neighborhoods = np.take_along_axis(np.repeat(np.repeat(freq_domain[np.newaxis,:], slices.shape[1], axis=0)[np.newaxis,:,:], slices.shape[0], axis=0), slices, axis=2) # gross repeating thing in first position just puts a copy of freq domain into every entry of a (2,500) array

    # FFT spectrum around carrier frequency
    iq_data_freq_neighborhoods = np.abs(np.take_along_axis(iq_data_freq, slices, axis=2))
    slices = None

    # Carrier frequency is found by weighted average of frequencies around the highest-power one. Since DC peak is not a delta, since if the true frequency is shifting, power is shifting among the FFT bins. 
    carrier_freqs = np.sum(freq_neighborhoods * iq_data_freq_neighborhoods/np.sum(iq_data_freq_neighborhoods, axis=2)[:,:,np.newaxis], axis=2)
    iq_data_freq_neighborhoods, freq_neighborhoods = None, None

    # Phases of the carrier signal
    row_inds = np.arange(2)[:,None]
    col_inds = np.arange(num_pairs)
    carrier_phases = np.abs(np.angle(iq_data_freq[row_inds, col_inds, carrier_indices]))

    # Down-mixes and eliminates any the phase due to the carrier
    iq_data = iq_data * np.exp(-1j * (2*np.pi*carrier_freqs[:,:,np.newaxis]/samp_freq*np.arange(num_samp) + carrier_phases[:,:,np.newaxis]))

    # Takes away any remnant phases between I/Q data
    avg_phases = np.average(np.unwrap(np.angle(iq_data)), axis=2) # unwrap is very important here b/c we're averaging. if angle is fluctuating around +/- pi, the average of np.angle() could be ~0, but average of np.unwrap(np.angle()) will be the correct phase
    iq_data *= np.exp(-1j*avg_phases[:,:,np.newaxis])
    return np.real(iq_data)    

def compute_ddf(dc_data, num_samp, samp_freq):

    time_domain = np.arange(num_samp)/samp_freq
    mask_l = (time_domain >= 0) & (time_domain < 1/FLIP_FREQ)
    mask_r = (time_domain >= 1/FLIP_FREQ) & (time_domain < 2/FLIP_FREQ)

    window_length = mask_l.sum() + mask_r.sum()
    num_windows = dc_data.shape[-1]//window_length

    rdfs_full = np.zeros((num_windows, 2, dc_data.shape[1]), dtype=np.float64)
    ddfs_full = np.zeros((num_windows, dc_data.shape[1]), dtype=np.float64)

    for i in range(num_windows):
        
        mask_l = np.roll(mask_l, i*window_length)
        mask_r = np.roll(mask_r, i*window_length)

        l_data = np.average(dc_data[:,:,mask_l], axis=2)
        r_data = np.average(dc_data[:,:,mask_r], axis=2)

        rdfs = (r_data - l_data)/(r_data + l_data)
        ddfs = 1/np.sqrt(2) * (rdfs[0] - rdfs[1])

        rdfs_full[i,:,:] = np.copy(rdfs)
        ddfs_full[i,:] = np.copy(ddfs)

    return ddfs_full, rdfs_full

if __name__ == '__main__':

    args = sys.argv

    for i, arg in enumerate(args):
        if arg == '-d' or arg == '--dec':
            DECIMATION = int(args[i+1])
        if arg == '-f' or arg == '--format':
            FORMAT_DATA = 1
        elif arg == '-h' or arg == '--help':
            print('''Usage: python3 compute_resolution.py <npy_file_path> [-h | --help] [-options]

OPTIONS
    A summary of options is included below.

    [-h | --help]
        Show this summary of options.

    [-d <dec> | --dec <dec>]
        Sets decimation to the integer <dec>. Sampling frequency is then 5e9/<dec>. Default is <dec>=1.
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
    
    data_path = args[1]
    if FORMAT_DATA:
        format_data(data_path)
        exit()
    iq_data, num_pairs, num_samp = get_iq_data(data_path)
    dc_data = process_to_dc(iq_data, num_pairs, num_samp, SAMP_FREQ)
    ddfs, rdfs = compute_ddf(dc_data, num_samp, SAMP_FREQ)

    np.savez(tmp_dir_path + 'ddfs_rdfs', ddfs=ddfs, rdfs=rdfs)


