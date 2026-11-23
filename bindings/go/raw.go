package nocturne

/*
#cgo nocturne_graph CFLAGS: -DNT_BINDING_graph
#cgo nocturne_spatial CFLAGS: -DNT_BINDING_spatial
#cgo nocturne_simulation CFLAGS: -DNT_BINDING_simulation
#cgo nocturne_posix_host CFLAGS: -DNT_BINDING_posix_host
#cgo nocturne_windows_host CFLAGS: -DNT_BINDING_windows_host
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <nocturne/component.h>

uint32_t nt_binding_module_count(void);
const char *nt_binding_schema(uint32_t id);
size_t nt_binding_layout(uint32_t id);
uint64_t nt_binding_constant(uint32_t id);
int nt_binding_call(uint32_t id, uintptr_t result, const uintptr_t *args, uint32_t count);

static void go_raw_write(uintptr_t destination, const void *source, size_t count)
{
    memcpy((void *)destination, source, count);
}

static void go_raw_read(void *destination, uintptr_t source, size_t count)
{
    memcpy(destination, (const void *)source, count);
}

static void go_raw_free(uintptr_t address)
{
    free((void *)address);
}

static void *go_raw_pointer(uintptr_t address)
{
    return (void *)address;
}
*/
import "C"

import (
	"encoding/binary"
	"encoding/json"
	"errors"
	"math"
	"slices"
	"strings"
	"sync"
	"unsafe"
)

// RawAddress is an address in native memory. It never holds a Go pointer.
type RawAddress uintptr

// RawSignature names the C types of a result and its arguments.
type RawSignature struct {
	Result string
	Args   []RawArgument
}

type RawArgument struct {
	Name string
	Type string
}

type rawFunction struct {
	RawSignature
	ID uint32
}

type rawField struct {
	Type         string
	Offset, Size uint32
}

type rawType struct {
	Size   uint32
	Fields map[string]rawField
}

type rawModule struct {
	Module    string
	Types     map[string]rawType
	Functions map[string]rawFunction
	Constants map[string]uint32
	Callbacks map[string]rawFunction
}

// RawAPI describes the headers linked into this installed Go package. It
// exposes the C ownership, pointer, callback, and error contracts directly.
type RawAPI struct {
	modules   []string
	types     map[string]rawType
	functions map[string]rawFunction
	constants map[string]uint32
	callbacks map[string]rawFunction
}

var loadRaw = sync.OnceValues(func() (*RawAPI, error) {
	api := &RawAPI{
		types:     make(map[string]rawType),
		functions: make(map[string]rawFunction),
		constants: make(map[string]uint32),
		callbacks: make(map[string]rawFunction),
	}
	for i := C.uint32_t(0); i < C.nt_binding_module_count(); i++ {
		data := C.nt_binding_schema(i)
		if data == nil {
			continue
		}
		var module rawModule
		if err := json.Unmarshal([]byte(C.GoString(data)), &module); err != nil {
			return nil, err
		}
		api.modules = append(api.modules, module.Module)
		for name, value := range module.Types {
			api.types[name] = value
		}
		for name, value := range module.Functions {
			api.functions[name] = value
		}
		for name, value := range module.Constants {
			api.constants[name] = value
		}
		for name, value := range module.Callbacks {
			api.callbacks[name] = value
		}
	}
	return api, nil
})

// NativeAPI opens the low-level interface for advanced native clients.
func NativeAPI() (*RawAPI, error) {
	if err := ensureABI(); err != nil {
		return nil, err
	}
	return loadRaw()
}

func (api *RawAPI) Modules() []string { return slices.Clone(api.modules) }

func (api *RawAPI) Signature(name string) (RawSignature, bool) {
	value, ok := api.functions[name]
	value.Args = slices.Clone(value.Args)
	return value.RawSignature, ok
}

func (api *RawAPI) Functions() []string {
	values := make([]string, 0, len(api.functions))
	for name := range api.functions {
		values = append(values, name)
	}
	slices.Sort(values)
	return values
}

func (api *RawAPI) Constant(name string) (uint64, bool) {
	id, ok := api.constants[name]
	return uint64(C.nt_binding_constant(C.uint32_t(id))), ok
}

func (api *RawAPI) CallbackSignature(name string) (RawSignature, bool) {
	value, ok := api.callbacks[name]
	value.Args = slices.Clone(value.Args)
	return value.RawSignature, ok
}

func (api *RawAPI) Size(name string) (int, error) {
	if name == "pointer" || strings.HasSuffix(name, "*") {
		return int(unsafe.Sizeof(RawAddress(0))), nil
	}
	value, ok := api.types[name]
	if !ok {
		return 0, UnsupportedCapability
	}
	return layout(value.Size), nil
}

// RawField reports the byte offset and size from the linked C compiler.
type RawField struct {
	Type         string
	Offset, Size int
}

func (api *RawAPI) Field(kind, name string) (RawField, error) {
	value, ok := api.types[kind].Fields[name]
	if !ok {
		return RawField{}, UnsupportedCapability
	}
	return RawField{Type: value.Type, Offset: layout(value.Offset), Size: layout(value.Size)}, nil
}

func layout(id uint32) int { return int(C.nt_binding_layout(C.uint32_t(id))) }

type rawAllocation struct {
	address RawAddress
	size    int
}

// RawArena owns zeroed argument, result, and temporary storage for one call
// scope. Native objects created through C must be destroyed according to
// their headers, separately from this storage.
type RawArena struct {
	api         *RawAPI
	allocations []rawAllocation
	closed      bool
}

func (api *RawAPI) WithArena(fn func(*RawArena) error) error {
	if fn == nil {
		return InvalidArgument
	}
	arena := &RawArena{api: api}
	defer arena.close()
	return fn(arena)
}

func (arena *RawArena) close() {
	arena.closed = true
	for _, allocation := range arena.allocations {
		C.go_raw_free(C.uintptr_t(allocation.address))
	}
	arena.allocations = nil
}

func (arena *RawArena) API() *RawAPI { return arena.api }

func (arena *RawArena) Allocate(bytes int) (RawAddress, error) {
	if arena == nil || arena.closed {
		return 0, StaleReference
	}
	if bytes <= 0 {
		return 0, InvalidArgument
	}
	address := RawAddress(uintptr(C.calloc(1, C.size_t(bytes))))
	if address == 0 {
		return 0, AllocationFailed
	}
	arena.allocations = append(arena.allocations, rawAllocation{address, bytes})
	return address, nil
}

// nativeSlice keeps pointer-bearing argument arrays in C-owned storage.
func nativeSlice[T any](arena *RawArena, count int) ([]T, error) {
	if count == 0 {
		return nil, nil
	}
	var element T
	size := int(unsafe.Sizeof(element))
	if size == 0 || count > int(^uint(0)>>1)/size {
		return nil, OutOfRange
	}
	address, err := arena.Allocate(count * size)
	if err != nil {
		return nil, err
	}
	return unsafe.Slice((*T)(C.go_raw_pointer(C.uintptr_t(address))), count), nil
}

func arenaByteView(arena *RawArena, glyph string) (C.NtByteView, error) {
	bytes, err := nativeSlice[byte](arena, len(glyph))
	if err != nil {
		return C.NtByteView{}, err
	}
	copy(bytes, glyph)
	return C.NtByteView{data: (*C.char)(unsafe.Pointer(unsafe.SliceData(bytes))), count: C.size_t(len(bytes))}, nil
}

func (arena *RawArena) New(kind string) (RawAddress, error) {
	if arena == nil || arena.closed {
		return 0, StaleReference
	}
	size, err := arena.api.Size(kind)
	if err != nil {
		return 0, err
	}
	return arena.Allocate(size)
}

func (arena *RawArena) owns(address RawAddress, size int) bool {
	for _, allocation := range arena.allocations {
		if address >= allocation.address && size <= allocation.size &&
			uintptr(address-allocation.address) <= uintptr(allocation.size-size) {
			return true
		}
	}
	return false
}

// Write copies bytes into storage owned by the arena.
func (arena *RawArena) Write(address RawAddress, data []byte) error {
	if arena == nil || arena.closed {
		return StaleReference
	}
	if !arena.owns(address, len(data)) {
		return OutOfRange
	}
	if len(data) != 0 {
		C.go_raw_write(C.uintptr_t(address), unsafe.Pointer(&data[0]), C.size_t(len(data)))
	}
	return nil
}

// Read copies bytes from a live native address. The caller is responsible for
// the validity and lifetime of borrowed addresses under the C contract.
func (arena *RawArena) Read(address RawAddress, count int) ([]byte, error) {
	if arena == nil || arena.closed {
		return nil, StaleReference
	}
	if count < 0 || (count > 0 && address == 0) {
		return nil, InvalidArgument
	}
	data := make([]byte, count)
	if count != 0 {
		C.go_raw_read(unsafe.Pointer(&data[0]), C.uintptr_t(address), C.size_t(count))
	}
	return data, nil
}

func (arena *RawArena) SetUnsigned(address RawAddress, size int, value uint64) error {
	var data [8]byte
	switch size {
	case 1:
		data[0] = byte(value)
	case 2:
		binary.NativeEndian.PutUint16(data[:], uint16(value))
	case 4:
		binary.NativeEndian.PutUint32(data[:], uint32(value))
	case 8:
		binary.NativeEndian.PutUint64(data[:], value)
	default:
		return InvalidArgument
	}
	return arena.Write(address, data[:size])
}

func (arena *RawArena) Unsigned(address RawAddress, size int) (uint64, error) {
	if size != 1 && size != 2 && size != 4 && size != 8 {
		return 0, InvalidArgument
	}
	data, err := arena.Read(address, size)
	if err != nil {
		return 0, err
	}
	switch size {
	case 1:
		return uint64(data[0]), nil
	case 2:
		return uint64(binary.NativeEndian.Uint16(data)), nil
	case 4:
		return uint64(binary.NativeEndian.Uint32(data)), nil
	default:
		return binary.NativeEndian.Uint64(data), nil
	}
}

func (arena *RawArena) SetFloat64(address RawAddress, value float64) error {
	return arena.SetUnsigned(address, 8, math.Float64bits(value))
}

func (arena *RawArena) Float64(address RawAddress) (float64, error) {
	bits, err := arena.Unsigned(address, 8)
	return math.Float64frombits(bits), err
}

func (arena *RawArena) SetPointer(address, target RawAddress) error {
	return arena.SetUnsigned(address, int(unsafe.Sizeof(target)), uint64(target))
}

func (arena *RawArena) Pointer(address RawAddress) (RawAddress, error) {
	value, err := arena.Unsigned(address, int(unsafe.Sizeof(RawAddress(0))))
	return RawAddress(value), err
}

func (arena *RawArena) ByteView(value string) (RawAddress, error) {
	view, err := arena.New("NtByteView")
	if err != nil || len(value) == 0 {
		return view, err
	}
	text, err := arena.Allocate(len(value))
	if err != nil {
		return 0, err
	}
	if err = arena.Write(text, []byte(value)); err != nil {
		return 0, err
	}
	data, _ := arena.api.Field("NtByteView", "data")
	count, _ := arena.api.Field("NtByteView", "count")
	if err = arena.SetPointer(view+RawAddress(data.Offset), text); err != nil {
		return 0, err
	}
	return view, arena.SetUnsigned(view+RawAddress(count.Offset), count.Size, uint64(len(value)))
}

// Call invokes a C function with the supplied argument storage. To pass a
// pointer, store its address in an argument slot with SetPointer first. The
// result slot is zero for void. Native result values are returned as data.
func (arena *RawArena) Call(name string, arguments ...RawAddress) (RawAddress, error) {
	if arena == nil || arena.closed {
		return 0, StaleReference
	}
	function, ok := arena.api.functions[name]
	if !ok {
		return 0, UnsupportedCapability
	}
	if len(arguments) != len(function.Args) {
		return 0, InvalidArgument
	}
	for i, arg := range function.Args {
		size, err := arena.api.Size(arg.Type)
		if err != nil {
			return 0, err
		}
		if !arena.owns(arguments[i], size) {
			return 0, OutOfRange
		}
	}
	var result RawAddress
	if function.Result != "void" {
		var err error
		result, err = arena.New(function.Result)
		if err != nil {
			return 0, err
		}
	}
	var args *C.uintptr_t
	if len(arguments) != 0 {
		args = (*C.uintptr_t)(unsafe.Pointer(&arguments[0]))
	}
	if C.nt_binding_call(C.uint32_t(function.ID), C.uintptr_t(result), args, C.uint32_t(len(arguments))) == 0 {
		return 0, UnsupportedCapability
	}
	return result, nil
}

// Result calls a function returning NtResult and translates that value.
func (arena *RawArena) Result(name string, arguments ...RawAddress) error {
	if arena == nil || arena.closed {
		return StaleReference
	}
	if signature, ok := arena.api.Signature(name); !ok {
		return UnsupportedCapability
	} else if signature.Result != "NtResult" {
		return InvalidArgument
	}
	value, err := arena.Call(name, arguments...)
	if err != nil {
		return err
	}
	size, err := arena.api.Size("NtResult")
	if err != nil {
		return err
	}
	status, err := arena.Unsigned(value, size)
	if err != nil {
		return err
	}
	return check(C.NtResult(status))
}

// RawTree borrows the native tree for operations that use its C contracts.
// Leave destruction, publication and managed callback registration to Tree.
type RawTree struct {
	*RawArena
	state *treeState
}

func (tree *Tree) WithNative(fn func(*RawTree) error) (err error) {
	if fn == nil {
		return InvalidArgument
	}
	api, err := NativeAPI()
	if err != nil {
		return err
	}
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	defer state.pruneEditorViews()
	defer state.pruneScheduleReceivers()
	defer func() {
		if result, ok := err.(Result); ok {
			err = state.finish(C.NtResult(result))
			return
		}
		callbacks := state.finish(C.NT_RESULT_OK)
		if err == nil {
			err = callbacks
		} else if callbacks != nil {
			err = errors.Join(err, callbacks)
		}
	}()
	return api.WithArena(func(arena *RawArena) error {
		return fn(&RawTree{RawArena: arena, state: state})
	})
}

// Handle returns a pointer argument slot containing the borrowed NtTree.
func (tree *RawTree) Handle() (RawAddress, error) {
	address, err := tree.New("pointer")
	if err != nil {
		return 0, err
	}
	return address, tree.SetPointer(address, RawAddress(uintptr(unsafe.Pointer(tree.state.native))))
}

// Node returns an argument slot containing the native value of a managed node.
func (tree *RawTree) Node(node Node) (RawAddress, error) {
	address, err := tree.New("NtNodeId")
	if err != nil {
		return 0, err
	}
	id, err := tree.state.reference(node)
	if err != nil {
		return 0, err
	}
	C.go_raw_write(C.uintptr_t(address), unsafe.Pointer(&id), C.size_t(unsafe.Sizeof(id)))
	return address, nil
}

// Adopt converts a live native node mounted through this tree into a Node.
func (tree *RawTree) Adopt(address RawAddress) (Node, error) {
	if tree.closed {
		return Node{}, StaleReference
	}
	var id C.NtNodeId
	if !tree.owns(address, int(unsafe.Sizeof(id))) {
		return Node{}, OutOfRange
	}
	C.go_raw_read(unsafe.Pointer(&id), C.uintptr_t(address), C.size_t(unsafe.Sizeof(id)))
	if id.tree != tree.state.native {
		return Node{}, InvalidArgument
	}
	var geometry C.NtNodeGeometry
	if err := check(C.nt_tree_geometry(tree.state.native, id, &geometry)); err != nil {
		return Node{}, err
	}
	return tree.state.node(id), nil
}
