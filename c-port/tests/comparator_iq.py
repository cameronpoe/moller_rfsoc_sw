import numpy as np

py = np.loadtxt("py_iq_before_dc.csv", delimiter=",", skiprows=1)
cc = np.loadtxt("c_iq_before_dc.csv", delimiter=",", skiprows=1)

diff = py - cc

print("max abs diff real =", np.max(np.abs(diff[:,0])))
print("max abs diff imag =", np.max(np.abs(diff[:,1])))

print(py[:10])
print(cc[:10])