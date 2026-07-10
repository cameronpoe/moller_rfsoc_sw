import numpy as np

py = np.loadtxt("ref_formatted_first1000.csv", delimiter=",", dtype=np.int32)
cc = np.loadtxt("c_first1000.csv", delimiter=",", dtype=np.int32)

print(py.shape, cc.shape)
print(np.max(np.abs(py - cc)))
print(np.where(py != cc))


# import numpy as np

# py = np.loadtxt("py_pi_process_to_dc.csv", delimiter=",", skiprows=1)
# cc = np.loadtxt("c_process_to_dc.csv", delimiter=",", skiprows=1, usecols=1)

# n = min(len(py), len(cc))
# py = py[:n]
# cc = cc[:n]

# diff = py - cc

# print("n =", n)
# print("max abs diff =", np.max(np.abs(diff)))
# print("mean abs diff =", np.mean(np.abs(diff)))
# print("rms diff =", np.sqrt(np.mean(diff**2)))

# corr = np.corrcoef(py, cc)[0, 1]
# print("corr =", corr)

# scale = np.dot(py, cc) / np.dot(cc, cc)
# print("best scale py ~= scale*c:", scale)

# scaled_diff = py - scale * cc
# print("scaled rms diff =", np.sqrt(np.mean(scaled_diff**2)))

# print("\nfirst 20:")
# for i in range(min(20, n)):
#     print(i, "PI =", py[i], "C =", cc[i], "diff =", diff[i])




# py = np.loadtxt("py_pi_process_to_dc_block4096.csv", delimiter=",", skiprows=1)
# cc = np.loadtxt("c_process_to_dc.csv", delimiter=",", skiprows=1, usecols=1)

# n = min(len(py), len(cc))
# py = py[:n]
# cc = cc[:n]

# diff = py - cc

# print("n =", n)
# print("max abs diff =", np.max(np.abs(diff)))
# print("mean abs diff =", np.mean(np.abs(diff)))
# print("rms diff =", np.sqrt(np.mean(diff**2)))
# print("corr =", np.corrcoef(py, cc)[0, 1])

# scale = np.dot(py, cc) / np.dot(cc, cc)
# print("best scale =", scale)
# print("scaled rms =", np.sqrt(np.mean((py - scale * cc)**2)))

# for i in range(20):
#     print(i, "PI =", py[i], "C =", cc[i], "diff =", diff[i])