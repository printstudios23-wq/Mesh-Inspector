#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Writes the sample meshes in this folder (plain Python 3, no dependencies).

  icosphere_clean.obj        watertight, manifold, outward normals - should report no problems
  icosphere_defects.obj      the same sphere with deliberate defects:
                               2 holes, 3 flipped triangles, 2 degenerate triangles,
                               1 duplicate triangle and a non-manifold fin
  icosphere_clean.stl        the clean sphere as an STL triangle soup (checks vertex welding)
"""
import math
import struct
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def icosphere(subdivisions=3, radius=10.0):
    t = (1.0 + math.sqrt(5.0)) / 2.0
    verts = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
             (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    verts = [tuple(c / math.sqrt(x * x + y * y + z * z) for c in (x, y, z)) for x, y, z in verts]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
             (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5),
             (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(subdivisions):
        cache = {}

        def midpoint(a, b):
            key = (min(a, b), max(a, b))
            if key not in cache:
                m = tuple((verts[a][i] + verts[b][i]) / 2 for i in range(3))
                n = math.sqrt(sum(c * c for c in m))
                verts.append(tuple(c / n for c in m))
                cache[key] = len(verts) - 1
            return cache[key]

        new = []
        for a, b, c in faces:
            ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
            new += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = new
    return [tuple(c * radius for c in v) for v in verts], faces


def write_obj(path, verts, faces, comment):
    with open(path, "w") as f:
        f.write("# %s\n" % comment)
        for v in verts:
            f.write("v %.6f %.6f %.6f\n" % v)
        for a, b, c in faces:
            f.write("f %d %d %d\n" % (a + 1, b + 1, c + 1))


def write_stl_binary(path, verts, faces):
    with open(path, "wb") as f:
        f.write(b"icosphere_clean (generated)".ljust(80, b"\0"))
        f.write(struct.pack("<I", len(faces)))
        for a, b, c in faces:
            f.write(struct.pack("<3f", 0, 0, 0))
            for i in (a, b, c):
                f.write(struct.pack("<3f", *verts[i]))
            f.write(struct.pack("<H", 0))


def main():
    verts, faces = icosphere(3)
    write_obj(os.path.join(HERE, "icosphere_clean.obj"), verts, faces, "clean icosphere: watertight, manifold")
    write_stl_binary(os.path.join(HERE, "icosphere_clean.stl"), verts, faces)

    v = list(verts)
    f = [list(t) for t in faces]

    def ring(center_idx, rings=1):
        """indices of the faces touching a vertex (and optionally its neighbours)"""
        sel = {center_idx}
        for _ in range(rings - 1):
            sel |= {i for t in f for i in t if any(j in sel for j in t)}
        return [k for k, t in enumerate(f) if any(i in sel for i in t)]

    # two holes: remove the faces around two vertices
    remove = set(ring(0)) | set(ring(300, 2))
    # three flipped triangles
    flip = [k for k in range(0, len(f), 400)][:3]
    for k in flip:
        f[k][1], f[k][2] = f[k][2], f[k][1]
    faces2 = [tuple(t) for k, t in enumerate(f) if k not in remove]

    # degenerate: one collapsed, one collinear
    extra = len(v)
    v += [(13.0, 0.0, 0.0), (14.0, 0.0, 0.0), (15.0, 0.0, 0.0)]
    faces2.append((extra, extra + 1, extra + 2))   # three collinear points
    faces2.append((5, 5, 6))                       # collapsed
    # duplicate triangle
    faces2.append(faces2[10])
    # non-manifold fin: a third triangle on an existing edge
    a, b, _ = faces2[20]
    v.append((v[a][0] * 1.4, v[a][1] * 1.4, v[a][2] * 1.4))
    faces2.append((a, b, len(v) - 1))

    write_obj(os.path.join(HERE, "icosphere_defects.obj"), v, faces2,
              "icosphere with deliberate defects: 2 holes, 3 flipped, 2 degenerate, 1 duplicate, 1 non-manifold fin")
    print("samples written to", HERE)


if __name__ == "__main__":
    main()
