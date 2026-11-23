package nocturne

/*
#include <nocturne/property.h>

typedef struct GoPropertyValue {
    NtValueKind kind;
    bool flag;
    int64_t signed_value;
    uint64_t unsigned_value;
    double real;
    NtByteView text;
    NtRgba color;
} GoPropertyValue;

static inline NtResult go_property_unpack(const NtValue *value, GoPropertyValue *out)
{
    out->kind = value->kind;
    switch (value->kind) {
    case NT_VALUE_BOOL: out->flag = value->as.flag; break;
    case NT_VALUE_I64: out->signed_value = value->as.i64; break;
    case NT_VALUE_U64: out->unsigned_value = value->as.u64; break;
    case NT_VALUE_REAL: out->real = value->as.real; break;
    case NT_VALUE_TEXT: out->text = value->as.text; break;
    case NT_VALUE_RGBA: out->color = value->as.rgba; break;
    default: return NT_RESULT_UNSUPPORTED_CAPABILITY;
    }
    return NT_RESULT_OK;
}

static inline NtResult go_property_pack(GoPropertyValue input, NtValue *out)
{
    *out = (NtValue){.kind = input.kind};
    switch (input.kind) {
    case NT_VALUE_BOOL: out->as.flag = input.flag; break;
    case NT_VALUE_I64: out->as.i64 = input.signed_value; break;
    case NT_VALUE_U64: out->as.u64 = input.unsigned_value; break;
    case NT_VALUE_REAL: out->as.real = input.real; break;
    case NT_VALUE_TEXT: out->as.text = input.text; break;
    case NT_VALUE_RGBA: out->as.rgba = input.color; break;
    default: return NT_RESULT_INVALID_ARGUMENT;
    }
    return NT_RESULT_OK;
}
*/
import "C"

import "unsafe"

type PropertyID uint32

const (
	TextProperty       PropertyID = C.NT_PROPERTY_TEXT
	ValueProperty      PropertyID = C.NT_PROPERTY_VALUE
	CheckedProperty    PropertyID = C.NT_PROPERTY_CHECKED
	SelectedProperty   PropertyID = C.NT_PROPERTY_SELECTED
	OffsetProperty     PropertyID = C.NT_PROPERTY_OFFSET
	ForegroundProperty PropertyID = C.NT_PROPERTY_FOREGROUND
	BackgroundProperty PropertyID = C.NT_PROPERTY_BACKGROUND
)

type PropertyKind uint32

const (
	BoolValue     PropertyKind = C.NT_VALUE_BOOL
	SignedValue   PropertyKind = C.NT_VALUE_I64
	UnsignedValue PropertyKind = C.NT_VALUE_U64
	RealValue     PropertyKind = C.NT_VALUE_REAL
	TextValue     PropertyKind = C.NT_VALUE_TEXT
	ColorValue    PropertyKind = C.NT_VALUE_RGBA
)

// PropertyValue holds the payload selected by Kind. Text reads own a
// copy of the property bytes.
type PropertyValue struct {
	Kind     PropertyKind
	Bool     bool
	Signed   int64
	Unsigned uint64
	Real     float64
	Text     string
	Color    Color
}

// PropertyRef identifies a property on a node in the destination tree.
type PropertyRef struct {
	Node Node
	ID   PropertyID
}

func (ref PropertyRef) native(state *treeState) (C.NtPropertyRef, error) {
	id, err := state.reference(ref.Node)
	return C.NtPropertyRef{node: id, id: C.NtPropertyId(ref.ID)}, err
}

func (state *treeState) nativePropertyRefs(arena *RawArena, properties []PropertyRef) ([]C.NtPropertyRef, error) {
	refs, err := nativeSlice[C.NtPropertyRef](arena, len(properties))
	if err != nil {
		return nil, err
	}
	for index, property := range properties {
		refs[index], err = property.native(state)
		if err != nil {
			return nil, err
		}
	}
	return refs, nil
}

func propertyFromNative(native *C.NtValue) (PropertyValue, error) {
	var data C.GoPropertyValue
	if err := check(C.go_property_unpack(native, &data)); err != nil {
		return PropertyValue{}, err
	}
	text, err := copyBytes(data.text)
	if err != nil {
		return PropertyValue{}, err
	}
	return PropertyValue{Kind: PropertyKind(data.kind), Bool: bool(data.flag),
		Signed: int64(data.signed_value), Unsigned: uint64(data.unsigned_value),
		Real: float64(data.real), Color: colorFromNative(data.color), Text: text}, nil
}

func propertyValuesFromNative(values *C.NtValue, count C.size_t) ([]PropertyValue, error) {
	inputs := make([]PropertyValue, int(count))
	for index, value := range unsafe.Slice(values, int(count)) {
		input, err := propertyFromNative(&value)
		if err != nil {
			return nil, err
		}
		inputs[index] = input
	}
	return inputs, nil
}

func (value PropertyValue) writeNative(arena *RawArena, out *C.NtValue) error {
	data := C.GoPropertyValue{kind: C.NtValueKind(value.Kind), flag: C.bool(value.Bool),
		signed_value: C.int64_t(value.Signed), unsigned_value: C.uint64_t(value.Unsigned),
		real: C.double(value.Real), color: value.Color.native()}
	if value.Kind == TextValue {
		bytes, err := arenaByteView(arena, value.Text)
		if err != nil {
			return err
		}
		data.text = bytes
	}
	return check(C.go_property_pack(data, out))
}

func (tree *Tree) Property(node Node, property PropertyID) (PropertyValue, error) {
	var value PropertyValue
	var copyError error
	err := tree.callNode(node, func(native *C.NtTree, id C.NtNodeId) C.NtResult {
		var out C.NtValue
		status := C.nt_tree_property_get(native, C.NtPropertyRef{node: id, id: C.NtPropertyId(property)}, &out)
		if status == C.NT_RESULT_OK {
			value, copyError = propertyFromNative(&out)
		}
		return status
	})
	if err != nil {
		return value, err
	}
	return value, copyError
}

func (tree *Tree) SetProperty(node Node, property PropertyID, value PropertyValue) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	ref, err := (PropertyRef{Node: node, ID: property}).native(state)
	if err != nil {
		return err
	}
	arena := RawArena{}
	defer arena.close()
	var input C.NtValue
	if err = value.writeNative(&arena, &input); err != nil {
		return err
	}
	return state.finish(C.nt_tree_property_set(state.native, ref, input))
}

type PropertyAssignment struct {
	Property PropertyRef
	Value    PropertyValue
}

func nativeAssignments(state *treeState, arena *RawArena, assignments []PropertyAssignment) ([]C.NtPropertyAssignment, error) {
	native, err := nativeSlice[C.NtPropertyAssignment](arena, len(assignments))
	if err != nil {
		return nil, err
	}
	for i, assignment := range assignments {
		native[i].property, err = assignment.Property.native(state)
		if err != nil {
			return nil, err
		}
		if err = assignment.Value.writeNative(arena, &native[i].value); err != nil {
			return nil, err
		}
	}
	return native, nil
}

// ApplyProperties prepares every assignment before committing any of them.
func (tree *Tree) ApplyProperties(assignments []PropertyAssignment) error {
	state, err := tree.acquire()
	if err != nil {
		return err
	}
	defer state.release()
	arena := RawArena{}
	defer arena.close()
	native, err := nativeAssignments(state, &arena, assignments)
	if err != nil {
		return err
	}
	return state.finish(C.nt_tree_properties_apply(state.native, unsafe.SliceData(native), C.size_t(len(native))))
}
