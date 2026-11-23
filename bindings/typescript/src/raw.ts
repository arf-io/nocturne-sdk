export interface RawExports {
  readonly memory: WebAssembly.Memory;
  nocturne_wasm_allocate(count: number): number;
  nocturne_wasm_release(address: number): void;
  nt_binding_module_count(): number;
  nt_binding_schema(module: number): number;
  nt_binding_layout(id: number): number;
  nt_binding_constant(id: number): bigint;
  nt_binding_callback_address(id: number): number;
  nt_binding_call(id: number, result: number, args: number, count: number): number;
}

export interface RawArgument {
  readonly name: string;
  readonly type: string;
}

export interface RawSignature {
  readonly result: string;
  readonly args: readonly RawArgument[];
}

interface RawFunction extends RawSignature {
  readonly id: number;
}

export interface RawField {
  readonly type: string;
  readonly offset: number;
  readonly size: number;
}

interface RawType {
  readonly size: number;
  readonly fields: Readonly<Record<string, RawField>>;
}

interface RawModule {
  readonly module: string;
  readonly types: Readonly<Record<string, RawType>>;
  readonly functions: Readonly<Record<string, RawFunction>>;
  readonly constants: Readonly<Record<string, number>>;
  readonly callbacks: Readonly<Record<string, RawFunction>>;
}

type NativeFailure = (code: number) => Error;
const decoder = new TextDecoder();
const encoder = new TextEncoder();
const pointerSize = 4;

/** The public C API and compiler layouts linked into this WASM instance. */
export class RawAPI {
  readonly modules: readonly string[];
  readonly functions: readonly string[];
  readonly #types = new Map<string, RawType>();
  readonly #functions = new Map<string, RawFunction>();
  readonly #constants = new Map<string, number>();
  readonly #callbacks = new Map<string, RawFunction>();
  readonly #contexts = new Map<number, RawCallbacks>();
  #lastContext = 0;

  constructor(private readonly native: RawExports, private readonly failure: NativeFailure) {
    const modules: string[] = [];
    for (let index = 0; index < native.nt_binding_module_count(); index++) {
      const address = native.nt_binding_schema(index) >>> 0;
      if (address === 0) continue;
      const tail = new Uint8Array(native.memory.buffer, address);
      const end = tail.indexOf(0);
      if (end < 0) throw failure(7);
      const module: RawModule = JSON.parse(decoder.decode(tail.subarray(0, end)));
      modules.push(module.module);
      for (const [key, value] of Object.entries(module.types)) this.#types.set(key, value);
      for (const [key, value] of Object.entries(module.functions)) this.#functions.set(key, value);
      for (const [key, value] of Object.entries(module.constants)) this.#constants.set(key, value);
      for (const [key, value] of Object.entries(module.callbacks)) this.#callbacks.set(key, value);
    }
    this.modules = Object.freeze(modules);
    this.functions = Object.freeze([...this.#functions.keys()].sort());
  }

  signature(name: string): RawSignature | undefined {
    const value = this.#functions.get(name);
    return value && { result: value.result, args: value.args.map(arg => ({ ...arg })) };
  }

  callbackSignature(name: string): RawSignature | undefined {
    const value = this.#callbacks.get(name);
    return value && { result: value.result, args: value.args.map(arg => ({ ...arg })) };
  }

  callbacks(handlers: Readonly<Record<string, RawCallback>>): RawCallbacks {
    const entries = new Map<number, CallbackEntry>();
    const pointers = new Map<string, number>();
    for (const [name, handler] of Object.entries(handlers)) {
      const signature = this.#callbacks.get(name);
      if (!signature) throw this.failure(7);
      if (typeof handler !== "function") throw this.failure(1);
      entries.set(signature.id, { signature, handler });
      pointers.set(name, this.native.nt_binding_callback_address(signature.id) >>> 0);
    }
    if (!entries.size) throw this.failure(1);
    if (this.#lastContext === 0xffffffff) throw this.failure(2);
    const token = ++this.#lastContext;
    const callbacks = new RawCallbacks(token, entries, pointers, this.failure, () => this.#contexts.delete(token));
    this.#contexts.set(token, callbacks);
    return callbacks;
  }

  /** @internal */
  invokeCallback(token: number, id: number, result: number, args: number, count: number): void {
    const callbacks = this.#contexts.get(token >>> 0);
    const entry = callbacks?.entry(id);
    if (!entry || !callbacks || count !== entry.signature.args.length) return;
    const view = new DataView(this.native.memory.buffer);
    const slots = Array.from({ length: count }, (_, i) => view.getUint32((args >>> 0) + i * pointerSize, true));
    this.withArena(arena => {
      const frame = new RawCallbackFrame(arena, this.native, this.failure, entry.signature, slots, result >>> 0);
      try {
        const outcome: unknown = entry.handler(frame);
        if (outcome !== undefined) {
          void Promise.resolve(outcome).catch(() => {});
          throw new TypeError("Native callbacks must return synchronously without a value");
        }
        if (entry.signature.result === "NtResult" && !frame.returned) frame.status(0);
      } catch (cause) {
        callbacks.save(cause);
        if (entry.signature.result === "NtResult") frame.status(9);
      } finally {
        frame.expire();
      }
    });
  }

  constant(name: string): bigint | undefined {
    const id = this.#constants.get(name);
    return id === undefined ? undefined : BigInt.asUintN(64, this.native.nt_binding_constant(id));
  }

  size(type: string): number {
    if (type === "pointer" || type.endsWith("*")) return pointerSize;
    const layout = this.#types.get(type);
    if (!layout) throw this.failure(7);
    return this.native.nt_binding_layout(layout.size) >>> 0;
  }

  field(type: string, name: string): RawField {
    const layout = this.#types.get(type)?.fields;
    if (!layout || !Object.hasOwn(layout, name)) throw this.failure(7);
    const field = layout[name];
    return { type: field.type, offset: this.native.nt_binding_layout(field.offset) >>> 0,
      size: this.native.nt_binding_layout(field.size) >>> 0 };
  }

  withArena<T>(call: (arena: RawArena) => T): T {
    const arena = new RawArena(this, this.native, this.failure, name => this.#functions.get(name));
    try {
      return call(arena);
    } finally {
      arena.close();
    }
  }
}

/** Temporary C storage. Native objects still follow the ownership rules in their headers. */
export class RawArena {
  readonly #allocations = new Map<number, number>();
  #closed = false;

  constructor(readonly api: RawAPI, private readonly native: RawExports,
    private readonly failure: NativeFailure, private readonly lookup: (name: string) => RawFunction | undefined) {}

  close(): void {
    for (const address of this.#allocations.keys()) this.native.nocturne_wasm_release(address);
    this.#allocations.clear();
    this.#closed = true;
  }

  #live(): void {
    if (this.#closed) throw this.failure(5);
  }

  allocate(count: number): number {
    this.#live();
    if (!Number.isInteger(count) || count <= 0 || count > 0xffffffff) throw this.failure(1);
    const address = this.native.nocturne_wasm_allocate(count) >>> 0;
    if (address === 0) throw this.failure(3);
    this.#allocations.set(address, count);
    new Uint8Array(this.native.memory.buffer, address, count).fill(0);
    return address;
  }

  value(type: string): number {
    this.#live();
    return this.allocate(this.api.size(type));
  }

  owns(address: number, count: number): boolean {
    if (!Number.isInteger(address) || !Number.isInteger(count) || count < 0) return false;
    for (const [begin, size] of this.#allocations) {
      if (address >= begin && address - begin <= size - count) return true;
    }
    return false;
  }

  write(address: number, data: Uint8Array): void {
    this.#live();
    if (!this.owns(address, data.length)) throw this.failure(2);
    new Uint8Array(this.native.memory.buffer, address, data.length).set(data);
  }

  /** Copy data from a live native address, including C-owned borrowed values. */
  read(address: number, count: number): Uint8Array {
    this.#live();
    if (!Number.isInteger(address) || !Number.isInteger(count) || count < 0
        || address < 0 || (!address && count)) throw this.failure(1);
    if (address + count > this.native.memory.buffer.byteLength) throw this.failure(2);
    return new Uint8Array(this.native.memory.buffer, address, count).slice();
  }

  setUnsigned(address: number, size: number, value: bigint): void {
    this.#live();
    if (!this.owns(address, size)) throw this.failure(2);
    const view = new DataView(this.native.memory.buffer);
    switch (size) {
      case 1: view.setUint8(address, Number(BigInt.asUintN(8, value))); break;
      case 2: view.setUint16(address, Number(BigInt.asUintN(16, value)), true); break;
      case 4: view.setUint32(address, Number(BigInt.asUintN(32, value)), true); break;
      case 8: view.setBigUint64(address, value, true); break;
      default: throw this.failure(1);
    }
  }

  unsigned(address: number, size: number): bigint {
    const data = this.read(address, size);
    const view = new DataView(data.buffer);
    switch (size) {
      case 1: return BigInt(view.getUint8(0));
      case 2: return BigInt(view.getUint16(0, true));
      case 4: return BigInt(view.getUint32(0, true));
      case 8: return view.getBigUint64(0, true);
      default: throw this.failure(1);
    }
  }

  setFloat64(address: number, value: number): void {
    this.#live();
    if (!this.owns(address, 8)) throw this.failure(2);
    new DataView(this.native.memory.buffer).setFloat64(address, value, true);
  }

  float64(address: number): number {
    return new DataView(this.read(address, 8).buffer).getFloat64(0, true);
  }

  setPointer(address: number, target: number): void {
    if (!Number.isInteger(target) || target < 0 || target > 0xffffffff) throw this.failure(1);
    this.setUnsigned(address, pointerSize, BigInt(target));
  }

  pointer(address: number): number {
    return Number(this.unsigned(address, pointerSize));
  }

  byteView(value: string | Uint8Array): number {
    if (typeof value === "string" && /[\uD800-\uDFFF]/u.test(value)) throw this.failure(1);
    const bytes = typeof value === "string" ? encoder.encode(value) : value;
    const view = this.value("NtByteView");
    if (bytes.length === 0) return view;
    const text = this.allocate(bytes.length);
    this.write(text, bytes);
    const data = this.api.field("NtByteView", "data"), count = this.api.field("NtByteView", "count");
    this.setPointer(view + data.offset, text);
    this.setUnsigned(view + count.offset, count.size, BigInt(bytes.length));
    return view;
  }

  /** Invoke a C function. Every argument is a slot containing a C value. */
  call(name: string, ...args: number[]): number {
    this.#live();
    const signature = this.lookup(name);
    if (!signature) throw this.failure(7);
    if (args.length !== signature.args.length) throw this.failure(1);
    for (const [index, arg] of signature.args.entries()) {
      if (!this.owns(args[index], this.api.size(arg.type))) throw this.failure(2);
    }
    const result = signature.result === "void" ? 0 : this.value(signature.result);
    const address = args.length === 0 ? 0 : this.native.nocturne_wasm_allocate(args.length * pointerSize) >>> 0;
    if (args.length !== 0 && address === 0) throw this.failure(3);
    try {
      const slots = new DataView(this.native.memory.buffer);
      for (const [index, arg] of args.entries()) slots.setUint32(address + index * pointerSize, arg, true);
      if (!this.native.nt_binding_call(signature.id, result, address, args.length)) throw this.failure(7);
      return result;
    } finally {
      if (address !== 0) this.native.nocturne_wasm_release(address);
    }
  }

  /** Invoke a C function returning NtResult and throw on a nonzero status. */
  result(name: string, ...args: number[]): void {
    this.#live();
    const signature = this.lookup(name);
    if (!signature) throw this.failure(7);
    if (signature.result !== "NtResult") throw this.failure(1);
    const result = Number(this.unsigned(this.call(name, ...args), this.api.size("NtResult")));
    if (result !== 0) throw this.failure(result | 0);
  }
}

export type RawCallback = (frame: RawCallbackFrame) => undefined;

interface CallbackEntry {
  readonly signature: RawFunction;
  readonly handler: RawCallback;
}

/** Owns callbacks that share one native context token. */
export class RawCallbacks {
  #closed = false;
  #errors: unknown[] = [];

  constructor(private readonly token: number, private readonly entries: ReadonlyMap<number, CallbackEntry>,
    private readonly pointers: ReadonlyMap<string, number>, private readonly failure: NativeFailure,
    private readonly release: () => void) {}

  get context(): number { return this.#closed ? 0 : this.token; }

  function(name: string): number {
    if (this.#closed) throw this.failure(5);
    const address = this.pointers.get(name);
    if (!address) throw this.failure(7);
    return address;
  }

  takeErrors(): unknown[] {
    const errors = this.#errors;
    this.#errors = [];
    return errors;
  }

  close(): void {
    if (!this.#closed) this.release();
    this.#closed = true;
  }

  /** @internal */
  entry(id: number): CallbackEntry | undefined { return this.#closed ? undefined : this.entries.get(id); }

  /** @internal */
  save(cause: unknown): void { this.#errors.push(cause); }
}

/** Borrowed C argument and result storage for an active callback. */
export class RawCallbackFrame {
  #live = true;
  #returned = false;

  constructor(readonly arena: RawArena, private readonly native: RawExports,
    private readonly failure: NativeFailure, private readonly signature: RawSignature,
    private readonly slots: readonly number[], private readonly result: number) {}

  #check(): void { if (!this.#live) throw this.failure(5); }

  /** Copy one by-value argument into the callback arena. */
  argument(index: number): number {
    this.#check();
    const argument = this.signature.args[index];
    if (!argument) throw this.failure(2);
    const out = this.arena.value(argument.type);
    this.arena.write(out, this.arena.read(this.slots[index], this.arena.api.size(argument.type)));
    return out;
  }

  /** Copy storage into a live mutable pointer argument using its C contract. */
  writeOutput(index: number, value: number, size: number): void {
    this.#check();
    const argument = this.signature.args[index];
    if (!argument || !this.arena.owns(value, size)) throw this.failure(2);
    if (!argument.type.endsWith("*") || argument.type.startsWith("const ")) throw this.failure(1);
    const out = this.arena.pointer(this.slots[index]);
    if (!out || out + size > this.native.memory.buffer.byteLength) throw this.failure(2);
    new Uint8Array(this.native.memory.buffer, out, size).set(this.arena.read(value, size));
  }

  returnValue(value: number): undefined {
    this.#check();
    if (!this.result) throw this.failure(1);
    const size = this.arena.api.size(this.signature.result);
    if (!this.arena.owns(value, size)) throw this.failure(2);
    new Uint8Array(this.native.memory.buffer, this.result, size).set(this.arena.read(value, size));
    this.#returned = true;
    return undefined;
  }

  /** @internal */
  get returned(): boolean { return this.#returned; }

  /** @internal */
  status(code: number): void {
    this.#check();
    const size = this.arena.api.size("NtResult");
    const out = new DataView(this.native.memory.buffer);
    if (size === 4) out.setUint32(this.result, code, true);
    else if (size === 2) out.setUint16(this.result, code, true);
    else out.setUint8(this.result, code);
  }

  /** @internal */
  expire(): void { this.#live = false; }
}
