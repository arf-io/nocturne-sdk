"""Compare public component observations and scalar animation across five installs."""

import argparse
import math
from pathlib import Path


LANGUAGES = ("c", "rust", "python", "go", "typescript")
EXACT_BEFORE = (
    "initial 7 3 1 0 2 👩‍💻 1 1 2 界 4",
    "event 1 0 0 1 1 0 1 41 4",
    "clock 1250 2 250",
    "failure 9",
    "held 7 3 1 0 2 👩‍💻 1 1 2 界 4",
    "updated 5 2 1 0 2 👩‍💻 1 1 2 界 5",
    "dispose 514",
)
EXACT_AFTER = ("iteration 18446744073709551615", "invalid 1")
TIMING = (
    # Local time, uneased progress, leg index, phase, direction.
    (0, 0, 0, 0, 1),
    (100, 0, 0, 1, 1),
    (433, 0.333, 0, 1, 1),
    (1100, 1, 1, 1, -1),
    (1433, 0.667, 1, 1, -1),
    (2100, 0, 2, 1, 1),
)
TOLERANCE = 2e-14


def read_trace(path):
    rows = path.read_text(encoding="utf-8").splitlines()
    split = len(EXACT_BEFORE)
    if tuple(rows[:split]) != EXACT_BEFORE:
        raise ValueError(f"{path}: unexpected component observations {rows[:split]!r}")
    if tuple(rows[split + len(TIMING):]) != EXACT_AFTER:
        raise ValueError(f"{path}: unexpected iteration or invalid-input result")
    samples = []
    for row, expected in zip(rows[split:split + len(TIMING)], TIMING, strict=True):
        tag, at, value, progress, leg, phase, direction = row.split()
        local_time, fraction, iteration, expected_phase, heading = expected
        if tag != "sample" or (int(at), int(leg), int(phase), int(direction)) != (
            local_time, iteration, expected_phase, heading,
        ):
            raise ValueError(f"{path}: timing at {local_time} is {row!r}")
        value, progress = float(value), float(progress)
        expected_value = -3.25 + 11.75 * (1 - math.cos(math.pi * fraction)) / 2
        check_number(path, local_time, "value", value, expected_value)
        check_number(path, local_time, "progress", progress, fraction)
        samples.append((value, progress))
    return samples


def check_number(path, time, name, actual, expected):
    if not math.isclose(actual, expected, rel_tol=0, abs_tol=TOLERANCE):
        raise ValueError(f"{path}: {name} at {time} is {actual!r}, expected {expected!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", type=Path, help="Directory with c.trace through typescript.trace")
    args = parser.parse_args()
    native = read_trace(args.traces / "c.trace")
    for name in LANGUAGES[1:]:
        path = args.traces / f"{name}.trace"
        for timing, observed, reference in zip(TIMING, read_trace(path), native, strict=True):
            for field, actual, expected in zip(("value", "progress"), observed, reference, strict=True):
                check_number(path, timing[0], field, actual, expected)
    print("Five installed consumers match the component observations and six native animation samples.")


if __name__ == "__main__":
    main()
