"""The values tools/hdr_test.cpp should read back: AMF's menu colours written into an HDR10 back buffer (10-bit PQ,
BT.2020) at 250 nits paper white, and as they are in SDR - computed here independently of the shader."""

M = [[0.6274040, 0.3292820, 0.0433136], [0.0690970, 0.9195400, 0.0113612], [0.0163916, 0.0880132, 0.8955950]]


def lin(v):
    v /= 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def pq(n):
    m1, m2, c1, c2, c3 = 0.1593017578125, 78.84375, 0.8359375, 18.8515625, 18.6875
    y = max(0.0, min(1.0, n / 10000.0)) ** m1
    return ((c1 + c2 * y) / (1 + c3 * y)) ** m2


for c in ((76, 175, 80), (191, 68, 68), (255, 255, 255), (128, 128, 128)):
    l = [lin(x) for x in c]
    b = [sum(M[r][k] * l[k] for k in range(3)) for r in range(3)]
    print(c, "SDR", tuple(round(x / 255 * 1023) for x in c), "PQ", tuple(round(pq(x * 250) * 1023) for x in b))
