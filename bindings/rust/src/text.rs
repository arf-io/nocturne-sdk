use crate::{bytes, ensure_abi, raw, status, NativeError};
use std::mem::MaybeUninit;
use std::ops::Range;
use std::ptr::{self, NonNull};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Cluster {
    pub id: u64,
    pub bytes: Range<usize>,
}

/// Owned UTF-8 with Nocturne's grapheme segmentation and stable cluster IDs.
pub struct Text(NonNull<raw::NtText>);

impl Text {
    pub fn new(text: &str) -> Result<Self, NativeError> {
        ensure_abi()?;
        let mut handle = ptr::null_mut();
        status(unsafe { raw::nt_text_create(ptr::null(), bytes(text), &mut handle) })?;
        Ok(Self(
            NonNull::new(handle).expect("successful text creation returned no handle"),
        ))
    }

    pub fn as_str(&self) -> &str {
        let view = unsafe { raw::nt_text_bytes(self.0.as_ptr()) };
        if view.count == 0 {
            return "";
        }
        // The native borrow remains live through this immutable Rust borrow.
        let bytes = unsafe { std::slice::from_raw_parts(view.data.cast(), view.count) };
        std::str::from_utf8(bytes).expect("Nocturne text stores validated UTF-8")
    }

    pub fn cluster_count(&self) -> usize {
        unsafe { raw::nt_text_cluster_count(self.0.as_ptr()) }
    }

    pub fn cluster(&self, index: usize) -> Result<Cluster, NativeError> {
        let mut cluster = MaybeUninit::uninit();
        status(unsafe { raw::nt_text_cluster(self.0.as_ptr(), index, cluster.as_mut_ptr()) })?;
        let cluster = unsafe { cluster.assume_init() };
        Ok(Cluster {
            id: cluster.id,
            bytes: cluster.source.begin..cluster.source.end,
        })
    }

    pub fn replace(&mut self, range: Range<usize>, replacement: &str) -> Result<(), NativeError> {
        let range = raw::NtByteSpan {
            begin: range.start,
            end: range.end,
        };
        status(unsafe { raw::nt_text_replace(self.0.as_ptr(), range, bytes(replacement)) })
    }
}

impl Drop for Text {
    fn drop(&mut self) {
        unsafe { raw::nt_text_destroy(self.0.as_ptr()) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn owned_counted_graphemes() {
        let mut text = Text::new("x\0👩‍💻").unwrap();
        assert_eq!(text.as_str(), "x\0👩‍💻");
        assert_eq!(text.cluster_count(), 3);
        let emoji = text.cluster(2).unwrap();
        assert_eq!(emoji.bytes, 2..13);
        text.replace(0..1, "!").unwrap();
        assert_eq!(text.cluster(2).unwrap(), emoji);
        let error = text.replace(3..4, "?").unwrap_err();
        assert_eq!(error.name(), Some("NT_RESULT_INVALID_ARGUMENT"));
        assert_eq!(text.as_str(), "!\0👩‍💻");
    }
}
