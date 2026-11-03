"""Returns the geometry unchanged, so a run measures only the transport."""


def echo(params, geo):
    return geo


OPERATIONS = {"echo": echo}
