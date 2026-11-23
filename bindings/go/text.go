package nocturne

/*
#include <nocturne/text.h>
*/
import "C"

import (
	"runtime"
	"sync"
)

// Text owns counted native UTF-8 and serializes operations on it.
// Copies refer to the same text. Close releases it for every copy.
type Text struct{ state *textState }

type textState struct {
	mu     sync.Mutex
	native *C.NtText
}

// Cluster describes a grapheme and its half-open byte range in the text.
type Cluster struct {
	ID         uint64
	Begin, End uint
}

// NewText copies a counted UTF-8 string, including any embedded zero bytes.
func NewText(value string) (*Text, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	var native *C.NtText
	status := C.nt_text_create(nil, nativeBytes(value), &native)
	runtime.KeepAlive(value)
	if err := check(status); err != nil {
		return nil, err
	}
	state := &textState{native: native}
	runtime.SetFinalizer(state, (*textState).close)
	return &Text{state: state}, nil
}

func (text *Text) borrow() (*textState, error) {
	if text == nil || text.state == nil {
		return nil, StaleReference
	}
	state := text.state
	state.mu.Lock()
	if state.native == nil {
		state.mu.Unlock()
		return nil, StaleReference
	}
	return state, nil
}

// Value returns a copy of the UTF-8 bytes.
func (text *Text) Value() (string, error) {
	state, err := text.borrow()
	if err != nil {
		return "", err
	}
	defer state.mu.Unlock()
	return copyBytes(C.nt_text_bytes(state.native))
}

// Len reports the count of extended grapheme clusters.
func (text *Text) Len() (uint, error) {
	state, err := text.borrow()
	if err != nil {
		return 0, err
	}
	defer state.mu.Unlock()
	return uint(C.nt_text_cluster_count(state.native)), nil
}

// Cluster reports the stable identity and current byte range of a grapheme.
func (text *Text) Cluster(index uint) (Cluster, error) {
	state, err := text.borrow()
	if err != nil {
		return Cluster{}, err
	}
	defer state.mu.Unlock()
	var cluster C.NtTextCluster
	if err := check(C.nt_text_cluster(state.native, C.size_t(index), &cluster)); err != nil {
		return Cluster{}, err
	}
	return Cluster{ID: uint64(cluster.id), Begin: uint(cluster.source.begin), End: uint(cluster.source.end)}, nil
}

// Replace modifies a range on scalar boundaries. Failure preserves the text.
func (text *Text) Replace(begin, end uint, replacement string) error {
	state, err := text.borrow()
	if err != nil {
		return err
	}
	defer state.mu.Unlock()
	span := C.NtByteSpan{begin: C.size_t(begin), end: C.size_t(end)}
	result := C.nt_text_replace(state.native, span, nativeBytes(replacement))
	runtime.KeepAlive(replacement)
	return check(result)
}

func (state *textState) close() {
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.native != nil {
		C.nt_text_destroy(state.native)
		state.native = nil
	}
}

// Close releases the text. Repeated calls are safe.
func (text *Text) Close() {
	if text != nil && text.state != nil {
		text.state.close()
	}
}
