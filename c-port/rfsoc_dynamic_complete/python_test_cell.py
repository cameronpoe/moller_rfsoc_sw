import ctypes
import time

class CaptureInfo(ctypes.Structure):
    _fields_ = [("abi_version", ctypes.c_uint32), ("reserved", ctypes.c_uint32),
        ("adc_words", ctypes.c_size_t), ("adc_bytes", ctypes.c_size_t),
        ("words_per_packet", ctypes.c_size_t), ("packet_count", ctypes.c_size_t),
        ("samples_per_packet", ctypes.c_size_t), ("sample_count", ctypes.c_size_t),
        ("gate_words", ctypes.c_size_t), ("first_timestamp", ctypes.c_uint64)]

class ProcessingResult(ctypes.Structure):
    _fields_ = [("abi_version", ctypes.c_uint32), ("reserved", ctypes.c_uint32),
        ("packet_count", ctypes.c_size_t), ("sample_count", ctypes.c_size_t),
        ("gate_event_count", ctypes.c_size_t), ("window_count", ctypes.c_size_t),
        ("dc_samples_ch2", ctypes.c_size_t), ("dc_samples_ch3", ctypes.c_size_t),
        ("valid_pair_count", ctypes.c_size_t),
        ("rdf2_resolution_ppm", ctypes.c_double),
        ("rdf3_resolution_ppm", ctypes.c_double),
        ("ddf_resolution_ppm", ctypes.c_double),
        ("allocation_seconds", ctypes.c_double), ("parser_seconds", ctypes.c_double),
        ("gate_seconds", ctypes.c_double), ("dc_seconds", ctypes.c_double),
        ("integration_seconds", ctypes.c_double), ("rdf_seconds", ctypes.c_double),
        ("cleanup_seconds", ctypes.c_double), ("total_seconds", ctypes.c_double)]

LIBRARY = "/home/xilinx/daniil/moller_rfsoc_sw/c-port/build/librfsoc_processing.so"
lib = ctypes.CDLL(LIBRARY)
u64p = ctypes.POINTER(ctypes.c_uint64)
lib.rfsoc_api_version.restype = ctypes.c_uint32
lib.rfsoc_inspect_capture.argtypes = [u64p, ctypes.c_size_t, u64p,
    ctypes.c_size_t, ctypes.c_size_t, ctypes.POINTER(CaptureInfo)]
lib.rfsoc_inspect_capture.restype = ctypes.c_int
lib.rfsoc_process_capture_ex.argtypes = [u64p, ctypes.c_size_t, u64p,
    ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_bool,
    ctypes.POINTER(ProcessingResult)]
lib.rfsoc_process_capture_ex.restype = ctypes.c_int

# Execute only after both DMA wait() calls. PYNQ continues to own the buffers.
buffer.invalidate()
buffer_gate.invalidate()
adc_ptr = buffer.ctypes.data_as(u64p)
gate_ptr = buffer_gate.ctypes.data_as(u64p)

info = CaptureInfo()
inspect_status = lib.rfsoc_inspect_capture(adc_ptr, buffer.size, gate_ptr,
    buffer_gate.size, int(words_per_packet), ctypes.byref(info))
print("inspect status:", inspect_status)
print("ADC virtual / physical:", hex(buffer.ctypes.data), hex(buffer.physical_address))
print("packets / samples:", info.packet_count, info.sample_count)

if inspect_status == 0:
    result = ProcessingResult()
    wall_start = time.perf_counter()
    process_status = lib.rfsoc_process_capture_ex(adc_ptr, buffer.size, gate_ptr,
        buffer_gate.size, int(words_per_packet), 4096, False,
        ctypes.byref(result))
    wall_seconds = time.perf_counter() - wall_start
    print("process status:", process_status)
    print("events / windows / valid pairs:", result.gate_event_count,
          result.window_count, result.valid_pair_count)
    print("DC samples ch2/ch3:", result.dc_samples_ch2, result.dc_samples_ch3)
    print("RDF ch2 resolution [ppm]:", result.rdf2_resolution_ppm)
    print("RDF ch3 resolution [ppm]:", result.rdf3_resolution_ppm)
    print("DDF resolution [ppm]:", result.ddf_resolution_ppm)
    print("C times [s]: parser={:.6f}, gate={:.6f}, dc={:.6f}, "
          "integration={:.6f}, rdf={:.6f}, total={:.6f}".format(
          result.parser_seconds, result.gate_seconds, result.dc_seconds,
          result.integration_seconds, result.rdf_seconds, result.total_seconds))
    print("Python wall time [s]:", wall_seconds)
