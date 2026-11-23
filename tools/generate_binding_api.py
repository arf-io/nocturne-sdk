#!/usr/bin/env python3
"""Generate the C ABI bridge used by the Go and WebAssembly packages."""

import argparse
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
HEADERS = ROOT / "include" / "nocturne"
OPTIONAL = {
    "graph": ("graph.h",),
    "spatial": ("spatial.h", "spatial_mesh.h", "spatial_surface.h"),
    "simulation": ("simulation.h",),
    "posix_host": ("posix_host.h",),
    "windows_host": ("windows_host.h",),
}
PRIMITIVES = {
    "_Bool", "char", "int", "unsigned int", "float", "double", "size_t", "ptrdiff_t",
    "uintptr_t", *(f"{sign}int{bits}_t" for sign in ("", "u") for bits in (8, 16, 32, 64)),
}
MACROS = {"NT_ABI_VERSION", "NT_CHOICE_NONE", "NT_BIDI_REMOVED_LEVEL"}
OUTPUTS = ("bindings/go/raw_api.inc", "bindings/typescript/native/raw_api.inc")


def declarations(clang, headers):
    source = "".join(f"#include <nocturne/{name}>\n" for name in headers)
    command = [clang, "-I", str(HEADERS.parent), "-std=c99", "-x", "c", "-",
               "-Xclang", "-ast-dump=json", "-fsyntax-only"]
    return json.loads(subprocess.check_output(command, input=source.encode()))["inner"]


def normalize(name):
    if "(unnamed " in name:
        return "aggregate"
    return name


def fields(record, prefix=""):
    anonymous = None
    for member in record.get("inner", []):
        if member["kind"] == "RecordDecl":
            anonymous = member
        if member["kind"] != "FieldDecl":
            continue
        name = prefix + member["name"]
        kind = normalize(member["type"]["qualType"])
        yield name, kind
        if kind == "aggregate":
            if anonymous is None:
                raise ValueError(f"Missing declaration for {name}")
            yield from fields(anonymous, name + ".")
        anonymous = None


def public_declarations(decls):
    functions, types, constants, callbacks = {}, {}, set(), {}
    records = {item["id"]: item for item in decls if item["kind"] == "RecordDecl"}
    named_records = {f"{record['tagUsed']} {record['name']}": record
                     for record in records.values()
                     if record.get("name") and record.get("completeDefinition")}
    for item in decls:
        name = item.get("name", "")
        kind = item["kind"]
        if kind == "FunctionDecl" and name.startswith("nt_"):
            if item.get("variadic"):
                raise ValueError(f"Unsupported variadic declaration {name}")
            signature = item["type"]["qualType"]
            result = signature[:signature.index("(")].strip()
            args = [{"name": arg["name"], "type": arg["type"]["qualType"]}
                    for arg in item.get("inner", []) if arg["kind"] == "ParmVarDecl"]
            functions[name] = {"result": result, "args": args}
        if kind == "EnumDecl":
            constants.update(val["name"] for val in item.get("inner", [])
                             if val["kind"] == "EnumConstantDecl" and val["name"].startswith("NT_"))
        if kind != "TypedefDecl" or not name.startswith("Nt"):
            continue
        alias = item["type"]["qualType"]
        if "(*)" in alias:
            callbacks[name] = alias
        record = named_records.get(alias)
        if record is None:
            for part in item.get("inner", []):
                declaration = part.get("ownedTagDecl", part.get("decl", {}))
                if declaration.get("id") in records:
                    record = records[declaration["id"]]
                    break
        if record and record.get("completeDefinition"):
            types[name] = dict(fields(record))
            for field, value in types[name].items():
                if "(*)" in value:
                    callbacks[f"{name}.{field}"] = value
        elif not alias.startswith(("struct ", "union ")):
            types[name] = {}
    return functions, types, constants, callbacks


def callback_signature(name, declaration):
    result, args = declaration.split("(*)", maxsplit=1)
    args = args[1:-1].split(", ")
    if not args or args[0] not in ("void *", "const void *"):
        raise ValueError(f"{name} does not take an opaque callback context")
    return result.strip(), args


class Generator:
    def __init__(self):
        self.sections = []
        self.known_functions = set()
        self.known_types = set()
        self.known_constants = set()
        self.known_callbacks = set()

    def add_module(self, name, headers, declarations):
        functions, types, constants, callbacks = public_declarations(declarations)
        if not self.sections:
            types.update({kind: {} for kind in PRIMITIVES})
            constants.update(MACROS)
        functions = {k: functions[k] for k in sorted(functions.keys() - self.known_functions)}
        types = {k: types[k] for k in sorted(types.keys() - self.known_types)}
        constants = sorted(constants - self.known_constants)
        callbacks = {k: callbacks[k] for k in sorted(callbacks.keys() - self.known_callbacks)}
        self.known_functions.update(functions)
        self.known_types.update(types)
        self.known_constants.update(constants)
        self.known_callbacks.update(callbacks)
        self.sections.append((name, headers, functions, types, constants, callbacks))

    def output(self):
        modules = []
        cases_size, cases_call, cases_constant, cases_callback = [], [], [], []
        lines = [
            "/* Generated by tools/generate_binding_api.py. */",
            "#include <stddef.h>", "#include <stdint.h>", "#include <string.h>",
            "#if defined(__wasm__)",
            "#define NT_BINDING_EXPORT(name) __attribute__((export_name(#name)))",
            '__attribute__((import_module("nocturne"), import_name("raw_callback")))',
            "extern void nt_binding_host_callback(uintptr_t context, uint32_t id, uintptr_t result,",
            "                                     const uintptr_t *args, uint32_t count);",
            "#else", "#define NT_BINDING_EXPORT(name)",
            "extern void nt_binding_host_callback(uintptr_t context, uint32_t id, uintptr_t result,",
            "                                     uintptr_t *args, uint32_t count);",
            "#endif", "",
        ]
        sizes = {}

        def size_id(expression, new_cases):
            if expression not in sizes:
                sizes[expression] = len(sizes) + 1
                new_cases.append(f"    case {sizes[expression]}: return {expression};")
            return sizes[expression]

        function_id, constant_id, callback_id = 0, 0, 0
        for module_index, (name, headers, functions, types, constants, callbacks) in enumerate(self.sections):
            guard = f"defined(NT_BINDING_{name})" if name != "nocturne" else None
            includes = [f"#include <nocturne/{header}>" for header in headers]
            lines.extend(guarded(guard, includes))
            types_json, constants_json, size_cases, call_cases, constant_cases = {}, {}, [], [], []
            for kind, members in types.items():
                types_json[kind] = {
                    "size": size_id(f"sizeof({kind})", size_cases),
                    "fields": {
                        field: {"type": field_type,
                                "offset": size_id(f"offsetof({kind}, {field})", size_cases),
                                "size": size_id(f"sizeof((({kind} *)0)->{field})", size_cases)}
                        for field, field_type in members.items()
                    },
                }
            for function, signature in functions.items():
                function_id += 1
                signature["id"] = function_id
                args = signature["args"]
                result = signature["result"]
                call_cases.extend([f"    case {function_id}: {{",
                                   f"        if (count != {len(args)}) return 0;"])
                for i, arg in enumerate(args):
                    call_cases.append(f"        {arg['type']} a{i};")
                    call_cases.append(f"        memcpy(&a{i}, (const void *)args[{i}], sizeof(a{i}));")
                call = f"{function}({', '.join(f'a{i}' for i in range(len(args)))})"
                if result == "void":
                    call_cases.append(f"        {call};")
                else:
                    call_cases.extend([f"        {result} value = {call};",
                                       "        memcpy((void *)result, &value, sizeof(value));"])
                call_cases.extend(["        return 1;", "    }"])
            for constant in constants:
                constant_id += 1
                constants_json[constant] = constant_id
                constant_cases.append(f"    case {constant_id}: return (uint64_t){constant};")
            callback_json, callback_defs, callback_cases = {}, [], []
            for callback, declaration in callbacks.items():
                callback_id += 1
                result, args = callback_signature(callback, declaration)
                function = f"nt_binding_proxy_{callback_id}"
                callback_json[callback] = {
                    "id": callback_id, "result": result,
                    "args": [{"name": f"arg{index}", "type": arg}
                             for index, arg in enumerate(args[1:], 1)],
                }
                signature = ", ".join(f"{arg} a{index}" for index, arg in enumerate(args))
                callback_defs.extend(["", f"static {result} {function}({signature})", "{"])
                if len(args) > 1:
                    slots = ", ".join(f"(uintptr_t)&a{i}" for i in range(1, len(args)))
                    callback_defs.append(f"    uintptr_t args[] = {{{slots}}};")
                output = "0"
                if result != "void":
                    default = "NT_RESULT_CALLBACK_FAILED" if result == "NtResult" else "0"
                    callback_defs.append(f"    {result} value = {default};")
                    output = "(uintptr_t)&value"
                argument = "args" if len(args) > 1 else "NULL"
                callback_defs.append(
                    f"    nt_binding_host_callback((uintptr_t)a0, {callback_id}, "
                    f"{output}, {argument}, {len(args) - 1});")
                if result != "void":
                    callback_defs.append("    return value;")
                callback_defs.append("}")
                callback_cases.append(f"    case {callback_id}: return (uintptr_t){function};")
            lines.extend(guarded(guard, callback_defs))
            schema = {"module": name, "types": types_json, "functions": functions,
                      "constants": constants_json, "callbacks": callback_json}
            data = json.dumps(schema, separators=(",", ":"))
            body = [f"static const char schema_{name}[] ="]
            body.extend("    " + json.dumps(data[i:i + 160]) for i in range(0, len(data), 160))
            body[-1] += ";"
            lines.extend(guarded(guard, body))
            modules.extend(guarded(guard, [f"    case {module_index}: return schema_{name};"]))
            cases_size.extend(guarded(guard, size_cases))
            cases_call.extend(guarded(guard, call_cases))
            cases_constant.extend(guarded(guard, constant_cases))
            cases_callback.extend(guarded(guard, callback_cases))
        for signature, cases, missing in (
            ("const char *nt_binding_schema(uint32_t id)", modules, "NULL"),
            ("size_t nt_binding_layout(uint32_t id)", cases_size, "0"),
            ("uint64_t nt_binding_constant(uint32_t id)", cases_constant, "0"),
            ("uintptr_t nt_binding_callback_address(uint32_t id)", cases_callback, "0"),
            ("int nt_binding_call(uint32_t id, uintptr_t result, const uintptr_t *args, uint32_t count)",
             cases_call, "0"),
        ):
            name = re.search(r"nt_binding_\w+", signature)[0]
            lines.extend(["", f"NT_BINDING_EXPORT({name})", signature, "{", "    switch (id) {",
                          *cases, f"    default: return {missing};", "    }", "}"])
        lines.insert(1, f"#define NT_BINDING_MODULE_COUNT {len(self.sections)}")
        lines += ["", "NT_BINDING_EXPORT(nt_binding_module_count)", "uint32_t nt_binding_module_count(void)",
                  "{", "    return NT_BINDING_MODULE_COUNT;", "}", ""]
        return "\n".join(lines)


def guarded(guard, lines):
    return [f"#if {guard}", *lines, "#endif"] if guard and lines else lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    excluded = {filename for group in OPTIONAL.values() for filename in group}
    core = tuple(sorted(header.name for header in HEADERS.glob("*.h") if header.name not in excluded))
    generator = Generator()
    generator.add_module("nocturne", core, declarations(args.clang, core))
    for module, headers in OPTIONAL.items():
        generator.add_module(module, headers, declarations(args.clang, headers))
    generated = generator.output()
    for filename in OUTPUTS:
        path = ROOT / filename
        if args.check:
            if not path.exists() or path.read_text() != generated:
                parser.error(f"Regenerate {filename} with {Path(__file__).name}")
        else:
            path.write_text(generated)


if __name__ == "__main__":
    main()
