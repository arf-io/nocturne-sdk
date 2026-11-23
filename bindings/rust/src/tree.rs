use crate::component::{self, CallbackFailure, CallbackFailures, Component, MountOptions};
use crate::{
    bytes, ensure_abi, native_error, raw, status, DispatchError, DispatchPolicy, Frame, Input,
    NativeError, Rect, Size,
};
use std::cell::RefCell;
use std::fmt;
use std::mem::{self, MaybeUninit};
use std::ptr::{self, NonNull};
use std::rc::{Rc, Weak};

/// A native operation can report an error and deliver callback failures.
#[derive(Debug)]
pub struct CallError {
    pub native: Option<NativeError>,
    pub callbacks: Vec<CallbackFailure>,
}

impl From<NativeError> for CallError {
    fn from(error: NativeError) -> Self {
        Self {
            native: Some(error),
            callbacks: Vec::new(),
        }
    }
}

impl fmt::Display for CallError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Some(error) = self.native {
            write!(f, "{error}")
        } else {
            write!(f, "{} Nocturne callback failure(s)", self.callbacks.len())
        }
    }
}

impl std::error::Error for CallError {}

#[derive(Clone, Debug)]
pub struct Node {
    id: raw::NtNodeId,
    tree: Weak<()>,
}

impl PartialEq for Node {
    fn eq(&self, other: &Self) -> bool {
        self.tree.ptr_eq(&other.tree)
            && self.id.slot == other.id.slot
            && self.id.generation == other.id.generation
    }
}

impl Eq for Node {}

pub struct Geometry {
    pub parent: Option<Node>,
    pub bounds: Rect,
    pub clip: Rect,
    pub placed: bool,
    pub enabled: bool,
}

#[derive(Debug)]
pub struct Dispatch {
    pub target: Option<Node>,
    pub reserved: bool,
    pub consumed: bool,
    pub text_delivered: bool,
}

pub struct Intent<'tree> {
    pub source: Node,
    pub kind: u32,
    pub payload: &'tree [u8],
}

/// An owned, caller-driven component tree. Cell and intent borrows are scoped
/// to immutable borrows of this tree.
pub struct Tree {
    native: Option<NonNull<raw::NtTree>>,
    identity: Rc<()>,
    failures: CallbackFailures,
}

fn root() -> raw::NtNodeId {
    raw::NtNodeId {
        tree: ptr::null(),
        slot: 0,
        generation: 0,
    }
}

impl Tree {
    pub fn new() -> Result<Self, NativeError> {
        ensure_abi()?;
        let mut pointer = ptr::null_mut();
        status(unsafe { raw::nt_tree_create(ptr::null(), &mut pointer) })?;
        Ok(Self {
            native: Some(
                NonNull::new(pointer).expect("successful tree creation returned no handle"),
            ),
            identity: Rc::new(()),
            failures: Rc::new(RefCell::new(Vec::new())),
        })
    }

    fn raw(&self) -> *mut raw::NtTree {
        self.native.expect("live tree").as_ptr()
    }

    fn node(&self, id: raw::NtNodeId) -> Node {
        Node {
            id,
            tree: Rc::downgrade(&self.identity),
        }
    }

    fn optional_node(&self, id: raw::NtNodeId) -> Option<Node> {
        (!id.tree.is_null()).then(|| self.node(id))
    }

    fn id(&self, node: &Node) -> Result<raw::NtNodeId, NativeError> {
        let identity = node
            .tree
            .upgrade()
            .ok_or_else(|| native_error(raw::NT_RESULT_STALE_REFERENCE))?;
        if !Rc::ptr_eq(&identity, &self.identity) {
            return Err(native_error(raw::NT_RESULT_INVALID_ARGUMENT));
        }
        Ok(node.id)
    }

    fn parent(&self, parent: Option<&Node>) -> Result<raw::NtNodeId, NativeError> {
        parent.map_or_else(|| Ok(root()), |node| self.id(node))
    }

    fn finish(&self, result: raw::NtResult) -> Result<(), CallError> {
        let callbacks = mem::take(&mut *self.failures.borrow_mut());
        let native = status(result).err();
        if native.is_some() || !callbacks.is_empty() {
            Err(CallError { native, callbacks })
        } else {
            Ok(())
        }
    }

    /// Mount a component and transfer its Rust-owned state to the tree.
    /// A failed mount drops the component before returning.
    pub fn mount<C: Component>(
        &mut self,
        parent: Option<&Node>,
        options: MountOptions,
        component: C,
    ) -> Result<Node, CallError> {
        let parent = self.parent(parent)?;
        let boxed = Box::new(component::State {
            value: component,
            failures: Rc::clone(&self.failures),
        });
        let pointer = Box::into_raw(boxed);
        let mut id = root();
        let result = unsafe {
            raw::nt_tree_mount(
                self.raw(),
                parent,
                &component::ops::<C>(),
                pointer.cast(),
                options,
                &mut id,
            )
        };
        if result != raw::NT_RESULT_OK {
            // A failed mount never transfers ownership to the native tree.
            drop(unsafe { Box::from_raw(pointer) });
        }
        self.finish(result)?;
        Ok(self.node(id))
    }

    /// Run an installed native mount function and attach the result to this tree.
    ///
    /// # Safety
    /// The closure must call a compatible native mount function using the
    /// supplied pointers and return its result. It must honor the function's
    /// input lifetimes, complete without unwinding, and leave the tree alive.
    pub unsafe fn mount_native(
        &mut self,
        parent: Option<&Node>,
        mount: impl FnOnce(*mut raw::NtTree, raw::NtNodeId, *mut raw::NtNodeId) -> raw::NtResult,
    ) -> Result<Node, CallError> {
        let parent = self.parent(parent)?;
        let mut id = root();
        let result = mount(self.raw(), parent, &mut id);
        self.finish(result)?;
        let node = self.node(id);
        self.geometry(&node)?;
        Ok(node)
    }

    /// Run a native operation with a node belonging to this tree.
    ///
    /// # Safety
    /// The closure must respect the native operation's argument and ownership
    /// contracts, complete without unwinding, and leave the tree alive.
    /// Native tree access through the supplied pointer ends with the closure.
    pub unsafe fn call_native(
        &mut self,
        node: &Node,
        operation: impl FnOnce(*mut raw::NtTree, raw::NtNodeId) -> raw::NtResult,
    ) -> Result<(), CallError> {
        let id = self.id(node)?;
        self.finish(operation(self.raw(), id))
    }

    pub fn mount_label(
        &mut self,
        parent: Option<&Node>,
        options: MountOptions,
        text: &str,
        style: raw::NtLabelStyle,
    ) -> Result<Node, CallError> {
        let label = raw::NtLabelOptions {
            text: bytes(text),
            style,
        };
        unsafe {
            self.mount_native(parent, |tree, parent, out| {
                raw::nt_label_mount(tree, parent, options, &label, out)
            })
        }
    }

    pub fn set_label(
        &mut self,
        node: &Node,
        text: &str,
        style: raw::NtLabelStyle,
    ) -> Result<(), CallError> {
        let label = raw::NtLabelOptions {
            text: bytes(text),
            style,
        };
        unsafe { self.call_native(node, |tree, id| raw::nt_label_set(tree, id, &label)) }
    }

    pub fn mount_box(
        &mut self,
        parent: Option<&Node>,
        options: MountOptions,
        kind: raw::NtBoxLayoutKind,
        padding: raw::NtBoxInsets,
    ) -> Result<Node, CallError> {
        let parent = self.parent(parent)?;
        let layout = raw::NtBoxLayout {
            kind,
            padding,
            gap: 0,
            horizontal: raw::NT_BOX_START,
            vertical: raw::NT_BOX_START,
            fallback: raw::NtBoxItem {
                columns: raw::NtExtentRange {
                    min: 0,
                    max: 0,
                    bounded: false,
                },
                rows: raw::NtExtentRange {
                    min: 0,
                    max: 0,
                    bounded: false,
                },
                grow_columns: false,
                grow_rows: false,
            },
            items: ptr::null(),
            item_count: 0,
        };
        let mut id = root();
        let result =
            unsafe { raw::nt_box_layout_mount(self.raw(), parent, options, &layout, &mut id) };
        self.finish(result)?;
        Ok(self.node(id))
    }

    pub fn remove(&mut self, node: &Node) -> Result<(), CallError> {
        let id = self.id(node)?;
        self.finish(unsafe { raw::nt_tree_remove(self.raw(), id) })
    }

    pub fn tick(&mut self, now: u64, motion: raw::NtMotionPolicy) -> Result<(), CallError> {
        self.finish(unsafe { raw::nt_tree_tick(self.raw(), now, motion) })
    }

    pub fn next_update(&self) -> Option<u64> {
        let deadline = unsafe { raw::nt_tree_next_update(self.raw()) };
        deadline.scheduled.then_some(deadline.at)
    }

    pub fn publish(&mut self, size: Size) -> Result<(), CallError> {
        self.finish(unsafe { raw::nt_tree_publish(self.raw(), size) })
    }

    pub fn frame(&self) -> Option<Frame<'_>> {
        let frame = unsafe { raw::nt_tree_frame(self.raw()) };
        (!frame.is_null()).then(|| unsafe { Frame::from_raw(frame) })
    }

    pub fn geometry(&self, node: &Node) -> Result<Geometry, NativeError> {
        let mut out = MaybeUninit::uninit();
        status(unsafe { raw::nt_tree_geometry(self.raw(), self.id(node)?, out.as_mut_ptr()) })?;
        let geometry = unsafe { out.assume_init() };
        Ok(Geometry {
            parent: self.optional_node(geometry.parent),
            bounds: geometry.bounds,
            clip: geometry.clip,
            placed: geometry.placed,
            enabled: geometry.enabled,
        })
    }

    pub fn set_focus(&mut self, node: Option<&Node>) -> Result<(), CallError> {
        let id = self.parent(node)?;
        self.finish(unsafe { raw::nt_tree_set_focus(self.raw(), id) })
    }

    pub fn focus(&self) -> Option<Node> {
        self.optional_node(unsafe { raw::nt_tree_focus(self.raw()) })
    }

    pub fn capture(&self, pointer: u64) -> Option<Node> {
        self.optional_node(unsafe { raw::nt_tree_capture(self.raw(), pointer) })
    }

    pub fn dispatch(
        &mut self,
        at: u64,
        input: Input<'_>,
        policy: DispatchPolicy<'_>,
    ) -> Result<Dispatch, DispatchError> {
        let (event, text) = input.native(at);
        let policy = policy.native();
        // A null native target has slot zero. This sentinel identifies the
        // unchanged report on pre-admission failure without reading padding.
        let mut out = raw::NtDispatchResult {
            target: raw::NtNodeId {
                tree: ptr::null(),
                slot: u32::MAX,
                generation: 0,
            },
            reserved: false,
            consumed: false,
            text_delivered: false,
        };
        let result = unsafe { raw::nt_tree_dispatch(self.raw(), &event, text, &policy, &mut out) };
        let delivery = (out.target.slot != u32::MAX).then(|| Dispatch {
            target: self.optional_node(out.target),
            reserved: out.reserved,
            consumed: out.consumed,
            text_delivered: out.text_delivered,
        });
        match self.finish(result) {
            Ok(()) => Ok(delivery.expect("admitted dispatch returned no report")),
            Err(cause) => Err(DispatchError { cause, delivery }),
        }
    }

    pub fn paste(&mut self, at: u64, text: &str) -> Result<Dispatch, DispatchError> {
        self.dispatch(at, Input::Paste(text), DispatchPolicy::default())
    }

    pub fn peek_intent(&self) -> Option<Intent<'_>> {
        let mut out = MaybeUninit::uninit();
        if !unsafe { raw::nt_tree_peek_intent(self.raw(), out.as_mut_ptr()) } {
            return None;
        }
        let out = unsafe { out.assume_init() };
        let payload = if out.payload.count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(out.payload.data.cast(), out.payload.count) }
        };
        Some(Intent {
            source: self.node(out.source),
            kind: out.kind,
            payload,
        })
    }

    pub fn pop_intent(&mut self) {
        unsafe { raw::nt_tree_pop_intent(self.raw()) };
    }

    /// Close the tree and return any failures delivered during disposal.
    pub fn close(mut self) -> Result<(), CallError> {
        self.destroy();
        self.finish(raw::NT_RESULT_OK)
    }

    fn destroy(&mut self) {
        if let Some(tree) = self.native.take() {
            unsafe { raw::nt_tree_destroy(tree.as_ptr()) };
        }
    }
}

impl Drop for Tree {
    fn drop(&mut self) {
        self.destroy();
    }
}
