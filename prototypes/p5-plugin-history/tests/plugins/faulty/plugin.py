"""Test-only plugin whose operations misbehave in different ways."""
import os
import time


def raise_midway(params, geo):
    partial = list(geo["vertices"]) + [1.0]  # half-built result, then failure
    del partial
    raise RuntimeError("boom after partly building")


def crash(params, geo):
    os._exit(3)


def hang(params, geo):
    time.sleep(60)
    return geo


def bad_index(params, geo):
    return {"vertices": [0.0, 0.0, 0.0], "triangles": [0, 1, 2]}


def nan_out(params, geo):
    return {"vertices": [float("nan"), 0.0, 0.0], "triangles": []}


def chatty(params, geo):
    print("hello from a plugin that prints to stdout")
    return geo


OPERATIONS = {"raise_midway": raise_midway, "crash": crash, "hang": hang,
              "bad_index": bad_index, "nan_out": nan_out, "chatty": chatty}
