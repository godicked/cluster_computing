import numpy as np
import sys

matrix=np.random.random((int(sys.argv[2]), int(sys.argv[3])))
nx,ny=np.shape(matrix)
CXY=np.zeros([ny, nx])
for i in range(ny):
    for j in range(nx):
        CXY[i,j]= matrix[j,i] * 100

#Human readable data
np.savetxt(sys.argv[1], CXY, fmt='%d')
