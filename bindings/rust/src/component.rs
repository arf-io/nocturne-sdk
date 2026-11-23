use crate::{bytes, raw, status, NativeError, Point, Rect, Size};
use std::any::Any;
use std::cell::RefCell;
use std::ffi::c_void;
use std::fmt;
use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::rc::Rc;

pub use raw::{
    NtCellStyle as CellStyle, NtConstraints as Constraints, NtMountOptions as MountOptions,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CallbackPhase {
    Measure,
    Layout,
    Update,
    Paint,
    Event,
    Dispose,
}

#[derive(Debug)]
pub enum CallbackCause {
    Status(NativeError),
    Panic(CallbackPanic),
}

pub struct CallbackPanic(Box<dyn Any + Send>);

impl CallbackPanic {
    pub fn message(&self) -> Option<&str> {
        self.0
            .downcast_ref::<String>()
            .map(String::as_str)
            .or_else(|| self.0.downcast_ref::<&str>().copied())
    }

    pub fn into_payload(self) -> Box<dyn Any + Send> {
        self.0
    }
}

impl fmt::Debug for CallbackPanic {
    fn fmt(&self, out: &mut fmt::Formatter<'_>) -> fmt::Result {
        out.debug_tuple("CallbackPanic")
            .field(&self.message())
            .finish()
    }
}

#[derive(Debug)]
pub struct CallbackFailure {
    pub phase: CallbackPhase,
    pub cause: CallbackCause,
}

pub(crate) type CallbackFailures = Rc<RefCell<Vec<CallbackFailure>>>;

pub struct Measure<'call> {
    native: *mut raw::NtMeasureContext,
    borrow: PhantomData<&'call mut raw::NtMeasureContext>,
}

impl Measure<'_> {
    pub fn child_count(&self) -> usize {
        unsafe { raw::nt_measure_child_count(self.native) }
    }

    pub fn child(&mut self, index: usize, constraints: Constraints) -> Result<Size, NativeError> {
        let mut id = MaybeUninit::uninit();
        status(unsafe { raw::nt_measure_child_at(self.native, index, id.as_mut_ptr()) })?;
        let mut size = MaybeUninit::uninit();
        status(unsafe {
            raw::nt_measure_child(
                self.native,
                id.assume_init(),
                constraints,
                size.as_mut_ptr(),
            )
        })?;
        Ok(unsafe { size.assume_init() })
    }
}

pub struct Layout<'call> {
    native: *mut raw::NtLayoutContext,
    borrow: PhantomData<&'call mut raw::NtLayoutContext>,
}

impl Layout<'_> {
    pub fn child_count(&self) -> usize {
        unsafe { raw::nt_layout_child_count(self.native) }
    }

    fn id(&self, index: usize) -> Result<raw::NtNodeId, NativeError> {
        let mut id = MaybeUninit::uninit();
        status(unsafe { raw::nt_layout_child_at(self.native, index, id.as_mut_ptr()) })?;
        Ok(unsafe { id.assume_init() })
    }

    pub fn child_size(&self, index: usize) -> Result<Size, NativeError> {
        let mut size = MaybeUninit::uninit();
        status(unsafe {
            raw::nt_layout_child_size(self.native, self.id(index)?, size.as_mut_ptr())
        })?;
        Ok(unsafe { size.assume_init() })
    }

    pub fn place(&mut self, index: usize, bounds: Rect, z_index: i32) -> Result<(), NativeError> {
        status(unsafe {
            raw::nt_layout_place(
                self.native,
                self.id(index)?,
                bounds,
                z_index,
                std::ptr::null(),
            )
        })
    }
}

pub struct Paint<'call> {
    native: *mut raw::NtPaintContext,
    borrow: PhantomData<&'call mut raw::NtPaintContext>,
}

impl Paint<'_> {
    pub fn clip(&self) -> Rect {
        unsafe { raw::nt_paint_clip(self.native) }
    }

    pub fn glyph(
        &mut self,
        point: Point,
        glyph: &str,
        columns: u8,
        style: CellStyle,
    ) -> Result<(), NativeError> {
        status(unsafe { raw::nt_paint_glyph(self.native, point, bytes(glyph), columns, style) })
    }

    pub fn fill(&mut self, bounds: Rect, color: raw::NtRgba) -> Result<(), NativeError> {
        status(unsafe { raw::nt_paint_fill(self.native, bounds, color) })
    }
}

pub struct Event<'call> {
    native: &'call raw::NtEvent,
    context: *mut raw::NtEventContext,
}

impl Event<'_> {
    pub fn kind(&self) -> raw::NtEventKind {
        self.native.kind
    }

    pub fn time(&self) -> u64 {
        self.native.at
    }

    pub fn phase(&self) -> raw::NtEventPhase {
        unsafe { raw::nt_event_phase(self.context) }
    }

    pub fn key(&self) -> Option<raw::NtKeyInput> {
        match self.kind() {
            raw::NT_EVENT_KEY => Some(unsafe { self.native.data.key }),
            _ => None,
        }
    }

    pub fn pointer(&self) -> Option<raw::NtPointerInput> {
        match self.kind() {
            raw::NT_EVENT_POINTER_DOWN | raw::NT_EVENT_POINTER_MOVE | raw::NT_EVENT_POINTER_UP => {
                Some(unsafe { self.native.data.pointer })
            }
            _ => None,
        }
    }

    pub fn local_position(&self) -> Option<Point> {
        let mut point = MaybeUninit::uninit();
        unsafe {
            raw::nt_event_local_position(self.context, point.as_mut_ptr())
                .then(|| point.assume_init())
        }
    }

    pub fn text(&self) -> Option<&str> {
        if !matches!(self.kind(), raw::NT_EVENT_TEXT | raw::NT_EVENT_PASTE) {
            return None;
        }
        let view = unsafe { self.native.data.text };
        if view.count == 0 {
            return Some("");
        }
        let data = unsafe { std::slice::from_raw_parts(view.data.cast(), view.count) };
        Some(std::str::from_utf8(data).expect("Nocturne text events contain UTF-8"))
    }

    pub fn emit(&mut self, kind: u32, payload: &[u8]) -> Result<(), NativeError> {
        let bytes = raw::NtByteView {
            data: payload.as_ptr().cast(),
            count: payload.len(),
        };
        status(unsafe { raw::nt_event_emit(self.context, kind, bytes) })
    }
}

/// A Rust component mounted in the ordinary native tree.
pub trait Component: 'static {
    fn measure(
        &self,
        context: &mut Measure<'_>,
        constraints: Constraints,
    ) -> Result<Size, NativeError>;

    fn layout(&self, _context: &mut Layout<'_>, _assigned: Size) -> Result<(), NativeError> {
        Ok(())
    }

    fn update(&mut self, _info: raw::NtUpdateInfo) -> Result<raw::NtUpdateReply, NativeError> {
        Ok(raw::NtUpdateReply {
            invalidate: raw::NT_INVALIDATE_NONE,
            next: raw::NtDeadline {
                scheduled: false,
                at: 0,
            },
        })
    }

    fn paint(&self, context: &mut Paint<'_>, assigned: Size) -> Result<(), NativeError>;

    fn event(
        &mut self,
        _event: &mut Event<'_>,
        _reply: &mut raw::NtEventReply,
    ) -> Result<(), NativeError> {
        Ok(())
    }
}

pub(crate) struct State<C> {
    pub value: C,
    pub failures: CallbackFailures,
}

fn callback(
    failures: &CallbackFailures,
    phase: CallbackPhase,
    operation: impl FnOnce() -> Result<(), NativeError>,
) -> raw::NtResult {
    let (code, cause) = match catch_unwind(AssertUnwindSafe(operation)) {
        Ok(Ok(())) => return raw::NT_RESULT_OK,
        Ok(Err(error)) => (error.code() as raw::NtResult, CallbackCause::Status(error)),
        Err(panic) => (
            raw::NT_RESULT_CALLBACK_FAILED,
            CallbackCause::Panic(CallbackPanic(panic)),
        ),
    };
    failures.borrow_mut().push(CallbackFailure { phase, cause });
    code
}

unsafe extern "C" fn measure<C: Component>(
    state: *const c_void,
    context: *mut raw::NtMeasureContext,
    limits: Constraints,
    out: *mut Size,
) -> raw::NtResult {
    let state = unsafe { &*state.cast::<State<C>>() };
    callback(&state.failures, CallbackPhase::Measure, || {
        let mut measure = Measure {
            native: context,
            borrow: PhantomData,
        };
        let preferred = state.value.measure(&mut measure, limits)?;
        unsafe { out.write(preferred) };
        Ok(())
    })
}

unsafe extern "C" fn layout<C: Component>(
    state: *const c_void,
    context: *mut raw::NtLayoutContext,
    assigned: Size,
) -> raw::NtResult {
    let state = unsafe { &*state.cast::<State<C>>() };
    callback(&state.failures, CallbackPhase::Layout, || {
        let mut layout = Layout {
            native: context,
            borrow: PhantomData,
        };
        state.value.layout(&mut layout, assigned)
    })
}

unsafe extern "C" fn update<C: Component>(
    state: *mut c_void,
    info: raw::NtUpdateInfo,
    reply: *mut raw::NtUpdateReply,
) -> raw::NtResult {
    let state = unsafe { &mut *state.cast::<State<C>>() };
    callback(&state.failures, CallbackPhase::Update, || {
        let result = state.value.update(info)?;
        unsafe { reply.write(result) };
        Ok(())
    })
}

unsafe extern "C" fn paint<C: Component>(
    state: *const c_void,
    context: *mut raw::NtPaintContext,
    assigned: Size,
) -> raw::NtResult {
    let state = unsafe { &*state.cast::<State<C>>() };
    callback(&state.failures, CallbackPhase::Paint, || {
        let mut paint = Paint {
            native: context,
            borrow: PhantomData,
        };
        state.value.paint(&mut paint, assigned)
    })
}

unsafe extern "C" fn event<C: Component>(
    state: *mut c_void,
    context: *mut raw::NtEventContext,
    native: *const raw::NtEvent,
    reply: *mut raw::NtEventReply,
) -> raw::NtResult {
    let state = unsafe { &mut *state.cast::<State<C>>() };
    callback(&state.failures, CallbackPhase::Event, || {
        let mut event = Event {
            native: unsafe { &*native },
            context,
        };
        state.value.event(&mut event, unsafe { &mut *reply })
    })
}

unsafe extern "C" fn dispose<C: Component>(state: *mut c_void) {
    let state = unsafe { Box::from_raw(state.cast::<State<C>>()) };
    let failures = Rc::clone(&state.failures);
    callback(&failures, CallbackPhase::Dispose, || {
        drop(state);
        Ok(())
    });
}

pub(crate) fn ops<C: Component>() -> raw::NtComponentOps {
    raw::NtComponentOps {
        measure: Some(measure::<C>),
        layout: Some(layout::<C>),
        update: Some(update::<C>),
        paint: Some(paint::<C>),
        event: Some(event::<C>),
        dispose: Some(dispose::<C>),
    }
}
