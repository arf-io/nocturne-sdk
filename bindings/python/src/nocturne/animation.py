"""Scalar animation on caller-owned local time."""

from dataclasses import dataclass
from enum import IntEnum

from ._base import NativeError, ensure_abi
from .raw import ffi, lib


class Easing(IntEnum):
    LINEAR = lib.NT_LINEAR
    EASE_IN = lib.NT_EASE_IN
    EASE_OUT = lib.NT_EASE_OUT
    EASE_IN_OUT = lib.NT_EASE_IN_OUT
    SMOOTH = lib.NT_SMOOTH
    SMOOTHER = lib.NT_SMOOTHER
    SINE = lib.NT_SINE
    STEP_START = lib.NT_STEP_START
    STEP_END = lib.NT_STEP_END


class Repeat(IntEnum):
    ONCE = lib.NT_ONCE
    LOOP = lib.NT_LOOP
    PING_PONG = lib.NT_PING_PONG


class AnimationPhase(IntEnum):
    DELAYED = lib.NT_DELAYED
    RUNNING = lib.NT_RUNNING
    FINISHED = lib.NT_FINISHED


@dataclass(frozen=True)
class AnimationSample:
    value: float
    progress: float
    iteration: int
    phase: AnimationPhase
    direction: int


def sample_tween(from_value, to_value, duration_us, elapsed_us, *,
                 delay_us=0, easing=Easing.LINEAR, repeat=Repeat.ONCE):
    """Evaluate a scalar tween without advancing a clock or retaining state."""
    ensure_abi()
    tween = ffi.new("NtTween *", {
        "from": from_value, "to": to_value, "duration_us": duration_us,
        "delay_us": delay_us, "easing": easing, "repeat": repeat,
    })
    sample = ffi.new("NtAnimationSample *")
    status = lib.nt_tween_sample(tween, elapsed_us, sample)
    if status == lib.NT_INVALID_ARGUMENT:
        raise NativeError(lib.NT_RESULT_INVALID_ARGUMENT)
    if status == lib.NT_OUT_OF_RANGE:
        raise NativeError(lib.NT_RESULT_OUT_OF_RANGE)
    if status != lib.NT_OK:
        raise NativeError(status)
    return AnimationSample(sample.value, sample.progress, sample.iteration,
                           AnimationPhase(sample.phase), sample.direction)
