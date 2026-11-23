use nocturne::raw;
use nocturne::{
    CallbackCause, CallbackPhase, Component, Constraints, DispatchPolicy, Event, Input, Measure,
    MountOptions, NativeError, Paint, Point, Size, Tree,
};
use std::cell::Cell;
use std::rc::Rc;

const OPTIONS: MountOptions = MountOptions {
    enabled: true,
    focusable: true,
    hit_testable: true,
};
const SIZE: Size = Size {
    columns: 4,
    rows: 1,
};
const GLYPH: &str = "👩‍💻";

struct Indicator {
    updates: usize,
    disposed: Rc<Cell<usize>>,
}

impl Component for Indicator {
    fn measure(
        &self,
        _context: &mut Measure<'_>,
        _constraints: Constraints,
    ) -> Result<Size, NativeError> {
        Ok(Size {
            columns: 2,
            rows: 1,
        })
    }

    fn update(&mut self, info: raw::NtUpdateInfo) -> Result<raw::NtUpdateReply, NativeError> {
        self.updates += 1;
        Ok(raw::NtUpdateReply {
            invalidate: raw::NT_INVALIDATE_PAINT,
            next: raw::NtDeadline {
                scheduled: self.updates == 1,
                at: info.now + 250,
            },
        })
    }

    fn paint(&self, context: &mut Paint<'_>, _assigned: Size) -> Result<(), NativeError> {
        let rgb = raw::NtRgba {
            red: 210,
            green: 240,
            blue: 250,
            alpha: 255,
        };
        let clear = raw::NtRgba {
            red: 0,
            green: 0,
            blue: 0,
            alpha: 0,
        };
        let style = raw::NtCellStyle {
            foreground: rgb,
            background: clear,
        };
        let (glyph, columns) = if self.updates <= 1 {
            (GLYPH, 2)
        } else {
            ("!", 1)
        };
        context.glyph(Point { x: 0, y: 0 }, glyph, columns, style)
    }

    fn event(
        &mut self,
        event: &mut Event<'_>,
        reply: &mut raw::NtEventReply,
    ) -> Result<(), NativeError> {
        if event.kind() == raw::NT_EVENT_FOCUS_GAIN {
            event.emit(42, b"focus")?;
            panic!("indicator focus handler failed");
        }
        if event.key().is_some_and(|key| key.key == u32::from('!')) {
            event.emit(43, b"discard")?;
            return Err(NativeError::from_code(9).unwrap());
        }
        if event.kind() == raw::NT_EVENT_POINTER_DOWN {
            assert_eq!(event.local_position().unwrap().x, 0);
            reply.capture = raw::NT_CAPTURE_TAKE;
        }
        if matches!(event.kind(), raw::NT_EVENT_TEXT | raw::NT_EVENT_PASTE) {
            let text = event.text().unwrap().as_bytes().to_vec();
            event.emit(41, &text)?;
            reply.consume = true;
        }
        Ok(())
    }
}

impl Drop for Indicator {
    fn drop(&mut self) {
        self.disposed.set(self.disposed.get() + 1);
    }
}

#[test]
fn rust_custom_component_uses_native_layout_and_scoped_failures() {
    let disposed = Rc::new(Cell::new(0));
    let mut tree = Tree::new().unwrap();
    let root = tree
        .mount_box(
            None,
            OPTIONS,
            raw::NT_BOX_LAYOUT_STACK,
            raw::NtBoxInsets {
                left: 1,
                top: 0,
                right: 0,
                bottom: 0,
            },
        )
        .unwrap();
    let child = tree
        .mount(
            Some(&root),
            OPTIONS,
            Indicator {
                updates: 0,
                disposed: Rc::clone(&disposed),
            },
        )
        .unwrap();
    tree.tick(1000, raw::NT_MOTION_FULL).unwrap();
    assert_eq!(tree.next_update(), Some(1250));
    tree.publish(SIZE).unwrap();
    let geometry = tree.geometry(&child).unwrap();
    assert_eq!(geometry.parent, Some(root.clone()));
    assert_eq!(geometry.bounds.origin.x, 1);
    let frame = tree.frame().unwrap();
    let continuation = frame.cell(Point { x: 2, y: 0 }).unwrap();
    assert_eq!(
        (
            continuation.glyph,
            continuation.lead.x,
            continuation.columns
        ),
        (GLYPH, 1, 2)
    );

    let focus = tree.set_focus(Some(&child)).unwrap_err();
    assert!(focus.native.is_none());
    assert_eq!(focus.callbacks.len(), 1);
    assert_eq!(focus.callbacks[0].phase, CallbackPhase::Event);
    match &focus.callbacks[0].cause {
        CallbackCause::Panic(cause) => {
            assert_eq!(cause.message(), Some("indicator focus handler failed"))
        }
        CallbackCause::Status(error) => panic!("unexpected status {error}"),
    }
    assert_eq!(tree.focus(), Some(child.clone()));
    assert!(tree.peek_intent().is_none());
    let key = raw::NtKeyInput {
        key: u32::from('!'),
        modifiers: 0,
        state: raw::NT_KEY_PRESS,
    };
    let refused = tree
        .dispatch(
            1000,
            Input::Key { key, text: "!" },
            DispatchPolicy::default(),
        )
        .unwrap_err();
    assert_eq!(refused.cause.native.unwrap().code(), 9);
    assert_eq!(refused.delivery.unwrap().target, Some(child.clone()));
    assert!(tree.peek_intent().is_none());
    let reserved = [raw::NtKeyChord {
        key: key.key,
        modifiers: 0,
    }];
    let reserved = tree
        .dispatch(
            1000,
            Input::Key { key, text: "!" },
            DispatchPolicy {
                reserved: &reserved,
                traverse_focus: false,
            },
        )
        .unwrap();
    assert!(reserved.reserved && tree.peek_intent().is_none());

    let pointer = raw::NtPointerInput {
        id: 7,
        position: Point { x: 1, y: 0 },
        button: raw::NT_BUTTON_PRIMARY,
        pressed: 0,
        has_position: true,
        modifiers: 0,
    };
    let refused = tree
        .dispatch(1000, Input::PointerUp(pointer), DispatchPolicy::default())
        .unwrap_err();
    assert!(refused.delivery.is_none());
    let pointer = raw::NtPointerInput {
        pressed: raw::NT_BUTTON_PRIMARY,
        ..pointer
    };
    tree.dispatch(1000, Input::PointerDown(pointer), DispatchPolicy::default())
        .unwrap();
    assert_eq!(tree.capture(7), Some(child.clone()));
    tree.dispatch(
        1000,
        Input::PointerCancel(raw::NtPointerCancel {
            id: 7,
            reason: raw::NT_CANCEL_HOST,
        }),
        DispatchPolicy::default(),
    )
    .unwrap();
    assert!(tree.capture(7).is_none());

    let input = "x\0👩‍💻";
    let dispatch = tree.paste(1000, input).unwrap();
    assert!(dispatch.consumed);
    let intent = tree.peek_intent().unwrap();
    assert_eq!((intent.kind, intent.payload), (41, input.as_bytes()));
    tree.pop_intent();
    tree.tick(1250, raw::NT_MOTION_FULL).unwrap();
    tree.publish(SIZE).unwrap();
    assert_eq!(
        tree.frame()
            .unwrap()
            .cell(Point { x: 1, y: 0 })
            .unwrap()
            .glyph,
        "!"
    );

    tree.remove(&child).unwrap();
    assert_eq!(
        tree.geometry(&child).err().unwrap().name(),
        Some("NT_RESULT_STALE_REFERENCE")
    );
    assert_eq!(disposed.get(), 1);
    for _ in 0..512 {
        let replacement = tree
            .mount(
                Some(&root),
                OPTIONS,
                Indicator {
                    updates: 0,
                    disposed: Rc::clone(&disposed),
                },
            )
            .unwrap();
        assert_ne!(replacement, child);
        tree.remove(&replacement).unwrap();
    }
    tree.mount(
        Some(&root),
        OPTIONS,
        Indicator {
            updates: 0,
            disposed: Rc::clone(&disposed),
        },
    )
    .unwrap();
    tree.close().unwrap();
    assert_eq!(disposed.get(), 514);

    let next = Tree::new().unwrap();
    assert_eq!(
        next.geometry(&child).err().unwrap().name(),
        Some("NT_RESULT_STALE_REFERENCE")
    );
}

#[test]
fn stock_label_owns_text_and_keeps_its_mount_on_update() {
    let style = raw::NtLabelStyle {
        width: raw::NtWidthPolicy {
            ambiguous: raw::NT_WIDTH_ONE,
            emoji: raw::NT_WIDTH_TWO,
        },
        wrap: raw::NT_WRAP_NONE,
        align: raw::NT_TEXT_ALIGN_START,
        direction: raw::NT_DIRECTION_AUTO,
        tab_stop: 4,
        ellipsis: false,
        color: raw::NtCellStyle {
            foreground: raw::NtRgba {
                red: 235,
                green: 240,
                blue: 250,
                alpha: 255,
            },
            background: raw::NtRgba {
                red: 25,
                green: 30,
                blue: 45,
                alpha: 255,
            },
        },
    };
    let mut tree = Tree::new().unwrap();
    let label = tree.mount_label(None, OPTIONS, "🌙", style).unwrap();
    tree.publish(SIZE).unwrap();
    assert_eq!(
        tree.frame()
            .unwrap()
            .cell(Point { x: 1, y: 0 })
            .unwrap()
            .glyph,
        "🌙"
    );
    let updated = String::from("Q");
    tree.set_label(&label, &updated, style).unwrap();
    drop(updated);
    tree.publish(SIZE).unwrap();
    assert_eq!(
        tree.frame()
            .unwrap()
            .cell(Point { x: 0, y: 0 })
            .unwrap()
            .glyph,
        "Q"
    );
    assert!(tree.geometry(&label).unwrap().placed);
    tree.remove(&label).unwrap();
}
