"""Python integration for the Nocturne C component SDK."""

from ._base import NativeError
from .animation import AnimationPhase, AnimationSample, Easing, Repeat, sample_tween
from .component import CallbackFailure, CallError, Component, Constraints, Event, Layout, Measure, Paint, Update
from .input import DispatchError, Input
from .raw import ffi, lib
from .text import Cluster, Text
from .tree import Cell, Dispatch, Frame, Geometry, Intent, Node, Tree


def abi_version():
    return int(lib.nt_abi_version())


def unicode_version():
    return ffi.string(lib.nt_unicode_version()).decode("ascii")


def available_components():
    optional = ("posix_host", "graph", "spatial", "simulation")
    mask = int(lib.NT_PYTHON_COMPONENTS_MASK)
    return ("nocturne", *(name for bit, name in enumerate(optional) if mask & (1 << bit)))


__all__ = ["AnimationPhase", "AnimationSample", "CallbackFailure", "CallError", "Cell", "Cluster", "Component",
           "Constraints", "Dispatch", "DispatchError", "Easing", "Event", "Frame", "Geometry", "Input", "Intent",
           "Layout", "Measure", "NativeError", "Node", "Paint", "Repeat", "Text", "Tree", "Update",
           "abi_version", "available_components", "sample_tween", "unicode_version"]
