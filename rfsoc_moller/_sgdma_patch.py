from pynq.lib.dma import _SGDMAChannel, DMA_TYPE_TX
from pynq import allocate
import numpy as np

if not hasattr(_SGDMAChannel, "transfer_rfsoc"):
    
    def transfer_rfsoc(self, array, packetbytes, cyclic=False):
        """Transfer memory with the DMA

        Transfer must only be called when the channel is halted
        For `nbytes`, 0 means everything after the starting point.

        If the AXI DMA is not configured for data re-alignment then a
        valid address must be aligned or undefined results occur.

        For MM2S (send), if Data Realignment Engine (DRE) is not included,
        the source address must be MM2S memory map data width aligned.

        For S2MM (recv), if Data Realignment Engine is not included,
        the destination address must be S2MM Memory Map data width aligned.

        For example, if memory map data width = 32, data is aligned if it is
        located at word offsets (32-bit offset), that is, 0x0, 0x4, 0x8, 0xC,
        and so forth.

        Cyclic Buffer Descriptor (BD) mode allows the DMA to loop through the
        buffer descriptors without user intervention. As the DMA cycles through
        the BDs indefinitely, the wait() function is not valid in this mode.
        Instead, use the stop() function to terminate DMA operation. This mode
        is only valid for the sendchannel.

        Parameters
        ----------
        array : ContiguousArray
            An contiguously allocated array to be transferred
        packetbytes: int
            The length of a packet sent to the DMA in bytes.
        start : int
            Offset into array to start. Default is 0.
        nbytes : int
            Number of bytes to transfer. Default is 0.
        cyclic : bool
            Enable cyclic BD mode. Default is False.

        """

        if not self.halted:
            raise RuntimeError("DMA channel not halted")
        nbytes = array.nbytes
        if nbytes%packetbytes != 0:
            raise RuntimeError(f"Array size is not a multiple of packetbytes: {packetbytes}")
        if int(nbytes/packetbytes) < 2:
            raise RuntimeError(f"Array size is not >= 2 packetbytes sizes")
        if packetbytes%self._align != 0:
            raise RuntimeError(f"packetbytes is not a multiple of self._align: {self._align}")
        start = 0 # start at beginning of the array
        if not self._dre and ((array.physical_address + start) % self._align) != 0:
            raise RuntimeError(
                "DMA does not support unaligned transfers; "
                "Starting address must be aligned to "
                "{} bytes.".format(self._align)
            )

        self._cyclic = cyclic

        if self._cyclic and (self._tx_rx != DMA_TYPE_TX):
            raise RuntimeError('Cyclic BD mode only valid in sendchannel')

        if packetbytes>(self._max_size - (self._max_size % self._align)):
            raise RuntimeError(f"packetbytes exceeds maximum length: {self._max_size - (self._max_size % self._align)}")
        remain = nbytes
        blk_size = packetbytes
        self._num_descr = int(nbytes/blk_size)

        # Zero-Allocate buffer for descriptors: uint32[_num_descr][16]
        # Descriptor is only 52 bytes but each one has to be 64-byte aligned!
        self._descr = allocate(shape=(self._num_descr, 16), dtype=np.uint32, target=self.device)

        # Idle DMA engine
        self.stop()


        # Fill out descriptors
        for i in range(0, self._num_descr):
            # Next descriptor (64-bit)
            if self._cyclic and (i == (self._num_descr - 1)):
                # In cyclic BD mode the last descriptor points back the the first
                self._descr[i, 0] = (
                self._descr.physical_address) & 0xffffffff
                self._descr[i, 1] = (
                self._descr.physical_address >> 32) & 0xffffffff
            else:
                self._descr[i, 0] = (
                self._descr.physical_address + (((i + 1) % self._num_descr) * 16 * 4)
                ) & 0xFFFFFFFF
                self._descr[i, 1] = (
                self._descr.physical_address + (((i + 1) % self._num_descr) * 16 * 4)
                >> 32
                ) & 0xFFFFFFFF

            # Buffer length
            if remain > blk_size:
                d_len = blk_size
            else:
                d_len = remain
            self._descr[i, 6] = d_len

            remain -= d_len

            # Buffer address (64-bit)
            self._descr[i, 2] = (array.physical_address + (i * blk_size)) & 0xFFFFFFFFFF # Changed to 0xFF FFFF FFFF from 0xFFFF FFFF
            self._descr[i, 3] = (
                (array.physical_address + start + (i * blk_size)) >> 32
            ) & 0xFFFFFFFF

            # First block
            if i == 0:
                self._descr[i, 6] |= 1 << 27

            # Last Block
            if remain == 0:
                self._descr[i, 6] |= 1 << 26

        if self._flush_before:
            array.flush()

        # Flush DMA descriptors
        self._descr.flush()

        # Write first desc
        self._mmio.write(self._offset + 0x08, self._descr.physical_address & 0xFFFFFFFF)
        self._mmio.write(
            self._offset + 0x0C, (self._descr.physical_address >> 32) & 0xFFFFFFFF
        )

        self._active_buffer = array

        # Let's go!
        self.transferred = 0
        self.start()

        # Writing last desc triggers the descriptor fetches
        if self._cyclic:
            # In cyclic BD mode the tail descriptor register must be programmed
            # with a value which is not part of the BD chain.
            self._mmio.write(
                    self._offset + 0x10,
                    (self._descr.physical_address +
                        ((self._num_descr) * 16 * 4)) & 0xffffffff)
            self._mmio.write(
                    self._offset + 0x14,
                    ((self._descr.physical_address +
                        ((self._num_descr) * 16 * 4)) >> 32) & 0xffffffff)
        else:
            self._mmio.write(
                    self._offset + 0x10,
                    (self._descr.physical_address +
                        ((self._num_descr - 1) * 16 * 4)) & 0xffffffff)
            self._mmio.write(
                    self._offset + 0x14,
                    ((self._descr.physical_address +
                        ((self._num_descr - 1) * 16 * 4)) >> 32) & 0xffffffff)
        return self._descr

    _SGDMAChannel.transfer_rfsoc = transfer_rfsoc

