import numpy as np
import sys, os

if __name__ == '__main__':

    dir_path = sys.argv[1]

    cwd = os.path.dirname(os.path.abspath(__file__))
    tmp_dir_path = cwd + '/' + 'tmp/'
    data_dir_path = cwd + '/' + dir_path

    ddf_names = os.listdir(data_dir_path)

    ddfs_big = np.array([], dtype=np.float64)
    rdfs_1_big = np.array([], dtype=np.float64)
    rdfs_2_big = np.array([], dtype=np.float64)

    for i in range(len(ddf_names)):
        npzfile = np.load(data_dir_path + ddf_names[i])
        ddfs_big = np.append(ddfs_big, npzfile['ddfs'].flatten())
        rdfs_1_big = np.append(rdfs_1_big, npzfile['rdfs'][:,0,:].flatten())
        rdfs_2_big = np.append(rdfs_2_big, npzfile['rdfs'][:,1,:].flatten())

    rdfs_big = np.array([rdfs_1_big, rdfs_2_big])
    
    np.savez(tmp_dir_path + 'bigddfs', ddfs=ddfs_big, rdfs=rdfs_big)