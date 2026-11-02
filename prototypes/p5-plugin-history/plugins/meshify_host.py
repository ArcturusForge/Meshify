"""Plugin host for Meshify prototype P5.

One request per process: JSON on stdin, JSON on stdout. Anything a plugin prints
is sent to stderr so it cannot corrupt the reply. Plugin API version 1:

    OPERATIONS = {"name": fn}
    fn(params: dict, geometry: dict) -> dict
    geometry = {"vertices": [x, y, z, ...] (mm), "triangles": [a, b, c, ...]}
"""
import importlib.util
import json
import sys
import traceback


def main():
    real_out = sys.stdout
    sys.stdout = sys.stderr
    try:
        req = json.loads(sys.stdin.read())
        spec = importlib.util.spec_from_file_location("meshify_plugin", req["entry"])
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        fn = module.OPERATIONS[req["op"]]
        g = req["geometry"]
        out = fn(req["params"], {"vertices": g["vertices"], "triangles": g["triangles"]})
        reply = {"ok": True, "geometry": out}
    except BaseException as e:  # a plugin may raise anything
        reply = {"ok": False, "error": "%s: %s" % (type(e).__name__, e),
                 "trace": traceback.format_exc()}
    real_out.write(json.dumps(reply))
    real_out.flush()


main()
