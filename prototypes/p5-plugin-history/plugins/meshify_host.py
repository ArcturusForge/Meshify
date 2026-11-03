"""Plugin host for Meshify prototype P5.

One request per process: JSON on stdin, JSON on stdout. Anything a plugin prints
is sent to stderr so it cannot corrupt the reply.

    OPERATIONS = {"name": fn}
    fn(params: dict, geometry: dict) -> dict
    geometry = {"vertices": [x, y, z, ...] (mm), "triangles": [a, b, c, ...]}

API 1 passes plain lists and the result goes back in the JSON reply.
API 2 passes read-only memoryviews of the mapped input file (no copy, no parsing;
numpy.frombuffer wraps them directly) and writes the result to a binary file. A
result may be lists, array.array, memoryviews or numpy arrays.
"""
import array
import importlib.util
import json
import mmap
import struct
import sys
import traceback

TRI = "I" if array.array("I").itemsize == 4 else "L"  # unsigned 32-bit


def as_bytes(obj, typecode):
    try:
        mv = memoryview(obj)
        if mv.format == typecode and mv.contiguous:
            return mv.tobytes()
    except TypeError:
        pass
    return array.array(typecode, obj).tobytes()


def run(req):
    spec = importlib.util.spec_from_file_location("meshify_plugin", req["entry"])
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    fn = module.OPERATIONS[req["op"]]
    if req["api"] == 1:
        g = req["geometry"]
        out = fn(req["params"], {"vertices": g["vertices"], "triangles": g["triangles"]})
        return {"ok": True, "geometry": out}
    with open(req["input_file"], "rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    magic, n, m, _ = struct.unpack_from("<4sIII", mm, 0)
    if magic != b"MSH1":
        raise ValueError("input file has no valid header")
    raw = memoryview(mm)
    geo = {"vertices": raw[16:16 + 24 * n].cast("d"),
           "triangles": raw[16 + 24 * n:16 + 24 * n + 12 * m].cast(TRI)}
    out = fn(req["params"], geo)
    v, t = as_bytes(out["vertices"], "d"), as_bytes(out["triangles"], TRI)
    with open(req["output_file"], "wb") as f:
        f.write(struct.pack("<4sIII", b"MSH1", len(v) // 24, len(t) // 12, 0))
        f.write(v)
        f.write(t)
    return {"ok": True}


def main():
    real_out = sys.stdout
    sys.stdout = sys.stderr
    try:
        reply = run(json.loads(sys.stdin.read()))
    except BaseException as e:  # a plugin may raise anything
        reply = {"ok": False, "error": "%s: %s" % (type(e).__name__, e),
                 "trace": traceback.format_exc()}
    real_out.write(json.dumps(reply))
    real_out.flush()


main()
