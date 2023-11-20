import TensorFrost as tf
import numpy as np
import matplotlib.pyplot as plt

def SomeFunction():
    u = tf.input([16, 16], tf.float32)
    v = tf.input([16, 16], tf.float32)

    i,j = u.indices

    laplacian = u[i-1, j] + u[i+1, j] + u[i, j-1] + u[i, j+1] - u[i, j] * 4.0

    dt = 0.1
    v_new = v + dt*laplacian
    u_new = u + dt*v_new

    return [u_new, v_new]

def SomeFunction2():
    A = tf.input([-1, -1], tf.float32)
    B = tf.input([-1, -1], tf.float32)

    N, M = A.shape
    K = B.shape[1]

    i, j, k = tf.indices([N, K, M])

    C = tf.zeros([N, K])
    tf.scatterAdd(C[i, j], A[i, k] * B[k, j])

    return [C]

def SomeFunction3():
    A = tf.input([-1, -1])
    B = tf.input([-1, -1])
    C = A + B
    return [C]

def QRDecomposition():
    A = tf.input([-1, -1], tf.float32)

    m, n = A.shape
    Q = tf.zeros([m, n])
    R = tf.zeros([n, n])

    def loop(i):
        j = tf.index(0, [m])
        R[i, i] = tf.sum(A[j, i] ** 2)
        Q[j, i] = A[j, i] * R[i, i]

        #R[i, i+1:n] = np.dot(Q[:, i].T, A[:, i+1:n])
        #A[:, i+1:n] -= np.outer(Q[:, i], R[i, i+1:n])

        p, k = tf.indices([m, n - i - 1])
        k += i + 1
        t = tf.index(0, [n - i - 1]) + i + 1
        R[i, t] = tf.sum(Q[p, i] * A[p, k], dim=0)
        A[p, k] -= Q[p, i] * R[i, k]
       
    tf.loop(end = n, body = loop)

    return [Q, R]

def PoissonSolver():
    x = tf.input([-1, -1], tf.float32)
    b = tf.input([-1, -1], tf.float32)
    n = tf.input([], tf.int32)

    i, j = x.indices

    def loop(t):
        nonlocal x
        x[i, j] = (x[i-1, j] + x[i+1, j] + x[i, j-1] + x[i, j+1] - b[i, j]) / 4.0

    tf.loop(end = n, body = loop)

    return [x]

def PoissonSolver2():
    x = tf.input([-1, -1], tf.float32)
    b = tf.input([-1, -1], tf.float32)
    n = tf.input([], tf.int32)

    i, j = x.indices

    x = (x[i-1, j] + x[i+1, j] + x[i, j-1] + x[i, j+1] - b[i, j]) / 4.0

    x = (x[i-1, j] + x[i+1, j] + x[i, j-1] + x[i, j+1] - b[i, j]) / 4.0
   
    return [x]



# Create a program that initializes the wave simulation
test = tf.program(SomeFunction)

test(list())

test.list_operations(compact=False)
test.kernel_hlsl()

Anp0 = np.random.rand(4, 2)
Bnp0 = np.random.rand(16)

print(Anp0)
print(Bnp0)

A = tf.memory(Anp0)
B = tf.memory(Bnp0)
A = tf.memory(np.zeros([4, 16]))

print(A)
print(B)

Anp = A.numpy
Bnp = B.numpy

print(Anp)
print(Bnp)


v = tf.memory(np.zeros([4, 4]))
for i in range(32):
    vnp = v.numpy
    vnp = 1 + vnp
    v = tf.memory(vnp)
    print("Used memory: " + str(tf.used_memory()))
	
print(v.numpy)