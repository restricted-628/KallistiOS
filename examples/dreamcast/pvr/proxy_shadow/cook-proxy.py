"""Cook the oriented, closed convex cube's face adjacency and normals."""
from math import sqrt

corners = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0),
           (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 4, 7, 3),
         (1, 2, 6, 5), (0, 1, 5, 4), (3, 7, 6, 2)]
edges = {}
for i, face in enumerate(faces):
    for a, b in zip(face, face[1:] + face[:1]):
        if (a, b) in edges or a == b:
            raise ValueError("duplicate or collapsed directed edge")
        edges[a, b] = i


def initializer(values):
    return "{" + ", ".join(str(x) for x in values) + "}"


print("static const shz_vec3_t corners[8] = {")
for x, y, z in corners:
    print(f"    {{.x={x}, .y={y}, .z={z}}},")
print("};\nstatic const struct face faces[6] = {")
for i, face in enumerate(faces):
    a, b, c = (corners[j] for j in face[:3])
    u = [b[k]-a[k] for k in range(3)]
    v = [c[k]-a[k] for k in range(3)]
    n = [u[(k+1)%3]*v[(k+2)%3] - u[(k+2)%3]*v[(k+1)%3] for k in range(3)]
    distances = [sum(n[k]*(p[k]-a[k]) for k in range(3)) for p in corners]
    if max(distances) > 0 or min(distances) >= 0 or any(distances[j] for j in face):
        raise ValueError("face must bound the outward convex hull")
    length = sqrt(sum(k*k for k in n))
    neighbors = [edges[b, a] for a, b in zip(face, face[1:] + face[:1])]
    normal = ", ".join(f".{k}={value/length:g}" for k, value in zip("xyz", n))
    print(f"    {{{initializer(face)}, {initializer(neighbors)}, {{{normal}}}}},")
print("};")
