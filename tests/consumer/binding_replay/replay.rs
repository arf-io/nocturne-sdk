use nocturne::raw;
use nocturne::{
    sample_tween, CellStyle, Component, Constraints, DispatchPolicy, Event, Input, Measure,
    MountOptions, NativeError, Paint, Point, Size, Tree, Tween,
};
use std::cell::RefCell;
use std::error::Error;
use std::rc::Rc;

const INK: CellStyle = CellStyle {
    foreground: raw::NtRgba {
        red: 230,
        green: 240,
        blue: 255,
        alpha: 255,
    },
    background: raw::NtRgba {
        red: 16,
        green: 24,
        blue: 32,
        alpha: 255,
    },
};
const PAYLOAD: &[u8] = b"x\0\xc3\xa9";

struct State {
    level: u32,
    keys: u32,
    updates: u32,
    elapsed: u64,
    disposals: u32,
    fail_paint: bool,
}

struct Meter(Rc<RefCell<State>>);

impl Component for Meter {
    fn measure(&self, _: &mut Measure<'_>, _: Constraints) -> Result<Size, NativeError> {
        Ok(Size {
            columns: 5,
            rows: 1,
        })
    }

    fn update(&mut self, info: raw::NtUpdateInfo) -> Result<raw::NtUpdateReply, NativeError> {
        let mut state = self.0.borrow_mut();
        state.updates += 1;
        state.elapsed = info.elapsed;
        Ok(raw::NtUpdateReply {
            invalidate: raw::NT_INVALIDATE_PAINT,
            next: raw::NtDeadline {
                scheduled: state.updates == 1,
                at: info.now + 250,
            },
        })
    }

    fn paint(&self, paint: &mut Paint<'_>, _: Size) -> Result<(), NativeError> {
        let state = self.0.borrow();
        if state.fail_paint {
            return Err(NativeError::from_code(raw::NT_RESULT_CALLBACK_FAILED as u32).unwrap());
        }
        paint.glyph(Point { x: 0, y: 0 }, &state.level.to_string(), 1, INK)?;
        paint.glyph(Point { x: 1, y: 0 }, "界", 2, INK)
    }

    fn event(
        &mut self,
        event: &mut Event<'_>,
        reply: &mut raw::NtEventReply,
    ) -> Result<(), NativeError> {
        if event.key().map(|key| key.key) != Some(u32::from('+')) {
            return Ok(());
        }
        let mut state = self.0.borrow_mut();
        state.keys += 1;
        state.level += 1;
        reply.consume = true;
        reply.invalidate = raw::NT_INVALIDATE_PAINT;
        event.emit(41, PAYLOAD)
    }
}

impl Drop for Meter {
    fn drop(&mut self) {
        self.0.borrow_mut().disposals += 1;
    }
}

fn record_frame(tree: &Tree, stage: &str) -> Result<(), NativeError> {
    let frame = tree.frame().expect("published frame");
    let size = frame.size();
    let label = frame.cell(Point { x: 2, y: 0 })?;
    let meter = frame.cell(Point { x: 2, y: 1 })?;
    let level = frame.cell(Point { x: 0, y: 1 })?;
    println!(
        "{stage} {} {} {} {} {} {} {} {} {} {} {}",
        size.columns,
        size.rows,
        label.lead.x,
        label.lead.y,
        label.columns,
        label.glyph,
        meter.lead.x,
        meter.lead.y,
        meter.columns,
        meter.glyph,
        level.glyph
    );
    Ok(())
}

fn replay() -> Result<(), Box<dyn Error>> {
    let state = Rc::new(RefCell::new(State {
        level: 4,
        keys: 0,
        updates: 0,
        elapsed: 0,
        disposals: 0,
        fail_paint: false,
    }));
    let mut tree = Tree::new()?;
    let passive = MountOptions {
        enabled: true,
        focusable: false,
        hit_testable: false,
    };
    let interactive = MountOptions {
        enabled: true,
        focusable: true,
        hit_testable: true,
    };
    let parent = tree.mount_box(
        None,
        passive,
        raw::NT_BOX_LAYOUT_COLUMN,
        raw::NtBoxInsets {
            left: 0,
            top: 0,
            right: 0,
            bottom: 0,
        },
    )?;
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
        color: INK,
    };
    tree.mount_label(Some(&parent), passive, "A👩‍💻", style)?;
    let node = tree.mount(Some(&parent), interactive, Meter(state.clone()))?;
    tree.tick(1000, raw::NT_MOTION_FULL)?;
    tree.publish(Size {
        columns: 7,
        rows: 3,
    })?;
    record_frame(&tree, "initial")?;
    let deadline = tree.next_update().expect("scheduled callback");
    tree.set_focus(Some(&node))?;

    let code = u32::from('+');
    let key = raw::NtKeyInput {
        key: code,
        modifiers: 0,
        state: raw::NT_KEY_PRESS,
    };
    let chord = raw::NtKeyChord {
        key: code,
        modifiers: 0,
    };
    let policy = DispatchPolicy {
        reserved: &[chord],
        traverse_focus: false,
    };
    let reserved = tree.dispatch(1000, Input::Key { key, text: "!" }, policy)?;
    assert_eq!(state.borrow().keys, 0);
    assert!(tree.peek_intent().is_none());
    let delivery = tree.dispatch(
        1000,
        Input::Key { key, text: "" },
        DispatchPolicy::default(),
    )?;
    let intent = tree.peek_intent().expect("key intent");
    assert_eq!(intent.source, node);
    assert_eq!(intent.payload, PAYLOAD);
    println!(
        "event {} {} {} {} {} {} {} {} {}",
        u8::from(reserved.reserved),
        u8::from(reserved.consumed),
        u8::from(reserved.text_delivered),
        u8::from(delivery.target.as_ref() == Some(&node)),
        u8::from(delivery.consumed),
        u8::from(delivery.text_delivered),
        state.borrow().keys,
        intent.kind,
        intent.payload.len()
    );
    tree.pop_intent();
    assert!(tree.peek_intent().is_none());

    tree.tick(1250, raw::NT_MOTION_FULL)?;
    {
        let state = state.borrow();
        println!("clock {deadline} {} {}", state.updates, state.elapsed);
    }
    state.borrow_mut().fail_paint = true;
    let next_size = Size {
        columns: 5,
        rows: 2,
    };
    let failure = tree.publish(next_size).expect_err("failed paint published");
    assert_eq!(failure.callbacks.len(), 1);
    println!("failure {}", failure.native.unwrap().code());
    record_frame(&tree, "held")?;
    state.borrow_mut().fail_paint = false;
    tree.publish(next_size)?;
    record_frame(&tree, "updated")?;
    tree.remove(&node)?;
    for _ in 0..512 {
        let next = tree.mount(Some(&parent), interactive, Meter(state.clone()))?;
        assert_ne!(next, node);
        tree.remove(&next)?;
    }
    tree.mount(Some(&parent), interactive, Meter(state.clone()))?;
    tree.close()?;
    println!("dispose {}", state.borrow().disposals);
    Ok(())
}

fn sample_animation() -> Result<(), NativeError> {
    let tween = Tween {
        from: -3.25,
        to: 8.5,
        duration_us: 1000,
        delay_us: 100,
        easing: raw::NT_SINE,
        repeat: raw::NT_PING_PONG,
    };
    for elapsed in [0, 100, 433, 1100, 1433, 2100] {
        let sample = sample_tween(&tween, elapsed)?;
        println!(
            "sample {elapsed} {:.17e} {:.17e} {} {} {}",
            sample.value, sample.progress, sample.iteration, sample.phase, sample.direction
        );
    }
    let mut cycle = Tween {
        from: 0.,
        to: 1.,
        duration_us: 1,
        delay_us: 0,
        easing: raw::NT_LINEAR,
        repeat: raw::NT_LOOP,
    };
    println!("iteration {}", sample_tween(&cycle, u64::MAX)?.iteration);
    cycle.duration_us = 0;
    let failure = sample_tween(&cycle, 0)
        .err()
        .expect("zero-duration loop accepted");
    println!("invalid {}", failure.code());
    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    replay()?;
    sample_animation()?;
    Ok(())
}
