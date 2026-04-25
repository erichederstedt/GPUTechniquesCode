import numpy as np
import struct

SIZE = 32
SAMPLES = 1024

def radical_inverse_VdC(bits):
    bits = ((bits << 16) | (bits >> 16)) & 0xFFFFFFFF
    bits = (((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1)) & 0xFFFFFFFF
    bits = (((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2)) & 0xFFFFFFFF
    bits = (((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4)) & 0xFFFFFFFF
    bits = (((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8)) & 0xFFFFFFFF
    return bits * 2.3283064365386963e-10

def hammersley(i, N):
    return i / N, radical_inverse_VdC(i)

def importance_sample_GGX(xi1, xi2, roughness):
    a = roughness * roughness
    phi = 2 * np.pi * xi1
    cos_theta = np.sqrt((1 - xi2) / max(1 + (a*a - 1) * xi2, 1e-7))
    sin_theta = np.sqrt(max(1 - cos_theta * cos_theta, 0))
    return np.array([np.cos(phi) * sin_theta, np.sin(phi) * sin_theta, cos_theta])

def G1_Smith(NdotV, roughness):
    a = roughness * roughness
    return 2 * NdotV / max(NdotV + np.sqrt(a*a + (1 - a*a) * NdotV * NdotV), 1e-7)

def G_Smith(NdotL, NdotV, roughness):
    return G1_Smith(NdotL, roughness) * G1_Smith(NdotV, roughness)

def bake_Eo(NdotV, roughness):
    V = np.array([np.sqrt(max(1 - NdotV*NdotV, 0)), 0.0, NdotV])
    acc = 0.0
    for i in range(SAMPLES):
        xi1, xi2 = hammersley(i, SAMPLES)
        H = importance_sample_GGX(xi1, xi2, roughness)
        L = 2 * np.dot(V, H) * H - V
        NdotL = max(L[2], 0)
        NdotH = max(H[2], 0)
        VdotH = max(np.dot(V, H), 0)
        if NdotL > 0:
            G = G_Smith(NdotL, NdotV, roughness)
            acc += (G * VdotH) / max(NdotH * NdotV, 1e-7)
    return acc / SAMPLES

print("Baking Eo LUT...")
Eo = np.zeros((SIZE, SIZE), dtype=np.float32)
for y in range(SIZE):
    roughness = (y + 0.5) / SIZE
    for x in range(SIZE):
        NdotV = (x + 0.5) / SIZE
        Eo[y, x] = bake_Eo(NdotV, roughness)
    print(f"  {y+1}/{SIZE}")

print("Baking Eavg LUT...")
Eavg = np.zeros((SIZE,), dtype=np.float32)
for y in range(SIZE):
    roughness = (y + 0.5) / SIZE
    total = 0.0
    for x in range(SIZE):
        NdotV = (x + 0.5) / SIZE
        total += Eo[y, x] * NdotV
    Eavg[y] = 2.0 * total / SIZE

# Save as raw R16F binary (float32 -> float16)
Eo_f16   = Eo.astype(np.float16)
Eavg_f16 = Eavg.astype(np.float16)

with open("Eo.r16f", "wb") as f:
    f.write(Eo_f16.tobytes())

with open("Eavg.r16f", "wb") as f:
    f.write(Eavg_f16.tobytes())

print(f"Saved Eo.r16f    ({SIZE*SIZE*2} bytes, {SIZE}x{SIZE} R16F)")
print(f"Saved Eavg.r16f  ({SIZE*2} bytes, {SIZE}x1 R16F)")
print("Done.")