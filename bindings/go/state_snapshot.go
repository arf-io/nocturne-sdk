package nocturne

/*
#include <nocturne/state.h>
*/
import "C"

import (
	"runtime"
	"sync"
	"unsafe"
)

const StateSchemaVersion = C.NT_STATE_SCHEMA_VERSION

// StateSnapshot owns an immutable, encoded property list. Copies share its
// lifetime. It can outlive its source tree and restore onto a different tree.
type StateSnapshot struct{ state *stateSnapshotState }

type stateSnapshotState struct {
	mu     sync.Mutex
	native *C.NtStateSnapshot
	uses   int
	closed bool
}

func ownStateSnapshot(native *C.NtStateSnapshot) *StateSnapshot {
	state := &stateSnapshotState{native: native}
	runtime.SetFinalizer(state, (*stateSnapshotState).close)
	return &StateSnapshot{state: state}
}

// ImportStateSnapshot validates and copies the encoded bytes.
func ImportStateSnapshot(bytes []byte) (*StateSnapshot, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	view := C.NtByteView{data: (*C.char)(unsafe.Pointer(unsafe.SliceData(bytes))), count: C.size_t(len(bytes))}
	var native *C.NtStateSnapshot
	status := C.nt_state_snapshot_import(nil, view, &native)
	runtime.KeepAlive(bytes)
	if err := check(status); err != nil {
		return nil, err
	}
	return ownStateSnapshot(native), nil
}

// CaptureState encodes the listed writable properties in the supplied order.
func (tree *Tree) CaptureState(properties []PropertyRef) (*StateSnapshot, error) {
	state, err := tree.acquire()
	if err != nil {
		return nil, err
	}
	defer state.release()
	var arena RawArena
	defer arena.close()
	refs, err := state.nativePropertyRefs(&arena, properties)
	if err != nil {
		return nil, err
	}
	var native *C.NtStateSnapshot
	status := C.nt_tree_state_capture(state.native, unsafe.SliceData(refs), C.size_t(len(refs)), nil, &native)
	if err = state.finish(status); err != nil {
		C.nt_state_snapshot_destroy(native)
		return nil, err
	}
	return ownStateSnapshot(native), nil
}

func (snapshot *StateSnapshot) borrow() (*C.NtStateSnapshot, func(), error) {
	if snapshot == nil || snapshot.state == nil {
		return nil, nil, StaleReference
	}
	state := snapshot.state
	state.mu.Lock()
	defer state.mu.Unlock()
	if state.closed {
		return nil, nil, StaleReference
	}
	state.uses++
	return state.native, state.release, nil
}

func (state *stateSnapshotState) release() {
	state.mu.Lock()
	defer state.mu.Unlock()
	state.uses--
	state.destroyIfUnused()
}

func (state *stateSnapshotState) destroyIfUnused() {
	if state.closed && state.uses == 0 && state.native != nil {
		C.nt_state_snapshot_destroy(state.native)
		state.native = nil
	}
}

func (state *stateSnapshotState) close() {
	state.mu.Lock()
	defer state.mu.Unlock()
	state.closed = true
	state.destroyIfUnused()
}

// Close invalidates the snapshot and releases it once admitted calls finish.
func (snapshot *StateSnapshot) Close() {
	if snapshot != nil && snapshot.state != nil {
		snapshot.state.close()
	}
}

// Bytes returns an owned copy of the versioned binary encoding.
func (snapshot *StateSnapshot) Bytes() ([]byte, error) {
	native, release, err := snapshot.borrow()
	if err != nil {
		return nil, err
	}
	defer release()
	bytes := C.nt_state_snapshot_bytes(native)
	if uint64(bytes.count) > uint64(^uint(0)>>1) {
		return nil, OutOfRange
	}
	view := unsafe.Slice((*byte)(unsafe.Pointer(bytes.data)), int(bytes.count))
	return append([]byte(nil), view...), nil
}

// RestoreState writes all destination properties or none of them. The
// caller supplies the destinations in snapshot order with matching IDs and types.
func (tree *Tree) RestoreState(properties []PropertyRef, snapshot *StateSnapshot) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	var arena RawArena
	defer arena.close()
	refs, err := state.nativePropertyRefs(&arena, properties)
	if err != nil {
		return err
	}
	native, release, err := snapshot.borrow()
	if err != nil {
		return err
	}
	defer release()
	return state.finish(C.nt_tree_state_restore(state.native, unsafe.SliceData(refs), C.size_t(len(refs)), native))
}
