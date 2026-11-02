"""Example plugin: repeat the current geometry along a line."""


def linear_array(params, geo):
    count = int(params["count"])
    if count < 1 or count > 1000:
        raise ValueError("count must be between 1 and 1000")
    step = [float(params["dx"]), float(params["dy"]), float(params["dz"])]  # mm
    v, t = geo["vertices"], geo["triangles"]
    n = len(v) // 3
    out_v, out_t = [], []
    for k in range(count):
        for i in range(n):
            for a in range(3):
                out_v.append(v[3 * i + a] + k * step[a])
        out_t.extend(i + k * n for i in t)
    return {"vertices": out_v, "triangles": out_t}


OPERATIONS = {"linear_array": linear_array}
