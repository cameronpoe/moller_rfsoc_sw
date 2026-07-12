#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

#include "rfsoc_types.h"
#include "io.h"
#include "iq_rot.h"
#include "integrator.h"
#include "tests/test2.h"

int main(
	void) {

	// return dump_process_to_dc_csv(
	//     "tests/20260624-3_data",
	//     "tests/c_process_to_dc.csv",
	//     0,
	//     4096,
	//     1000
	// );
	// return dump_gate_parser_csv(
	//     "tests/20260624-3_data_gate",
	//     "tests/c_gate_events.csv",
	//     "tests/c_gate_windows.csv"
	// );
	return dump_process_to_dc_csv(
		"tests/20260624-3_data",
		"tests/c_bullshit.csv",
		2,
		32768,
		1000);
}
