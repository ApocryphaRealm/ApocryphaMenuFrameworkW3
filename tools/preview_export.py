"""Test data for tools/preview_test.cpp: turns one of the item-model prototype's outputs (an OBJ + MTL + PNG textures from
4. plans/item-explorer-witcher3/mesh-prototype/item_mesh.py) into the AMF_PreviewMesh layout (sdk/include/AMFPreview.h),
written as one .amfprev file:

    "AMFP" u32 version 1, u32 vertexCount, indexCount, drawCount, textureCount, float bounds[6]
    vertices (32 B each), u32 indices, draws (AMF_PreviewDraw, 104 B each), then per texture:
    i32 format, width, height, mips, u32 size, data

Textures go as RGBA8 (the prototype decoded them); the real model reader hands BC blocks. Tangents are rebuilt from the
UVs (the OBJ has none). Normals and tangents are packed 10:10:10:2 as the game stores them.

    python tools/preview_export.py "<out dir of one item>" <file.amfprev>
"""
import glob
import os
import struct
import sys

import numpy as np
from PIL import Image


def pack1010102(v, w2):
    q = np.clip(np.round((v * 0.5 + 0.5) * 1023.0), 0, 1023).astype(np.uint32)
    return q[:, 0] | (q[:, 1] << 10) | (q[:, 2] << 20) | (np.uint32(w2) << 30)


def main(src, dst):
    obj = glob.glob(os.path.join(src, "*.obj"))[0]
    mtl = glob.glob(os.path.join(src, "*.mtl"))[0]
    maps = {}
    cur = None
    for line in open(mtl, encoding="utf-8"):
        t = line.strip()
        if t.startswith("newmtl "):
            cur = t[7:]
            maps[cur] = {}
        elif t.startswith("map_Kd ") and cur:
            maps[cur]["d"] = t[7:]
        elif "normal map" in t and cur:
            maps[cur]["n"] = t.split(":", 1)[1].strip()
    vs, vts, vns = [], [], []
    groups = []   # (material, [ (v,vt,vn) x3 ... ])
    for line in open(obj, encoding="utf-8"):
        p = line.split()
        if not p:
            continue
        if p[0] == "v":
            vs.append([float(x) for x in p[1:4]])
        elif p[0] == "vt":
            vts.append([float(x) for x in p[1:3]])
        elif p[0] == "vn":
            vns.append([float(x) for x in p[1:4]])
        elif p[0] == "usemtl":
            groups.append((p[1], []))
        elif p[0] == "f":
            corners = []
            for c in p[1:]:
                a = (c.split("/") + ["", ""])[:3]
                corners.append(tuple(int(x) - 1 if x else -1 for x in a))
            for k in range(1, len(corners) - 1):
                groups[-1][1].extend([corners[0], corners[k], corners[k + 1]])
    vs, vts, vns = np.array(vs, np.float32), np.array(vts or [[0, 0]], np.float32), np.array(vns or [[0, 0, 1]], np.float32)

    uniq = {}
    pos, uv, nrm, indices, draws = [], [], [], [], []
    textures, texidx = [], {}

    def tex(path, fmt):
        key = (path, fmt)
        if key not in texidx:
            im = Image.open(os.path.join(src, path)).convert("RGBA")
            texidx[key] = len(textures)
            textures.append((fmt, im.width, im.height, im.tobytes()))
        return texidx[key]

    for mat, tris in groups:
        first = len(indices)
        for c in tris:
            if c not in uniq:
                uniq[c] = len(pos)
                pos.append(vs[c[0]])
                uv.append(vts[c[1]] if c[1] >= 0 else [0, 0])
                nrm.append(vns[c[2]] if c[2] >= 0 else [0, 0, 1])
            indices.append(uniq[c])
        m = maps.get(mat, {})
        d = tex(m["d"], 3) if m.get("d") and os.path.exists(os.path.join(src, m["d"])) else -1
        n = tex(m["n"], 4) if m.get("n") and os.path.exists(os.path.join(src, m["n"])) else -1
        draws.append((first, len(indices) - first, 0, d, n, 0, (1, 1, 1, 0.5), (1, 1, 1, 1), (0, 0, 0, 0), (0, 0, 0, 0)))
    pos = np.array(pos, np.float32)
    uv = np.array(uv, np.float32)
    uv[:, 1] = 1.0 - uv[:, 1]   # the prototype flipped V for OBJ
    nrm = np.array(nrm, np.float32)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-6)
    idx = np.array(indices, np.uint32).reshape(-1, 3)
    # tangents from the UVs
    tan = np.zeros_like(pos)
    bit = np.zeros_like(pos)
    p0, p1, p2 = pos[idx[:, 0]], pos[idx[:, 1]], pos[idx[:, 2]]
    t0, t1, t2 = uv[idx[:, 0]], uv[idx[:, 1]], uv[idx[:, 2]]
    e1, e2 = p1 - p0, p2 - p0
    d1, d2 = t1 - t0, t2 - t0
    r = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    r = np.where(np.abs(r) < 1e-12, 1e-12, r)
    sdir = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / r[:, None]
    tdir = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / r[:, None]
    for k in range(3):
        np.add.at(tan, idx[:, k], sdir)
        np.add.at(bit, idx[:, k], tdir)
    tan = tan - nrm * np.sum(nrm * tan, 1, keepdims=True)
    tan /= np.maximum(np.linalg.norm(tan, axis=1, keepdims=True), 1e-6)
    sign = np.sum(np.cross(nrm, tan) * bit, 1) >= 0
    npk = pack1010102(nrm, 0)
    tpk = pack1010102(tan, 0) | np.where(sign, np.uint32(3) << 30, np.uint32(0))

    with open(dst, "wb") as f:
        bmin, bmax = pos.min(0), pos.max(0)
        f.write(b"AMFP" + struct.pack("<IIIII", 1, len(pos), len(indices), len(draws), len(textures)))
        f.write(struct.pack("<6f", *bmin, *bmax))
        for i in range(len(pos)):
            f.write(struct.pack("<5fIII", *pos[i], *uv[i], int(npk[i]), int(tpk[i]), 0))
        f.write(np.array(indices, np.uint32).tobytes())
        for d in draws:
            f.write(struct.pack("<IIiiiI4f4f4f4f", d[0], d[1], d[2], d[3], d[4], d[5], *d[6], *d[7], *d[8], *d[9]))
        for fmt, w, h, data in textures:
            f.write(struct.pack("<iiiiI", fmt, w, h, 1, len(data)))
            f.write(data)
    print("%s: %d vertices, %d triangles, %d draws, %d textures -> %s" % (os.path.basename(src), len(pos), len(indices) // 3,
                                                                       len(draws), len(textures), dst))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
