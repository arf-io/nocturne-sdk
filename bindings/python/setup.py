from setuptools import setup

# An installed prefix or archive can change while CFFI's generated C is unchanged.
setup(cffi_modules=["build_ffi.py:builder"], options={"build_ext": {"force": True}})
