# -------------------------------------------------------------------------------------------------
# Copyright (C) 2026 University of California, Berkeley
# SPDX-License-Identifier: MIT
# ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- --
# This package is significantly built around the RFSoC-MTS package from Advanced Micro Devices, Inc.
# ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- --
import pynq
from pynq import Overlay, MMIO, allocate
import xrfclk
import xrfdc
import numpy as np
import time
import os
import subprocess

# MODULE_PATH = os.path.dirname(os.path.realpath(__file__))
MODULE_PATH = os.path.dirname(os.path.realpath('/home/xilinx/jupyter_notebooks/moller/cameron/')) # just for prototyping, uncomment above later
CLOCKWIZARD_LOCK_ADDRESS = 0x0004
CLOCKWIZARD_RESET_ADDRESS = 0x0000
CLOCKWIZARD_RESET_TOKEN = 0x000A
MTS_START_TILE = 0x01
MAX_DAC_TILES = 4
MAX_ADC_TILES = 4
DAC_REF_TILE = 2
ADC_REF_TILE = 2
DEVICETREE_OVERLAY_FOR_PLDRAM = 'ddr4.dtbo'

ACLK_FREQ = 125e6 # Frequency of the clock associated with the RFDC output AXI stream
HEADER_WORDS = 2
BITS_PER_WORD = 64
BITS_PER_SAMPLE = 24
NUM_DATA_STREAMS = 8
SAMPLE_FREQ = 5e9
DECIMATION = 320

RFSOC4X2_LMK_FREQ = 500.0
RFSOC4X2_LMX_FREQ = 500.0
RFSOC4X2_DAC_TILES = 0b0000 # We are not using DAC, so set to 0
RFSOC4X2_ADC_TILES = 0b0101

ZCU208_LMK_FREQ = 500.0
ZCU208_LMX_FREQ = 4000.0
ZCU208_DAC_TILES = 0b0011
ZCU208_ADC_TILES = 0b0011

class mollerOverlay(Overlay):
    """
    The MOLLER overlay supports data-taking operations for the BCM receivers in the MOLLER experiment.
    """
    def __init__(self, bitfile_name='mts.bit', **kwargs):
        """
         This overlay class supports the MOLLER overlay. It configures the PL gpio, internal memories,
         PL memory, and DMA interfaces. There are additional helper functions to: Configure and verify
         MTS, set data-taking parameters, and format ADC data. In addition to the bitfile_name, the 
         active ADC and DAC tiles must be provided to use in the MTS initialization.         
        """
        board = os.getenv('BOARD') 
        # Run lsmod command to get the loaded modules list
        output = subprocess.check_output(['lsmod'])
        # Check if "zocl" is present in the output
        if b'zocl' in output:
            # If present, remove the module using rmmod command
            rmmod_output = subprocess.run(['rmmod', 'zocl'])
            # Check return code
            assert rmmod_output.returncode == 0, "Could not restart zocl. Please Shutdown All Kernels and then restart"
            # If successful, load the module using modprobe command
            modprobe_output = subprocess.run(['modprobe', 'zocl'])
            assert modprobe_output.returncode == 0, "Could not restart zocl. It did not restart as expected"
        else:
            modprobe_output = subprocess.run(['modprobe', 'zocl'])
            # Check return code
            assert modprobe_output.returncode == 0, "Could not restart ZOCL!"

        dts = pynq.DeviceTreeSegment(resolve_binary_path(DEVICETREE_OVERLAY_FOR_PLDRAM))
        if not dts.is_dtbo_applied():
            dts.insert()
        # must configure clock synthesizers 
        # the LMK04828 PL_CLK and PL_SYSREF clocks
        if board == 'RFSoC4x2':
            xrfclk.set_ref_clks(lmk_freq = RFSOC4X2_LMK_FREQ, lmx_freq = RFSOC4X2_LMX_FREQ)
            self.ACTIVE_DAC_TILES = RFSOC4X2_DAC_TILES
            self.ACTIVE_ADC_TILES = RFSOC4X2_ADC_TILES
        elif board == 'ZCU208':
            xrfclk.set_ref_clks(lmk_freq = ZCU208_LMK_FREQ, lmx_freq = ZCU208_LMX_FREQ)
            self.ACTIVE_DAC_TILES = ZCU208_DAC_TILES
            self.ACTIVE_ADC_TILES = ZCU208_ADC_TILES
        else:
            assert false, "Board Not Supported"
        time.sleep(0.5)        
        super().__init__(resolve_binary_path(bitfile_name), **kwargs)
        self.xrfdc = self.usp_rf_data_converter_1       
        self.xrfdc.mts_dac_config.RefTile = DAC_REF_TILE  # DAC tile distributing reference clock
        self.xrfdc.mts_adc_config.RefTile = ADC_REF_TILE  # ADC                

        # map PL GPIO/MMIO registers
        self.fifo_flush = self.axi_gpio_0.channel1[0]
        mmio_phys_addr = int(self.ip_dict['trigger_mmio_0']['phys_addr'])
        mmio_addr_range = int(self.ip_dict['trigger_mmio_0']['addr_range'])
        self.mmio = MMIO(mmio_phys_addr, mmio_addr_range)
        
        # map DMAs
        self.dma = self.axi_dma_0
        self.dma_recv = self.dma.recvchannel
        self.dma_gate = self.axi_dma_1
        self.dma_gate_recv = self.dma_gate.recvchannel
        
        # Reset GPIOs and bring to known state
        self.fifo_flush.off() # active low flush of the DMA fifo
        
    def read_mmio(self, verbose=True):
        """
        Reads MMIO register values.
        """
        
        reg0 = self.mmio.read(0x00)
        reg1 = self.mmio.read(0x04)
        reg2 = self.mmio.read(0x08)
        reg3 = self.mmio.read(0x0C)
        reg4 = self.mmio.read(0x10)
        
        if verbose:
            print(f'Register 0: {hex(reg0)}')
            print(f'Register 1: {hex(reg1)}')
            print(f'Register 2: {hex(reg2)}')
            print(f'Register 3: {hex(reg3)}')
            print(f'Register 4: {hex(reg4)}')
            print('')
            print(f'Trigger mode: {((reg0 >> 0) & 0x0000_0001)}')
            print(f'Use ramp: {((reg0 >> 1) & 0x0000_0001)}')
            print(f'Number of triggers: {reg1}')
            if reg2 == 0:
                trigger_rate = 0
                duty_cycle = 0
            else:
                trigger_rate = ACLK_FREQ/reg2
                duty_cycle = 100*reg3/reg2
            print(f'Effective trigger rate: {trigger_rate} Hz')
            print(f'Duty cycle: {duty_cycle}%')
            print(f'Bytes per packet: {reg4 * BITS_PER_WORD/8 * 1e-3} kB')
            print(f'Words per packet: {reg4}')
                
        return [reg0, reg1, reg2, reg3, reg4]
        
    def write_mmio(self, setting, value, verbose=False):
        """
        Exposes MMIO for editing settings. Possible settings are:
         -- trigger_mode: 0 for self-trigger, 1 for external trigger
         -- use_ramp: 0 to capture ADC output, 1 for ramp octet into the mux module
         -- num_triggers: for both modes, number of full gates (one rising/one falling edge) to capture
         -- trigger_freq: for self-trigger mode, the frequency in Hz of the integrate gate
         -- duty_cycle: for self-trigger mode, fraction of the gate that is high
         -- bytes_per_packet: number of bytes per packet/buffer descriptor represents
        """
        
        # Note: trigger_mode and use_ramp are set in the same 32 bit register. Bit 0 is 
        #       is trigger_mode, bit 1 is use_ramp
        if setting == 'trigger_mode':
            if value == 1:
                self.mmio.write(0x0, self.mmio.read(0x0) | (1 << 0))
            elif value == 0:
                self.mmio.write(0x0, self.mmio.read(0x0) & ~(1 << 0))
            else:
                print(f'Error: value {value} is not valid.')            
        elif setting == 'use_ramp':
            if value == 1:
                self.mmio.write(0x0, self.mmio.read(0x0) | (1 << 1))
            elif value == 0:
                self.mmio.write(0x0, self.mmio.read(0x0) & ~(1 << 1))
            else:
                print(f'Error: value {value} is not valid.')
        elif setting == 'num_triggers':
            if value < 0 or value >= 2**32:
                print(f'Error: value {value} is not valid.')
            else:
                self.mmio.write(0x04, value)
        elif setting == 'trigger_freq':
            aclk_cycles_full = int(np.round(ACLK_FREQ/value))
            if aclk_cycles_full < 0 or aclk_cycles_full >= 2**32:
                print(f'Error: value {value} is not valid.')
            else:
                self.mmio.write(0x08, aclk_cycles_full)
        elif setting == 'duty_cycle':
            if value <= 0 or value >= 1:
                print(f'Error: value {value} is not valid.')
            else:
                aclk_cycles_full = self.mmio.read(0x08)
                if verbose:
                    print(f'Using aclk_cycles_full = {aclk_cycles_full} ({ACLK_FREQ/aclk_cycles_full} Hz trigger)')
                self.mmio.write(0x0C, int(np.round(value*aclk_cycles_full)))
        elif setting == 'bytes_per_packet':
            # `value` is the requested number of BYTES per packet.
            # Condition it so that:
            #   - it is a multiple of 4096 bytes, AND
            #   - its word count is of the form HEADER_WORDS + a*N (a, N integers),
            #     where a = words_to_fit_samples,
            # without exceeding 2**26 - 1 bytes.
            # The MMIO register expects the value in WORDS, so we convert at the end.

            alignment_bytes = 4096          # byte alignment requirement
            max_bytes = 2**26 - 1           # max bytes a buffer descriptor can represent
            bytes_per_word = BITS_PER_WORD // 8

            # a = number of words it takes to fit all streams of a single sample
            a = int(NUM_DATA_STREAMS * BITS_PER_SAMPLE / BITS_PER_WORD)

            # Alignment step expressed in WORDS (4096 bytes worth of words).
            if alignment_bytes % bytes_per_word != 0:
                print('Error: alignment_bytes is not a whole number of words.')
                return
            words_per_alignment = alignment_bytes // bytes_per_word   # e.g. 512 for 8-byte words

            # Solvability: a multiple of `words_per_alignment` can be congruent to
            # HEADER_WORDS (mod a) only if g = gcd(words_per_alignment, a) divides HEADER_WORDS.
            gcd_val = np.gcd(words_per_alignment, a)
            if HEADER_WORDS % gcd_val != 0:
                print('Error: Due to number of header words, byte alignment, and number '
                      'of words it takes to fit all streams of a single sample, no '
                      'integral words_per_packet exists.')
                return

            # Convert the requested byte count to words, clamping to the max.
            # value is in bytes; floor-divide to words (any sub-word remainder is dropped,
            # which is fine since we'll re-align to 4096-byte boundaries anyway).
            max_words = max_bytes // bytes_per_word
            target_words = min(value // bytes_per_word, max_words)

            # Largest multiple of words_per_alignment at or below the target (in words).
            candidate = (target_words // words_per_alignment) * words_per_alignment

            # Step down by one alignment block (in words) until the mod condition holds.
            # At most a // gcd_val iterations are ever needed.
            found = False
            for _ in range(a // gcd_val):
                if (candidate - HEADER_WORDS) % a == 0:
                    found = True
                    break
                candidate -= words_per_alignment

            if not found or candidate < 0:
                print('Error: no valid bytes_per_packet at or below the requested value.')
                return

            words_per_packet = int(candidate)

            if verbose:
                bytes_per_packet = words_per_packet * bytes_per_word
                print(f'bytes_per_packet requested {value}, '
                      f'set to {bytes_per_packet} ({words_per_packet} words)')

            self.mmio.write(0x10, words_per_packet)      
        else:
            print(f'Error: {setting} doesn\'t match valid list of settings.')
        
        return

    def memdict_to_view(self, ip, dtype='int16'):
        """ Configures access to internal memory via MMIO"""
        baseAddress = self.mem_dict[ip]["phys_addr"]
        mem_range = self.mem_dict[ip]["addr_range"]
        ipmmio = MMIO(baseAddress, mem_range)
        return ipmmio.array[0:ipmmio.length].view(dtype)

    def sync_tiles(self, dacTarget=-1, adcTarget=-1):
        """ Configures RFSoC MTS alignment"""
        # Set which RF tiles use MTS and turn MTS off
        if self.ACTIVE_DAC_TILES > 0:
            self.xrfdc.mts_dac_config.Tiles = self.ACTIVE_DAC_TILES # group defined in binary 0b1111
            self.xrfdc.mts_dac_config.SysRef_Enable = 1
            self.xrfdc.mts_dac_config.Target_Latency = dacTarget 
            self.xrfdc.mts_dac()
        else:
            self.xrfdc.mts_dac_config.Tiles = 0x0
            self.xrfdc.mts_dac_config.SysRef_Enable = 0
        if self.ACTIVE_ADC_TILES > 0:
            self.xrfdc.mts_adc_config.Tiles = self.ACTIVE_ADC_TILES
            self.xrfdc.mts_adc_config.SysRef_Enable = 1
            self.xrfdc.mts_adc_config.Target_Latency = adcTarget
            self.xrfdc.mts_adc()
        else:
            self.xrfdc.mts_adc_config.Tiles = 0x0
            self.xrfdc.mts_adc_config.SysRef_Enable = 0

    def init_tile_sync(self):
        """ Resets the MTS alignment engine"""
        self.xrfdc.mts_adc_config.Tiles = 0b0001
        self.xrfdc.mts_adc_config.SysRef_Enable = 1
        self.xrfdc.mts_adc_config.Target_Latency = -1
        self.xrfdc.mts_adc()
        # Reset MTS ClockWizard MMCM - refer to PG065
        self.clocktreeMTS.MTSclkwiz.mmio.write_reg(CLOCKWIZARD_RESET_ADDRESS, CLOCKWIZARD_RESET_TOKEN)
        time.sleep(0.1)
        # Reset only user selected DAC tiles
        bitvector = self.ACTIVE_DAC_TILES
        for n in range(MAX_DAC_TILES):
            if (bitvector & 0x1):
                self.xrfdc.dac_tiles[n].Reset()
            bitvector = bitvector >> 1
        # Reset ADC FIFO of only user selected tiles - restarts MTS engine
        for toggleValue in range(0,1):
            bitvector = self.ACTIVE_ADC_TILES
            for n in range(MAX_ADC_TILES):
                if (bitvector & 0x1):
                    self.xrfdc.adc_tiles[n].SetupFIFOBoth(toggleValue)
                bitvector = bitvector >> 1
 
    def verify_clock_tree(self):
        """ Verify the PL and PL_SYSREF clocks are active by verifying an MMCM is in the LOCKED state"""
        Xstatus = self.clocktreeMTS.MTSclkwiz.read(CLOCKWIZARD_LOCK_ADDRESS) # reads the LOCK register
        # the ClockWizard AXILite registers are NOT fully mapped: refer to PG065
        if (Xstatus != 1):
            raise Exception("The MTS ClockTree has failed to LOCK. Please verify board clocking configuration")
            
    def clear_buffers(self):
        try:
            self.buffer.freebuffer()
            del self.buffer
        except:
            pass

        try:
            self.buffer_gate.freebuffer()
            del self.buffer_gate
        except:
            pass
        
        return
    
    def take_data(self, verbose=False, in_place=True):
        """ Takes data using values set in MMIO. """
        
        self.clear_buffers()
        
        regs = self.read_mmio(verbose=False)
        num_triggers = regs[1]
        words_per_packet = regs[4]
        bytes_per_packet = int(words_per_packet * BITS_PER_WORD / 8)
                
        num_words_gate = 2*num_triggers
        
        words_per_packet_no_header = words_per_packet - HEADER_WORDS

        num_samples = np.round(SAMPLE_FREQ / DECIMATION / (ACLK_FREQ/regs[2]) * num_triggers)
        words_per_full_sample = int(BITS_PER_SAMPLE*NUM_DATA_STREAMS/BITS_PER_WORD)
        num_words_no_header = num_samples * words_per_full_sample
        
        new_num_words_no_header = int((num_words_no_header // words_per_packet_no_header) * words_per_packet_no_header)
        new_num_words = int((num_words_no_header // words_per_packet_no_header) * words_per_packet)
        
        new_num_samples = int(new_num_words_no_header / words_per_full_sample)
        
        self.buffer_gate = allocate(shape=(num_words_gate,), dtype=np.uint64)
        self.buffer = allocate(shape=(new_num_words,), dtype=np.uint64, target=self.ddr4_0)
        assert (self.buffer.physical_address == self.ddr4_0.base_address), "Buffer was not allocated to the expected PL-DRAM!"
        
        if verbose:
            print(f'Original number of samples: {num_samples}')
            print(f'Original number of words (no headers): {num_words_no_header}')
            print(f'New number of samples: {new_num_samples}')
            print(f'New num of words: {new_num_words}')
            print(f'Number of descriptors: {new_num_words / words_per_packet}')
            print(f'Buffer length per descriptor (kB): {bytes_per_packet * 1e-3}')
            print(f'Total buffer length (MB): {new_num_words / words_per_packet * bytes_per_packet * 1e-6}')
            print(f'Time length (sec): {new_num_samples / (SAMPLE_FREQ / DECIMATION)}')
        
        self.dma_recv.stop()
        self.dma_gate_recv.stop()

        self.fifo_flush.off()

        self.dma.register_map.S2MM_DMACR.Reset = 1
        self.dma.register_map.S2MM_DMACR.Reset = 0
        self.dma_gate.register_map.S2MM_DMACR.Reset = 1
        self.dma_gate.register_map.S2MM_DMACR.Reset = 0
        
        self.dma_recv.transfer_rfsoc(self.buffer, packetbytes=bytes_per_packet)
        self.dma_gate_recv.transfer(self.buffer_gate)

        self.fifo_flush.on()

        self.dma_recv.wait()
        self.dma_gate_recv.wait()
        
        if in_place:
            return
        else:
            return self.buffer, self.buffer_gate
    
    def format_data(self, clear_buffers=True):
    
        buffer_gate_np = np.array(self.buffer_gate)
        buffer_nonzero = buffer_gate_np[buffer_gate_np != 0]
        buffer_gate_np = None
        rising_edge_mask = ((buffer_nonzero & (1 << 63)) >> 63) == 0
        rising_edges = buffer_nonzero[rising_edge_mask]
        falling_edges = buffer_nonzero[~rising_edge_mask] & ((1 << 63) - 1)
        buffer_nonzero = None
        rising_edges = rising_edges.astype(np.float64) / ACLK_FREQ
        falling_edges = falling_edges.astype(np.float64) / ACLK_FREQ
        
        edge_times = np.array([rising_edges, falling_edges]).T
        rising_edges, falling_edges = None, None
        
        buffer_np = np.frombuffer(self.buffer, dtype=np.uint64)        
        
        regs = self.read_mmio(verbose=False)
        num_words = buffer_np.size
        words_per_packet = regs[4]
        
        expected_magic_word_indices = np.arange(int(num_words / words_per_packet)) * words_per_packet
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
        
        if clear_buffers:
            self.clear_buffers()
        
        return buffer_np, first_ts, edge_times

def resolve_binary_path(bitfile_name):
    """ this helper function is necessary to locate the bit file during overlay loading"""
    if os.path.isfile(bitfile_name):
        return bitfile_name
    elif os.path.isfile(os.path.join(MODULE_PATH, bitfile_name)):
        return os.path.join(MODULE_PATH, bitfile_name)
    else:
        raise FileNotFoundError(f'Cannot find {bitfile_name}.')
# -------------------------------------------------------------------------------------------------
