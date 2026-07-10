import numpy as np

py = np.loadtxt("py_pi_process_to_dc.csv", delimiter=",", skiprows=1)
cc = np.loadtxt("c_bullshit.csv", delimiter=",", skiprows=1, usecols=1)

n = min(len(py), len(cc))
py = py[:n]
cc = cc[:n]

diff = py - cc
ppm = 1e6 * abs(diff) / np.mean(np.abs(py))

print("n =", n)
print("max abs diff =", np.max(np.abs(diff)))
print("mean abs diff =", np.mean(np.abs(diff)))
print("rms diff =", np.sqrt(np.mean(diff**2)))

bad = np.where(np.abs(diff) > 1e-6)[0]
print("bad count =", len(bad))

print("\nfirst 10 comparison:")
for i in range(min(800, n)):
    # print(i, "py =", py[i], "c =", cc[i], "diff =", diff[i])
    print(i, "py =", py[i], "c =", cc[i], "diff =", diff[i], "ppm = ", ppm[i])

if len(bad) > 0:
    print("\nfirst bad indices:")
    for i in bad[:20]:
        print(i, "py =", py[i], "c =", cc[i], "diff =", diff[i], "ppm = ", ppm[i])