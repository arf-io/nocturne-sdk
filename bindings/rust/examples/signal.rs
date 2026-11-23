use nocturne::raw;
use nocturne::{
    CellStyle, Component, Constraints, DispatchPolicy, Event, Input, Measure, MountOptions,
    NativeError, Paint, Point, Size, Tree,
};
use std::error::Error;
use std::io::{self, BufRead, Write};

const WIDTH: u32 = 12;
const SIZE: Size = Size {
    columns: WIDTH,
    rows: 2,
};
const INK: CellStyle = CellStyle {
    foreground: raw::NtRgba {
        red: 100,
        green: 230,
        blue: 190,
        alpha: 255,
    },
    background: raw::NtRgba {
        red: 24,
        green: 32,
        blue: 46,
        alpha: 255,
    },
};

struct Signal {
    level: u32,
}

impl Component for Signal {
    fn measure(
        &self,
        _measure: &mut Measure<'_>,
        _limits: Constraints,
    ) -> Result<Size, NativeError> {
        Ok(Size {
            columns: WIDTH,
            rows: 1,
        })
    }

    fn paint(&self, paint: &mut Paint<'_>, assigned: Size) -> Result<(), NativeError> {
        for x in 0..assigned.columns.min(WIDTH) {
            let glyph = if x < self.level { "#" } else { "." };
            paint.glyph(Point { x: x as i32, y: 0 }, glyph, 1, INK)?;
        }
        Ok(())
    }

    fn event(
        &mut self,
        event: &mut Event<'_>,
        reply: &mut raw::NtEventReply,
    ) -> Result<(), NativeError> {
        let Some(key) = event.key().filter(|key| key.state == raw::NT_KEY_PRESS) else {
            return Ok(());
        };
        self.level = match char::from_u32(key.key) {
            Some('+') => (self.level + 1).min(WIDTH),
            Some('-') => self.level.saturating_sub(1),
            _ => return Ok(()),
        };
        reply.consume = true;
        reply.invalidate = raw::NT_INVALIDATE_PAINT;
        Ok(())
    }
}

fn render(tree: &Tree, output: &mut impl Write) -> Result<(), Box<dyn Error>> {
    let frame = tree.frame().expect("published frame");
    for y in 0..frame.size().rows {
        let mut x = 0;
        while x < frame.size().columns {
            let cell = frame.cell(Point {
                x: x as i32,
                y: y as i32,
            })?;
            if cell.columns == 0 {
                output.write_all(b" ")?;
                x += 1;
            } else {
                output.write_all(cell.glyph.as_bytes())?;
                x += u32::from(cell.columns);
            }
        }
        writeln!(output)?;
    }
    output.flush()?;
    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    let mut tree = Tree::new()?;
    let passive = MountOptions {
        enabled: true,
        focusable: false,
        hit_testable: false,
    };
    let root = tree.mount_box(
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
    let title_style = raw::NtLabelStyle {
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
    tree.mount_label(Some(&root), passive, "Signal level", title_style)?;
    let meter = tree.mount(
        Some(&root),
        MountOptions {
            focusable: true,
            ..passive
        },
        Signal { level: 4 },
    )?;

    let mut now = 0u64;
    tree.tick(now, raw::NT_MOTION_FULL)?;
    tree.publish(SIZE)?;
    tree.set_focus(Some(&meter))?;
    let mut output = io::stdout().lock();
    render(&tree, &mut output)?;
    for line in io::stdin().lock().lines() {
        let Some(key) = line?.chars().next() else {
            continue;
        };
        if key == 'q' {
            break;
        }
        if matches!(key, '+' | '-') {
            now = now.saturating_add(50_000);
            tree.tick(now, raw::NT_MOTION_FULL)?;
            let input = Input::Key {
                key: raw::NtKeyInput {
                    key: u32::from(key),
                    modifiers: 0,
                    state: raw::NT_KEY_PRESS,
                },
                text: "",
            };
            tree.dispatch(now, input, DispatchPolicy::default())?;
            tree.publish(SIZE)?;
            render(&tree, &mut output)?;
        }
    }
    tree.close()?;
    Ok(())
}
