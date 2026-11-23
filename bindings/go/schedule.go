package nocturne

/*
#include <nocturne/schedule.h>
#include "bridge.h"

extern void nocturneGoIntentReceive(uintptr_t handle, NtNodeId owner, NtEventIntentView *intent);

static void go_intent_receive(void *context, NtNodeId owner, const NtEventIntentView *intent)
{
    const void *previous = nt_go_swap_context(NULL);
    nocturneGoIntentReceive((uintptr_t)context, owner, (NtEventIntentView *)intent);
    nt_go_swap_context(previous);
}

static NtResult go_schedule_subscribe(NtTree *tree, NtNodeId owner, uint32_t kind,
                                      uintptr_t handle, NtScheduleId *out)
{
    return nt_tree_schedule_subscribe(tree, owner, kind, go_intent_receive, (void *)handle, out);
}
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

type ScheduleID uint64

type ScheduleLimits struct {
	Batches, Subscriptions uint
}

type ScheduleRun struct {
	Attempted uint
	Failed    ScheduleID
}

func (tree *Tree) ConfigureSchedule(limits ScheduleLimits) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	return check(C.nt_tree_schedule_configure(state.native,
		C.NtScheduleLimits{batches: C.size_t(limits.Batches), subscriptions: C.size_t(limits.Subscriptions)}))
}

// ScheduleBatch queues an owned copy. Removing the owner or any target
// cancels the batch.
func (tree *Tree) ScheduleBatch(owner Node, deadline uint64, assignments []PropertyAssignment) (ScheduleID, error) {
	state, err := tree.acquire()
	if err != nil {
		return 0, err
	}
	defer state.release()
	id, err := state.reference(owner)
	if err != nil {
		return 0, err
	}
	arena := RawArena{}
	defer arena.close()
	native, err := nativeAssignments(state, &arena, assignments)
	if err != nil {
		return 0, err
	}
	var queued C.NtScheduleId
	err = check(C.nt_tree_schedule_batch(state.native, id, C.NtTimeUs(deadline),
		unsafe.SliceData(native), C.size_t(len(native)), &queued))
	return ScheduleID(queued), err
}

func (tree *Tree) CancelSchedule(id ScheduleID) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	status := C.nt_tree_schedule_cancel(state.native, C.NtScheduleId(id))
	if status == C.NT_RESULT_OK || status == C.NT_RESULT_STALE_REFERENCE {
		state.dropScheduleReceiver(id)
	}
	return check(status)
}

// NextScheduled returns the earliest queued deadline, if any.
func (tree *Tree) NextScheduled() (at uint64, scheduled bool, err error) {
	state, err := tree.acquire()
	if err != nil {
		return 0, false, err
	}
	defer state.release()
	next := C.nt_tree_schedule_next(state.native)
	return uint64(next.at), bool(next.scheduled), nil
}

// RunSchedule attempts at most budget due batches. A failed batch is consumed
// and returned in the report alongside the native error.
func (tree *Tree) RunSchedule(now uint64, budget uint) (ScheduleRun, error) {
	state, err := tree.acquire()
	if err != nil {
		return ScheduleRun{}, err
	}
	defer state.release()
	var report C.NtScheduleRun
	status := C.nt_tree_schedule_run(state.native, C.NtTimeUs(now), C.size_t(budget), &report)
	return ScheduleRun{Attempted: uint(report.attempted), Failed: ScheduleID(report.failed)}, state.finish(status)
}

// IntentReceiver receives an owned intent copy. A returned error is reported
// by DeliverIntents after native delivery completes.
type IntentReceiver func(owner Node, intent EventIntent) error

type receiverCallback struct {
	tree    *treeState
	receive IntentReceiver
}

type scheduledReceiver struct {
	owner  C.NtNodeId
	handle cgo.Handle
}

//export nocturneGoIntentReceive
func nocturneGoIntentReceive(handle C.uintptr_t, owner C.NtNodeId, native *C.NtEventIntentView) {
	callback := cgo.Handle(handle).Value().(*receiverCallback)
	state := callback.tree
	defer func() {
		if cause := recover(); cause != nil {
			state.recordCallbackFailure("intentReceive", &PanicError{Value: cause})
		}
	}()
	intent, err := state.eventIntent(native)
	if err == nil {
		err = callback.receive(state.node(owner), intent)
	}
	if err != nil {
		state.recordCallbackFailure("intentReceive", err)
	}
}

// SubscribeIntent installs one receiver for an exact kind. Removing its
// owner, canceling the ID, or closing the tree releases the receiver.
func (tree *Tree) SubscribeIntent(owner Node, kind uint32, receive IntentReceiver) (ScheduleID, error) {
	state, err := tree.acquire()
	if err != nil {
		return 0, err
	}
	defer state.release()
	if receive == nil {
		return 0, InvalidArgument
	}
	node, err := state.reference(owner)
	if err != nil {
		return 0, err
	}
	handle := cgo.NewHandle(&receiverCallback{tree: state, receive: receive})
	var registered C.NtScheduleId
	result := C.go_schedule_subscribe(state.native, node, C.uint32_t(kind), C.uintptr_t(handle), &registered)
	if err = check(result); err != nil {
		handle.Delete()
		return 0, err
	}
	id := ScheduleID(registered)
	if state.receivers == nil {
		state.receivers = make(map[ScheduleID]scheduledReceiver)
	}
	state.receivers[id] = scheduledReceiver{owner: node, handle: handle}
	return id, nil
}

func (state *treeState) dropScheduleReceiver(id ScheduleID) {
	if receiver, live := state.receivers[id]; live {
		receiver.handle.Delete()
		delete(state.receivers, id)
	}
}

func (state *treeState) pruneScheduleReceivers() {
	for id, receiver := range state.receivers {
		var geometry C.NtNodeGeometry
		if state.native == nil || C.nt_tree_geometry(state.native, receiver.owner, &geometry) == C.NT_RESULT_STALE_REFERENCE {
			state.dropScheduleReceiver(id)
		}
	}
}

// DeliverIntents consumes up to budget queued intents. Receiver failures
// accompany the consumed count and leave later receivers and items eligible.
func (tree *Tree) DeliverIntents(budget uint) (uint, error) {
	state, err := tree.acquire()
	if err != nil {
		return 0, err
	}
	defer state.release()
	var delivered C.size_t
	status := C.nt_tree_schedule_deliver(state.native, C.size_t(budget), &delivered)
	return uint(delivered), state.finish(status)
}
