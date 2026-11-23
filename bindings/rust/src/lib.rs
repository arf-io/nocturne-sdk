//! Rust access to the installed Nocturne component SDK.

#[allow(non_camel_case_types, non_snake_case, non_upper_case_globals)]
pub mod raw {
    //! C declarations generated from the selected installed SDK headers.
    include!(concat!(env!("OUT_DIR"), "/raw.rs"));
}

mod component;
mod frame;
mod input;
mod text;
mod tree;

pub use component::{
    CallbackCause, CallbackFailure, CallbackPanic, CallbackPhase, CellStyle, Component,
    Constraints, Event, Layout, Measure, MountOptions, Paint,
};
pub use frame::{Cell, Frame};
pub use input::{DispatchError, DispatchPolicy, Input};
pub use raw::{NtAnimationSample as AnimationSample, NtTween as Tween};
pub use raw::{NtCellPoint as Point, NtCellRect as Rect, NtCellSize as Size};
pub use text::{Cluster, Text};
pub use tree::{CallError, Dispatch, Geometry, Intent, Node, Tree};

use std::ffi::CStr;
use std::fmt;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeError {
    code: u32,
}

impl NativeError {
    pub fn from_code(code: u32) -> Option<Self> {
        (code != 0).then_some(Self { code })
    }
    pub fn code(self) -> u32 {
        self.code
    }

    pub fn name(self) -> Option<&'static str> {
        let code = i32::try_from(self.code).ok()?;
        let name = unsafe { raw::nt_result_name(code) };
        if name.is_null() {
            None
        } else {
            unsafe { CStr::from_ptr(name) }.to_str().ok()
        }
    }
}

impl fmt::Display for NativeError {
    fn fmt(&self, out: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.name() {
            Some(name) => write!(out, "{name} ({})", self.code),
            None => write!(out, "unknown Nocturne result {}", self.code),
        }
    }
}

impl std::error::Error for NativeError {}

pub(crate) fn status(code: raw::NtResult) -> Result<(), NativeError> {
    if code == raw::NT_RESULT_OK {
        Ok(())
    } else {
        Err(native_error(code))
    }
}

// Native C enum signedness follows the selected toolchain.
#[allow(clippy::unnecessary_cast)]
pub(crate) fn native_error(code: raw::NtResult) -> NativeError {
    debug_assert_ne!(code, raw::NT_RESULT_OK);
    NativeError { code: code as u32 }
}

pub(crate) fn ensure_abi() -> Result<(), NativeError> {
    if abi_version() == ABI_VERSION {
        Ok(())
    } else {
        status(raw::NT_RESULT_UNSUPPORTED_CAPABILITY)
    }
}

#[allow(clippy::unnecessary_cast)]
pub const ABI_VERSION: u32 = raw::NT_RUST_HEADER_ABI_VERSION as u32;

pub fn abi_version() -> u32 {
    unsafe { raw::nt_abi_version() }
}

pub fn unicode_version() -> &'static str {
    unsafe { CStr::from_ptr(raw::nt_unicode_version()) }
        .to_str()
        .expect("Nocturne's Unicode version is ASCII")
}

/// Sample a scalar tween at caller-owned local time in microseconds.
///
/// Configure easing and repeat with the corresponding `raw::NT_*` constants.
/// The sample owns its value, progress, iteration, phase and direction.
pub fn sample_tween(tween: &Tween, elapsed_us: u64) -> Result<AnimationSample, NativeError> {
    ensure_abi()?;
    let mut sample = std::mem::MaybeUninit::uninit();
    let result = unsafe { raw::nt_tween_sample(tween, elapsed_us, sample.as_mut_ptr()) };
    match result {
        raw::NT_OK => Ok(unsafe { sample.assume_init() }),
        raw::NT_INVALID_ARGUMENT => Err(native_error(raw::NT_RESULT_INVALID_ARGUMENT)),
        raw::NT_OUT_OF_RANGE => Err(native_error(raw::NT_RESULT_OUT_OF_RANGE)),
        other => Err(NativeError { code: other as u32 }),
    }
}

pub(crate) fn bytes(text: &str) -> raw::NtByteView {
    raw::NtByteView {
        data: text.as_ptr().cast(),
        count: text.len(),
    }
}
