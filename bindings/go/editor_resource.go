package nocturne

import "sync"

// The native handle remains live while a view retains it. An active tree
// borrows it without holding the mutex across native callbacks.
type editorResource[T comparable] struct {
	mu      sync.Mutex
	native  T
	destroy func(T)
	closed  bool
	views   int
	owner   *treeState
}

func (resource *editorResource[T]) lockOpen() error {
	resource.mu.Lock()
	if resource.closed {
		resource.mu.Unlock()
		return StaleReference
	}
	if resource.owner != nil {
		resource.mu.Unlock()
		return WrongPhase
	}
	return nil
}

func (resource *editorResource[T]) retainView(owner *treeState) error {
	resource.mu.Lock()
	defer resource.mu.Unlock()
	if resource.closed {
		return StaleReference
	}
	if resource.owner != nil && resource.owner != owner {
		return WrongPhase
	}
	resource.owner = owner
	resource.views++
	return nil
}

func (resource *editorResource[T]) borrowTree(owner *treeState) bool {
	resource.mu.Lock()
	defer resource.mu.Unlock()
	if resource.owner != nil && resource.owner != owner {
		return false
	}
	resource.owner = owner
	return true
}

func (resource *editorResource[T]) releaseTree(owner *treeState) {
	resource.mu.Lock()
	defer resource.mu.Unlock()
	if resource.owner == owner {
		resource.owner = nil
	}
}

func (resource *editorResource[T]) dropView(lastForTree bool) {
	resource.mu.Lock()
	defer resource.mu.Unlock()
	resource.views--
	if lastForTree {
		resource.owner = nil
	}
	resource.destroyClosedHandle()
}

func (resource *editorResource[T]) close() {
	resource.mu.Lock()
	defer resource.mu.Unlock()
	resource.closed = true
	resource.destroyClosedHandle()
}

func (resource *editorResource[T]) destroyClosedHandle() {
	var zero T
	if resource.closed && resource.views == 0 && resource.native != zero {
		resource.destroy(resource.native)
		resource.native = zero
	}
}
