import numpy as numpy
import scipy as spy
import matplotlib.pyplot as plt

import time

m = 512
n = 1024


#稀疏矩阵的产生，A使用的是正态稀疏矩阵
u= spy.sparse.rand(n,1,density=0.1,format='csc',dtype=None)
u1 = u.nonzero()
row = u1[0]
col = u1[1]
data = np.random.randn(int(0.1*n))
u = csc_matrix((data, (row, col)), shape=(n,1)).toarray()  

#u1 = u.nonzero()        #观察是否是正态分布
#plt.hist(u[u1[0],u1[1]].tolist())

#u = u.todense()  #转为非稀疏形式

a = np.random.randn(m,n)
b = np.dot(a,u)
v = 1e-3      #v为题目里面的miu

def f(x0):    #目标函数
    return 1/2*np.dot((np.dot(a,x0)-b).T,np.dot(a,x0)-b)+v*sum(abs(x0))

#==========初始值=============================
x0 = np.zeros((n,1))

y = []
time1 = []

start = time.clock()

#=========开始迭代==========================
for i in range(1000):
    
    y.append(f(x0)[0,0])    #存放每次的迭代函数值
    
    g0 = (np.dot(np.dot(a.T,a),x0)-np.dot(a.T,b) + v*np.sign(x0)) #次梯度
    t = 0.01/np.sqrt(sum(np.dot(g0.T,g0)))    #设为0.01效果比0.1好很多，步长

       
    x1 = x0 - t[0]*g0  
    x0 = x1
    
    end = time.clock()
    time1.append(end)

y = np.array(y).reshape((1000,1))    

time1 = np.array(time1)
time1 = time1 - start
time2 = time1[np.where(y - y[999] < 10e-4)[0][0]]

plt.plot(y)    