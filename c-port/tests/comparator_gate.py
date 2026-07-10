import numpy as np
import pandas as pd

py_ev = pd.read_csv("py_gate_events.csv")
cc_ev = pd.read_csv("c_gate_events.csv")

py_win = pd.read_csv("py_gate_windows.csv")
cc_win = pd.read_csv("c_gate_windows.csv")

print("events shapes:", py_ev.shape, cc_ev.shape)
print("windows shapes:", py_win.shape, cc_win.shape)

event_cols_exact = ["idx", "raw_word", "msb", "edge_name", "ts_ticks"]

for col in event_cols_exact:
    same = (py_ev[col].to_numpy() == cc_ev[col].to_numpy()).all()
    print(f"events {col}: same =", same)

print("events time max diff =",
      np.max(np.abs(py_ev["time_sec"].to_numpy() - cc_ev["time_sec"].to_numpy())))

window_cols_exact = [
    "idx",
    "rising_ticks",
    "falling_ticks",
    "width_ticks",
]

for col in window_cols_exact:
    same = (py_win[col].to_numpy() == cc_win[col].to_numpy()).all()
    print(f"windows {col}: same =", same)

for col in ["rising_sec", "falling_sec", "width_sec"]:
    print(f"windows {col} max diff =",
          np.max(np.abs(py_win[col].to_numpy() - cc_win[col].to_numpy())))

bad_events = np.where(py_ev["ts_ticks"].to_numpy() != cc_ev["ts_ticks"].to_numpy())[0]
bad_windows = np.where(py_win["rising_ticks"].to_numpy() != cc_win["rising_ticks"].to_numpy())[0]

print("bad event count:", len(bad_events))
print("bad window count:", len(bad_windows))

if len(bad_events) > 0:
    print("first bad event:")
    i = bad_events[0]
    print("PY:")
    print(py_ev.iloc[i])
    print("C:")
    print(cc_ev.iloc[i])

if len(bad_windows) > 0:
    print("first bad window:")
    i = bad_windows[0]
    print("PY:")
    print(py_win.iloc[i])
    print("C:")
    print(cc_win.iloc[i])