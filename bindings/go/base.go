// Package nocturne hosts native Nocturne components from Go.
package nocturne

/*
#include <nocturne/abi.h>
#include <nocturne/text.h>
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// Result preserves a native Nocturne status.
type Result int32

const (
	OK                    Result = C.NT_RESULT_OK
	InvalidArgument       Result = C.NT_RESULT_INVALID_ARGUMENT
	OutOfRange            Result = C.NT_RESULT_OUT_OF_RANGE
	AllocationFailed      Result = C.NT_RESULT_ALLOCATION_FAILED
	InsufficientBuffer    Result = C.NT_RESULT_INSUFFICIENT_BUFFER
	StaleReference        Result = C.NT_RESULT_STALE_REFERENCE
	WrongPhase            Result = C.NT_RESULT_WRONG_PHASE
	UnsupportedCapability Result = C.NT_RESULT_UNSUPPORTED_CAPABILITY
	TransportFailed       Result = C.NT_RESULT_TRANSPORT_FAILED
	CallbackFailed        Result = C.NT_RESULT_CALLBACK_FAILED
)

func (result Result) Error() string {
	name := C.nt_result_name(C.int32_t(result))
	if name == nil {
		return fmt.Sprintf("unknown Nocturne result (%d)", result)
	}
	return C.GoString(name)
}

func check(value C.NtResult) error {
	if value == C.NT_RESULT_OK {
		return nil
	}
	return Result(value)
}

// ABIVersion reports the version of the linked native library.
func ABIVersion() uint32 { return uint32(C.nt_abi_version()) }

func ensureABI() error {
	if ABIVersion() != C.NT_ABI_VERSION {
		return UnsupportedCapability
	}
	return nil
}

// UnicodeVersion reports the Unicode data version used by the native library.
func UnicodeVersion() string { return C.GoString(C.nt_unicode_version()) }

func nativeBytes(value string) C.NtByteView {
	return C.NtByteView{data: (*C.char)(unsafe.Pointer(unsafe.StringData(value))), count: C.size_t(len(value))}
}

func copyBytes(view C.NtByteView) (string, error) {
	if view.count == 0 {
		return "", nil
	}
	if uint64(view.count) > uint64(^uint(0)>>1) {
		return "", OutOfRange
	}
	return string(unsafe.Slice((*byte)(unsafe.Pointer(view.data)), int(view.count))), nil
}
