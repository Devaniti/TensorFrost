import TensorFrost as tf
import numpy as np
import matplotlib.pyplot as plt
import time

tf.initialize(tf.cpu, "/Zi")
#tf.initialize(tf.cpu, "/O2 /fp:fast /openmp:experimental /Zi")

n0 = 32
m0 = 64
k0 = 32

def matmul():
    A = tf.input([n0, m0], tf.float32)
    B = tf.input([m0, k0], tf.float32)

    N, M = A.shape
    K = B.shape[1]
    
    C = tf.zeros([N, K])

    i, j, k = tf.indices([N, K, M])

    tf.scatterAdd(C[i, j], A[i, k] * B[k, j])

    return [C]

def add():
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

def ComputeColor():
    vx = tf.input([N, N], tf.float32)

    # compute magnitude
    #mag = tf.sqrt(vx*vx + vy*vy)

    return [vx * 255.0]

N = 512 
M = 512

def Bilinear(tex, x, y):
    xi, yi = tf.floor(x), tf.floor(y)
    xf, yf = x-xi, y-yi
    xi, yi = tf.int(xi), tf.int(yi)
    oxf, oyf = 1.0-xf, 1.0-yf
    return tex[xi, yi]*oxf*oyf + tex[xi+1, yi]*xf*oyf + tex[xi, yi+1]*oxf*yf + tex[xi+1, yi+1]*xf*yf

def CubicHermit(x):
    x2 = x * x
    x3 = x2 * x
    return [-0.5 * x3 + x2 - 0.5 * x, 1.5 * x3 - 2.5 * x2 + 1.0, -1.5 * x3 + 2.0 * x2 + 0.5 * x, 0.5 * x3 - 0.5 * x2]

def CubicInterp(tex, x, y):
    xi, yi = tf.floor(x), tf.floor(y)
    xf, yf = x-xi, y-yi
    xi, yi = tf.int(xi), tf.int(yi)

    wx = CubicHermit(xf)
    wy = CubicHermit(yf)

    valueY = 0
    for j in range(-1, 3):
        valueX = 0
        for i in range(-1, 3):
            valueX = valueX + tex[xi + i, yi + j] * wx[i + 1]
        valueY = valueY + valueX * wy[j + 1]
    return valueY

def EulerAdvection(vx, vy, dt):
    i,j = vx.indices
    x, y = tf.float(i), tf.float(j)
    x1, y1 = x - vx*dt, y - vy*dt
    return x1, y1

def RK4Advection(vx, vy, dt):
    i, j = vx.indices
    x, y = tf.float(i), tf.float(j)

    x1, y1 = x - vx*dt/2.0, y - vy*dt/2.0
    vx1, vy1 = Bilinear(vx, x1, y1), Bilinear(vy, x1, y1)

    x2, y2 = x - vx1*dt/2.0, y - vy1*dt/2.0
    vx2, vy2 = Bilinear(vx, x2, y2), Bilinear(vy, x2, y2)

    x3, y3 = x - vx2*dt, y - vy2*dt
    vx3, vy3 = Bilinear(vx, x3, y3), Bilinear(vy, x3, y3)

    x4, y4 = x - (vx + 2.0*vx1 + 2.0*vx2 + vx3)*dt/6.0, y - (vy + 2.0*vy1 + 2.0*vy2 + vy3)*dt/6.0
    return x4, y4

def SemiLagrange(vx, vy, pressure, density, dt):
    x1, y1 = RK4Advection(vx, vy, dt)
    #x1, y1 = EulerAdvection(vx, vy, dt)

    #vx = CubicInterp(vx, x1, y1)
    #vy = CubicInterp(vy, x1, y1)
    #pressure = CubicInterp(pressure, x1, y1)
    vx = Bilinear(vx, x1, y1)
    vy = Bilinear(vy, x1, y1)
    #pressure = Bilinear(pressure, x1, y1)
    #densitylin = Bilinear(density, x1, y1)
    #dens1 = densitylin*0.99
    #dens2 = densitylin*1.00
    #dens3 = tf.min(dens1, dens2)
    #dens4 = tf.max(dens1, dens2)
    density = Bilinear(density, x1, y1)

    return [vx, vy, pressure, density]

def BFECC(vx, vy, pressure, density, dt):
    i, j = vx.indices
    x, y = tf.float(i), tf.float(j)
    
    # advect backwards
    x1, y1 = x - vx*dt, y - vy*dt
    vx1, vy1 = Bilinear(vx, x1, y1), Bilinear(vy, x1, y1)
    density1 = Bilinear(density, x1, y1)

    # advect forwards
    x2, y2 = x + vx*dt, y + vy*dt
    vx2, vy2 = Bilinear(vx1, x2, y2), Bilinear(vy1, x2, y2)
    density2 = Bilinear(density1, x2, y2)

    # compute backwards forwards error correction
    vx = vx + (vx - vx2)*0.5
    vy = vy + (vy - vy2)*0.5
    density = density + (density - density2)*0.5

    # advect corrected backwards
    vx3, vy3 = Bilinear(vx, x1, y1), Bilinear(vy, x1, y1)
    density3 = Bilinear(density, x1, y1)

    return [vx3, vy3, pressure, density3]

def Boundary(i, j):
    N1, M1 = i.shape
    return 1.0 - tf.float((i < 3) | (i > N1-4) | (j < 3) | (j > M1-4))

def Jacobi(pressure, div, iterations):
    i, j = pressure.indices

    edge = Boundary(i, j)

    # pressure solve
    for it in range(iterations):
        pressure = edge * (pressure[i-1, j] + pressure[i+1, j] + pressure[i, j-1] + pressure[i, j+1] - div) / 4.0

    return pressure

def Restrict(field):
    N1, M1 = field.shape
    N2, M2 = N1/2, M1/2
    i, j = tf.indices([N2, M2])
    i, j = 2*i, 2*j
    return 0.25*(field[i, j] + field[i+1, j] + field[i, j+1] + field[i+1, j+1])

def Prolong(field, orig):
    i, j = orig.indices
    i, j = i/2, j/2
    return orig + field[i, j]

def Residual(pressure, div):
    i, j = pressure.indices
    return div - (pressure[i-1, j] + pressure[i+1, j] + pressure[i, j-1] + pressure[i, j+1] - 4.0*pressure)

def VCycle(pressure, div):
    pressure = Jacobi(pressure, div, 1)

    res = Residual(pressure, div)
    res = Restrict(res)
    pressure0 = Jacobi(tf.zeros(res.shape), 4.0*res, 8)

    #Currently not working
    #res1 = Residual(pressure0, 4.0*res)
    #res1 = Restrict(res1)
    #pressure1 = Jacobi(tf.zeros(res1.shape), 16.0*res1, 8)
    #pressure0 = Prolong(pressure1, pressure0)

    pressure = Prolong(pressure0, pressure)

    pressure = Jacobi(pressure, div, 1)

    return pressure

def PressureSolve(pressure, div):
    pressure = VCycle(pressure, div)
    pressure = VCycle(pressure, div)
    return pressure

def Smoothstep(edge0, edge1, x):
    x = (x - edge0) / (edge1 - edge0)
    x = tf.clamp(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)
    
def FluidTest():
    vx = tf.input([N, M], tf.float32)
    vy = tf.input([N, M], tf.float32)
    pressure = tf.input([N, M], tf.float32)
    density = tf.input([N, M], tf.float32)
    canvas = tf.zeros([N, M, 3], tf.float32)

    dt = 1.0
    i,j = vx.indices
    x, y = tf.float(i), tf.float(j)

    vx, vy, pressure, density = SemiLagrange(vx, vy, pressure, density, dt)
    
    # add source
    source = 0.16*tf.exp(-((y-M/5.0)**2.0 + (x-2.0*N/3.0)**2.0)/100.0)
    source = source - 0.15*tf.exp(-((y-4.0*M/5.0)**2.0 + (x-N/3.0)**2.0)/100.0)
    vy = vy + source
    density = density + source*source

    edge = Boundary(i, j)
    vx = vx * edge
    vy = vy * edge
    density = tf.clamp(density * edge, -0.5, 1.0)

    # pressure solve
    # compute divergence
    div = (vx[i+1, j] - vx[i-1, j] + vy[i, j+1] - vy[i, j-1]) / 2.0
    curl = tf.abs(vy[i+1, j] - vy[i-1, j] - vx[i, j+1] + vx[i, j-1]) / 2.0

    pressure = PressureSolve(pressure, div)

    # subtract pressure gradient
    gradx = (pressure[i+1, j] - pressure[i-1, j])*1.0
    grady = (pressure[i, j+1] - pressure[i, j-1])*1.0
    vx = vx - gradx
    vy = vy - grady

    # vortex confinement

    # compute gradient of curl magnitude
    gradx = (curl[i+1, j] - curl[i-1, j])*1.0
    grady = (curl[i, j+1] - curl[i, j-1])*1.0

    # normalize gradient
    mag = tf.sqrt(gradx*gradx + grady*grady) + 1e-5
    gradx = gradx / mag
    grady = grady / mag

    # compute vortex force
    vortx = -grady * curl
    vorty = gradx * curl

    # add vortex force
    vort_scale = 0.2
    vx = vx + vortx * dt * vort_scale
    vy = vy + vorty * dt * vort_scale

    #mag = 0.2*tf.sqrt(vx*vx + vy*vy)
    mag = 2.5*density

    #canvas[i, j, 0] = 255.0*Smoothstep(0.0, 0.33, mag)
    #canvas[i, j, 1] = 255.0*Smoothstep(0.33, 0.66, mag)
    #canvas[i, j, 2] = 255.0*Smoothstep(0.66, 1.0, mag)
    canvas[i, j, 0] = 255.0 * (0.277 + mag * (0.105 + mag * (-0.330 + mag * (-4.634 + mag * (6.228 + mag * (4.776 - 5.435 * mag))))))
    canvas[i, j, 1] = 255.0 * (0.005 + mag * (1.404 + mag * (0.214 + mag * (-5.799 + mag * (14.179 + mag * (-13.745 + 4.645 * mag))))))
    canvas[i, j, 2] = 255.0 * (0.334 + mag * (1.384 + mag * (0.095 + mag * (-19.332 + mag * (56.690 + mag * (-65.353 + 26.312 * mag))))))

    return [vx, vy, pressure, canvas, div, density, Residual(pressure, div)]


tf.initialize(tf.cpu, "")
fluid = tf.program(FluidTest)

def WaveIteration(u, v, dt):
    i,j = u.indices
    laplacian = u[i-1, j] + u[i+1, j] + u[i, j-1] + u[i, j+1] - u * 4.0
    force = laplacian - 0.1 * tf.sin(2.0*np.pi*u)
    v_new = v + dt*force
    u_new = u + dt*v_new
    return u_new, v_new

def WaveEq():
    u = tf.input([-1, -1], tf.float32)
    v = tf.input([-1, -1], tf.float32)

    u,v = WaveIteration(u, v, 0.2)

    return [u, v]

wave = tf.program(WaveEq)
wave.list_operations(compact=False)

fluid = tf.program(FluidTest)
fluid.list_operations(compact=False)

mmul = tf.program(matmul)
mmul.list_operations(compact=False)
mmul.kernel_c()

Anp = np.random.rand(n0, m0).astype(np.float32)
Bnp = np.random.rand(m0, k0).astype(np.float32)

A = tf.memory(Anp)
B = tf.memory(Bnp)
C, = mmul(A, B)

Cnp = C.numpy

print(Cnp)

#compare to numpy
Cnp2 = np.dot(Anp, Bnp)
print(Cnp2)

print(Cnp - Cnp2)

S = 128

def mandelbrot():
    canvas = tf.zeros([3, S, S], tf.float32)
    i, j = tf.indices([S, S])
    x, y = tf.float(i), tf.float(j)

    canvas[0, i, j] = tf.sin(x / (S * 2 * np.pi))
    canvas[1, i, j] = tf.cos(y / (S * 2 * np.pi))
    canvas[2, i, j] = tf.sin(x / (S * 2 * np.pi)) * tf.cos(y / (S * 2 * np.pi))

    return [canvas]

mand = tf.program(mandelbrot)

mand.list_operations(compact=False)
res = mand()

resnp = res[0].numpy

print(resnp.shape)

#def Boundary(i, j):
#    N1, M1 = i.shape
#    return 1.0 - tf.float((i < 2) | (i > N1-3) | (j < 2) | (j > M1-3))
#
#def Jacobi(pressure, div, iterations):
#    i, j = pressure.indices
#
#    edge = Boundary(i, j)
#
#    # pressure solve
#    for it in range(iterations):
#        pressure = edge * (pressure[i-1, j] + pressure[i+1, j] + pressure[i, j-1] + pressure[i, j+1] - div) / 4.0
#
#    return pressure
#
#def Restrict(field):
#    N1, M1 = field.shape
#    N2, M2 = N1/2, M1/2
#    i, j = tf.indices([N2, M2])
#    i, j = 2*i, 2*j
#    return field[i, j] + field[i+1, j] + field[i, j+1] + field[i+1, j+1]
#
#def Prolong(field, orig):
#    i, j = orig.indices
#    i, j = i/2, j/2
#    return orig + field[i, j]
#
#def Residual(pressure, div):
#    i, j = pressure.indices
#    return pressure[i-1, j] + pressure[i+1, j] + pressure[i, j-1] + pressure[i, j+1] - 4.0*pressure - div
#
#def PressureSolve(pressure, div):
#    pressure = Jacobi(pressure, div, 1)
#
#    res = Residual(pressure, div)
#    res = Restrict(res)
#    pressure0 = Jacobi(0.0001*res, res, 4)
#    pressure = Prolong(pressure0, pressure)
#
#    pressure = Jacobi(pressure, div, 1)
#
#    return pressure
#
#
#def Solver():
#    pressure = tf.input([N, M], tf.float32)
#    div = tf.input([N, M], tf.float32)
#
#    pressure = PressureSolve(pressure, div)
#
#    return [pressure]
#
#solver = tf.program(Solver)
#solver.list_operations(compact=True)