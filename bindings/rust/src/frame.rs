use crate::{raw, status, NativeError, Point, Size};
use std::marker::PhantomData;
use std::mem::MaybeUninit;

/// A view of the tree's current committed cell frame.
pub struct Frame<'tree> {
    raw: *const raw::NtFrame,
    lifetime: PhantomData<&'tree raw::NtFrame>,
}

/// A single cell. A wide-glyph continuation refers to the same glyph and lead.
pub struct Cell<'frame> {
    pub glyph: &'frame str,
    pub lead: Point,
    pub columns: u8,
    pub style: raw::NtCellStyle,
    pub owner: u64,
}

impl<'tree> Frame<'tree> {
    pub(crate) unsafe fn from_raw(raw: *const raw::NtFrame) -> Self {
        Self {
            raw,
            lifetime: PhantomData,
        }
    }

    pub fn size(&self) -> Size {
        unsafe { raw::nt_frame_size(self.raw) }
    }

    pub fn cell(&self, point: Point) -> Result<Cell<'_>, NativeError> {
        let mut cell = MaybeUninit::uninit();
        status(unsafe { raw::nt_frame_cell(self.raw, point, cell.as_mut_ptr()) })?;
        let cell = unsafe { cell.assume_init() };
        let glyph = if cell.glyph.count == 0 {
            ""
        } else {
            let bytes =
                unsafe { std::slice::from_raw_parts(cell.glyph.data.cast(), cell.glyph.count) };
            std::str::from_utf8(bytes).expect("Nocturne frames contain validated graphemes")
        };
        Ok(Cell {
            glyph,
            lead: cell.lead,
            columns: cell.glyph_columns,
            style: cell.style,
            owner: cell.owner,
        })
    }
}
