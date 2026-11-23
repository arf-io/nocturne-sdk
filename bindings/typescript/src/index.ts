import { RawAPI, type RawArena, type RawCallbackFrame, type RawCallbacks, type RawExports } from "./raw.js";
import { spatialGeometry, type SpatialGeometry } from "./spatial.js";
import { spatialMeshBindings, type SpatialMeshes, type SpatialRaster, type SpatialSampling } from "./spatial_mesh.js";
import { flowSimulation, type FlowSimulation } from "./simulation_flow.js";
import { surfaceTransportSimulation, type SurfaceTransportSimulation } from "./simulation_transport.js";
import { bodySimulation, type BodySimulation } from "./simulation_body.js";

export { RawAPI, type RawArena, type RawArgument, type RawCallback, type RawCallbackFrame,
  type RawCallbacks, type RawField, type RawSignature } from "./raw.js";
export type { SpatialCamera, SpatialClippedSegment, SpatialClipVertex, SpatialGeometry, SpatialHit, SpatialPath,
  SpatialPathOptions, SpatialPathSample, SpatialPickTriangle, SpatialProjected, SpatialProjection, SpatialScreen,
  SpatialTransform, SpatialTransformOptions, SpatialTriangle, SpatialContact, SpatialSurfaceMotion, SpatialSurfaceSample,
  SpatialSweepTriangle } from "./spatial.js";
export type { Flow, FlowInput, FlowPort, FlowSample, FlowStage, SprayBatch, SprayOptions, SpraySample } from "./simulation_flow.js";
export type { TransportBudget, TransportCell, TransportCellOptions, TransportEdge, TransportOptions,
  TransportStep } from "./simulation_transport.js";
export type { AngularPair, AngularStore, BodyLoad, Buoyancy, DragResult, FloatingBody, Hull, HullGeometry,
  HullImmersion, HullTriangle, LinearPair, LinearStore, WaterPlane, WaterTriangle } from "./simulation_body.js";
export type { SpatialCellHit, SpatialFace, SpatialMesh, SpatialMeshData, SpatialMeshInstance, SpatialMeshes,
  SpatialRaster, SpatialRasterOptions, SpatialRasterSample, SpatialSampling, SpatialTexture, SpatialVertex } from "./spatial_mesh.js";

type NativeWasm = WebAssembly.Exports & RawExports & {
  memory: WebAssembly.Memory;
  _initialize(): void;
  nocturne_wasm_abi_version(): number;
  nocturne_wasm_result_name(code: number): number;
  nocturne_wasm_allocate(count: number): number;
  nocturne_wasm_release(address: number): void;
  nocturne_wasm_tween_sample?: (from: number, to: number, duration: bigint, delay: bigint,
    easing: number, repeat: number, elapsed: bigint, out: number) => number;
  nocturne_wasm_text_create(data: number, count: number, out: number): number;
  nocturne_wasm_text_destroy(handle: number): void;
  nocturne_wasm_text_value(handle: number, out: number): void;
  nocturne_wasm_text_count(handle: number): number;
  nocturne_wasm_text_cluster(handle: number, index: number, out: number): number;
  nocturne_wasm_text_replace(handle: number, begin: number, end: number, data: number, count: number): number;
  nocturne_wasm_tree_create(out: number): number;
  nocturne_wasm_tree_destroy(tree: number): void;
  nocturne_wasm_tree_mount_box(tree: number, slot: number, generation: number, flags: number,
    kind: number, gap: number, out: number): number;
  nocturne_wasm_tree_mount_component(tree: number, slot: number, generation: number,
    flags: number, handle: number, out: number): number;
  nocturne_wasm_tree_mount_panel_content?: (tree: number, slot: number, generation: number,
    flags: number, handle: number, columns: number, rows: number, hasUpdate: number, hasEvent: number,
    out: number) => number;
  nocturne_wasm_tree_mount_label(tree: number, slot: number, generation: number, flags: number,
    data: number, count: number, foreground: number, background: number, out: number): number;
  nocturne_wasm_tree_set_label(tree: number, slot: number, generation: number, data: number, count: number,
    foreground: number, background: number): number;
  nocturne_wasm_tree_mount_graph?: (tree: number, slot: number, generation: number, flags: number,
    graph: number, out: number) => number;
  nocturne_wasm_tree_set_graph?: (tree: number, slot: number, generation: number, graph: number) => number;
  nocturne_wasm_vector_cross?: (ax: number, ay: number, az: number, bx: number, by: number, bz: number,
    out: number) => number;
  nocturne_wasm_vector_normalize?: (x: number, y: number, z: number, out: number) => number;
  nocturne_wasm_water_sample?: (waves: number, count: number, x: number, z: number, seconds: number,
    out: number) => number;
  nocturne_wasm_tree_remove(tree: number, slot: number, generation: number): number;
  nocturne_wasm_tree_publish(tree: number, columns: number, rows: number): number;
  nocturne_wasm_tree_has_frame(tree: number): number;
  nocturne_wasm_tree_frame_size(tree: number, out: number): void;
  nocturne_wasm_tree_frame_cell(tree: number, x: number, y: number, out: number): number;
  nocturne_wasm_tree_tick(tree: number, now: bigint, motion: number): number;
  nocturne_wasm_tree_next_update(tree: number, scheduled: number): bigint;
  nocturne_wasm_tree_set_focus(tree: number, slot: number, generation: number): number;
  nocturne_wasm_measure_count(context: number): number;
  nocturne_wasm_measure_child(context: number, index: number, minColumns: number, maxColumns: number,
    columnsBounded: number, minRows: number, maxRows: number, rowsBounded: number, out: number): number;
  nocturne_wasm_layout_count(context: number): number;
  nocturne_wasm_layout_size(context: number, index: number, out: number): number;
  nocturne_wasm_layout_place(context: number, index: number, x: number, y: number,
    columns: number, rows: number, depth: number): number;
  nocturne_wasm_update_reply(out: number, invalidate: number, scheduled: number, next: bigint): void;
  nocturne_wasm_paint_glyph(context: number, x: number, y: number, data: number, count: number,
    columns: number, foreground: number, background: number): number;
  nocturne_wasm_paint_fill(context: number, x: number, y: number, columns: number, rows: number, fill: number): number;
  nocturne_wasm_tree_dispatch(tree: number, input: number, text: number, count: number, reserved: number,
    reservedCount: number, traverse: number, delivery: number): number;
  nocturne_wasm_event_key(input: number, out: number): void;
  nocturne_wasm_event_text(input: number, out: number): void;
  nocturne_wasm_event_local(context: number, out: number): number;
  nocturne_wasm_event_emit(context: number, kind: number, bytes: number, count: number): number;
  nocturne_wasm_event_consume(reply: number): void;
  nocturne_wasm_event_invalidate(reply: number, flags: number): void;
  nocturne_wasm_event_capture(reply: number, capture: number): void;
  nocturne_wasm_tree_focus(tree: number, out: number): void;
  nocturne_wasm_tree_capture(tree: number, pointer: bigint, out: number): void;
  nocturne_wasm_tree_intent(tree: number, out: number): number;
  nocturne_wasm_tree_pop_intent(tree: number): void;
};

const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true });

export class NativeError extends Error {
  constructor(public readonly code: number, public readonly nativeName: string | null) {
    super(`${nativeName ?? "unknown Nocturne result"} (${code})`);
    this.name = "NativeError";
  }
}

export type Utf8 = string | Uint8Array;

export const Easing = Object.freeze({
  linear: 0, easeIn: 1, easeOut: 2, easeInOut: 3, smooth: 4,
  smoother: 5, sine: 6, stepStart: 7, stepEnd: 8,
} as const);
export const Repeat = Object.freeze({ once: 0, loop: 1, pingPong: 2 } as const);
export const AnimationPhase = Object.freeze({ delayed: 0, running: 1, finished: 2 } as const);

export interface Tween {
  readonly from: number;
  readonly to: number;
  readonly durationUs: bigint;
  readonly delayUs?: bigint;
  readonly easing?: typeof Easing[keyof typeof Easing];
  readonly repeat?: typeof Repeat[keyof typeof Repeat];
}

export interface AnimationSample {
  readonly value: number;
  readonly progress: number;
  readonly iteration: bigint;
  readonly phase: typeof AnimationPhase[keyof typeof AnimationPhase];
  readonly direction: 1 | -1;
}

export interface Keyframe {
  readonly atUs: bigint;
  readonly value: number;
  /** Easing for the segment starting at this key. */
  readonly easing?: typeof Easing[keyof typeof Easing];
}

export interface KeyframeTrack {
  readonly keys: readonly Keyframe[];
  readonly delayUs?: bigint;
  readonly repeat?: typeof Repeat[keyof typeof Repeat];
}

export const PlaybackState = Object.freeze({ playing: 0, paused: 1, cancelled: 2 } as const);

/** A snapshot that maps the caller's host time to local microseconds. */
export interface Playback {
  readonly state: typeof PlaybackState[keyof typeof PlaybackState];
  elapsed(nowUs: bigint): bigint;
  /** Transitions return a new snapshot. */
  pause(nowUs: bigint): Playback;
  resume(nowUs: bigint): Playback;
  seek(nowUs: bigint, elapsedUs: bigint): Playback;
  cancel(nowUs: bigint): Playback;
}

export const RetargetPolicy = Object.freeze({ keepVelocity: 0, resetVelocity: 1, settle: 2 } as const);

export interface SpringTuning {
  readonly responseUs: bigint;
  readonly positionTolerance: number;
  readonly velocityTolerance: number;
}

export interface SpringOptions {
  readonly tuning: SpringTuning;
  readonly value: number;
  readonly velocity?: number;
  readonly target: number;
  readonly startUs: bigint;
}

export interface SpringSample {
  readonly value: number;
  readonly velocity: number;
  readonly phase: AnimationSample["phase"];
}

/** A critically damped scalar segment. Retargeting returns a new segment. */
export interface Spring {
  sample(nowUs: bigint, reducedMotion?: boolean): SpringSample;
  retarget(nowUs: bigint, target: number, policy: typeof RetargetPolicy[keyof typeof RetargetPolicy]): Spring;
}

export interface Path {
  readonly points: readonly Point[];
  readonly closed?: boolean;
  readonly boundary?: typeof Repeat[keyof typeof Repeat];
}

export interface PathSample {
  readonly position: Point;
  readonly tangent: Point;
  readonly distance: number;
  readonly length: number;
}

/** Scale and rotate about the anchor, then translate by the offset. */
export interface PanelPose {
  readonly anchor: Point;
  readonly offset: Point;
  readonly scale: Point;
  readonly turns: number;
  readonly opacity: number;
}

export interface PanelTransition {
  readonly from: PanelPose;
  readonly to: PanelPose;
  readonly delayUs?: bigint;
  readonly durationUs: bigint;
  readonly easing?: typeof Easing[keyof typeof Easing];
}

export type SurfaceMask =
  | { readonly kind: "rectangle" | "ellipse" | "diamond"; readonly bounds: Bounds }
  | { readonly kind: "roundedRectangle"; readonly bounds: Bounds; readonly radius: number }
  | { readonly kind: "alpha"; readonly bounds: Bounds; readonly alpha: Uint8Array; readonly stride: number };

export interface PanelSample {
  readonly destination: Point;
  readonly source: Point;
  readonly viewportSize: Size;
  readonly regionSize: Size;
  readonly elapsedUs: bigint;
}

export interface PanelPixel {
  readonly source: Point;
  readonly opacity: number;
}

export interface PanelLayer {
  readonly source: Frame;
  readonly region: Bounds;
  readonly viewport: Bounds;
  readonly pose: PanelPose;
  readonly elapsedUs?: bigint;
  readonly mask?: SurfaceMask;
  readonly remap?: (sample: PanelSample) => PanelPixel;
}

export type SurfaceOwners = "clear" | "shared" | { readonly map: (owner: bigint) => bigint };

export interface SurfaceLayer {
  readonly source: Frame;
  readonly region: Bounds;
  readonly position: Point;
  readonly opacity?: number;
  readonly mask?: SurfaceMask;
  readonly owners?: SurfaceOwners;
}

export interface SurfacePanelLayer extends PanelLayer {
  readonly owners?: SurfaceOwners;
}

export interface TimelineClip {
  readonly channel: bigint;
  readonly spanUs: bigint;
  readonly delayUs?: bigint;
  readonly tween: Tween;
}

export interface TimelineOptions {
  readonly delayUs?: bigint;
  readonly repeat?: typeof Repeat[keyof typeof Repeat];
  readonly reverse?: boolean;
  readonly layout?: "sequence" | "parallel";
}

/** A nonnegative rational rate. Zero numerator and denominator select unit rate. */
export interface TrackRate {
  readonly numerator: number;
  readonly denominator: number;
}

export interface TrackEdit {
  readonly kind: "dwell" | "skip";
  readonly atUs: bigint;
  readonly lengthUs: bigint;
}

export interface TrackTime {
  readonly rate?: TrackRate;
  readonly edits?: readonly TrackEdit[];
  readonly quantumUs?: bigint;
}

export interface RandomTrack {
  readonly from: number;
  readonly to: number;
  readonly intervalUs: bigint;
  readonly seed?: bigint;
  readonly stream?: bigint;
  readonly smooth?: boolean;
}

export interface TrackSchedule {
  readonly staggerUs?: bigint;
  readonly jitterUs?: bigint;
  readonly durationVariationUs?: bigint;
  readonly batchSize?: bigint;
  readonly seed?: bigint;
}

export interface TrackSlot {
  readonly delayUs: bigint;
  readonly spanUs: bigint;
}

export type TimelineSource =
  | { readonly kind: "tween"; readonly tween: Tween }
  | { readonly kind: "keyframes"; readonly keyframes: KeyframeTrack }
  | { readonly kind: "random"; readonly random: RandomTrack };

export type TimelineNode =
  | { readonly kind: "clip"; readonly delayUs?: bigint; readonly channel: bigint; readonly spanUs: bigint;
      readonly timing?: TrackTime; readonly source: TimelineSource }
  | { readonly kind: "sequence" | "parallel"; readonly delayUs?: bigint; readonly children: readonly TimelineNode[] };

export interface TimelineSample {
  readonly value: number;
  readonly playheadUs: bigint;
  readonly sourceUs: bigint;
  readonly iteration: bigint;
  readonly phase: typeof AnimationPhase[keyof typeof AnimationPhase];
  readonly clipPhase: typeof AnimationPhase[keyof typeof AnimationPhase];
  readonly direction: 1 | -1;
}

export interface Timeline {
  readonly durationUs: bigint;
  readonly channelCount: number;
  channelAt(index: number): bigint;
  sample(channel: bigint, elapsedUs: bigint): TimelineSample;
  close(): void;
}

export interface Cluster {
  readonly id: bigint;
  readonly begin: number;
  readonly end: number;
}

export interface Text {
  readonly value: string;
  readonly count: number;
  cluster(index: number): Cluster;
  replace(begin: number, end: number, value: Utf8): void;
  close(): void;
}

export interface LineBreak {
  readonly offset: number;
  readonly mandatory: boolean;
}

export interface ScriptRun {
  readonly begin: number;
  readonly end: number;
  /** Four-letter ISO 15924 script code. */
  readonly script: string;
}

export type Direction = "auto" | "ltr" | "rtl";

/** The logical level of a scalar omitted from visual order by rule X9. */
export const BidiRemovedLevel = 255;

export interface BidiScalar {
  readonly begin: number;
  readonly end: number;
  readonly scalar: number;
  readonly level: number;
  readonly paragraphLevel: number;
  readonly paragraphSeparator: boolean;
}

export interface BidiVisual {
  readonly logicalIndex: number;
  readonly begin: number;
  readonly end: number;
  readonly displayScalar: number;
  readonly level: number;
}

/** An owned analysis with UTF-8 byte offsets into the original input. */
export interface Bidi {
  readonly count: number;
  scalar(index: number): BidiScalar;
  /** Reorder a half-open byte range within one paragraph on scalar boundaries. */
  reorderLine(begin: number, end: number): readonly BidiVisual[];
  close(): void;
}

export interface WidthPolicy {
  /** Omission or zero selects one cell. */
  readonly ambiguous?: 0 | 1 | 2;
  /** Omission or zero selects two cells. */
  readonly emoji?: 0 | 1 | 2;
}

export const GlyphPresentation = Object.freeze({
  text: 0, emoji: 1, isolatedMark: 2, replacement: 3, invisible: 4, tab: 5, lineBreak: 6,
} as const);

export interface GlyphMetrics {
  readonly presentation: typeof GlyphPresentation[keyof typeof GlyphPresentation];
  readonly columns: number;
}

export type WrapMode = "none" | "word" | "cluster";
export type TextAlign = "start" | "center" | "end";

export interface TextLayoutOptions {
  readonly width?: WidthPolicy;
  readonly bounds?: Constraints;
  readonly wrap?: WrapMode;
  readonly align?: TextAlign;
  readonly baseDirection?: Direction;
  /** Tab stop in columns. Omission or zero selects four columns. */
  readonly tabStop?: number;
  readonly ellipsis?: boolean;
}

export interface LayoutCluster extends Cluster {
  /** Display glyph, which may differ from the source bytes. */
  readonly glyph: string;
  readonly position: Point;
  readonly columns: number;
  readonly bidiLevel: number;
}

/** Immutable snapshot of source, cluster IDs, and cell geometry. */
export interface TextLayout {
  readonly source: string;
  readonly size: Size;
  readonly count: number;
  cluster(index: number): LayoutCluster;
  /** Map a visual grid edge to its canonical logical caret. */
  hit(edge: Point): Caret;
  caretPosition(caret: Caret): Point;
  close(): void;
}

export interface EditorOptions {
  readonly singleLine?: boolean;
  readonly readOnly?: boolean;
}

/** A grapheme boundary expressed as a UTF-8 byte offset. */
export interface Caret {
  readonly offset: number;
  readonly affinity: "leading" | "trailing";
}

export interface Selection {
  readonly anchor: Caret;
  readonly active: Caret;
}

export type EditorMove = "previousCluster" | "nextCluster" | "lineStart" | "lineEnd" | "documentStart" | "documentEnd";

export const EditorAction = Object.freeze({
  move: 0, deletePrevious: 1, deleteNext: 2, newline: 3, undo: 4, redo: 5,
  historyOlder: 6, historyNewer: 7, registerCopy: 8, registerPaste: 9,
} as const);

export interface EditorBinding {
  readonly chord: KeyChord;
  readonly action: typeof EditorAction[keyof typeof EditorAction];
  readonly move?: EditorMove;
  readonly slot?: number;
  readonly extend?: boolean;
  readonly repeat?: boolean;
}

export interface EditorKeyMap {
  readonly bindings: readonly EditorBinding[];
  readonly registers?: EditorRegisters;
}

export interface EditorRegisterValue {
  readonly text: string;
  readonly occupied: boolean;
}

export interface EditorRegisters {
  read(slot: number): EditorRegisterValue;
  write(slot: number, value: Utf8): void;
  close(): void;
}

export interface EditorUndoState {
  readonly undoCount: number;
  readonly redoCount: number;
}

export interface Editor {
  readonly value: string;
  readonly selection: Selection;
  readonly selectedText: string;
  readonly undoState: EditorUndoState;
  readonly historyCount: number;
  setSelection(selection: Selection): void;
  move(direction: EditorMove, extend?: boolean): void;
  selectAt(layout: TextLayout, cell: Point, extend?: boolean): void;
  caretPosition(layout: TextLayout): Point;
  replaceSelection(value: Utf8): void;
  delete(backward: boolean): void;
  setUndoLimit(count: number): void;
  undo(): boolean;
  redo(): boolean;
  setHistoryLimit(count: number): void;
  addHistory(value: Utf8): void;
  historyEntry(index: number): string;
  stepHistory(older: boolean): boolean;
  handleKey(key: KeyInput, keymap?: EditorKeyMap): boolean;
  close(): void;
}

export interface EditorViewOptions {
  readonly editor: Editor;
  readonly keys?: EditorKeyMap;
  readonly style?: ControlStyle;
  readonly wrap?: WrapMode;
  readonly followCaret?: boolean;
  readonly showCaret?: boolean;
  readonly submitOnEnter?: boolean;
  readonly scrollKeys?: boolean;
}

export interface Size {
  readonly columns: number;
  readonly rows: number;
}

export interface Point {
  readonly x: number;
  readonly y: number;
}

export type Color = readonly [number, number, number, number];

const terminalColors = { mono: 0, indexed: 1, truecolor: 2 } as const;
export type TerminalColorMode = keyof typeof terminalColors;

export interface TerminalCapabilities {
  readonly color?: TerminalColorMode;
  readonly interactive?: boolean;
  readonly utf8?: boolean;
  readonly halfBlocks?: boolean;
  readonly braille?: boolean;
  readonly pointer?: boolean;
  readonly bracketedPaste?: boolean;
  readonly width?: WidthPolicy;
}

export interface AnsiOptions {
  readonly capabilities: TerminalCapabilities;
  readonly matte?: Color;
  readonly palette?: readonly Color[];
}


export interface Style {
  readonly foreground: Color;
  readonly background: Color;
}

export const ColorSpace = Object.freeze({
  srgb: 0, linearSrgb: 1, hsl: 2, hsv: 3, oklab: 4, oklch: 5, cmyk: 6,
} as const);
export type ColorSpace = typeof ColorSpace[keyof typeof ColorSpace];
export const GamutPolicy = Object.freeze({ clip: 0, reduceChroma: 1, reject: 2 } as const);
export type GamutPolicy = typeof GamutPolicy[keyof typeof GamutPolicy];
export const HueInterpolation = Object.freeze({ shorter: 0, longer: 1, increasing: 2, decreasing: 3 } as const);
export type HueInterpolation = typeof HueInterpolation[keyof typeof HueInterpolation];
export const AlphaInterpolation = Object.freeze({ premultiplied: 0, straight: 1 } as const);
export type AlphaInterpolation = typeof AlphaInterpolation[keyof typeof AlphaInterpolation];

/** Unquantized color in a named space. Only CMYK uses the fourth channel. */
export interface PaintColor {
  readonly space: ColorSpace;
  readonly channels: readonly [number, number, number, number?];
  readonly alpha: number;
}

export interface ColorInterpolation {
  readonly space?: ColorSpace;
  readonly gamut?: GamutPolicy;
  readonly hue?: HueInterpolation;
  readonly alpha?: AlphaInterpolation;
}

export const PaintAddress = Object.freeze({ pad: 0, repeat: 1, reflect: 2 } as const);
export type PaintAddress = typeof PaintAddress[keyof typeof PaintAddress];

export interface ContentCell {
  readonly position: Point;
  readonly columns: 0 | 1 | 2;
  readonly glyph?: Utf8;
  readonly style: Style;
  readonly owner?: bigint;
}

export const ImageFilter = Object.freeze({ nearest: 0, bilinear: 1 } as const);
export type ImageFilter = typeof ImageFilter[keyof typeof ImageFilter];

export interface ImageSampling {
  readonly filter?: ImageFilter;
  readonly horizontal?: PaintAddress;
  readonly vertical?: PaintAddress;
}

export interface ImageView {
  readonly pixels: readonly Color[];
  readonly size: Size;
  /** Row stride measured in pixels. */
  readonly stride: number;
}

export interface ImageRasterOptions extends ImageSampling {
  readonly sampling?: SamplingMode;
  readonly owner?: bigint;
}

export interface GradientStop {
  readonly position: number;
  readonly color: PaintColor;
  readonly easing?: Keyframe["easing"];
}

export interface Gradient {
  readonly stops: readonly GradientStop[];
  readonly address?: PaintAddress;
  readonly interpolation?: ColorInterpolation;
}

export const BlendMode = Object.freeze({
  normal: 0, multiply: 1, screen: 2, overlay: 3, darken: 4,
  lighten: 5, colorDodge: 6, colorBurn: 7, hardLight: 8, softLight: 9,
  difference: 10, exclusion: 11, hue: 12, saturation: 13, color: 14,
  luminosity: 15, add: 16, subtract: 17, divide: 18, linearBurn: 19,
  linearLight: 20, vividLight: 21, pinLight: 22, hardMix: 23, darkerColor: 24,
  lighterColor: 25, dissolve: 26,
} as const);
export type BlendMode = typeof BlendMode[keyof typeof BlendMode];

export const CompositeOperator = Object.freeze({
  sourceOver: 0, source: 1, destination: 2, clear: 3,
  sourceIn: 4, sourceOut: 5, sourceAtop: 6, destinationOver: 7,
  destinationIn: 8, destinationOut: 9, destinationAtop: 10, xor: 11,
  lighter: 12,
} as const);
export type CompositeOperator = typeof CompositeOperator[keyof typeof CompositeOperator];

export interface CompositeOptions {
  readonly blend?: BlendMode;
  readonly operation?: CompositeOperator;
  readonly space?: ColorSpace;
  readonly gamut?: GamutPolicy;
  readonly hue?: HueInterpolation;
  readonly seed?: bigint;
  readonly sampleIndex?: bigint;
}

export const PaletteFilter = Object.freeze({ linear: 0, step: 1 } as const);
export type PaletteFilter = typeof PaletteFilter[keyof typeof PaletteFilter];

export interface PaletteRamp {
  readonly palette: readonly Color[];
  readonly filter?: PaletteFilter;
  readonly address?: PaintAddress;
  readonly interpolation?: ColorInterpolation;
  readonly easing?: Keyframe["easing"];
}

export const OpticalFalloff = Object.freeze({ linear: 0, gaussian: 1, inverseQuadratic: 2 } as const);
export type OpticalFalloff = typeof OpticalFalloff[keyof typeof OpticalFalloff];

export interface RippleField {
  readonly kind: "ripple";
  readonly origin: Point;
  readonly aspect: Point;
  readonly firstRadius: number;
  readonly lastRadius: number;
  readonly width: number;
  readonly peak: number;
  readonly startUs: bigint;
  readonly lifetimeUs: bigint;
  readonly spacingUs: bigint;
  readonly rings: number;
  readonly falloff: OpticalFalloff;
  readonly fade: Keyframe["easing"];
}

export interface WaveField {
  readonly kind: "wave";
  readonly origin: Point;
  readonly cyclesPerUnit: Point;
  readonly phaseTurns: number;
  readonly startUs: bigint;
  readonly periodUs: bigint;
}

export type Field = RippleField | WaveField;
export interface FieldQuery {
  readonly position: Point;
  readonly now: bigint;
  readonly motion?: Motion;
}
export interface FieldSample {
  readonly value: number;
  readonly coverage: number;
}
export interface FieldInk {
  readonly color: Color;
  readonly palette?: PaletteRamp;
  readonly opacity: number;
}

export const PaintPattern = Object.freeze({
  linear: 0, radial: 1, conic: 2, wave: 3, plasma: 4, moire: 5, aurora: 6,
  interference: 7, chevrons: 8, diamond: 9, checker: 10, uniform: 11,
} as const);
export type PaintPattern = typeof PaintPattern[keyof typeof PaintPattern];
export const PaintRotation = Object.freeze({ fixed: 0, spin: 1, swing: 2 } as const);
export type PaintRotation = typeof PaintRotation[keyof typeof PaintRotation];

export interface PaintField {
  readonly pattern?: PaintPattern;
  readonly origin?: Point;
  /** Positive scale on each axis. */
  readonly scale: Point;
  readonly angleDegrees?: number;
  readonly rotation?: PaintRotation;
  readonly rotationPeriodUs?: bigint;
  readonly swingDegrees?: number;
  readonly reverseRotation?: boolean;
  readonly phase?: number;
  readonly waveAmplitude?: number;
  readonly phaseTrack?: Tween;
}

export interface PaintSample {
  readonly position: Point;
  readonly elapsedUs?: bigint;
  readonly motion?: Motion;
}

export type PaintSource = {
  /** Straight-alpha multiplier in [0,1]. Use one for full opacity. */
  readonly opacity: number;
  readonly opacityTrack?: KeyframeTrack;
} & (
  | { readonly kind: "solid"; readonly color: PaintColor }
  | { readonly kind: "gradient"; readonly gradient: Gradient; readonly field: PaintField }
  | { readonly kind: "palette"; readonly palette: PaletteRamp; readonly field: PaintField }
);

export interface TextPaintStyle {
  readonly color: Style;
  readonly owner?: bigint;
  readonly foreground?: PaintSource;
  readonly background?: PaintSource;
}

/** A half-open UTF-8 byte range matched against a cluster's first byte. */
export interface TextPaintSpan {
  readonly begin: number;
  readonly end: number;
  readonly style: TextPaintStyle;
}

export interface TextPaintOptions {
  readonly base: TextPaintStyle;
  readonly spans?: readonly TextPaintSpan[];
  readonly elapsedUs?: bigint;
  readonly motion?: Motion;
  readonly gamut?: GamutPolicy;
}

export const TextEffectOrder = Object.freeze({ logical: 0, reverseLogical: 1, visual: 2, reverseVisual: 3 } as const);
export type TextEffectOrder = typeof TextEffectOrder[keyof typeof TextEffectOrder];
export const TextVisibility = Object.freeze({ reveal: 0, hide: 1, visible: 2 } as const);
export type TextVisibility = typeof TextVisibility[keyof typeof TextVisibility];
export const TextGlyphEffectKind = Object.freeze({ original: 0, scramble: 1, flipboard: 2 } as const);
export type TextGlyphEffectKind = typeof TextGlyphEffectKind[keyof typeof TextGlyphEffectKind];

export interface TextGlyphEffect {
  readonly kind: TextGlyphEffectKind;
  readonly alphabet?: readonly Utf8[];
  readonly intervalUs?: bigint;
  readonly rotations?: number;
}

export interface TextTrails {
  readonly count: number;
  readonly spacingUs: bigint;
  readonly decay: number;
}

export interface TextPose {
  readonly glyph: Utf8;
  readonly offset: Point;
  readonly opacity: number;
  readonly hueDegrees: number;
}

/** Owned sample data, including the original cluster and its exact local clock. */
export interface TextEffectSample {
  readonly cluster: LayoutCluster;
  readonly visualIndex: number;
  readonly logicalIndex: number;
  readonly rank: number;
  readonly count: number;
  readonly lineBegin: number;
  readonly lineEnd: number;
  readonly elapsedUs: bigint;
  readonly localUs: bigint;
  readonly durationUs: bigint;
  readonly progress: number;
  readonly travel: number;
  readonly seed: bigint;
  readonly identity: bigint;
}

export interface TextMotion {
  readonly travel?: Point;
  readonly orbit?: Point;
  readonly phaseTurns?: number;
  readonly cycles?: number;
  readonly rankTurns?: number;
  readonly hueDegrees?: number;
  readonly easing?: Keyframe["easing"];
  readonly seededPhase?: boolean;
}

/** Return a pose, leave it unchanged, or return an explicit signed native status. */
export type TextTransform = (sample: TextEffectSample, pose: TextPose) => TextPose | number | void;

export interface TextEffect {
  readonly delayUs?: bigint;
  readonly durationUs?: bigint;
  readonly schedule?: TrackSchedule;
  readonly order?: TextEffectOrder;
  readonly visibility?: TextVisibility;
  readonly opacityEasing?: Keyframe["easing"];
  readonly glyphs?: TextGlyphEffect;
  readonly seed?: bigint;
  readonly trails?: TextTrails;
  readonly transform?: TextTransform;
}

export interface Cell {
  readonly owner: bigint;
  readonly glyph: string;
  readonly lead: Point;
  readonly columns: number;
  readonly style: Style;
}

export interface Frame {
  readonly size: Size;
  cell(at: Point): Cell;
}

export interface CellBackground {
  readonly color: Color;
  readonly owner: bigint;
}

/** A mutable frame whose read operations see its current cells. */
export interface OffscreenFrame extends Frame {
  background(at: Point): CellBackground;
  resize(size: Size): void;
  setGlyph(at: Point, glyph: Utf8, columns: number, style: Style, owner?: bigint): void;
  clearGlyph(at: Point): void;
  surface(): Surface;
  close(): void;
}

/** A view which keeps its captured bounds until the owned frame closes. */
export interface Surface {
  child(placement: Bounds, mask?: SurfaceMask): Surface;
  readonly clip: Bounds;
  coverage(at: Point): number;
  fill(bounds: Bounds, color: Color, owner?: bigint): void;
  glyph(at: Point, glyph: Utf8, columns: number, style: Style, owner?: bigint): void;
  eraseGlyph(at: Point): void;
  /** Paint a layout atomically, with the last matching byte span winning. */
  paintText(at: Point, layout: TextLayout, options: TextPaintOptions): void;
  paintStyledText(at: Point, layout: TextLayout, options: TextPaintOptions, styles: TextStyles): void;
  paintTextEffect(at: Point, layout: TextLayout, options: TextPaintOptions, effect: TextEffect): void;
  compose(layers: readonly SurfaceLayer[]): void;
  projectPanels(layers: readonly SurfacePanelLayer[]): void;
}

export interface Node {
  readonly slot: number;
  readonly generation: number;
}

export interface MountOptions {
  readonly parent?: Node;
  readonly enabled?: boolean;
  readonly focusable?: boolean;
  readonly hitTestable?: boolean;
}

export interface ControlStyle {
  readonly normal: Style;
  readonly focused: Style;
  readonly engaged: Style;
  readonly ambiguousWidth?: 1 | 2;
  readonly emojiWidth?: 1 | 2;
}

export interface NotificationAction {
  readonly id: bigint;
  readonly label: Utf8;
  readonly disabled?: boolean;
}

export interface NotificationOptions {
  readonly id: bigint;
  readonly title?: Utf8;
  readonly message?: Utf8;
  readonly dismissLabel?: Utf8;
  readonly actions?: readonly NotificationAction[];
  /** Absolute tree time in microseconds. Zero persists. */
  readonly expiresAt?: bigint;
  readonly columns?: number;
  readonly messageRows?: number;
  readonly style?: ControlStyle;
  readonly dismissible?: boolean;
  readonly dismissOnAction?: boolean;
}

export const NotificationStatus = Object.freeze({ active: 0, expired: 1, dismissed: 2 } as const);
export type NotificationStatus = typeof NotificationStatus[keyof typeof NotificationStatus];

export interface NotificationState {
  readonly id: bigint;
  readonly selectedAction: bigint;
  readonly expiresAt: bigint;
  readonly status: NotificationStatus;
}

export const NotificationIntentKind = 0x4e4e0001;
export const NotificationEventKind = Object.freeze({ performed: 1, dismissed: 2 } as const);
export type NotificationEventKind = typeof NotificationEventKind[keyof typeof NotificationEventKind];

export interface NotificationEvent {
  readonly kind: NotificationEventKind;
  readonly notification: bigint;
  readonly action: bigint;
}

const choiceKinds = ["list", "radio", "select"] as const;
const choiceNone = 0xffffffff;

export interface ChoiceEntry {
  readonly label: Utf8;
  readonly disabled?: boolean;
}

export interface ChoiceOptions {
  readonly kind: typeof choiceKinds[number];
  readonly entries: readonly ChoiceEntry[];
  readonly selected?: number;
  readonly visibleRows?: number;
  readonly style?: ControlStyle;
}

export interface ChoiceState {
  readonly selected: number | undefined;
  readonly expanded: boolean;
}

export interface ProgressOptions {
  readonly value: number;
  readonly maximum: number;
  readonly filled: Style;
  readonly remaining: Style;
  readonly vertical?: boolean;
}

export interface CursorOptions {
  readonly style: Style;
  readonly phaseUs?: bigint;
  readonly visible: boolean;
}

export interface WaitOptions {
  readonly style: Style;
  readonly phaseUs?: bigint;
  readonly ascii?: boolean;
}

export interface ScrollbarOptions {
  readonly content: number;
  readonly viewport: number;
  readonly offset: number;
  readonly thumb: Style;
  readonly track: Style;
  readonly vertical?: boolean;
}

export interface PanelOptions {
  readonly fill?: Color;
  readonly padding?: { readonly left?: number; readonly top?: number; readonly right?: number; readonly bottom?: number };
  readonly framed?: boolean;
  readonly ascii?: boolean;
  readonly border?: Style;
  readonly title?: Utf8;
  readonly titleStyle?: Style;
}

export interface ViewportOptions {
  readonly scrollColumns?: boolean;
  readonly scrollRows?: boolean;
  readonly requested?: Size;
}

export interface ViewportState {
  readonly placed: boolean;
  readonly child?: Node;
  readonly viewport: Size;
  readonly content: Size;
  readonly effective: Size;
}

const windowModes = ["normal", "minimized", "maximized"] as const;
const windowDocks = ["float", "left", "right", "top", "bottom"] as const;

export interface WindowPlacement {
  readonly floating: Bounds;
  readonly mode?: typeof windowModes[number];
  readonly dock?: typeof windowDocks[number];
  readonly dockExtent?: number;
}

export interface WindowOptions {
  readonly placement?: WindowPlacement;
  readonly panel?: PanelOptions;
  readonly title?: Utf8;
  readonly disableDrag?: boolean;
  readonly disableResize?: boolean;
}

export interface WindowAnimation {
  readonly destination: Bounds;
  readonly startUs: bigint;
  readonly durationUs: bigint;
  readonly easing?: typeof Easing[keyof typeof Easing];
}

export interface WindowState {
  readonly requested: WindowPlacement;
  readonly published: Geometry;
  readonly animating: boolean;
}

const plotKinds = ["bars", "pie", "donut", "scatter", "line", "edges"] as const;
const samplingModes = ["cell", "halfBlock", "braille", "hybrid"] as const;

export type SamplingMode = typeof samplingModes[number];

export interface CanvasPixel {
  readonly color: Color;
  readonly owner: bigint;
  readonly covered: boolean;
}

export interface CanvasInk {
  readonly color: Color;
  readonly owner?: bigint;
  readonly composite?: CompositeOptions;
}

export interface SampledCell {
  readonly glyph: string;
  readonly style: Style;
  readonly owner: bigint;
}

export interface Canvas {
  /** Dimensions in samples. Each output cell covers up to two by four samples. */
  readonly size: Size;
  clear(background: Color): void;
  pixel(at: Point): CanvasPixel;
  point(at: Point, ink: CanvasInk): void;
  line(from: Point, to: Point, ink: CanvasInk): void;
  triangle(a: Point, b: Point, c: Point, ink: CanvasInk): void;
  fill(bounds: Bounds, ink: CanvasInk): void;
  border(bounds: Bounds, ink: CanvasInk): void;
  render(mode: SamplingMode): OffscreenFrame;
  close(): void;
}


export interface PlotDatum {
  readonly x: number;
  readonly y: number;
  readonly color?: Color;
}

export interface PlotDomain {
  readonly min: number;
  readonly max: number;
}

export interface PlotBounds {
  readonly x: PlotDomain;
  readonly y: PlotDomain;
}

export interface PlotViewport {
  readonly domain: PlotBounds;
  readonly cells: Bounds;
  readonly cellAspect?: number;
  readonly equalUnits?: boolean;
}

export interface PlotSegment {
  readonly first: Point;
  readonly last: Point;
}

export interface PlotReveal {
  readonly startUs?: bigint;
  readonly durationUs: bigint;
  readonly easing?: typeof Easing[keyof typeof Easing];
}

export interface PlotOptions {
  readonly kind: typeof plotKinds[number];
  readonly data: readonly PlotDatum[];
  readonly dataLimit?: number;
  readonly sampleStride?: number;
  readonly domain?: PlotBounds;
  readonly autoX?: boolean;
  readonly autoY?: boolean;
  readonly baseline?: number;
  readonly holeRatio?: number;
  readonly cellAspect?: number;
  readonly ascii?: boolean;
  readonly equalUnits?: boolean;
  readonly sampleColors?: boolean;
  readonly sampling?: SamplingMode;
  readonly reveal?: PlotReveal;
  readonly preferred?: Size;
  readonly foreground?: Color;
  readonly background?: Color;
}

export interface DigitsOptions {
  readonly value: bigint;
  readonly fractionalDigits?: number;
  readonly ascii?: boolean;
  readonly foreground?: Color;
  readonly background?: Color;
}

const particleRecipes = ["burst", "nova", "glitter", "rain", "snow", "embers", "leaves", "sparks", "firework"] as const;

export type ParticleRecipe = typeof particleRecipes[number];

export interface ParticleEffectOptions {
  readonly recipe: ParticleRecipe;
  readonly seed?: bigint;
  readonly stream?: bigint;
  readonly startUs?: bigint;
  readonly preferred?: Size;
}

export interface ParticleEffectMotion {
  readonly radial: boolean;
  readonly directionTurns: number;
  readonly sweepTurns: number;
  readonly speedMin: number;
  readonly speedMax: number;
  readonly swayAmplitude: Point;
  readonly swayPeriodUs: bigint;
  readonly glitterPeriodUs: bigint;
  readonly glitterMinAlpha: number;
}

export interface ParticleEffect {
  readonly view: ParticleViewOptions;
  readonly motion: ParticleEffectMotion;
}

export interface ParticleRange { readonly min: Point; readonly max: Point }

export interface ParticleEmitterOptions {
  readonly seed?: bigint;
  readonly stream?: bigint;
  readonly startUs?: bigint;
  readonly periodUs?: bigint;
  readonly emissionLimit?: bigint;
  readonly birthsPerTick?: number;
  readonly capacity?: number;
  readonly lifetimeMinUs?: bigint;
  readonly lifetimeMaxUs?: bigint;
  readonly origin?: ParticleRange;
  readonly velocity?: ParticleRange;
  readonly acceleration?: Point;
  readonly birthColor?: Color;
  readonly endColor?: Color;
  readonly colorEasing?: typeof Easing[keyof typeof Easing];
}

export interface ParticleSample {
  readonly id: bigint;
  readonly bornUs: bigint;
  readonly ageUs: bigint;
  readonly lifetimeUs: bigint;
  readonly progress: number;
  readonly position: Point;
  readonly velocity: Point;
  readonly color: Color;
}

export interface ParticleTrailOptions { readonly spacingUs: bigint; readonly samples: number }

export interface ParticlePaintOptions {
  readonly glyph?: Utf8;
  readonly width?: WidthPolicy;
  readonly trail?: ParticleTrailOptions;
  readonly trailOpacity?: number;
}

export interface ParticleViewOptions {
  readonly emitter?: ParticleEmitterOptions;
  readonly paint?: ParticlePaintOptions;
  readonly preferred?: Size;
  readonly frameIntervalUs?: bigint;
}

export interface VisualBranchOptions {
  readonly seed?: bigint;
  readonly stream?: bigint;
  readonly start?: Point;
  readonly end?: Point;
  readonly trunkSegments?: number;
  readonly branchCount?: number;
  readonly branchSegments?: number;
  readonly jitterCells?: number;
  readonly branchReachCells?: number;
  readonly startUs?: bigint;
  readonly lifetimeUs?: bigint;
  readonly revealUs?: bigint;
  readonly flickerUs?: bigint;
  readonly color?: Color;
  readonly flickerMinAlpha?: number;
}

export const VisualBranchNoParent = 0xffffffff;

export interface VisualBranchSegment {
  readonly id: number;
  readonly parentId: number;
  readonly start: Point;
  readonly end: Point;
  readonly color: Color;
}

export interface ParticleReleaseOptions {
  readonly count?: number;
  readonly lifetimeMinUs?: bigint;
  readonly lifetimeMaxUs?: bigint;
  readonly velocity?: ParticleRange;
  readonly acceleration?: Point;
  readonly endColor?: Color;
  readonly colorEasing?: typeof Easing[keyof typeof Easing];
}

export interface ParticleDepositsOptions {
  readonly seed?: bigint;
  readonly stream?: bigint;
  readonly capacity?: number;
  readonly holdUs?: bigint;
  readonly release?: ParticleReleaseOptions;
}

export interface ParticleContact {
  readonly cell: Point;
  readonly at: bigint;
  readonly mass: number;
  readonly color: Color;
}

export interface ParticlePile {
  readonly cell: Point;
  readonly contacts: number;
  readonly mass: bigint;
  readonly color: Color;
}

export interface ParticleRelease {
  readonly depositId: bigint;
  readonly particle: ParticleSample;
}

export interface ParticleDepositPaint {
  readonly pileGlyph?: Utf8;
  readonly pileWidth?: WidthPolicy;
  readonly release?: ParticlePaintOptions;
}

export interface ParticleDeposits {
  add(contact: ParticleContact): bigint;
  advance(now: bigint): void;
  piles(now: bigint): readonly ParticlePile[];
  releases(now: bigint): readonly ParticleRelease[];
  deadline(now: bigint, motion?: Motion, frameIntervalUs?: bigint): bigint | undefined;
  close(): void;
}

export interface ParticleEmitter {
  sample(now: bigint): readonly ParticleSample[];
  particle(id: bigint, now: bigint): ParticleSample | undefined;
  trail(id: bigint, now: bigint, options: ParticleTrailOptions): readonly ParticleSample[];
  deadline(now: bigint, motion?: Motion, frameIntervalUs?: bigint): bigint | undefined;
  close(): void;
}

export type BoxKind = "row" | "column" | "stack";
export type BoxAlign = "start" | "center" | "end";

export interface BoxItem {
  readonly columns?: Extent;
  readonly rows?: Extent;
  readonly growColumns?: boolean;
  readonly growRows?: boolean;
}

export interface BoxLayout {
  readonly kind?: BoxKind;
  readonly padding?: { readonly left?: number; readonly top?: number; readonly right?: number; readonly bottom?: number };
  readonly gap?: number;
  readonly horizontal?: BoxAlign;
  readonly vertical?: BoxAlign;
  readonly fallback?: BoxItem;
  readonly items?: readonly BoxItem[];
}

export interface RuleOptions {
  readonly vertical?: boolean;
  readonly ascii?: boolean;
  readonly style?: Style;
}

export type Motion = "full" | "reduced";
export type Invalidation = "none" | "paint" | "layout";

export interface Bounds {
  readonly origin: Point;
  readonly size: Size;
}

export interface Geometry {
  readonly parent?: Node;
  readonly bounds: Bounds;
  readonly clip: Bounds;
  readonly placed: boolean;
  readonly enabled: boolean;
}

export const Property = Object.freeze({ text: 1, value: 2, checked: 3, selected: 4,
  offset: 5, foreground: 6, background: 7 } as const);

export type PropertyValue =
  | { readonly kind: "bool"; readonly value: boolean }
  | { readonly kind: "signed"; readonly value: bigint }
  | { readonly kind: "unsigned"; readonly value: bigint }
  | { readonly kind: "real"; readonly value: number }
  | { readonly kind: "text"; readonly value: string }
  | { readonly kind: "color"; readonly value: Color };

/** Byte offsets into the original counted UTF-8 source. */
export interface ByteSpan {
  readonly begin: number;
  readonly end: number;
}

export interface StyleElement {
  readonly role?: bigint;
  readonly region?: bigint;
  readonly states?: bigint;
}

export interface StyleCondition {
  readonly role?: bigint;
  readonly region?: bigint;
  readonly require?: bigint;
  readonly forbid?: bigint;
}

export interface StyleSelector {
  readonly subject?: StyleCondition;
  readonly ancestor?: StyleCondition;
  readonly relation?: "self" | "parent" | "ancestor";
  readonly textRegion?: bigint;
  readonly clusterId?: bigint;
  readonly bytes?: ByteSpan;
  /** Inclusive Unicode scalar bounds. */
  readonly characters?: { readonly first: number; readonly last: number };
}

export interface StyleRule {
  readonly selector?: StyleSelector;
  readonly property: number;
  readonly value: PropertyValue;
  readonly override?: boolean;
}

export interface StyleSheet {
  /** Release the sheet after any admitted operation has finished. */
  close(): void;
}

export interface StylePath {
  readonly element?: StyleElement;
  /** Immediate parent first. */
  readonly ancestors?: readonly StyleElement[];
}

export interface StyleRegion {
  readonly tag: bigint;
  readonly bytes: ByteSpan;
}

export interface StyleSubject {
  readonly path?: StylePath;
  readonly source?: Utf8;
  readonly text?: ByteSpan;
  readonly clusterId?: bigint;
  readonly regions?: readonly StyleRegion[];
}

export interface StyleMount {
  readonly node: Node;
  readonly element: StyleElement;
}

export interface TextStyles {
  readonly sheets: readonly StyleSheet[];
  readonly path?: StylePath;
  readonly regions?: readonly StyleRegion[];
}

export interface PropertyRef {
  readonly node: Node;
  readonly id: number;
}

export interface PropertyInfo {
  readonly id: number;
  readonly kind: PropertyValue["kind"];
  readonly writable: boolean;
  readonly invalidation: Invalidation;
}

export interface PropertyPreparation {
  readonly prepared: unknown;
}

export interface PropertyDescriptor {
  readonly id: number;
  readonly kind: PropertyValue["kind"];
  readonly invalidation?: Invalidation;
  readonly read: () => PropertyValue | number;
  /** Return an opaque preparation, a native result, or no preparation. */
  readonly prepare?: (value: PropertyValue) => PropertyPreparation | number | undefined;
  /** Commit when commit is true. Otherwise discard any prepared resources. */
  readonly finish?: (value: PropertyValue, prepared: unknown, commit: boolean) => void;
}

export interface PropertyAssignment {
  readonly property: PropertyRef;
  readonly value: PropertyValue;
}

export const StateSchemaVersion = 1;

/** An immutable, encoded property list that can outlive its source tree. */
export interface StateSnapshot {
  /** Each read returns an independent copy of the versioned binary encoding. */
  readonly bytes: Uint8Array;
  close(): void;
}

export type BindingCompute = (inputs: readonly PropertyValue[]) => PropertyValue | number;

export type BindingValue =
  | { readonly kind: PropertyValue["kind"]; readonly source: "input" }
  | { readonly kind: PropertyValue["kind"]; readonly source: "property"; readonly property: PropertyRef }
  | { readonly kind: PropertyValue["kind"]; readonly source: "computed";
      readonly dependencies: readonly number[]; readonly compute: BindingCompute };

export interface BindingTarget {
  readonly property: PropertyRef;
  readonly value: number;
}

export interface BindingGraph {
  /** Supply inputs in declaration order and atomically commit all targets. */
  apply(inputs: readonly PropertyValue[]): void;
  /** Release the graph and its compute functions, including after tree close. */
  close(): void;
}

export interface ScheduleLimits {
  readonly batches: number;
  readonly subscriptions: number;
}

export interface ScheduleRun {
  readonly attempted: number;
  readonly failed: bigint;
}

/** An admitted run consumed the failed batch represented in report. */
export class ScheduleError extends NativeError {
  constructor(cause: NativeError, public readonly report: ScheduleRun) {
    super(cause.code, cause.nativeName);
    this.name = "ScheduleError";
  }
}

export interface Extent {
  readonly min: number;
  readonly max?: number;
}

export interface Constraints {
  readonly columns: Extent;
  readonly rows: Extent;
}

export interface Measure {
  readonly childCount: number;
  child(index: number, constraints: Constraints): Size;
}

export interface Layout {
  readonly childCount: number;
  childSize(index: number): Size;
  place(index: number, bounds: Bounds, depth?: number): void;
  placeClipped(index: number, bounds: Bounds, clip: Bounds, depth?: number): void;
}

export interface Paint {
  /** Paintable rectangle in component-local coordinates. */
  readonly clip: Bounds;
  eraseGlyph(at: Point): void;
  glyph(at: Point, value: Utf8, columns: number, style: Style): void;
  fill(bounds: Bounds, color: Color): void;
  /** Paint a layout into this component's clip, using its mounted owner. */
  paintText(at: Point, layout: TextLayout, options: TextPaintOptions): void;
  paintStyledText(at: Point, layout: TextLayout, options: TextPaintOptions, styles: TextStyles): void;
  paintTextEffect(at: Point, layout: TextLayout, options: TextPaintOptions, effect: TextEffect): void;
  /** Sample field backgrounds at cell centers inside this mount's clip. */
  drawField(bounds: Bounds, field: Field, ink: FieldInk, now: bigint, motion?: Motion): void;
  /** Draw held piles and releases through this mount's local clip and owner. */
  drawParticleDeposits(store: ParticleDeposits, now: bigint, options?: ParticleDepositPaint, motion?: Motion): void;
  /** Draw the emitter through this mount's local clip and owner. */
  drawParticles(emitter: ParticleEmitter, now: bigint, options?: ParticlePaintOptions, motion?: Motion): void;
  /** Draw sampled branch segments through this mount's local clip and owner. */
  drawVisualBranch(options: VisualBranchOptions, now: bigint, glyph: Utf8, width?: WidthPolicy, motion?: Motion): void;
  /** Project published frames into this component's paint clip. */
  projectPanels(layers: readonly PanelLayer[]): void;
  /** Draw the samples through this mount's clip and owner. */
  paintRaster(raster: SpatialRaster, origin: Point, mode: SpatialSampling): void;
}

export interface Update {
  readonly now: bigint;
  readonly elapsed: bigint;
  readonly motion: Motion;
}

export interface UpdateReply {
  readonly invalidate?: Invalidation;
  readonly next?: bigint;
}

export const EventKind = Object.freeze({
  key: 0, text: 1, paste: 2, pointerDown: 3, pointerMove: 4, pointerUp: 5, wheel: 6,
  pointerCancel: 7, focusGain: 8, focusLoss: 9, hostCancel: 10, resize: 11,
} as const);

export const EventPhase = Object.freeze({ preview: 0, target: 1, bubble: 2 } as const);
export type EventPhaseValue = typeof EventPhase[keyof typeof EventPhase];
export type EventKindValue = typeof EventKind[keyof typeof EventKind];

export const KeyState = Object.freeze({ press: 0, repeat: 1, release: 2 } as const);
export const NamedKey = Object.freeze({
  escape: 0x110000, enter: 0x110001, tab: 0x110002, backspace: 0x110003, insert: 0x110004, delete: 0x110005,
  home: 0x110006, end: 0x110007, pageUp: 0x110008, pageDown: 0x110009,
  up: 0x11000a, down: 0x11000b, left: 0x11000c, right: 0x11000d,
  f1: 0x11000e, f2: 0x11000f, f3: 0x110010, f4: 0x110011, f5: 0x110012, f6: 0x110013,
  f7: 0x110014, f8: 0x110015, f9: 0x110016, f10: 0x110017, f11: 0x110018, f12: 0x110019,
} as const);
export const KeyModifier = Object.freeze({
  shift: 1, alt: 1 << 1, control: 1 << 2, super: 1 << 3,
  hyper: 1 << 4, meta: 1 << 5, capsLock: 1 << 6, numLock: 1 << 7,
} as const);

export interface KeyChord {
  readonly code: number;
  readonly modifiers?: number;
}

export interface KeyInput extends KeyChord {
  readonly state?: number;
}

export type Input =
  | { readonly kind: typeof EventKind.key; readonly key: KeyInput; readonly text?: Utf8 }
  | { readonly kind: typeof EventKind.text | typeof EventKind.paste; readonly text: Utf8 }
  | { readonly kind: typeof EventKind.pointerDown | typeof EventKind.pointerMove | typeof EventKind.pointerUp;
      readonly pointer: bigint; readonly at?: Point; readonly button?: number; readonly pressed?: number;
      readonly modifiers?: number }
  | { readonly kind: typeof EventKind.pointerCancel; readonly pointer: bigint; readonly reason?: number }
  | { readonly kind: typeof EventKind.hostCancel; readonly reason?: number }
  | { readonly kind: typeof EventKind.wheel; readonly at: Point; readonly dx: number; readonly dy: number;
      readonly modifiers?: number }
  | { readonly kind: typeof EventKind.resize; readonly size: Size };

export interface DispatchPolicy {
  readonly reserved?: readonly KeyChord[];
  readonly traverseFocus?: boolean;
}

export interface Delivery {
  readonly admitted: boolean;
  readonly target?: Node;
  readonly reserved: boolean;
  readonly consumed: boolean;
  readonly textDelivered: boolean;
}

export interface Intent {
  readonly source?: Node;
  readonly kind: number;
  readonly payload: Uint8Array;
}

export interface IntentField {
  readonly id: number;
  readonly value: PropertyValue;
}

/** A copy of the native FIFO item, including a possibly removed source. */
export type EventIntent = Intent & (
  | { readonly payloadKind: "bytes" }
  | { readonly payloadKind: "fields"; readonly fields: readonly IntentField[] }
);

export type IntentReceiver = (owner: Node, intent: EventIntent) => void;

export type ObservedInput = Input | { readonly kind: typeof EventKind.focusGain | typeof EventKind.focusLoss };

const keyboardProtocols = { xterm: 0, kitty: 1 } as const;
// Check the typed array brand across realms without invoking a caller-defined tag.
const typedArrayPrototype = Object.getPrototypeOf(Uint8Array.prototype);
const typedArrayTag = Object.getOwnPropertyDescriptor(typedArrayPrototype, Symbol.toStringTag)!.get!;
const typedArrayLength = Object.getOwnPropertyDescriptor(typedArrayPrototype, "length")!.get!;
const typedArrayOffset = Object.getOwnPropertyDescriptor(typedArrayPrototype, "byteOffset")!.get!;
const typedArrayBuffer = Object.getOwnPropertyDescriptor(typedArrayPrototype, "buffer")!.get!;

export interface InputDecoderOptions {
  readonly escapeDelayUs?: bigint;
  readonly pasteLimit?: number;
  readonly keyboard?: keyof typeof keyboardProtocols;
}

export type InputRead =
  | { readonly status: "pending" | "discarded"; readonly consumed: number }
  | { readonly status: "ready"; readonly consumed: number; readonly at: bigint; readonly input: Input };

/** Allocation failure can report an accepted prefix for the caller to retain. */
export class InputReadError extends NativeError {
  constructor(error: NativeError, public readonly partial: InputRead) {
    super(error.code, error.nativeName);
    this.name = "InputReadError";
  }
}

export interface InputDecoder {
  /** Read at most one record. Advance by consumed, including when it is zero. */
  read(bytes: Uint8Array, now: bigint): InputRead;
  deadline(): bigint | undefined;
  expectCursorReport(expected: boolean): void;
  cancel(now: bigint, reason: number): Input;
  close(): void;
}


export interface RuleEvent {
  readonly input: ObservedInput;
  readonly at: bigint;
  readonly phase: EventPhaseValue;
  readonly localPosition?: Point;
}

export type EventPredicate = (event: RuleEvent, properties: readonly PropertyValue[]) => boolean | number;
export type EventDerive = (event: RuleEvent, properties: readonly PropertyValue[]) => PropertyValue | number;

export interface EventFieldRule {
  readonly id: number;
  readonly kind: PropertyValue["kind"];
  readonly derive: EventDerive;
}

export interface EventRule {
  readonly event: EventKindValue;
  readonly phase: EventPhaseValue;
  readonly properties?: readonly PropertyRef[];
  readonly predicate?: EventPredicate;
  readonly intentKind: number;
  readonly fields?: readonly EventFieldRule[];
}

export const ControlIntentKind = 0x4e430001;
export const ControlEventKind = Object.freeze({
  activated: 0, itemChanged: 1, checkboxChanged: 2, choiceChanged: 3,
  scrollChanged: 4, editorChanged: 5, editorSubmitted: 6,
} as const);

export interface ControlEvent {
  readonly kind: typeof ControlEventKind[keyof typeof ControlEventKind];
  readonly value: bigint;
}

export class DispatchError extends Error {
  constructor(public readonly cause: unknown, public readonly delivery: Delivery) {
    super(cause instanceof Error ? cause.message : "Nocturne dispatch failed");
    this.name = "DispatchError";
  }
}

export interface Event {
  readonly kind: number;
  readonly at: bigint;
  readonly input: ObservedInput;
  readonly phase: EventPhaseValue;
  readonly key?: KeyInput;
  readonly text?: string;
  readonly localPosition?: Point;
  emit(kind: number, value: Utf8): void;
  emitFields(kind: number, fields: readonly IntentField[]): void;
  applyRules(rules: readonly EventRule[]): number;
  consume(): void;
  invalidate(reason?: Invalidation): void;
  capture(request: "keep" | "take" | "release"): void;
  requestFocus(): void;
}

export interface Component {
  measure?(context: Measure, limits: Constraints): Size;
  layout?(context: Layout, size: Size): void;
  update?(info: Update): UpdateReply | undefined;
  paint?(context: Paint, size: Size): void;
  event?(event: Event): void;
  dispose?(): void;
}

export interface PanelContentProvider {
  readonly preferred: Size;
  readonly paint: NonNullable<Component["paint"]>;
  readonly update?: Component["update"];
  readonly event?: Component["event"];
  readonly dispose?: Component["dispose"];
}

export interface CallbackFailure {
  readonly phase: keyof Component | "bindingCompute" | "eventPredicate" | "eventDerive" | "intentReceive"
    | "propertyRead" | "propertyPrepare" | "propertyFinish";
  readonly cause: unknown;
}

export class CallError extends Error {
  constructor(public readonly native: NativeError | null, public readonly callbacks: readonly CallbackFailure[]) {
    super(native ? native.message : "Nocturne callback failed during the native call");
    this.name = "CallError";
  }
}

/** Receiver failures accompany the number of FIFO items already consumed. */
export class IntentDeliveryError extends CallError {
  constructor(public readonly delivered: number, callbacks: readonly CallbackFailure[]) {
    super(null, callbacks);
    this.name = "IntentDeliveryError";
  }
}

export type NocturneModule = "nocturne" | "graph" | "spatial" | "simulation";

export interface GraphNode {
  readonly id: bigint;
  readonly pinned?: boolean;
  readonly position?: Point;
}

export interface GraphEdge {
  readonly id: bigint;
  readonly source: bigint;
  readonly target: bigint;
  readonly directed?: boolean;
}

export interface GraphLabel {
  readonly node: bigint;
  readonly text: Utf8;
}

export interface GraphViewColors {
  readonly node?: Color;
  readonly pinned?: Color;
  readonly edge?: Color;
  readonly selected?: Color;
  readonly background?: Color;
}

export interface GraphSnapshot {
  readonly nodes: ReadonlyArray<Required<GraphNode>>;
  readonly edges: ReadonlyArray<Required<GraphEdge>>;
}

const graphHitKinds = ["none", "node", "edge"] as const;

export interface GraphHit {
  readonly kind: typeof graphHitKinds[number];
  readonly id: bigint;
}

export const GraphSelectionIntent = 0x4e470001;

export interface Graph {
  readonly nodes: readonly GraphNode[];
  readonly edges: readonly GraphEdge[];
  readonly layout?: "layered" | "radial" | "force";
  readonly forceIterations?: number;
  readonly preferred?: Size;
  readonly labels?: readonly GraphLabel[];
  readonly width?: WidthPolicy;
  readonly colors?: GraphViewColors;
}

export interface Vec3 {
  readonly x: number;
  readonly y: number;
  readonly z: number;
}

export interface WaterWave {
  readonly amplitude: number;
  readonly cyclesPerX: number;
  readonly cyclesPerZ: number;
  readonly cyclesPerSecond: number;
  readonly phase: number;
}

export interface WaterSample {
  readonly height: number;
  readonly gradientX: number;
  readonly gradientZ: number;
  readonly normal: Vec3;
  readonly verticalVelocity: number;
  readonly laplacian: number;
}

export interface Tree {
  readonly frame: Frame | undefined;
  readonly focus: Node | undefined;
  readonly nextUpdate: bigint | undefined;
  mountBox(kind: BoxKind, options?: MountOptions, gap?: number): Node;
  mountBoxLayout(layout: BoxLayout, options?: MountOptions): Node;
  setBoxLayout(node: Node, layout: BoxLayout): void;
  mountRule(rule: RuleOptions, options?: MountOptions): Node;
  setRule(node: Node, rule: RuleOptions): void;
  mountLabel(value: Utf8, options?: MountOptions, style?: Style): Node;
  mountButton(label: Utf8, options?: MountOptions, style?: ControlStyle): Node;
  buttonPressed(node: Node): boolean;
  mountItem(label: Utf8, selected: boolean, options?: MountOptions, style?: ControlStyle): Node;
  item(node: Node): boolean;
  setItem(node: Node, selected: boolean): void;
  mountCheckbox(label: Utf8, checked: boolean, options?: MountOptions, style?: ControlStyle): Node;
  checkbox(node: Node): boolean;
  setCheckbox(node: Node, checked: boolean): void;
  mountChoice(choice: ChoiceOptions, options?: MountOptions): Node;
  configureChoice(node: Node, choice: ChoiceOptions): void;
  choice(node: Node): ChoiceState;
  setChoice(node: Node, selected?: number): void;
  mountProgress(progress: ProgressOptions, options?: MountOptions): Node;
  setProgress(node: Node, progress: ProgressOptions): void;
  progress(node: Node): ProgressOptions;
  mountCursor(cursor: CursorOptions, options?: MountOptions): Node;
  setCursor(node: Node, cursor: CursorOptions): void;
  mountWait(wait: WaitOptions, options?: MountOptions): Node;
  setWait(node: Node, wait: WaitOptions): void;
  mountScrollbar(bar: ScrollbarOptions, options?: MountOptions): Node;
  setScrollbar(node: Node, bar: ScrollbarOptions): void;
  scrollbar(node: Node): ScrollbarOptions;
  mountEditorView(view: EditorViewOptions, options?: MountOptions): Node;
  scrollEditorView(node: Node, requested: Size): void;
  editorViewOffset(node: Node): Size;
  mountPanel(panel: PanelOptions, options?: MountOptions): Node;
  /** Mount a leaf with a required paint callback and optional update, event and disposal. */
  mountPanelContent(provider: PanelContentProvider, options?: MountOptions): Node;
  setPanel(node: Node, panel: PanelOptions): void;
  mountViewport(viewport: ViewportOptions, options?: MountOptions): Node;
  setViewport(node: Node, viewport: ViewportOptions): void;
  viewport(node: Node): ViewportState;
  mountWindowWorkspace(preferred: Size, options?: MountOptions): Node;
  mountWindow(window: WindowOptions, options: MountOptions & { readonly parent: Node }): Node;
  setWindowPlacement(node: Node, placement: WindowPlacement): void;
  setWindowPanel(node: Node, panel: PanelOptions): void;
  /** Raise a published window and focus it or the named descendant. */
  focusWindow(node: Node, target?: Node): void;
  animateWindow(node: Node, animation: WindowAnimation): void;
  window(node: Node): WindowState;
  mountNotification(notification: NotificationOptions, options?: MountOptions): Node;
  setNotification(node: Node, notification: NotificationOptions): void;
  notification(node: Node): NotificationState;
  dismissNotification(node: Node): void;
  mountPlot(plot: PlotOptions, options?: MountOptions): Node;
  setPlot(node: Node, plot: PlotOptions): void;
  mountDigits(digits: DigitsOptions, options?: MountOptions): Node;
  setDigits(node: Node, digits: DigitsOptions): void;
  mountParticleEffect(effect: ParticleEffectOptions | ParticleEffect, options?: MountOptions): Node;
  setParticleEffect(node: Node, effect: ParticleEffectOptions | ParticleEffect): void;
  mountParticleView(view?: ParticleViewOptions, options?: MountOptions): Node;
  setParticleView(node: Node, view: ParticleViewOptions): void;
  mountComponent(component: Component, options?: MountOptions): Node;
  mountGraph(graph: Graph, options?: MountOptions): Node;
  setLabel(node: Node, value: Utf8, style?: Style): void;
  setGraph(node: Node, graph: Graph): void;
  graphHitAt(node: Node, at: Point): GraphHit;
  graphSelection(node: Node): GraphHit;
  selectGraph(node: Node, selection: GraphHit): void;
  withNative<T>(call: (tree: RawTree) => T): T;
  remove(node: Node): void;
  move(node: Node, newParent?: Node): void;
  invalidate(node: Node, reason: Invalidation): void;
  geometry(node: Node): Geometry;
  hit(point: Point): Node | undefined;
  /** Compare the identity of two references issued by this tree. */
  sameNode(left: Node | undefined, right: Node | undefined): boolean;
  property(node: Node, id: number): PropertyValue;
  setProperty(node: Node, id: number, value: PropertyValue): void;
  defineProperties(node: Node, descriptors: readonly PropertyDescriptor[]): void;
  properties(node: Node): readonly PropertyInfo[];
  applyProperties(assignments: readonly PropertyAssignment[]): void;
  captureState(properties: readonly PropertyRef[]): StateSnapshot;
  restoreState(properties: readonly PropertyRef[], snapshot: StateSnapshot): void;
  applyStyles(sheets: readonly StyleSheet[], mounts: readonly StyleMount[], targets: readonly PropertyRef[]): void;
  createBindingGraph(values: readonly BindingValue[], targets: readonly BindingTarget[]): BindingGraph;
  configureSchedule(limits: ScheduleLimits): void;
  /** Queue an owned copy. Removing the owner or any target cancels the batch. */
  scheduleBatch(owner: Node, deadline: bigint, assignments: readonly PropertyAssignment[]): bigint;
  cancelSchedule(id: bigint): void;
  readonly nextScheduled: bigint | undefined;
  runSchedule(now: bigint, budget: number): ScheduleRun;
  subscribeIntent(owner: Node, kind: number, receive: IntentReceiver): bigint;
  deliverIntents(budget: number): number;
  tick(now: bigint, motion?: Motion): void;
  setFocus(node?: Node): void;
  focusAdvance(reverse?: boolean): void;
  capture(pointer: bigint): Node | undefined;
  dispatch(now: bigint, input: Input, policy?: DispatchPolicy): Delivery;
  peekIntent(): Intent | undefined;
  peekEventIntent(): EventIntent | undefined;
  popIntent(): void;
  publish(size: Size): void;
  close(): void;
}

export interface Nocturne extends FlowSimulation, SurfaceTransportSimulation, BodySimulation {
  readonly abiVersion: number;
  readonly unicodeVersion: string;
  readonly availableComponents: readonly NocturneModule[];
  readonly raw: RawAPI;
  readonly spatial: SpatialGeometry & SpatialMeshes;
  createText(value: Utf8): Text;
  /** Return opportunities in byte order, ending in a mandatory break. */
  lineBreaks(value: Utf8): readonly LineBreak[];
  scriptRuns(value: Utf8): readonly ScriptRun[];
  createBidi(value: Utf8, direction?: Direction): Bidi;
  measureGlyph(glyph: Utf8, width?: WidthPolicy): GlyphMetrics;
  /** Snapshot owned text and preserve its current cluster identities. */
  createTextLayout(text: Text, options?: TextLayoutOptions): TextLayout;
  layoutText(value: Utf8, options?: TextLayoutOptions): TextLayout;
  createStyleSheet(rules: readonly StyleRule[]): StyleSheet;
  /** Returns a copied property value if a sheet declaration matches. */
  resolveStyle(sheets: readonly StyleSheet[], subject: StyleSubject, property: number): PropertyValue | undefined;
  importStateSnapshot(bytes: Uint8Array): StateSnapshot;
  colorFromRGBA(color: Color): PaintColor;
  colorToRGBA(color: PaintColor, gamut?: GamutPolicy): Color;
  convertColor(color: PaintColor, space: ColorSpace, gamut?: GamutPolicy): PaintColor;
  mixColor(a: PaintColor, b: PaintColor, progress: number, interpolation?: ColorInterpolation): PaintColor;
  parseBlendMode(name: string): BlendMode;
  parseCompositeOperator(name: string): CompositeOperator;
  compositeColor(backdrop: PaintColor, source: PaintColor, options?: CompositeOptions): PaintColor;
  compositeRGBA(backdrop: Color, source: Color, options?: CompositeOptions): Color;
  lookupPalette(palette: readonly Color[], index: number): Color;
  nearestPalette(palette: readonly Color[], color: PaintColor, gamut?: GamutPolicy): number;
  sampleGradient(gradient: Gradient, coordinate: number): PaintColor;
  samplePalette(palette: PaletteRamp, coordinate: number): PaintColor;
  opticalFalloff(falloff: OpticalFalloff, distance: number): number;
  defaultRippleField(): RippleField;
  defaultWaveField(): WaveField;
  sampleField(field: Field, query: FieldQuery): FieldSample;
  fieldColor(field: Field, ink: FieldInk, query: FieldQuery): Color;
  /** Return the next boundary or active sample time in microseconds. */
  fieldDeadline(field: Field, now: bigint, frameIntervalUs: bigint, motion?: Motion): bigint | undefined;
  paintCoordinate(field: PaintField, sample: PaintSample): number;
  samplePaint(paint: PaintSource, sample: PaintSample): PaintColor;
  samplePaintRGBA(paint: PaintSource, sample: PaintSample, gamut?: GamutPolicy): Color;
  textPoseMotion(sample: TextEffectSample, pose: TextPose, motion: TextMotion): TextPose;
  textPoseNoise(sample: TextEffectSample, pose: TextPose, extent: Point, intervalUs: bigint, smooth?: boolean): TextPose;
  createEditor(value: Utf8, options?: EditorOptions): Editor;
  createEditorRegisters(slots: number): EditorRegisters;
  createTree(): Tree;
  createInputDecoder(options?: InputDecoderOptions): InputDecoder;
  resolveScroll(options: ViewportOptions, viewport: Size, content: Size): Size;
  anchorRect(parent: Size, child: Size, horizontal: BoxAlign, vertical: BoxAlign, offset: Point): Bounds;
  xterm256Palette(): readonly Color[];
  /** Present one frame synchronously to a caller-owned byte writer. */
  writeAnsi(frame: Frame, now: bigint, options: AnsiOptions, write: (bytes: Uint8Array) => void): void;
  createCanvas(size: Size): Canvas;
  /** Resolve the native name, including "half-block" for the "halfBlock" mode. */
  parseSamplingMode(name: string): SamplingMode;
  /** Reduce eight row-major final pixels and their underlying background colors. */
  reduceCellSamples(pixels: readonly CanvasPixel[], background: readonly Color[], mode: SamplingMode): SampledCell;
  createOffscreenFrame(size: Size): OffscreenFrame;
  frameFromCells(size: Size, cells: readonly ContentCell[]): OffscreenFrame;
  sampleImage(image: ImageView, u: number, v: number, sampling?: ImageSampling): Color;
  rasterizeImage(image: ImageView, extent: Size, options?: ImageRasterOptions): OffscreenFrame;
  sampleVisualBranch(options: VisualBranchOptions, now: bigint, motion?: Motion): readonly VisualBranchSegment[];
  visualBranchDeadline(options: VisualBranchOptions, now: bigint, motion?: Motion, frameIntervalUs?: bigint): bigint | undefined;
  createParticleDeposits(options?: ParticleDepositsOptions): ParticleDeposits;
  /** Read editable defaults for a native effect recipe. */
  particleEffectRecipe(recipe: ParticleRecipe): ParticleEffect;
  /** Create an owned emitter from the effect's emitter and motion settings. */
  createParticleEffectEmitter(effect: ParticleEffect): ParticleEmitter;
  createParticleEmitter(options?: ParticleEmitterOptions): ParticleEmitter;
  createTimeline(clips: readonly TimelineClip[], options?: TimelineOptions): Timeline;
  createTimelineTree(root: TimelineNode, options?: Omit<TimelineOptions, "layout">): Timeline;
  limitTrackRate(requested: TrackRate, minimum: TrackRate, maximum: TrackRate): TrackRate;
  mapTrackTime(timing: TrackTime, elapsedUs: bigint): bigint;
  sampleRandomTrack(track: RandomTrack, atUs: bigint): number;
  scheduleTrack(schedule: TrackSchedule, rank: bigint, item: bigint, spanUs: bigint): TrackSlot;
  /** Evaluate the easing curve at a finite progress in [0, 1]. */
  ease(easing: typeof Easing[keyof typeof Easing], progress: number): number;
  /** Sample a scalar tween at caller-owned local time in microseconds. */
  sampleTween(tween: Tween, elapsedUs: bigint): AnimationSample;
  /** Sample keys starting at zero, in strictly increasing time order. */
  sampleKeyframes(track: KeyframeTrack, elapsedUs: bigint): AnimationSample;
  createPlayback(nowUs: bigint): Playback;
  createSpring(options: SpringOptions): Spring;
  pathLength(path: Path): number;
  /** Resolve a signed distance and return position and travel tangent. */
  samplePath(path: Path, distance: number): PathSample;
  projectPanelPoint(pose: PanelPose, point: Point): Point;
  samplePanelPose(transition: PanelTransition, elapsedUs: bigint, motion?: Motion): PanelPose;
  /** Project a point within an inclusive data domain to continuous cell edges. */
  projectPlot(view: PlotViewport, point: Point): Point;
  /** Clip a directed segment. A hidden segment returns undefined. */
  clipPlotSegment(domain: PlotBounds, segment: PlotSegment): PlotSegment | undefined;
  /** Decode the kind and payload of a queued stock-control intent. */
  decodeControlIntent(intent: Intent): ControlEvent;
  /** Decode a typed notification FIFO item. */
  decodeNotificationIntent(intent: EventIntent): NotificationEvent;
  layoutGraph(graph: Graph): GraphSnapshot;
  decodeGraphIntent(intent: EventIntent): GraphHit;
  crossVector(left: Vec3, right: Vec3): Vec3;
  normalizeVector(vector: Vec3): Vec3;
  sampleWater(waves: readonly WaterWave[], x: number, z: number, seconds: number): WaterSample;
  sampleFlowSection(section: FlowSection, rate: number): FlowSectionSample;
  exchangeHeat(stores: ThermalPair, conductance: number, elapsedUs: bigint): ThermalPair;
}

export interface FlowSection {
  readonly width: number;
  readonly speed: number;
  readonly maximumDepth: number;
}

export interface FlowSectionSample {
  readonly depth: number;
  readonly carriedRate: number;
  readonly excessRate: number;
}

export interface ThermalStore {
  readonly heatCapacity: number;
  readonly energy: number;
}

export interface ThermalPair {
  readonly first: ThermalStore;
  readonly second: ThermalStore;
}

/** A synchronous borrow of a managed tree and its native argument storage. */
export interface RawTree {
  readonly arena: RawArena;
  /** A pointer argument slot containing the borrowed NtTree. */
  handle(): number;
  /** A slot containing the native value of a managed node. */
  node(node?: Node): number;
  /** Adopt a live native node mounted in the same tree. */
  adopt(address: number): Node;
}

class Runtime {
  readonly #components = new Map<number, {
    component: Component; owner: OwnedTree; save: (failure: CallbackFailure) => void;
  }>();
  #lastComponent = 0;
  #currentContext: symbol | undefined;
  #raw?: RawAPI;
  #spatialMeshes?: ReturnType<typeof spatialMeshBindings>;

  constructor(readonly native: NativeWasm) {}

  get raw(): RawAPI {
    return this.#raw ??= new RawAPI(this.native, code => this.failure(code));
  }

  get spatialMeshes(): ReturnType<typeof spatialMeshBindings> {
    return this.#spatialMeshes ??= spatialMeshBindings(this.raw, code => this.failure(code),
      handle => new OwnedOffscreenFrame(this, handle));
  }

  view(): DataView {
    return new DataView(this.native.memory.buffer);
  }

  decode(start: number, count: number): string {
    return decoder.decode(new Uint8Array(this.native.memory.buffer, start, count));
  }

  decodeCString(address: number): string | null {
    if (address === 0) return null;
    const tail = new Uint8Array(this.native.memory.buffer, address);
    const count = tail.indexOf(0);
    return count >= 0 ? this.decode(address, count) : null;
  }

  failure(code: number): NativeError {
    return new NativeError(code, this.decodeCString(this.native.nocturne_wasm_result_name(code) >>> 0));
  }

  check(code: number): void {
    if (code !== 0) throw this.failure(code);
  }

  withBuffer<T>(count: number, call: (address: number) => T): T {
    const address = this.native.nocturne_wasm_allocate(toU32(Math.max(1, count))) >>> 0;
    if (address === 0) throw this.failure(3);
    try {
      return call(address);
    } finally {
      this.native.nocturne_wasm_release(address);
    }
  }

  withBytes<T>(input: Utf8, call: (address: number, count: number) => T): T {
    if (typeof input === "string" && /[\uD800-\uDFFF]/u.test(input)) throw this.failure(1);
    const bytes = typeof input === "string" ? encoder.encode(input) : input;
    if (bytes.length === 0) return call(0, 0);
    return this.withBuffer(bytes.length, address => {
      new Uint8Array(this.native.memory.buffer, address, bytes.length).set(bytes);
      return call(address, bytes.length);
    });
  }

  retain(component: Component, owner: OwnedTree, save: (failure: CallbackFailure) => void): number {
    if (this.#lastComponent === 0xffffffff) throw this.failure(3);
    const id = ++this.#lastComponent;
    this.#components.set(id, { component, owner, save });
    return id;
  }

  release(id: number): void {
    this.#components.delete(id >>> 0);
  }

  invoke(id: number, phase: keyof Component,
    call: (component: Component, borrow: () => void, owner: OwnedTree) => void): number {
    const state = this.#components.get(id >>> 0);
    if (!state) return 5;
    const previous = this.#currentContext;
    const current = Symbol();
    this.#currentContext = current;
    let active = true;
    const borrow = () => {
      if (!active) throw this.failure(5);
      if (this.#currentContext !== current) throw this.failure(6);
    };
    try {
      call(state.component, borrow, state.owner);
      return 0;
    } catch (cause) {
      state.save({ phase, cause });
      return 9;
    } finally {
      active = false;
      this.#currentContext = previous;
    }
  }

  withoutComponentContext<T>(call: () => T): T {
    const previous = this.#currentContext;
    this.#currentContext = undefined;
    try {
      return call();
    } finally {
      this.#currentContext = previous;
    }
  }

  sizeCall(call: (out: number) => number): Size {
    return this.withBuffer(8, out => {
      this.check(call(out));
      const data = this.view();
      return { columns: data.getUint32(out, true), rows: data.getUint32(out + 4, true) };
    });
  }
}

const graphLayout = { layered: 0, radial: 1, force: 2 } as const;

function vectorAt(data: DataView, address: number): Vec3 {
  return {
    x: data.getFloat64(address, true),
    y: data.getFloat64(address + 8, true),
    z: data.getFloat64(address + 16, true),
  };
}

function toU32(value: number): number {
  if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) {
    throw new RangeError("Expected an unsigned 32-bit integer");
  }
  return value;
}

function toI32(value: number): number {
  if (!Number.isInteger(value) || value < -0x80000000 || value > 0x7fffffff) {
    throw new RangeError("Expected a signed 32-bit integer");
  }
  return value;
}

function toI64(value: bigint): bigint {
  if (typeof value !== "bigint" || BigInt.asIntN(64, value) !== value) {
    throw new RangeError("Expected a signed 64-bit bigint");
  }
  return value;
}

function toU64(value: bigint): bigint {
  if (typeof value !== "bigint" || value < 0n || value > 0xffffffffffffffffn) {
    throw new RangeError("Expected an unsigned 64-bit bigint");
  }
  return value;
}

class OwnedText implements Text {
  readonly #runtime: Runtime;
  #handle: number;
  #busy = false;

  constructor(runtime: Runtime, handle: number) {
    this.#runtime = runtime;
    this.#handle = handle;
  }

  static handle(runtime: Runtime, text: Text): number {
    if (!text || typeof text !== "object" || !(#handle in text) || text.#runtime !== runtime) {
      throw runtime.failure(1);
    }
    return text.#borrow();
  }

  #borrow(): number {
    if (this.#handle === 0) throw this.#runtime.failure(5);
    return this.#handle;
  }

  get value(): string {
    const handle = this.#borrow();
    return this.#runtime.withBuffer(8, out => {
      this.#runtime.native.nocturne_wasm_text_value(handle, out);
      const data = this.#runtime.view();
      return this.#runtime.decode(data.getUint32(out, true), data.getUint32(out + 4, true));
    });
  }

  get count(): number {
    return this.#runtime.native.nocturne_wasm_text_count(this.#borrow()) >>> 0;
  }

  cluster(index: number): Cluster {
    const handle = this.#borrow();
    return this.#runtime.withBuffer(16, out => {
      this.#runtime.check(this.#runtime.native.nocturne_wasm_text_cluster(handle, toU32(index), out));
      const data = this.#runtime.view();
      return {
        id: data.getBigUint64(out, true),
        begin: data.getUint32(out + 8, true),
        end: data.getUint32(out + 12, true),
      };
    });
  }

  replace(begin: number, end: number, value: Utf8): void {
    const handle = this.#borrow();
    if (this.#busy) throw this.#runtime.failure(6);
    this.#busy = true;
    try {
      this.#runtime.withBytes(value, (address, count) => {
        this.#runtime.check(this.#runtime.native.nocturne_wasm_text_replace(
          handle, toU32(begin), toU32(end), address, count,
        ));
      });
    } finally {
      this.#busy = false;
    }
  }

  close(): void {
    if (this.#busy) throw this.#runtime.failure(6);
    if (this.#handle !== 0) {
      this.#runtime.native.nocturne_wasm_text_destroy(this.#handle);
      this.#handle = 0;
    }
  }
}

function createText(runtime: Runtime, value: Utf8): OwnedText {
  return runtime.withBytes(value, (address, count) => runtime.withBuffer(4, out => {
    runtime.check(runtime.native.nocturne_wasm_text_create(address, count, out));
    return new OwnedText(runtime, runtime.view().getUint32(out, true));
  }));
}

const boxKind = { row: 0, column: 1, stack: 2 } as const;
const motionCode = { full: 0, reduced: 1 } as const;
const invalidationCode = { none: 0, paint: 1, layout: 2 } as const;
const captureCode = { keep: 0, take: 1, release: 2 } as const;
const defaultStyle: Style = { foreground: [235, 240, 250, 255], background: [0, 0, 0, 0] };

function mountFlags(options: MountOptions): number {
  return (options.enabled === false ? 0 : 1) | (options.focusable ? 2 : 0) | (options.hitTestable ? 4 : 0);
}

function packedColor(color: Color): number {
  return color.reduce((value, channel, index) => {
    if (toU32(channel) > 255) throw new RangeError("Expected a color channel from zero to 255");
    return value | (channel << (index * 8));
  }, 0) >>> 0;
}

function pointerSlot(arena: RawArena, target: number): number {
  const address = arena.value("pointer");
  arena.setPointer(address, target);
  return address;
}

function boolSlot(arena: RawArena, value: boolean): number {
  const address = arena.value("_Bool");
  arena.setUnsigned(address, arena.api.size("_Bool"), value ? 1n : 0n);
  return address;
}

function sizeSlot(arena: RawArena, count: number): number {
  const address = arena.value("size_t");
  arena.setUnsigned(address, arena.api.size("size_t"), BigInt(toU32(count)));
  return address;
}

function enumSlot(arena: RawArena, type: string, value: number): number {
  const address = arena.value(type);
  arena.setUnsigned(address, arena.api.size(type), BigInt(toU32(value)));
  return address;
}

function readNativeRows<Row>(arena: RawArena, name: string, type: string, args: readonly number[],
  decode: (address: number) => Row): Row[] {
  const count = arena.value("size_t");
  const out = pointerSlot(arena, count);
  arena.result(name, ...args, pointerSlot(arena, 0), sizeSlot(arena, 0), out);
  const needed = Number(arena.unsigned(count, arena.api.size("size_t")));
  if (needed === 0) return [];
  const stride = arena.api.size(type);
  const rows = arena.allocate(needed * stride);
  arena.result(name, ...args, pointerSlot(arena, rows), sizeSlot(arena, needed), out);
  return Array.from({ length: Number(arena.unsigned(count, arena.api.size("size_t"))) },
    (_, index) => decode(rows + index * stride));
}

function nativeSourceSpan(arena: RawArena, address: number, type: string): Pick<Cluster, "begin" | "end"> {
  const span = address + arena.api.field(type, "source").offset;
  return { begin: nativeField(arena, span, "NtByteSpan", "begin"),
    end: nativeField(arena, span, "NtByteSpan", "end") };
}

const bidiDirection = { auto: 0, ltr: 1, rtl: 2 } as const;
const wrapCode = { none: 0, word: 1, cluster: 2 } as const;
const textAlignCode = { start: 0, center: 1, end: 2 } as const;

function writeWidthPolicy(arena: RawArena, address: number, width?: WidthPolicy): void {
  const ambiguous = width?.ambiguous ?? 0;
  const emoji = width?.emoji ?? 0;
  setNativeField(arena, address, "NtWidthPolicy", "ambiguous", ambiguous === 0 ? 1 : toU32(ambiguous));
  setNativeField(arena, address, "NtWidthPolicy", "emoji", emoji === 0 ? 2 : toU32(emoji));
}

function enumCode<Key extends string>(runtime: Runtime, codes: Readonly<Record<Key, number>>, value: Key): number {
  if (typeof value !== "string" || !Object.hasOwn(codes, value)) throw runtime.failure(1);
  return codes[value];
}

function writeExtent(arena: RawArena, address: number, extent?: Extent): void {
  const max = extent?.max;
  setNativeField(arena, address, "NtExtentRange", "min", toU32(extent?.min ?? 0));
  setNativeField(arena, address, "NtExtentRange", "max", toU32(max ?? 0));
  setNativeField(arena, address, "NtExtentRange", "bounded", max === undefined ? 0 : 1);
}

function createTextLayout(runtime: Runtime, text: Text, options: TextLayoutOptions): TextLayout {
  return runtime.raw.withArena(arena => {
    const type = "NtTextLayoutOptions";
    const settings = arena.value(type);
    writeWidthPolicy(arena, settings + arena.api.field(type, "width").offset, options.width);
    const bounds = settings + arena.api.field(type, "bounds").offset;
    const constraints = options.bounds;
    writeExtent(arena, bounds + arena.api.field("NtConstraints", "columns").offset, constraints?.columns);
    writeExtent(arena, bounds + arena.api.field("NtConstraints", "rows").offset, constraints?.rows);
    setNativeField(arena, settings, type, "wrap", enumCode(runtime, wrapCode, options.wrap ?? "none"));
    setNativeField(arena, settings, type, "align", enumCode(runtime, textAlignCode, options.align ?? "start"));
    setNativeField(arena, settings, type, "base_direction", enumCode(runtime, bidiDirection, options.baseDirection ?? "auto"));
    const tabStop = options.tabStop ?? 0;
    setNativeField(arena, settings, type, "tab_stop", tabStop === 0 ? 4 : toU32(tabStop));
    setNativeField(arena, settings, type, "ellipsis", options.ellipsis ? 1 : 0);
    const out = arena.value("pointer");
    const handle = OwnedText.handle(runtime, text);
    arena.result("nt_text_layout_create", pointerSlot(arena, 0), pointerSlot(arena, handle),
      pointerSlot(arena, settings), pointerSlot(arena, out));
    return new OwnedTextLayout(runtime, arena.pointer(out));
  });
}

class OwnedTextLayout implements TextLayout {
  readonly #runtime: Runtime;
  #handle: number;
  #uses = 0;
  #closed = false;

  constructor(runtime: Runtime, handle: number) {
    this.#runtime = runtime;
    this.#handle = handle;
  }

  static use<T>(runtime: Runtime, layout: TextLayout, call: (handle: number) => T): T {
    if (!layout || typeof layout !== "object" || !(#handle in layout) || layout.#runtime !== runtime) {
      throw runtime.failure(1);
    }
    return layout.#use(call);
  }

  #use<T>(call: (handle: number) => T): T {
    if (this.#closed) throw this.#runtime.failure(5);
    this.#uses++;
    try {
      return call(this.#handle);
    } finally {
      this.#uses--;
      this.#destroyIfUnused();
    }
  }

  #call<T>(call: (arena: RawArena, handle: number) => T): T {
    return this.#use(handle => this.#runtime.raw.withArena(arena => call(arena, pointerSlot(arena, handle))));
  }

  get source(): string {
    return this.#call((arena, handle) => readByteView(this.#runtime, arena,
      arena.call("nt_text_layout_source", handle)));
  }

  get size(): Size {
    return this.#call((arena, handle) => nativeSize(arena, arena.call("nt_text_layout_size", handle)));
  }

  get count(): number {
    return this.#call((arena, handle) => Number(arena.unsigned(
      arena.call("nt_text_layout_cluster_count", handle), arena.api.size("size_t"))));
  }

  cluster(index: number): LayoutCluster {
    return this.#call((arena, handle) => {
      const type = "NtClusterView";
      const out = arena.value(type);
      arena.result("nt_text_layout_cluster", handle, sizeSlot(arena, index), pointerSlot(arena, out));
      return readLayoutCluster(this.#runtime, arena, out);
    });
  }

  hit({ x, y }: Point): Caret {
    return this.#call((arena, handle) => {
      const out = arena.value("NtCaret");
      arena.result("nt_text_layout_hit", handle, cellPointSlot(arena, { x, y }), pointerSlot(arena, out));
      return readCaret(arena, out);
    });
  }

  caretPosition({ offset, affinity }: Caret): Point {
    return this.#call((arena, handle) => {
      const caret = arena.value("NtCaret");
      writeCaret(arena, caret, { offset, affinity });
      const out = arena.value("NtCellPoint");
      arena.result("nt_text_layout_caret_position", handle, caret, pointerSlot(arena, out));
      return nativeCellPoint(arena, out);
    });
  }

  close(): void {
    this.#closed = true;
    this.#destroyIfUnused();
  }

  #destroyIfUnused(): void {
    if (this.#closed && this.#uses === 0 && this.#handle !== 0) {
      this.#runtime.raw.withArena(arena => arena.call("nt_text_layout_destroy", pointerSlot(arena, this.#handle)));
      this.#handle = 0;
    }
  }
}

function readLayoutCluster(runtime: Runtime, arena: RawArena, address: number): LayoutCluster {
  const type = "NtClusterView";
  const id = arena.api.field(type, "id");
  return {
    ...nativeSourceSpan(arena, address, type),
    id: arena.unsigned(address + id.offset, id.size),
    glyph: readByteView(runtime, arena, address + arena.api.field(type, "glyph").offset),
    position: nativeCellPoint(arena, address + arena.api.field(type, "position").offset),
    columns: nativeField(arena, address, type, "columns"),
    bidiLevel: nativeField(arena, address, type, "bidi_level"),
  };
}

class OwnedBidi implements Bidi {
  #handle: number;

  constructor(private readonly runtime: Runtime, handle: number) { this.#handle = handle; }

  #call<T>(call: (arena: RawArena, handle: number) => T): T {
    if (this.#handle === 0) throw this.runtime.failure(5);
    return this.runtime.raw.withArena(arena => call(arena, pointerSlot(arena, this.#handle)));
  }

  get count(): number {
    return this.#call((arena, handle) => Number(arena.unsigned(
      arena.call("nt_bidi_count", handle), arena.api.size("size_t"))));
  }

  scalar(index: number): BidiScalar {
    return this.#call((arena, handle) => {
      const type = "NtBidiScalar";
      const value = arena.value(type);
      arena.result("nt_bidi_scalar", handle, sizeSlot(arena, index), pointerSlot(arena, value));
      return { ...nativeSourceSpan(arena, value, type), scalar: nativeField(arena, value, type, "scalar"),
        level: nativeField(arena, value, type, "level"),
        paragraphLevel: nativeField(arena, value, type, "paragraph_level"),
        paragraphSeparator: nativeField(arena, value, type, "paragraph_separator") !== 0 };
    });
  }

  reorderLine(begin: number, end: number): readonly BidiVisual[] {
    return this.#call((arena, handle) => {
      const line = arena.value("NtByteSpan");
      setNativeField(arena, line, "NtByteSpan", "begin", toU32(begin));
      setNativeField(arena, line, "NtByteSpan", "end", toU32(end));
      const type = "NtBidiVisual";
      return readNativeRows(arena, "nt_bidi_reorder_line", type, [handle, line], value =>
        ({ ...nativeSourceSpan(arena, value, type), logicalIndex: nativeField(arena, value, type, "logical_index"),
          displayScalar: nativeField(arena, value, type, "display_scalar"), level: nativeField(arena, value, type, "level") }));
    });
  }

  close(): void {
    if (this.#handle !== 0) {
      this.#call((arena, handle) => arena.call("nt_bidi_destroy", handle));
      this.#handle = 0;
    }
  }
}

function floatSlot(arena: RawArena, value: number): number {
  const address = arena.value("double");
  arena.setFloat64(address, value);
  return address;
}

function timeSlot(arena: RawArena, value: bigint): number {
  const address = arena.value("uint64_t");
  arena.setUnsigned(address, arena.api.size("uint64_t"), toU64(value));
  return address;
}

function animationCall(runtime: Runtime, arena: RawArena, name: string, ...args: number[]): void {
  const result = arena.call(name, ...args);
  runtime.check(Number(arena.unsigned(result, arena.api.size("NtStatus"))) | 0);
}

function withAnimationValue<T>(runtime: Runtime, type: "NtPlayback" | "NtSpring", bytes: Uint8Array | undefined,
  call: (arena: RawArena, value: number, pointer: number) => T): T {
  return runtime.raw.withArena(arena => {
    const value = arena.value(type);
    if (bytes) arena.write(value, bytes);
    return call(arena, value, pointerSlot(arena, value));
  });
}

function playbackFromNative(runtime: Runtime, arena: RawArena, address: number): Playback {
  const type = "NtPlayback";
  const bytes = arena.read(address, arena.api.size(type));
  const transition = (name: string, nowUs: bigint, ...times: bigint[]): Playback =>
    withAnimationValue(runtime, type, bytes, (arena, value, pointer) => {
      animationCall(runtime, arena, name, pointer, timeSlot(arena, nowUs), ...times.map(time => timeSlot(arena, time)));
      return playbackFromNative(runtime, arena, value);
    });
  return Object.freeze<Playback>({
    state: nativeField(arena, address, type, "state") as Playback["state"],
    elapsed(nowUs) {
      return withAnimationValue(runtime, type, bytes, (arena, _value, pointer) => {
        const out = arena.value("uint64_t");
        animationCall(runtime, arena, "nt_playback_elapsed", pointer, timeSlot(arena, nowUs), pointerSlot(arena, out));
        return arena.unsigned(out, arena.api.size("uint64_t"));
      });
    },
    pause: nowUs => transition("nt_playback_pause", nowUs),
    resume: nowUs => transition("nt_playback_resume", nowUs),
    seek: (nowUs, elapsedUs) => transition("nt_playback_seek", nowUs, elapsedUs),
    cancel: nowUs => transition("nt_playback_cancel", nowUs),
  });
}

function springFromNative(runtime: Runtime, arena: RawArena, address: number): Spring {
  const type = "NtSpring";
  const bytes = arena.read(address, arena.api.size(type));
  return Object.freeze<Spring>({
    sample(nowUs, reducedMotion = false) {
      return withAnimationValue(runtime, type, bytes, (arena, _value, pointer) => {
        const sample = "NtSpringSample";
        const out = arena.value(sample);
        animationCall(runtime, arena, "nt_spring_sample", pointer, timeSlot(arena, nowUs),
          boolSlot(arena, reducedMotion), pointerSlot(arena, out));
        return {
          value: arena.float64(out + arena.api.field(sample, "value").offset),
          velocity: arena.float64(out + arena.api.field(sample, "velocity").offset),
          phase: nativeField(arena, out, sample, "phase") as SpringSample["phase"],
        };
      });
    },
    retarget(nowUs, target, policy) {
      return withAnimationValue(runtime, type, bytes, (arena, value, pointer) => {
        const policySlot = arena.value("NtRetargetPolicy");
        arena.setUnsigned(policySlot, arena.api.size("NtRetargetPolicy"), BigInt(toU32(policy)));
        animationCall(runtime, arena, "nt_spring_retarget", pointer, timeSlot(arena, nowUs),
          floatSlot(arena, target), policySlot);
        return springFromNative(runtime, arena, value);
      });
    },
  });
}

function thermalStore(arena: RawArena, address: number): ThermalStore {
  const type = "NtThermalStore";
  return { heatCapacity: arena.float64(address + arena.api.field(type, "heat_capacity").offset),
    energy: arena.float64(address + arena.api.field(type, "energy").offset) };
}

function setNativeField(arena: RawArena, address: number, type: string, field: string, value: number | bigint): void {
  const { offset, size } = arena.api.field(type, field);
  arena.setUnsigned(address + offset, size, BigInt(value));
}

function nativeField(arena: RawArena, address: number, type: string, field: string, signed = false): number {
  const { offset, size } = arena.api.field(type, field);
  const bits = arena.unsigned(address + offset, size);
  return Number(signed ? BigInt.asIntN(8 * size, bits) : bits);
}

function nativeBounds(arena: RawArena, address: number): Bounds {
  const point = address + arena.api.field("NtCellRect", "origin").offset;
  const size = address + arena.api.field("NtCellRect", "size").offset;
  return {
    origin: nativeCellPoint(arena, point),
    size: nativeSize(arena, size),
  };
}

function nativeCellPoint(arena: RawArena, address: number): Point {
  return { x: nativeField(arena, address, "NtCellPoint", "x", true),
    y: nativeField(arena, address, "NtCellPoint", "y", true) };
}

function writeCellPoint(arena: RawArena, address: number, point: Point): void {
  setNativeField(arena, address, "NtCellPoint", "x", toI32(point.x));
  setNativeField(arena, address, "NtCellPoint", "y", toI32(point.y));
}

function cellPointSlot(arena: RawArena, point: Point): number {
  const out = arena.value("NtCellPoint");
  writeCellPoint(arena, out, point);
  return out;
}

function nativeSize(arena: RawArena, address: number): Size {
  return { columns: nativeField(arena, address, "NtCellSize", "columns"),
    rows: nativeField(arena, address, "NtCellSize", "rows") };
}

function writeNativeSize(arena: RawArena, address: number, size: Size): void {
  setNativeField(arena, address, "NtCellSize", "columns", toU32(size.columns));
  setNativeField(arena, address, "NtCellSize", "rows", toU32(size.rows));
}

function writeNativeBounds(arena: RawArena, address: number, bounds: Bounds): void {
  const point = address + arena.api.field("NtCellRect", "origin").offset;
  writeCellPoint(arena, point, bounds.origin);
  writeNativeSize(arena, address + arena.api.field("NtCellRect", "size").offset, bounds.size);
}

function nativeNode(scope: RawTree, address: number): Node | undefined {
  return nativeField(scope.arena, address, "NtNodeId", "tree") ? scope.adopt(address) : undefined;
}

function nativeGeometry(scope: RawTree, address: number): Geometry {
  const arena = scope.arena;
  const type = "NtNodeGeometry";
  return {
    parent: nativeNode(scope, address + arena.api.field(type, "parent").offset),
    bounds: nativeBounds(arena, address + arena.api.field(type, "bounds").offset),
    clip: nativeBounds(arena, address + arena.api.field(type, "clip").offset),
    placed: nativeField(arena, address, type, "placed") !== 0,
    enabled: nativeField(arena, address, type, "enabled") !== 0,
  };
}

const propertyKind = { bool: 0, signed: 1, unsigned: 2, real: 3, text: 4, color: 5 } as const;

type NativeNodes = Pick<RawTree, "arena" | "node">;

function propertyReference(scope: NativeNodes, node: Node, id: number): number {
  const arena = scope.arena;
  const type = "NtPropertyRef";
  const reference = arena.value(type);
  const { offset, size } = arena.api.field(type, "node");
  arena.write(reference + offset, arena.read(scope.node(node), size));
  setNativeField(arena, reference, type, "id", toU32(id));
  return reference;
}

function propertyInput(arena: RawArena, input: PropertyValue): number {
  const type = "NtValue";
  const address = arena.value(type);
  setNativeField(arena, address, type, "kind", toU32(propertyKind[input.kind]));
  switch (input.kind) {
    case "bool":
      if (typeof input.value !== "boolean") throw new TypeError("Expected a Boolean property value");
      setNativeField(arena, address, type, "as.flag", input.value ? 1 : 0);
      break;
    case "signed": {
      const { offset, size } = arena.api.field(type, "as.i64");
      arena.setUnsigned(address + offset, size, toI64(input.value));
      break;
    }
    case "unsigned": {
      const { offset, size } = arena.api.field(type, "as.u64");
      arena.setUnsigned(address + offset, size, toU64(input.value));
      break;
    }
    case "real":
      arena.setFloat64(address + arena.api.field(type, "as.real").offset, input.value);
      break;
    case "text": {
      const { offset, size } = arena.api.field(type, "as.text");
      arena.write(address + offset, arena.read(arena.byteView(input.value), size));
      break;
    }
    case "color":
      setNativeField(arena, address, type, "as.rgba", packedColor(input.value));
      break;
  }
  return address;
}

function copyNativeValue(arena: RawArena, type: string, destination: number, source: number): void {
  arena.write(destination, arena.read(source, arena.api.size(type)));
}

function valueArray(arena: RawArena, type: string, count: number): number {
  return count === 0 ? 0 : arena.allocate(arena.api.size(type) * count);
}

function writePropertyRef(scope: NativeNodes, address: number, property: PropertyRef): void {
  copyNativeValue(scope.arena, "NtPropertyRef", address, propertyReference(scope, property.node, property.id));
}

function propertyRefArray(scope: NativeNodes, properties: readonly PropertyRef[]): number {
  const arena = scope.arena;
  const type = "NtPropertyRef";
  const size = arena.api.size(type);
  const refs = valueArray(arena, type, properties.length);
  for (const [i, property] of properties.entries()) {
    writePropertyRef(scope, refs + i * size, property);
  }
  return refs;
}

function assignmentArray(scope: RawTree, assignments: readonly PropertyAssignment[]): number {
  const arena = scope.arena;
  const type = "NtPropertyAssignment";
  const size = arena.api.size(type);
  const property = arena.api.field(type, "property").offset;
  const value = arena.api.field(type, "value").offset;
  const address = valueArray(arena, type, assignments.length);
  for (const [index, assignment] of assignments.entries()) {
    const entry = address + index * size;
    writePropertyRef(scope, entry + property, assignment.property);
    copyNativeValue(arena, "NtValue", entry + value, propertyInput(arena, assignment.value));
  }
  return address;
}

function bindingTargetArray(scope: RawTree, targets: readonly BindingTarget[]): number {
  const arena = scope.arena;
  const type = "NtBindingTarget";
  const size = arena.api.size(type);
  const property = arena.api.field(type, "property").offset;
  const address = valueArray(arena, type, targets.length);
  for (const [index, target] of targets.entries()) {
    const entry = address + index * size;
    writePropertyRef(scope, entry + property, target.property);
    setNativeField(arena, entry, type, "value", toU32(target.value));
  }
  return address;
}

const bindingSource = { input: 0, property: 1, computed: 2 } as const;

function bindingValueArray(scope: RawTree, values: readonly BindingValue[],
  retain: (compute: BindingCompute) => RawCallbacks): number {
  const arena = scope.arena;
  const type = "NtBindingValue";
  const size = arena.api.size(type);
  const address = valueArray(arena, type, values.length);
  for (const [index, value] of values.entries()) {
    const entry = address + index * size;
    setNativeField(arena, entry, type, "kind", toU32(propertyKind[value.kind]));
    setNativeField(arena, entry, type, "source", toU32(bindingSource[value.source]));
    if (value.source === "property") {
      writePropertyRef(scope, entry + arena.api.field(type, "from.property").offset, value.property);
    } else if (value.source === "computed") {
      const deps = Array.from(value.dependencies);
      const indices = valueArray(arena, "size_t", deps.length);
      const indexSize = arena.api.size("size_t");
      for (const [index, dependency] of deps.entries()) {
        arena.setUnsigned(indices + index * indexSize, indexSize, BigInt(toU32(dependency)));
      }
      const handler = retain(value.compute);
      setNativeField(arena, entry, type, "from.computed.dependencies", indices);
      setNativeField(arena, entry, type, "from.computed.count", deps.length);
      setNativeField(arena, entry, type, "from.computed.evaluate", handler.function("NtBindingCompute"));
      setNativeField(arena, entry, type, "from.computed.context", handler.context);
    }
  }
  return address;
}

function callbackProperties(runtime: Runtime, frame: RawCallbackFrame, index: number): PropertyValue[] {
  const arena = frame.arena;
  const args = arena.pointer(frame.argument(index));
  const count = Number(arena.unsigned(frame.argument(index + 1), arena.api.size("size_t")));
  const size = arena.api.size("NtValue");
  return Array.from({ length: count }, (_, i) => propertyOutput(runtime, arena, args + i * size));
}

function callbackFailureStatus(runtime: Runtime, frame: RawCallbackFrame, code: number): undefined {
  if (code === 0) throw runtime.failure(1);
  const arena = frame.arena;
  const result = arena.value("NtResult");
  arena.setUnsigned(result, arena.api.size("NtResult"), BigInt(toI32(code)));
  return frame.returnValue(result);
}

function writeCallbackProperty(frame: RawCallbackFrame, outer: RawArena, index: number, value: PropertyValue): void {
  // The callback frame expires before C copies borrowed text.
  const native = propertyInput(outer, value);
  const arena = frame.arena;
  const out = arena.value("NtValue");
  copyNativeValue(arena, "NtValue", out, native);
  frame.writeOutput(index, out, arena.api.size("NtValue"));
}

function bindingEvaluator(runtime: Runtime, compute: BindingCompute, applicationArena: () => RawArena): RawCallbacks {
  if (typeof compute !== "function") throw runtime.failure(1);
  return runtime.raw.callbacks({
    NtBindingCompute(frame) {
      const result = synchronousCompositionValue(compute(callbackProperties(runtime, frame, 0)),
        "Binding compute must return synchronously");
      if (typeof result === "number") return callbackFailureStatus(runtime, frame, result);
      writeCallbackProperty(frame, applicationArena(), 2, result);
    },
  });
}

function propertyOutput(runtime: Runtime, arena: RawArena, address: number): PropertyValue {
  const type = "NtValue";
  switch (nativeField(arena, address, type, "kind")) {
    case propertyKind.bool:
      return { kind: "bool", value: nativeField(arena, address, type, "as.flag") !== 0 };
    case propertyKind.signed: {
      const { offset, size } = arena.api.field(type, "as.i64");
      return { kind: "signed", value: BigInt.asIntN(64, arena.unsigned(address + offset, size)) };
    }
    case propertyKind.unsigned: {
      const { offset, size } = arena.api.field(type, "as.u64");
      return { kind: "unsigned", value: arena.unsigned(address + offset, size) };
    }
    case propertyKind.real:
      return { kind: "real", value: arena.float64(address + arena.api.field(type, "as.real").offset) };
    case propertyKind.text: {
      const view = address + arena.api.field(type, "as.text").offset;
      return { kind: "text", value: runtime.decode(nativeField(arena, view, "NtByteView", "data"),
        nativeField(arena, view, "NtByteView", "count")) };
    }
    case propertyKind.color: {
      const { offset, size } = arena.api.field(type, "as.rgba");
      return { kind: "color", value: colorAt(new DataView(arena.read(address + offset, size).buffer), 0) };
    }
    default:
      throw runtime.failure(7);
  }
}

const propertyKindNames = new Map(Object.entries(propertyKind).map(([kind, code]) => [code as number, kind as PropertyValue["kind"]]));
const propertyInvalidations = new Map(Object.entries(invalidationCode).map(([kind, code]) => [code as number, kind as Invalidation]));

function propertyProvider(runtime: Runtime, descriptors: ReadonlyMap<number, PropertyDescriptor>,
  failure: (phase: CallbackFailure["phase"], cause: unknown) => void): RawCallbacks {
  const pending = new Map<number, unknown>();
  const id = (frame: RawCallbackFrame) =>
    Number(frame.arena.unsigned(frame.argument(0), frame.arena.api.size("NtPropertyId")));
  const invoke = (frame: RawCallbackFrame, phase: "propertyRead" | "propertyPrepare" | "propertyFinish",
    work: () => undefined): undefined => {
    try {
      return runtime.withoutComponentContext(work);
    } catch (cause) {
      failure(phase, cause);
      if (phase !== "propertyFinish") return callbackFailureStatus(runtime, frame, 9);
    }
  };
  const handlers = runtime.raw.callbacks({
    NtPropertyProviderRead(frame) {
      return invoke(frame, "propertyRead", () => {
        const descriptor = descriptors.get(id(frame))!;
        const value = synchronousCompositionValue(descriptor.read(), "Property read must return synchronously");
        if (typeof value === "number") return callbackFailureStatus(runtime, frame, value);
        const arena = frame.arena;
        return frame.returnValue(arena.call("nt_property_output_set", frame.argument(1), propertyInput(arena, value)));
      });
    },
    NtPropertyProviderPrepare(frame) {
      return invoke(frame, "propertyPrepare", () => {
        const prepare = descriptors.get(id(frame))!.prepare;
        if (!prepare) return undefined;
        const arena = frame.arena;
        const result = synchronousCompositionValue(prepare(propertyOutput(runtime, arena, frame.argument(1))),
          "Property preparation must return synchronously");
        if (result === undefined || result === 0) return undefined;
        if (typeof result === "number") return callbackFailureStatus(runtime, frame, result);
        const token = pending.size + 1;
        const prepared = result.prepared;
        frame.writeOutput(2, pointerSlot(arena, token), arena.api.size("pointer"));
        pending.set(token, prepared);
        return undefined;
      });
    },
    NtPropertyProviderFinish(frame) {
      return invoke(frame, "propertyFinish", () => {
        const arena = frame.arena;
        const token = arena.pointer(frame.argument(2));
        const prepared = pending.get(token);
        pending.delete(token);
        const value = propertyOutput(runtime, arena, frame.argument(1));
        const commit = arena.unsigned(frame.argument(3), arena.api.size("_Bool")) !== 0n;
        const finish = descriptors.get(id(frame))!.finish!;
        synchronousCompositionValue(finish(value, prepared, commit), "Property finish must return synchronously");
        return undefined;
      });
    },
    NtPropertyProviderRelease() {
      pending.clear();
      handlers.close();
    },
  });
  return handlers;
}

class OwnedStateSnapshot implements StateSnapshot {
  readonly #runtime: Runtime;
  #handle: number;
  #uses = 0;
  #closed = false;

  constructor(runtime: Runtime, handle: number) {
    this.#runtime = runtime;
    this.#handle = handle;
  }

  static use<T>(runtime: Runtime, snapshot: StateSnapshot, call: (handle: number) => T): T {
    if (!snapshot || typeof snapshot !== "object" || !(#handle in snapshot) || snapshot.#runtime !== runtime) {
      throw runtime.failure(1);
    }
    return snapshot.#use(call);
  }

  #use<T>(call: (handle: number) => T): T {
    if (this.#closed) throw this.#runtime.failure(5);
    this.#uses++;
    try {
      return call(this.#handle);
    } finally {
      this.#uses--;
      this.#destroyIfUnused();
    }
  }

  get bytes(): Uint8Array {
    return this.#use(handle => this.#runtime.raw.withArena(arena => {
      const view = arena.call("nt_state_snapshot_bytes", pointerSlot(arena, handle));
      return readByteViewBytes(arena, view);
    }));
  }

  #destroyIfUnused(): void {
    if (!this.#closed || this.#uses || !this.#handle) return;
    this.#runtime.raw.withArena(arena => arena.call("nt_state_snapshot_destroy", pointerSlot(arena, this.#handle)));
    this.#handle = 0;
  }

  close(): void {
    this.#closed = true;
    this.#destroyIfUnused();
  }
}

function importStateSnapshot(runtime: Runtime, bytes: Uint8Array): StateSnapshot {
  if (!ArrayBuffer.isView(bytes) || Object.prototype.toString.call(bytes) !== "[object Uint8Array]") {
    throw runtime.failure(1);
  }
  return runtime.raw.withArena(arena => {
    const out = arena.value("pointer");
    arena.result("nt_state_snapshot_import", pointerSlot(arena, 0), arena.byteView(bytes), pointerSlot(arena, out));
    return new OwnedStateSnapshot(runtime, arena.pointer(out));
  });
}

function writeByteSpan(arena: RawArena, address: number, span: ByteSpan): void {
  setNativeField(arena, address, "NtByteSpan", "begin", toU32(span.begin));
  setNativeField(arena, address, "NtByteSpan", "end", toU32(span.end));
}

function writeStyleTags(arena: RawArena, address: number, type: string,
  value: Pick<StyleElement, "role" | "region">): void {
  setNativeField(arena, address, type, "role", toU64(value.role ?? 0n));
  setNativeField(arena, address, type, "region", toU64(value.region ?? 0n));
}

function writeStyleElement(arena: RawArena, address: number, element: StyleElement): void {
  const type = "NtStyleElement";
  writeStyleTags(arena, address, type, element);
  setNativeField(arena, address, type, "states", toU64(element.states ?? 0n));
}

function writeStyleCondition(arena: RawArena, address: number, condition: StyleCondition): void {
  const type = "NtStyleCondition";
  writeStyleTags(arena, address, type, condition);
  setNativeField(arena, address, type, "require", toU64(condition.require ?? 0n));
  setNativeField(arena, address, type, "forbid", toU64(condition.forbid ?? 0n));
}

const styleRelation = { self: 0, parent: 1, ancestor: 2 } as const;

function writeStyleSelector(runtime: Runtime, arena: RawArena, address: number, selector: StyleSelector): void {
  const type = "NtStyleSelector";
  writeStyleCondition(arena, address + arena.api.field(type, "subject").offset, selector.subject ?? {});
  writeStyleCondition(arena, address + arena.api.field(type, "ancestor").offset, selector.ancestor ?? {});
  setNativeField(arena, address, type, "relation", enumCode(runtime, styleRelation, selector.relation ?? "self"));
  setNativeField(arena, address, type, "text_region", toU64(selector.textRegion ?? 0n));
  setNativeField(arena, address, type, "cluster_id", toU64(selector.clusterId ?? 0n));
  const bytes = selector.bytes;
  if (bytes !== undefined) {
    setNativeField(arena, address, type, "match_bytes", 1);
    writeByteSpan(arena, address + arena.api.field(type, "bytes").offset, bytes);
  }
  const characters = selector.characters;
  if (characters !== undefined) {
    setNativeField(arena, address, type, "match_characters", 1);
    setNativeField(arena, address, type, "first_scalar", toU32(characters.first));
    setNativeField(arena, address, type, "last_scalar", toU32(characters.last));
  }
}

function createStyleSheet(runtime: Runtime, rules: readonly StyleRule[]): StyleSheet {
  return runtime.raw.withArena(arena => {
    const entries = Array.from(rules);
    const type = "NtStyleRule";
    const size = arena.api.size(type);
    const selector = arena.api.field(type, "selector").offset;
    const value = arena.api.field(type, "value").offset;
    const data = valueArray(arena, type, entries.length);
    for (const [i, rule] of entries.entries()) {
      const entry = data + i * size;
      writeStyleSelector(runtime, arena, entry + selector, rule.selector ?? {});
      setNativeField(arena, entry, type, "property", toU32(rule.property));
      setNativeField(arena, entry, type, "override", rule.override ? 1 : 0);
      copyNativeValue(arena, "NtValue", entry + value, propertyInput(arena, rule.value));
    }
    const out = arena.value("pointer");
    arena.result("nt_style_sheet_create", pointerSlot(arena, 0), pointerSlot(arena, data), sizeSlot(arena, entries.length),
      pointerSlot(arena, out));
    return new OwnedStyleSheet(runtime, arena.pointer(out));
  });
}

class OwnedStyleSheet implements StyleSheet {
  readonly #runtime: Runtime;
  #handle: number;
  #uses = 0;
  #closed = false;

  constructor(runtime: Runtime, handle: number) {
    this.#runtime = runtime;
    this.#handle = handle;
  }

  static acquire(runtime: Runtime, sheet: StyleSheet): { handle: number; release: () => void } {
    if (!sheet || typeof sheet !== "object" || !(#handle in sheet) || sheet.#runtime !== runtime) {
      throw runtime.failure(1);
    }
    if (sheet.#closed) throw runtime.failure(5);
    sheet.#uses++;
    return {
      handle: sheet.#handle,
      release: () => {
        sheet.#uses--;
        sheet.#destroyIfUnused();
      },
    };
  }

  #destroyIfUnused(): void {
    if (!this.#closed || this.#uses || !this.#handle) return;
    this.#runtime.raw.withArena(arena => arena.call("nt_style_sheet_destroy", pointerSlot(arena, this.#handle)));
    this.#handle = 0;
  }

  close(): void {
    this.#closed = true;
    this.#destroyIfUnused();
  }
}

function withStyleSheets<T>(runtime: Runtime, arena: RawArena, address: number,
  sheets: readonly StyleSheet[], call: () => T): T {
  const entries = Array.from(sheets);
  const size = arena.api.size("pointer");
  const items = valueArray(arena, "pointer", entries.length);
  const releases: (() => void)[] = [];
  try {
    for (const [index, sheet] of entries.entries()) {
      const { handle, release } = OwnedStyleSheet.acquire(runtime, sheet);
      releases.push(release);
      arena.setPointer(items + index * size, handle);
    }
    setNativeField(arena, address, "NtStyleSheets", "items", items);
    setNativeField(arena, address, "NtStyleSheets", "count", entries.length);
    return call();
  } finally {
    for (let index = releases.length - 1; index >= 0; index--) releases[index]();
  }
}

function writeStylePath(arena: RawArena, address: number, path: StylePath): void {
  const type = "NtStylePath";
  writeStyleElement(arena, address + arena.api.field(type, "element").offset, path.element ?? {});
  const entries = Array.from(path.ancestors ?? []);
  const size = arena.api.size("NtStyleElement");
  const data = valueArray(arena, "NtStyleElement", entries.length);
  for (const [i, ancestor] of entries.entries()) writeStyleElement(arena, data + i * size, ancestor);
  setNativeField(arena, address, type, "ancestors", data);
  setNativeField(arena, address, type, "ancestor_count", entries.length);
}

function writeStyleRegions(arena: RawArena, address: number, containerType: string, regions: readonly StyleRegion[]): void {
  const entries = Array.from(regions);
  const type = "NtStyleRegion";
  const size = arena.api.size(type);
  const bytes = arena.api.field(type, "bytes").offset;
  const data = valueArray(arena, type, entries.length);
  for (const [i, region] of entries.entries()) {
    const entry = data + i * size;
    setNativeField(arena, entry, type, "tag", toU64(region.tag));
    writeByteSpan(arena, entry + bytes, region.bytes);
  }
  setNativeField(arena, address, containerType, "regions", data);
  setNativeField(arena, address, containerType, "region_count", entries.length);
}

function resolveStyle(runtime: Runtime, sheets: readonly StyleSheet[], subject: StyleSubject,
  property: number): PropertyValue | undefined {
  return runtime.raw.withArena(arena => {
    const type = "NtStyleSubject";
    const input = arena.value(type);
    writeStylePath(arena, input + arena.api.field(type, "path").offset, subject.path ?? {});
    copyNativeValue(arena, "NtByteView", input + arena.api.field(type, "source").offset,
      arena.byteView(subject.source ?? ""));
    const text = subject.text;
    if (text !== undefined) writeByteSpan(arena, input + arena.api.field(type, "text").offset, text);
    setNativeField(arena, input, type, "cluster_id", toU64(subject.clusterId ?? 0n));
    writeStyleRegions(arena, input, type, subject.regions ?? []);
    const nativeSheets = arena.value("NtStyleSheets");
    const propertySlot = enumSlot(arena, "NtPropertyId", property);
    const matched = boolSlot(arena, false);
    const out = arena.value("NtValue");
    return withStyleSheets(runtime, arena, nativeSheets, sheets, () => {
      arena.result("nt_style_resolve", nativeSheets, pointerSlot(arena, input), propertySlot,
        pointerSlot(arena, matched), pointerSlot(arena, out));
      return arena.unsigned(matched, arena.api.size("_Bool")) !== 0n ? propertyOutput(runtime, arena, out) : undefined;
    });
  });
}

function drawStyledText(runtime: Runtime, arena: RawArena, name: string, view: number, at: Point,
  layout: TextLayout, options: TextPaintOptions, styles: TextStyles, beforeDraw?: () => void): void {
  const type = "NtTextStyles";
  const spec = arena.value(type);
  const position = cellPointSlot(arena, at);
  const settings = textPaintOptions(runtime, arena, options);
  writeStylePath(arena, spec + arena.api.field(type, "path").offset, styles.path ?? {});
  writeStyleRegions(arena, spec, type, styles.regions ?? []);
  withStyleSheets(runtime, arena, spec + arena.api.field(type, "sheets").offset, styles.sheets, () => {
    beforeDraw?.();
    OwnedTextLayout.use(runtime, layout, handle =>
      arena.result(name, view, position, pointerSlot(arena, handle), settings, pointerSlot(arena, spec)));
  });
}

function intentFieldArray(arena: RawArena, fields: readonly IntentField[]): number {
  const type = "NtIntentField";
  const size = arena.api.size(type);
  const value = arena.api.field(type, "value").offset;
  const address = valueArray(arena, type, fields.length);
  for (const [index, field] of fields.entries()) {
    const out = address + index * size;
    setNativeField(arena, out, type, "id", toU32(field.id));
    copyNativeValue(arena, "NtValue", out + value, propertyInput(arena, field.value));
  }
  return address;
}

function readEventIntent(runtime: Runtime, arena: RawArena, address: number,
  node: (address: number) => Node | undefined): EventIntent {
  const type = "NtEventIntentView";
  const viewType = "NtIntentView";
  const intent = address + arena.api.field(type, "intent").offset;
  const view = intent + arena.api.field(viewType, "payload").offset;
  const common = {
    source: node(intent + arena.api.field(viewType, "source").offset),
    kind: nativeField(arena, intent, viewType, "kind"),
    payload: readByteViewBytes(arena, view),
  };
  if (nativeField(arena, address, type, "payload_kind") === 0) return { ...common, payloadKind: "bytes" };
  const fieldType = "NtIntentField";
  const size = arena.api.size(fieldType);
  const value = arena.api.field(fieldType, "value").offset;
  const data = nativeField(arena, address, type, "fields");
  const count = nativeField(arena, address, type, "field_count");
  const fields = Array.from({ length: count }, (_, i) => {
    const entry = data + i * size;
    return { id: nativeField(arena, entry, fieldType, "id"), value: propertyOutput(runtime, arena, entry + value) };
  });
  return { ...common, payloadKind: "fields", fields };
}

function readObservedInput(runtime: Runtime, arena: RawArena, address: number): ObservedInput {
  const type = "NtEvent";
  const kind = nativeField(arena, address, type, "kind") as EventKindValue;
  const at = (field: string) => address + arena.api.field(type, "data." + field).offset;
  switch (kind) {
    case EventKind.key: {
      const key = at("key");
      return { kind, key: { code: nativeField(arena, key, "NtKeyInput", "key"),
        modifiers: nativeField(arena, key, "NtKeyInput", "modifiers"),
        state: nativeField(arena, key, "NtKeyInput", "state") } };
    }
    case EventKind.text:
    case EventKind.paste:
      return { kind, text: readByteView(runtime, arena, at("text")) };
    case EventKind.pointerDown:
    case EventKind.pointerMove:
    case EventKind.pointerUp: {
      const pointer = at("pointer");
      const pointerType = "NtPointerInput";
      const id = arena.api.field(pointerType, "id");
      return { kind, pointer: arena.unsigned(pointer + id.offset, id.size),
        at: nativeField(arena, pointer, pointerType, "has_position")
          ? nativeCellPoint(arena, pointer + arena.api.field(pointerType, "position").offset) : undefined,
        button: nativeField(arena, pointer, pointerType, "button"),
        pressed: nativeField(arena, pointer, pointerType, "pressed"),
        modifiers: nativeField(arena, pointer, pointerType, "modifiers") };
    }
    case EventKind.wheel: {
      const wheel = at("wheel");
      const wheelType = "NtWheelInput";
      return { kind, at: nativeCellPoint(arena, wheel + arena.api.field(wheelType, "position").offset),
        dx: nativeField(arena, wheel, wheelType, "dx", true),
        dy: nativeField(arena, wheel, wheelType, "dy", true),
        modifiers: nativeField(arena, wheel, wheelType, "modifiers") };
    }
    case EventKind.pointerCancel: {
      const cancel = at("pointer_cancel");
      const id = arena.api.field("NtPointerCancel", "id");
      return { kind, pointer: arena.unsigned(cancel + id.offset, id.size),
        reason: nativeField(arena, cancel, "NtPointerCancel", "reason") };
    }
    case EventKind.hostCancel:
      return { kind, reason: nativeField(arena, address, type, "data.reason") };
    case EventKind.resize:
      return { kind, size: nativeSize(arena, at("resize")) };
    case EventKind.focusGain:
    case EventKind.focusLoss:
      return { kind };
    default:
      throw runtime.failure(1);
  }
}

interface EventScope {
  readonly context: number;
  observe(): RuleEvent;
  node(arena: RawArena, node: Node): number;
  fail(phase: "eventPredicate" | "eventDerive", cause: unknown): void;
}

function eventRuleHandlers(runtime: Runtime, outer: RawArena, event: EventScope,
  predicate: EventPredicate | undefined, derives: readonly EventDerive[]): RawCallbacks {
  let next = 0;
  const handlers: Record<string, (frame: RawCallbackFrame) => undefined> = {};
  if (predicate) handlers.NtEventPredicate = frame => {
    try {
      const matched = synchronousCompositionValue(predicate(event.observe(), callbackProperties(runtime, frame, 1)),
        "Event predicate must return synchronously");
      if (typeof matched === "number") return callbackFailureStatus(runtime, frame, matched);
      if (typeof matched !== "boolean") throw new TypeError("Expected a Boolean event predicate");
      frame.writeOutput(3, boolSlot(frame.arena, matched), frame.arena.api.size("_Bool"));
      return undefined;
    } catch (cause) {
      event.fail("eventPredicate", cause);
      return callbackFailureStatus(runtime, frame, 9);
    }
  };
  if (derives.length) handlers.NtEventDerive = frame => {
    try {
      const result = synchronousCompositionValue(derives[next++](event.observe(), callbackProperties(runtime, frame, 1)),
        "Event derivation must return synchronously");
      if (typeof result === "number") return callbackFailureStatus(runtime, frame, result);
      writeCallbackProperty(frame, outer, 3, result);
      return undefined;
    } catch (cause) {
      event.fail("eventDerive", cause);
      return callbackFailureStatus(runtime, frame, 9);
    }
  };
  return runtime.raw.callbacks(handlers);
}

function applyEventRules(runtime: Runtime, event: EventScope, rules: readonly EventRule[]): number {
  const callbacks: RawCallbacks[] = [];
  try {
    return runtime.raw.withArena(arena => {
      const items = Array.from(rules);
      const type = "NtEventRule";
      const size = arena.api.size(type);
      const rulesData = valueArray(arena, type, items.length);
      const observed = event.observe();
      for (const [index, rule] of items.entries()) {
        const address = rulesData + index * size;
        const kind = toU32(rule.event);
        const phase = toU32(rule.phase);
        const properties = Array.from(rule.properties ?? []);
        const propertyType = "NtPropertyRef";
        const propertySize = arena.api.size(propertyType);
        const propertyData = valueArray(arena, propertyType, properties.length);
        if (kind === observed.input.kind && phase === observed.phase) {
          const nodes = { arena, node: (node: Node) => event.node(arena, node) };
          for (const [i, property] of properties.entries()) {
            writePropertyRef(nodes, propertyData + i * propertySize, property);
          }
        }

        const fields = Array.from(rule.fields ?? []);
        const fieldType = "NtEventFieldRule";
        const fieldSize = arena.api.size(fieldType);
        const fieldData = valueArray(arena, fieldType, fields.length);
        const derives = fields.map(field => field.derive);
        const predicate = rule.predicate;
        if (predicate !== undefined && typeof predicate !== "function") throw runtime.failure(1);
        if (predicate || fields.length) {
          const handler = eventRuleHandlers(runtime, arena, event, predicate, derives);
          callbacks.push(handler);
          setNativeField(arena, address, type, "context", handler.context);
          if (predicate) setNativeField(arena, address, type, "predicate", handler.function("NtEventPredicate"));
          for (const [i, field] of fields.entries()) {
            const entry = fieldData + i * fieldSize;
            setNativeField(arena, entry, fieldType, "id", toU32(field.id));
            setNativeField(arena, entry, fieldType, "kind", toU32(propertyKind[field.kind]));
            if (typeof derives[i] === "function") {
              setNativeField(arena, entry, fieldType, "derive", handler.function("NtEventDerive"));
            }
          }
        }
        setNativeField(arena, address, type, "event", kind);
        setNativeField(arena, address, type, "phase", phase);
        setNativeField(arena, address, type, "properties", propertyData);
        setNativeField(arena, address, type, "property_count", properties.length);
        setNativeField(arena, address, type, "intent_kind", toU32(rule.intentKind));
        setNativeField(arena, address, type, "fields", fieldData);
        setNativeField(arena, address, type, "field_count", fields.length);
      }
      const emitted = sizeSlot(arena, 0);
      arena.result("nt_event_apply_rules", pointerSlot(arena, event.context), pointerSlot(arena, rulesData),
        sizeSlot(arena, items.length), pointerSlot(arena, emitted));
      return Number(arena.unsigned(emitted, arena.api.size("size_t")));
    });
  } finally {
    for (const handler of callbacks) handler.close();
  }
}

function readCaret(arena: RawArena, address: number): Caret {
  return {
    offset: nativeField(arena, address, "NtCaret", "offset"),
    affinity: nativeField(arena, address, "NtCaret", "affinity") === 0 ? "leading" : "trailing",
  };
}

function writeCaret(arena: RawArena, address: number, caret: Caret): void {
  setNativeField(arena, address, "NtCaret", "offset", toU32(caret.offset));
  setNativeField(arena, address, "NtCaret", "affinity", toU32(({ leading: 0, trailing: 1 })[caret.affinity]));
}

const editorMove = {
  previousCluster: 0, nextCluster: 1, lineStart: 2, lineEnd: 3, documentStart: 4, documentEnd: 5,
} as const;

function readByteViewBytes(arena: RawArena, address: number): Uint8Array {
  return arena.read(nativeField(arena, address, "NtByteView", "data"),
    nativeField(arena, address, "NtByteView", "count"));
}

function readByteView(runtime: Runtime, arena: RawArena, address: number): string {
  return runtime.decode(nativeField(arena, address, "NtByteView", "data"),
    nativeField(arena, address, "NtByteView", "count"));
}

function editorKeyMap(arena: RawArena, map: EditorKeyMap, registers: number): number {
  const type = "NtEditorKeyMap";
  const address = arena.value(type);
  const data = map.bindings;
  const count = data.length;
  const bindingType = "NtEditorBinding";
  const size = arena.api.size(bindingType);
  const entries = count ? arena.allocate(count * size) : 0;
  for (let index = 0; index < count; index++) {
    const binding = data[index];
    const entry = entries + index * size;
    const chord = entry + arena.api.field(bindingType, "chord").offset;
    setNativeField(arena, chord, "NtKeyChord", "key", toU32(binding.chord.code));
    setNativeField(arena, chord, "NtKeyChord", "modifiers", toU32(binding.chord.modifiers ?? 0));
    setNativeField(arena, entry, bindingType, "action", toU32(binding.action));
    setNativeField(arena, entry, bindingType, "move", toU32(editorMove[binding.move ?? "previousCluster"]));
    setNativeField(arena, entry, bindingType, "slot", toU32(binding.slot ?? 0));
    setNativeField(arena, entry, bindingType, "extend", binding.extend ? 1 : 0);
    setNativeField(arena, entry, bindingType, "repeat", binding.repeat ? 1 : 0);
  }
  setNativeField(arena, address, type, "bindings", entries);
  setNativeField(arena, address, type, "count", count);
  setNativeField(arena, address, type, "registers", registers);
  return pointerSlot(arena, address);
}

class EditorResource {
  #handle: number;
  #owner?: object;
  #views = 0;
  #closed = false;

  constructor(readonly runtime: Runtime, handle: number,
              private readonly destroy: "nt_editor_destroy" | "nt_editor_registers_destroy") {
    this.#handle = handle;
  }

  use<T>(call: (handle: number) => T): T {
    if (this.#closed) throw this.runtime.failure(5);
    this.borrow(this);
    try {
      return call(this.#handle);
    } finally {
      this.release(this);
    }
  }

  borrow(owner: object): void {
    if (this.#owner !== undefined && this.#owner !== owner) throw this.runtime.failure(6);
    if (this.#owner === this) throw this.runtime.failure(6);
    this.#owner = owner;
  }

  release(owner: object): void {
    if (this.#owner === owner) this.#owner = undefined;
  }

  retainView(owner: object): number {
    if (this.#closed) throw this.runtime.failure(5);
    this.borrow(owner);
    this.#views++;
    return this.#handle;
  }

  dropView(): void {
    this.#views--;
    this.#destroyClosedHandle();
  }

  close(): void {
    if (this.#owner === this) throw this.runtime.failure(6);
    this.#closed = true;
    this.#destroyClosedHandle();
  }

  #destroyClosedHandle(): void {
    if (this.#closed && this.#views === 0 && this.#handle) {
      this.runtime.raw.withArena(arena => arena.call(this.destroy, pointerSlot(arena, this.#handle)));
      this.#handle = 0;
    }
  }
}

const editorResources = new WeakMap<Editor, EditorResource>();
const registerResources = new WeakMap<EditorRegisters, EditorResource>();

function sameRuntimeResource(runtime: Runtime, resource: EditorResource | undefined): EditorResource {
  if (!resource || resource.runtime !== runtime) throw runtime.failure(1);
  return resource;
}

class OwnedEditorRegisters implements EditorRegisters {
  readonly #resource: EditorResource;

  constructor(runtime: Runtime, handle: number) {
    this.#resource = new EditorResource(runtime, handle, "nt_editor_registers_destroy");
    registerResources.set(this, this.#resource);
  }

  read(slot: number): EditorRegisterValue {
    const resource = this.#resource;
    return resource.use(handle => resource.runtime.raw.withArena(arena => {
      const type = "NtEditorRegisterValue";
      const out = arena.value(type);
      arena.result("nt_editor_register_read", pointerSlot(arena, handle), sizeSlot(arena, slot), pointerSlot(arena, out));
      return {
        text: readByteView(resource.runtime, arena, out + arena.api.field(type, "text").offset),
        occupied: nativeField(arena, out, type, "occupied") !== 0,
      };
    }));
  }

  write(slot: number, value: Utf8): void {
    const resource = this.#resource;
    resource.use(handle => resource.runtime.raw.withArena(arena =>
      arena.result("nt_editor_register_write", pointerSlot(arena, handle), sizeSlot(arena, slot), arena.byteView(value))));
  }

  close(): void { this.#resource.close(); }
}

class OwnedEditor implements Editor {
  readonly #resource: EditorResource;

  constructor(private readonly runtime: Runtime, handle: number) {
    this.#resource = new EditorResource(runtime, handle, "nt_editor_destroy");
    editorResources.set(this, this.#resource);
  }

  #call<T>(call: (arena: RawArena, editor: number) => T): T {
    return this.#resource.use(handle => this.runtime.raw.withArena(arena => call(arena, pointerSlot(arena, handle))));
  }

  get value(): string {
    return this.#call((arena, editor) =>
      readByteView(this.runtime, arena, arena.call("nt_text_bytes", arena.call("nt_editor_text", editor))));
  }

  get selection(): Selection {
    return this.#call((arena, editor) => {
      const result = arena.call("nt_editor_selection", editor);
      return {
        anchor: readCaret(arena, result + arena.api.field("NtEditorSelection", "anchor").offset),
        active: readCaret(arena, result + arena.api.field("NtEditorSelection", "active").offset),
      };
    });
  }

  get selectedText(): string {
    return this.#call((arena, editor) =>
      readByteView(this.runtime, arena, arena.call("nt_editor_selected_text", editor)));
  }

  setSelection(selection: Selection): void {
    this.#call((arena, editor) => {
      const type = "NtEditorSelection";
      const value = arena.value(type);
      writeCaret(arena, value + arena.api.field(type, "anchor").offset, selection.anchor);
      writeCaret(arena, value + arena.api.field(type, "active").offset, selection.active);
      arena.result("nt_editor_set_selection", editor, value);
    });
  }

  move(direction: EditorMove, extend = false): void {
    this.#call((arena, editor) => {
      const type = "NtEditorMove";
      const value = arena.value(type);
      arena.setUnsigned(value, arena.api.size(type), BigInt(toU32(editorMove[direction])));
      arena.result("nt_editor_move", editor, value, boolSlot(arena, extend));
    });
  }

  selectAt(layout: TextLayout, cell: Point, extend = false): void {
    this.#call((arena, editor) => {
      const position = cellPointSlot(arena, cell);
      OwnedTextLayout.use(this.runtime, layout, handle =>
        arena.result("nt_editor_select_at", editor, pointerSlot(arena, handle), position, boolSlot(arena, extend)));
    });
  }

  caretPosition(layout: TextLayout): Point {
    return this.#call((arena, editor) => {
      const out = arena.value("NtCellPoint");
      OwnedTextLayout.use(this.runtime, layout, handle =>
        arena.result("nt_editor_caret_position", editor, pointerSlot(arena, handle), pointerSlot(arena, out)));
      return nativeCellPoint(arena, out);
    });
  }

  replaceSelection(value: Utf8): void {
    this.#call((arena, editor) => arena.result("nt_editor_replace_selection", editor, arena.byteView(value)));
  }

  delete(backward: boolean): void {
    this.#call((arena, editor) => arena.result("nt_editor_delete", editor, boolSlot(arena, backward)));
  }

  setUndoLimit(count: number): void {
    this.#call((arena, editor) => arena.result("nt_editor_set_undo_limit", editor, sizeSlot(arena, count)));
  }

  get undoState(): EditorUndoState {
    return this.#call((arena, editor) => {
      const out = arena.call("nt_editor_undo_state", editor);
      return { undoCount: nativeField(arena, out, "NtEditorUndoState", "undo_count"),
        redoCount: nativeField(arena, out, "NtEditorUndoState", "redo_count") };
    });
  }

  #journal(name: "nt_editor_undo" | "nt_editor_redo"): boolean {
    return this.#call((arena, editor) => {
      const out = arena.value("_Bool");
      arena.result(name, editor, pointerSlot(arena, out));
      return arena.unsigned(out, arena.api.size("_Bool")) !== 0n;
    });
  }

  undo(): boolean { return this.#journal("nt_editor_undo"); }
  redo(): boolean { return this.#journal("nt_editor_redo"); }

  setHistoryLimit(count: number): void {
    this.#call((arena, editor) => arena.result("nt_editor_set_history_limit", editor, sizeSlot(arena, count)));
  }

  addHistory(value: Utf8): void {
    this.#call((arena, editor) => arena.result("nt_editor_history_add", editor, arena.byteView(value)));
  }

  get historyCount(): number {
    return this.#call((arena, editor) =>
      Number(arena.unsigned(arena.call("nt_editor_history_count", editor), arena.api.size("size_t"))));
  }

  historyEntry(index: number): string {
    return this.#call((arena, editor) => {
      const out = arena.value("NtByteView");
      arena.result("nt_editor_history_entry", editor, sizeSlot(arena, index), pointerSlot(arena, out));
      return readByteView(this.runtime, arena, out);
    });
  }

  stepHistory(older: boolean): boolean {
    return this.#call((arena, editor) => {
      const changed = arena.value("_Bool");
      arena.result("nt_editor_history_step", editor, boolSlot(arena, older), pointerSlot(arena, changed));
      return arena.unsigned(changed, arena.api.size("_Bool")) !== 0n;
    });
  }

  handleKey(key: KeyInput, keymap?: EditorKeyMap): boolean {
    return this.#call((arena, editor) => {
      const run = (registers: number) => {
        const type = "NtEvent";
        const event = arena.value(type);
        const data = event + arena.api.field(type, "data.key").offset;
        setNativeField(arena, event, type, "kind", EventKind.key);
        setNativeField(arena, data, "NtKeyInput", "key", toU32(key.code));
        setNativeField(arena, data, "NtKeyInput", "modifiers", toU32(key.modifiers ?? 0));
        setNativeField(arena, data, "NtKeyInput", "state", toU32(key.state ?? KeyState.press));
        const map = keymap ? editorKeyMap(arena, keymap, registers) : pointerSlot(arena, 0);
        const out = arena.value("_Bool");
        arena.result("nt_editor_input", editor, pointerSlot(arena, event), map, pointerSlot(arena, out));
        return arena.unsigned(out, arena.api.size("_Bool")) !== 0n;
      };
      const registers = keymap?.registers;
      if (registers === undefined) return run(0);
      return sameRuntimeResource(this.runtime, registerResources.get(registers)).use(run);
    });
  }

  close(): void { this.#resource.close(); }
}

function writeTrackRate(arena: RawArena, address: number, rate: TrackRate): void {
  setNativeField(arena, address, "NtTrackRate", "numerator", toU32(rate.numerator));
  setNativeField(arena, address, "NtTrackRate", "denominator", toU32(rate.denominator));
}

function writeTrackTime(arena: RawArena, address: number, timing: TrackTime): void {
  const type = "NtTrackTime";
  const { rate, edits = [], quantumUs = 0n } = timing;
  if (rate) writeTrackRate(arena, address + arena.api.field(type, "rate").offset, rate);
  const count = toU32(edits.length);
  const editType = "NtTrackEdit";
  const size = arena.api.size(editType);
  const array = count ? arena.allocate(count * size) : 0;
  const values = Array.from({ length: count }, (_, index) => edits[index]);
  const kinds = { dwell: 0, skip: 1 } as const;
  for (const [index, edit] of values.entries()) {
    const entry = array + index * size;
    setNativeField(arena, entry, editType, "kind", toU32(kinds[edit.kind]));
    setNativeField(arena, entry, editType, "at_us", toU64(edit.atUs));
    setNativeField(arena, entry, editType, "length_us", toU64(edit.lengthUs));
  }
  setNativeField(arena, address, type, "edits", array);
  setNativeField(arena, address, type, "count", count);
  setNativeField(arena, address, type, "quantum_us", toU64(quantumUs));
}

function writeRandomTrack(arena: RawArena, address: number, track: RandomTrack): void {
  const type = "NtRandomTrack";
  arena.setFloat64(address + arena.api.field(type, "from").offset, track.from);
  arena.setFloat64(address + arena.api.field(type, "to").offset, track.to);
  setNativeField(arena, address, type, "interval_us", toU64(track.intervalUs));
  setNativeField(arena, address, type, "seed", toU64(track.seed ?? 0n));
  setNativeField(arena, address, type, "stream", toU64(track.stream ?? 0n));
  setNativeField(arena, address, type, "smooth", track.smooth ? 1 : 0);
}

function writeKeyframeTrack(arena: RawArena, address: number, track: KeyframeTrack): void {
  const keys = track.keys;
  const count = toU32(keys.length);
  const keyType = "NtKeyframe";
  const size = arena.api.size(keyType);
  const array = count ? arena.allocate(count * size) : 0;
  const valueOffset = arena.api.field(keyType, "value").offset;
  const keyframes = Array.from({ length: count }, (_, index) => keys[index]);
  for (const [index, key] of keyframes.entries()) {
    const at = array + index * size;
    setNativeField(arena, at, keyType, "at_us", toU64(key.atUs));
    arena.setFloat64(at + valueOffset, key.value);
    setNativeField(arena, at, keyType, "easing", toU32(key.easing ?? Easing.linear));
  }
  const type = "NtKeyframeTrack";
  setNativeField(arena, address, type, "keys", array);
  setNativeField(arena, address, type, "count", count);
  setNativeField(arena, address, type, "delay_us", toU64(track.delayUs ?? 0n));
  setNativeField(arena, address, type, "repeat", toU32(track.repeat ?? Repeat.once));
}

function writeTween(arena: RawArena, address: number, tween: Tween): void {
  const type = "NtTween";
  arena.setFloat64(address + arena.api.field(type, "from").offset, tween.from);
  arena.setFloat64(address + arena.api.field(type, "to").offset, tween.to);
  setNativeField(arena, address, type, "duration_us", toU64(tween.durationUs));
  setNativeField(arena, address, type, "delay_us", toU64(tween.delayUs ?? 0n));
  setNativeField(arena, address, type, "easing", toU32(tween.easing ?? Easing.linear));
  setNativeField(arena, address, type, "repeat", toU32(tween.repeat ?? Repeat.once));
}

function writeTimelineSource(runtime: Runtime, arena: RawArena, address: number, source: TimelineSource): void {
  const type = "NtTimelineClip";
  switch (source.kind) {
    case "tween": {
      const tween = address + arena.api.field(type, "source.tween").offset;
      setNativeField(arena, address, type, "kind", 0);
      writeTween(arena, tween, source.tween);
      break;
    }
    case "keyframes":
      setNativeField(arena, address, type, "kind", 1);
      writeKeyframeTrack(arena, address + arena.api.field(type, "source.keyframes").offset, source.keyframes);
      break;
    case "random":
      setNativeField(arena, address, type, "kind", 2);
      writeRandomTrack(arena, address + arena.api.field(type, "source.random").offset, source.random);
      break;
    default:
      throw runtime.failure(1);
  }
}

function writeTimelineNode(runtime: Runtime, arena: RawArena, address: number, node: TimelineNode, depth: number): void {
  // Match the native depth limit while marshalling, including cyclic descriptions.
  if (depth === 128) throw runtime.failure(2);
  const type = "NtTimelineNode";
  setNativeField(arena, address, type, "delay_us", toU64(node.delayUs ?? 0n));
  const kind = node.kind;
  switch (kind) {
    case "clip": {
      const clipType = "NtTimelineClip";
      const clip = address + arena.api.field(type, "data.clip").offset;
      setNativeField(arena, address, type, "kind", 0);
      setNativeField(arena, clip, clipType, "channel", toU64(node.channel));
      setNativeField(arena, clip, clipType, "span_us", toU64(node.spanUs));
      const timing = node.timing;
      if (timing) writeTrackTime(arena, clip + arena.api.field(clipType, "timing").offset, timing);
      writeTimelineSource(runtime, arena, clip, node.source);
      break;
    }
    case "sequence":
    case "parallel": {
      const children = node.children;
      const count = toU32(children.length);
      const size = arena.api.size(type);
      const array = count ? arena.allocate(count * size) : 0;
      const values = Array.from({ length: count }, (_, index) => children[index]);
      setNativeField(arena, address, type, "kind", kind === "sequence" ? 1 : 2);
      setNativeField(arena, address, type, "data.group.children", array);
      setNativeField(arena, address, type, "data.group.count", count);
      for (const [index, child] of values.entries()) {
        writeTimelineNode(runtime, arena, array + index * size, child, depth + 1);
      }
      break;
    }
    default:
      throw runtime.failure(1);
  }
}

function compileTimeline(runtime: Runtime, options: Omit<TimelineOptions, "layout">,
  writeRoot: (arena: RawArena, root: number) => void): Timeline {
  return runtime.raw.withArena(arena => {
    const node = arena.value("NtTimelineNode");
    writeRoot(arena, node);
    const type = "NtTimelineOptions";
    const settings = arena.value(type);
    setNativeField(arena, settings, type, "delay_us", toU64(options.delayUs ?? 0n));
    setNativeField(arena, settings, type, "repeat", toU32(options.repeat ?? Repeat.once));
    setNativeField(arena, settings, type, "reverse", options.reverse ? 1 : 0);
    const out = arena.value("pointer");
    arena.result("nt_timeline_create", pointerSlot(arena, 0), pointerSlot(arena, node),
      pointerSlot(arena, settings), pointerSlot(arena, out));
    return new OwnedTimeline(runtime, arena.pointer(out));
  });
}

class OwnedTimeline implements Timeline {
  #handle: number;

  constructor(private readonly runtime: Runtime, handle: number) { this.#handle = handle; }

  #call<T>(call: (arena: RawArena, handle: number) => T): T {
    if (this.#handle === 0) throw this.runtime.failure(5);
    return this.runtime.raw.withArena(arena => call(arena, pointerSlot(arena, this.#handle)));
  }

  get durationUs(): bigint {
    return this.#call((arena, handle) => arena.unsigned(
      arena.call("nt_timeline_duration", handle), arena.api.size("NtTimeUs")));
  }

  get channelCount(): number {
    return this.#call((arena, handle) => Number(arena.unsigned(
      arena.call("nt_timeline_channel_count", handle), arena.api.size("size_t"))));
  }

  channelAt(index: number): bigint {
    return this.#call((arena, handle) => {
      const out = arena.value("uint64_t");
      arena.result("nt_timeline_channel_at", handle, sizeSlot(arena, index), pointerSlot(arena, out));
      return arena.unsigned(out, arena.api.size("uint64_t"));
    });
  }

  sample(channel: bigint, elapsedUs: bigint): TimelineSample {
    return this.#call((arena, handle) => {
      const channelSlot = arena.value("uint64_t"), elapsedSlot = arena.value("NtTimeUs");
      arena.setUnsigned(channelSlot, 8, toU64(channel));
      arena.setUnsigned(elapsedSlot, 8, toU64(elapsedUs));
      const type = "NtTimelineSample";
      const out = arena.value(type);
      arena.result("nt_timeline_sample", handle, channelSlot, elapsedSlot, pointerSlot(arena, out));
      const wide = (name: string) => {
        const field = arena.api.field(type, name);
        return arena.unsigned(out + field.offset, field.size);
      };
      return { value: arena.float64(out + arena.api.field(type, "value").offset),
        playheadUs: wide("playhead_us"), sourceUs: wide("source_us"), iteration: wide("iteration"),
        phase: nativeField(arena, out, type, "phase") as TimelineSample["phase"],
        clipPhase: nativeField(arena, out, type, "clip_phase") as TimelineSample["clipPhase"],
        direction: nativeField(arena, out, type, "direction", true) as TimelineSample["direction"] };
    });
  }

  close(): void {
    if (!this.#handle) return;
    this.#call((arena, handle) => arena.call("nt_timeline_destroy", handle));
    this.#handle = 0;
  }
}

function writeCellStyle(arena: RawArena, address: number, style: Style): void {
  setNativeField(arena, address, "NtCellStyle", "foreground", packedColor(style.foreground));
  setNativeField(arena, address, "NtCellStyle", "background", packedColor(style.background));
}

function readCellStyle(arena: RawArena, address: number): Style {
  const color = (field: "foreground" | "background"): Color => {
    const layout = arena.api.field("NtCellStyle", field);
    const rgba = arena.read(address + layout.offset, layout.size);
    return [rgba[0], rgba[1], rgba[2], rgba[3]];
  };
  return { foreground: color("foreground"), background: color("background") };
}

function rgbaSlot(arena: RawArena, color: Color): number {
  const out = arena.value("NtRgba");
  arena.setUnsigned(out, arena.api.size("NtRgba"), BigInt(packedColor(color)));
  return out;
}

function readRGBA(arena: RawArena, address: number): Color {
  return colorAt(new DataView(arena.read(address, arena.api.size("NtRgba")).buffer), 0);
}

function contentCells(runtime: Runtime, arena: RawArena, cells: readonly ContentCell[], count: number): number {
  const type = "NtContentCell", size = arena.api.size(type);
  const native = valueArray(arena, type, count);
  for (let index = 0; index < count; index++) {
    const address = native + index * size;
    const { position, columns, glyph = "", style, owner = 0n } = cells[index];
    if (columns !== 0 && columns !== 1 && columns !== 2) throw runtime.failure(1);
    writeCellPoint(arena, address + arena.api.field(type, "position").offset, position);
    copyNativeValue(arena, "NtByteView", address + arena.api.field(type, "glyph").offset, arena.byteView(glyph));
    setNativeField(arena, address, type, "columns", columns);
    writeCellStyle(arena, address + arena.api.field(type, "style").offset, style);
    setNativeField(arena, address, type, "owner", toU64(owner));
  }
  return pointerSlot(arena, native);
}

function imageView(arena: RawArena, image: ImageView): number {
  const type = "NtImageView", address = arena.value(type);
  const { pixels, size, stride } = image;
  const count = toU32(pixels.length);
  const data = valueArray(arena, "NtRgba", count), step = arena.api.size("NtRgba");
  for (let index = 0; index < count; index++) {
    arena.setUnsigned(data + index * step, step, BigInt(packedColor(pixels[index])));
  }
  setNativeField(arena, address, type, "pixels", data);
  setNativeField(arena, address, type, "count", count);
  setNativeField(arena, address, type, "stride", toU32(stride));
  writeNativeSize(arena, address + arena.api.field(type, "size").offset, size);
  return pointerSlot(arena, address);
}

function imageSampling(arena: RawArena, sampling: ImageSampling): number {
  const type = "NtImageSampling", address = arena.value(type);
  setNativeField(arena, address, type, "filter", toU32(sampling.filter ?? ImageFilter.nearest));
  setNativeField(arena, address, type, "horizontal", toU32(sampling.horizontal ?? PaintAddress.pad));
  setNativeField(arena, address, type, "vertical", toU32(sampling.vertical ?? PaintAddress.pad));
  return address;
}

function writePaintColor(arena: RawArena, address: number, color: PaintColor): void {
  const type = "NtColor";
  const channels = address + arena.api.field(type, "channel").offset;
  const step = arena.api.size("double");
  const [a, b, c, d = 0] = color.channels;
  for (const [index, channel] of [a, b, c, d].entries()) {
    arena.setFloat64(channels + index * step, channel);
  }
  setNativeField(arena, address, type, "space", toU32(color.space));
  arena.setFloat64(address + arena.api.field(type, "alpha").offset, color.alpha);
}

function readPaintColor(arena: RawArena, address: number): PaintColor {
  const type = "NtColor";
  const channels = address + arena.api.field(type, "channel").offset;
  const step = arena.api.size("double");
  return {
    space: nativeField(arena, address, type, "space") as ColorSpace,
    channels: [arena.float64(channels), arena.float64(channels + step),
      arena.float64(channels + 2 * step), arena.float64(channels + 3 * step)],
    alpha: arena.float64(address + arena.api.field(type, "alpha").offset),
  };
}

function paintColorSlot(arena: RawArena, color: PaintColor): number {
  const address = arena.value("NtColor");
  writePaintColor(arena, address, color);
  return address;
}

function writeColorInterpolation(arena: RawArena, address: number, value: ColorInterpolation = {}): void {
  const type = "NtColorInterpolation";
  setNativeField(arena, address, type, "space", toU32(value.space ?? ColorSpace.srgb));
  setNativeField(arena, address, type, "gamut", toU32(value.gamut ?? GamutPolicy.clip));
  setNativeField(arena, address, type, "hue", toU32(value.hue ?? HueInterpolation.shorter));
  setNativeField(arena, address, type, "alpha", toU32(value.alpha ?? AlphaInterpolation.premultiplied));
}

function compositePointer(arena: RawArena, options: CompositeOptions | undefined): number {
  if (!options) return pointerSlot(arena, 0);
  const type = "NtCompositeOptions", address = arena.value(type);
  setNativeField(arena, address, type, "blend", toU32(options.blend ?? BlendMode.normal));
  setNativeField(arena, address, type, "operation", toU32(options.operation ?? CompositeOperator.sourceOver));
  setNativeField(arena, address, type, "space", toU32(options.space ?? ColorSpace.linearSrgb));
  setNativeField(arena, address, type, "gamut", toU32(options.gamut ?? GamutPolicy.clip));
  setNativeField(arena, address, type, "hue", toU32(options.hue ?? HueInterpolation.shorter));
  setNativeField(arena, address, type, "seed", toU64(options.seed ?? 0n));
  setNativeField(arena, address, type, "sample_index", toU64(options.sampleIndex ?? 0n));
  return pointerSlot(arena, address);
}

function parseNativeName(runtime: Runtime, name: string, type: string, operation: string): number {
  if (typeof name !== "string" || name.includes("\0")) throw runtime.failure(1);
  const bytes = encoder.encode(name);
  return runtime.raw.withArena(arena => {
    const input = arena.allocate(bytes.length + 1);
    arena.write(input, bytes);
    const out = arena.value(type);
    arena.result(operation, pointerSlot(arena, input), pointerSlot(arena, out));
    return Number(arena.unsigned(out, arena.api.size(type)));
  });
}

function writeGradient(arena: RawArena, address: number, gradient: Gradient): void {
  const type = "NtGradient";
  const stopType = "NtGradientStop";
  const stops = gradient.stops;
  const count = toU32(stops.length);
  const size = arena.api.size(stopType);
  const array = count ? arena.allocate(count * size) : 0;
  for (let index = 0; index < count; index++) {
    const stop = stops[index];
    const at = array + index * size;
    arena.setFloat64(at + arena.api.field(stopType, "position").offset, stop.position);
    writePaintColor(arena, at + arena.api.field(stopType, "color").offset, stop.color);
    setNativeField(arena, at, stopType, "from_here", toU32(stop.easing ?? Easing.linear));
  }
  setNativeField(arena, address, type, "stops", array);
  setNativeField(arena, address, type, "count", count);
  setNativeField(arena, address, type, "address", toU32(gradient.address ?? PaintAddress.pad));
  writeColorInterpolation(arena, address + arena.api.field(type, "interpolation").offset, gradient.interpolation);
}

function writePalette(arena: RawArena, address: number, palette: readonly Color[]): void {
  const type = "NtPaletteView";
  const count = toU32(palette.length);
  const size = arena.api.size("NtRgba");
  const array = count ? arena.allocate(count * size) : 0;
  for (let index = 0; index < count; index++) {
    arena.setUnsigned(array + index * size, size, BigInt(packedColor(palette[index])));
  }
  setNativeField(arena, address, type, "colors", array);
  setNativeField(arena, address, type, "count", count);
}

function writePaletteRamp(arena: RawArena, address: number, ramp: PaletteRamp): void {
  const type = "NtPaletteRamp";
  writePalette(arena, address + arena.api.field(type, "palette").offset, ramp.palette);
  setNativeField(arena, address, type, "filter", toU32(ramp.filter ?? PaletteFilter.linear));
  setNativeField(arena, address, type, "address", toU32(ramp.address ?? PaintAddress.pad));
  setNativeField(arena, address, type, "easing", toU32(ramp.easing ?? Easing.linear));
  writeColorInterpolation(arena, address + arena.api.field(type, "interpolation").offset, ramp.interpolation);
}

const rippleNumbers = { firstRadius: "first_radius", lastRadius: "last_radius", width: "width", peak: "peak" } as const;
const rippleTimes = { startUs: "start_us", lifetimeUs: "lifetime_us", spacingUs: "spacing_us" } as const;

function readRippleField(arena: RawArena, address: number): RippleField {
  const type = "NtRippleField";
  const offset = (name: string) => address + arena.api.field(type, name).offset;
  return {
    kind: "ripple", origin: readContinuousPoint(arena, offset("origin")), aspect: readContinuousPoint(arena, offset("aspect")),
    firstRadius: arena.float64(offset("first_radius")), lastRadius: arena.float64(offset("last_radius")),
    width: arena.float64(offset("width")), peak: arena.float64(offset("peak")),
    startUs: arena.unsigned(offset("start_us"), 8), lifetimeUs: arena.unsigned(offset("lifetime_us"), 8),
    spacingUs: arena.unsigned(offset("spacing_us"), 8), rings: nativeField(arena, address, type, "rings"),
    falloff: nativeField(arena, address, type, "falloff") as OpticalFalloff,
    fade: nativeField(arena, address, type, "fade") as RippleField["fade"],
  };
}

function readWaveField(arena: RawArena, address: number): WaveField {
  const type = "NtWaveField";
  const offset = (name: string) => address + arena.api.field(type, name).offset;
  return { kind: "wave", origin: readContinuousPoint(arena, offset("origin")),
    cyclesPerUnit: readContinuousPoint(arena, offset("cycles_per_unit")), phaseTurns: arena.float64(offset("phase_turns")),
    startUs: arena.unsigned(offset("start_us"), 8), periodUs: arena.unsigned(offset("period_us"), 8) };
}

function fieldPointer(runtime: Runtime, arena: RawArena, field: Field): number {
  const value = arena.value("NtField");
  const kind = field.kind;
  if (kind === "ripple") {
    const type = "NtRippleField", address = value + arena.api.field("NtField", "as.ripple").offset;
    setNativeField(arena, value, "NtField", "kind", Number(arena.api.constant("NT_FIELD_RIPPLE")));
    writeContinuousPoint(arena, address + arena.api.field(type, "origin").offset, field.origin);
    writeContinuousPoint(arena, address + arena.api.field(type, "aspect").offset, field.aspect);
    for (const key of Object.keys(rippleNumbers) as (keyof typeof rippleNumbers)[]) {
      arena.setFloat64(address + arena.api.field(type, rippleNumbers[key]).offset, field[key]);
    }
    for (const key of Object.keys(rippleTimes) as (keyof typeof rippleTimes)[]) {
      setNativeField(arena, address, type, rippleTimes[key], toU64(field[key]));
    }
    setNativeField(arena, address, type, "rings", toU32(field.rings));
    setNativeField(arena, address, type, "falloff", toU32(field.falloff));
    setNativeField(arena, address, type, "fade", toU32(field.fade ?? Easing.linear));
  } else if (kind === "wave") {
    const type = "NtWaveField", address = value + arena.api.field("NtField", "as.wave").offset;
    setNativeField(arena, value, "NtField", "kind", Number(arena.api.constant("NT_FIELD_WAVE")));
    writeContinuousPoint(arena, address + arena.api.field(type, "origin").offset, field.origin);
    writeContinuousPoint(arena, address + arena.api.field(type, "cycles_per_unit").offset, field.cyclesPerUnit);
    arena.setFloat64(address + arena.api.field(type, "phase_turns").offset, field.phaseTurns);
    setNativeField(arena, address, type, "start_us", toU64(field.startUs));
    setNativeField(arena, address, type, "period_us", toU64(field.periodUs));
  } else throw runtime.failure(1);
  return pointerSlot(arena, value);
}

function fieldQuerySlot(runtime: Runtime, arena: RawArena, query: FieldQuery): number {
  const type = "NtFieldQuery", address = arena.value(type);
  writeContinuousPoint(arena, address + arena.api.field(type, "position").offset, query.position);
  setNativeField(arena, address, type, "now", toU64(query.now));
  setNativeField(arena, address, type, "motion", enumCode(runtime, motionCode, query.motion ?? "full"));
  return address;
}

function fieldInkPointer(arena: RawArena, ink: FieldInk): number {
  const type = "NtFieldInk", address = arena.value(type);
  const palette = ink.palette;
  const ramp = palette ? arena.value("NtPaletteRamp") : 0;
  if (palette) writePaletteRamp(arena, ramp, palette);
  setNativeField(arena, address, type, "palette", ramp);
  setNativeField(arena, address, type, "color", packedColor(ink.color));
  arena.setFloat64(address + arena.api.field(type, "opacity").offset, ink.opacity);
  return pointerSlot(arena, address);
}

function paintSampleSlot(runtime: Runtime, arena: RawArena, sample: PaintSample): number {
  const type = "NtPaintSample";
  const address = arena.value(type);
  const { x, y } = sample.position;
  arena.setFloat64(address + arena.api.field(type, "x").offset, x);
  arena.setFloat64(address + arena.api.field(type, "y").offset, y);
  setNativeField(arena, address, type, "elapsed_us", toU64(sample.elapsedUs ?? 0n));
  setNativeField(arena, address, type, "motion", enumCode(runtime, motionCode, sample.motion ?? "full"));
  return address;
}

function writePaintField(arena: RawArena, address: number, field: PaintField): void {
  const type = "NtPaintField";
  const { origin, scale, phaseTrack } = field;
  arena.setFloat64(address + arena.api.field(type, "origin_x").offset, origin?.x ?? 0);
  arena.setFloat64(address + arena.api.field(type, "origin_y").offset, origin?.y ?? 0);
  arena.setFloat64(address + arena.api.field(type, "scale_x").offset, scale.x);
  arena.setFloat64(address + arena.api.field(type, "scale_y").offset, scale.y);
  arena.setFloat64(address + arena.api.field(type, "angle_degrees").offset, field.angleDegrees ?? 0);
  arena.setFloat64(address + arena.api.field(type, "swing_degrees").offset, field.swingDegrees ?? 0);
  arena.setFloat64(address + arena.api.field(type, "phase").offset, field.phase ?? 0);
  arena.setFloat64(address + arena.api.field(type, "wave_amplitude").offset, field.waveAmplitude ?? 0);
  setNativeField(arena, address, type, "pattern", toU32(field.pattern ?? PaintPattern.linear));
  setNativeField(arena, address, type, "rotation", toU32(field.rotation ?? PaintRotation.fixed));
  setNativeField(arena, address, type, "rotation_period_us", toU64(field.rotationPeriodUs ?? 0n));
  setNativeField(arena, address, type, "reverse_rotation", field.reverseRotation ? 1 : 0);
  if (phaseTrack !== undefined) {
    const track = arena.value("NtTween");
    writeTween(arena, track, phaseTrack);
    setNativeField(arena, address, type, "phase_track", track);
  }
}

function paintSourceAddress(runtime: Runtime, arena: RawArena, paint: PaintSource): number {
  const type = "NtPaint";
  const address = arena.value(type);
  switch (paint.kind) {
    case "solid":
      setNativeField(arena, address, type, "kind", 0);
      writePaintColor(arena, address + arena.api.field(type, "content.solid").offset, paint.color);
      break;
    case "gradient":
      setNativeField(arena, address, type, "kind", 1);
      writeGradient(arena, address + arena.api.field(type, "content.gradient").offset, paint.gradient);
      writePaintField(arena, address + arena.api.field(type, "field").offset, paint.field);
      break;
    case "palette":
      setNativeField(arena, address, type, "kind", 2);
      writePaletteRamp(arena, address + arena.api.field(type, "content.palette").offset, paint.palette);
      writePaintField(arena, address + arena.api.field(type, "field").offset, paint.field);
      break;
    default:
      throw runtime.failure(1);
  }
  arena.setFloat64(address + arena.api.field(type, "opacity").offset, paint.opacity);
  const opacityTrack = paint.opacityTrack;
  if (opacityTrack !== undefined) {
    const track = arena.value("NtKeyframeTrack");
    writeKeyframeTrack(arena, track, opacityTrack);
    setNativeField(arena, address, type, "opacity_track", track);
  }
  return address;
}

function writeTextPaintStyle(runtime: Runtime, arena: RawArena, address: number, style: TextPaintStyle): void {
  const type = "NtTextPaintStyle";
  const { color, foreground, background } = style;
  writeCellStyle(arena, address + arena.api.field(type, "color").offset, color);
  setNativeField(arena, address, type, "owner", toU64(style.owner ?? 0n));
  if (foreground !== undefined) setNativeField(arena, address, type, "foreground", paintSourceAddress(runtime, arena, foreground));
  if (background !== undefined) setNativeField(arena, address, type, "background", paintSourceAddress(runtime, arena, background));
}

function textPaintOptions(runtime: Runtime, arena: RawArena, options: TextPaintOptions): number {
  const type = "NtTextPaintOptions";
  const address = arena.value(type);
  writeTextPaintStyle(runtime, arena, address + arena.api.field(type, "base").offset, options.base);
  const spans = options.spans ?? [];
  const count = toU32(spans.length);
  const spanType = "NtTextPaintSpan";
  const size = arena.api.size(spanType);
  const array = count ? arena.allocate(count * size) : 0;
  for (let index = 0; index < count; index++) {
    const { begin, end, style } = spans[index];
    const at = array + index * size;
    const range = at + arena.api.field(spanType, "source").offset;
    setNativeField(arena, range, "NtByteSpan", "begin", toU32(begin));
    setNativeField(arena, range, "NtByteSpan", "end", toU32(end));
    writeTextPaintStyle(runtime, arena, at + arena.api.field(spanType, "style").offset, style);
  }
  setNativeField(arena, address, type, "spans", array);
  setNativeField(arena, address, type, "count", count);
  setNativeField(arena, address, type, "elapsed_us", toU64(options.elapsedUs ?? 0n));
  setNativeField(arena, address, type, "motion", enumCode(runtime, motionCode, options.motion ?? "full"));
  setNativeField(arena, address, type, "gamut", toU32(options.gamut ?? GamutPolicy.clip));
  return pointerSlot(arena, address);
}

function writeTrackSchedule(arena: RawArena, address: number, schedule: TrackSchedule = {}): void {
  const type = "NtTrackSchedule";
  setNativeField(arena, address, type, "stagger_us", toU64(schedule.staggerUs ?? 0n));
  setNativeField(arena, address, type, "jitter_us", toU64(schedule.jitterUs ?? 0n));
  setNativeField(arena, address, type, "duration_variation_us", toU64(schedule.durationVariationUs ?? 0n));
  setNativeField(arena, address, type, "batch_size", toU64(schedule.batchSize ?? 0n));
  setNativeField(arena, address, type, "seed", toU64(schedule.seed ?? 0n));
}

function textEffectAddress(arena: RawArena, effect: TextEffect): number {
  const type = "NtTextEffect";
  const out = arena.value(type);
  setNativeField(arena, out, type, "delay_us", toU64(effect.delayUs ?? 0n));
  setNativeField(arena, out, type, "duration_us", toU64(effect.durationUs ?? 0n));
  setNativeField(arena, out, type, "order", toU32(effect.order ?? TextEffectOrder.logical));
  setNativeField(arena, out, type, "visibility", toU32(effect.visibility ?? TextVisibility.reveal));
  setNativeField(arena, out, type, "opacity_easing", toU32(effect.opacityEasing ?? Easing.linear));
  setNativeField(arena, out, type, "seed", toU64(effect.seed ?? 0n));
  writeTrackSchedule(arena, out + arena.api.field(type, "schedule").offset, effect.schedule);
  const glyphs = effect.glyphs;
  if (glyphs !== undefined) {
    const address = out + arena.api.field(type, "glyphs").offset;
    const glyphType = "NtTextGlyphEffect";
    const alphabet = glyphs.alphabet ?? [];
    const count = toU32(alphabet.length);
    const size = arena.api.size("NtByteView");
    const array = count > 0 ? arena.allocate(count * size) : 0;
    for (let index = 0; index < count; index++) {
      const bytes = arena.byteView(alphabet[index]);
      arena.write(array + index * size, arena.read(bytes, size));
    }
    setNativeField(arena, address, glyphType, "kind", toU32(glyphs.kind));
    setNativeField(arena, address, glyphType, "alphabet", array);
    setNativeField(arena, address, glyphType, "count", count);
    setNativeField(arena, address, glyphType, "interval_us", toU64(glyphs.intervalUs ?? 0n));
    setNativeField(arena, address, glyphType, "rotations", toU32(glyphs.rotations ?? 0));
  }
  const trails = effect.trails;
  if (trails !== undefined) {
    const address = out + arena.api.field(type, "trails").offset;
    const trailType = "NtTextTrails";
    setNativeField(arena, address, trailType, "count", toU32(trails.count));
    setNativeField(arena, address, trailType, "spacing_us", toU64(trails.spacingUs));
    arena.setFloat64(address + arena.api.field(trailType, "decay").offset, trails.decay);
  }
  return out;
}

function readTextEffectSample(runtime: Runtime, arena: RawArena, address: number): TextEffectSample {
  const type = "NtTextEffectSample";
  const field = (name: string, signed = false) => nativeField(arena, address, type, name, signed);
  const wide = (name: string) => {
    const { offset, size } = arena.api.field(type, name);
    return arena.unsigned(address + offset, size);
  };
  return {
    cluster: readLayoutCluster(runtime, arena, address + arena.api.field(type, "cluster").offset),
    visualIndex: field("visual_index"), logicalIndex: field("logical_index"), rank: field("rank"), count: field("count"),
    lineBegin: field("line_begin", true), lineEnd: field("line_end", true),
    elapsedUs: wide("elapsed_us"), localUs: wide("local_us"), durationUs: wide("duration_us"),
    progress: arena.float64(address + arena.api.field(type, "progress").offset),
    travel: arena.float64(address + arena.api.field(type, "travel").offset),
    seed: wide("seed"), identity: wide("identity"),
  };
}

function textEffectClock(arena: RawArena, sample: TextEffectSample): number {
  const type = "NtTextEffectSample";
  const out = arena.value(type);
  setNativeField(arena, out, type, "rank", toU32(sample.rank));
  setNativeField(arena, out, type, "local_us", toU64(sample.localUs));
  setNativeField(arena, out, type, "seed", toU64(sample.seed));
  setNativeField(arena, out, type, "identity", toU64(sample.identity));
  arena.setFloat64(out + arena.api.field(type, "progress").offset, sample.progress);
  arena.setFloat64(out + arena.api.field(type, "travel").offset, sample.travel);
  return pointerSlot(arena, out);
}

function readTextPose(arena: RawArena, address: number, glyph: Utf8): TextPose {
  const type = "NtTextPose";
  return {
    glyph,
    offset: readContinuousPoint(arena, address + arena.api.field(type, "offset").offset),
    opacity: arena.float64(address + arena.api.field(type, "opacity").offset),
    hueDegrees: arena.float64(address + arena.api.field(type, "hue_degrees").offset),
  };
}

function writeTextPose(arena: RawArena, address: number, pose: TextPose): void {
  const type = "NtTextPose";
  writeContinuousPoint(arena, address + arena.api.field(type, "offset").offset, pose.offset);
  arena.setFloat64(address + arena.api.field(type, "opacity").offset, pose.opacity);
  arena.setFloat64(address + arena.api.field(type, "hue_degrees").offset, pose.hueDegrees);
}

function textMotionSlot(arena: RawArena, motion: TextMotion): number {
  const type = "NtTextMotion";
  const out = arena.value(type);
  const zero = { x: 0, y: 0 };
  writeContinuousPoint(arena, out + arena.api.field(type, "travel").offset, motion.travel ?? zero);
  writeContinuousPoint(arena, out + arena.api.field(type, "orbit").offset, motion.orbit ?? zero);
  arena.setFloat64(out + arena.api.field(type, "phase_turns").offset, motion.phaseTurns ?? 0);
  arena.setFloat64(out + arena.api.field(type, "cycles").offset, motion.cycles ?? 0);
  arena.setFloat64(out + arena.api.field(type, "rank_turns").offset, motion.rankTurns ?? 0);
  arena.setFloat64(out + arena.api.field(type, "hue_degrees").offset, motion.hueDegrees ?? 0);
  setNativeField(arena, out, type, "easing", toU32(motion.easing ?? Easing.linear));
  setNativeField(arena, out, type, "seeded_phase", motion.seededPhase ? 1 : 0);
  return pointerSlot(arena, out);
}

function transformText(runtime: Runtime, paintArena: RawArena, transform: TextTransform): RawCallbacks {
  return runtime.raw.callbacks({
    NtTextTransform(frame) {
      const arena = frame.arena;
      const sample = readTextEffectSample(runtime, arena, arena.pointer(frame.argument(0)));
      const source = arena.pointer(frame.argument(1));
      const type = "NtTextPose";
      const glyphField = arena.api.field(type, "glyph");
      const glyph = readByteView(runtime, arena, source + glyphField.offset);
      const initial = readTextPose(arena, source, glyph);
      const pose = synchronousCompositionValue(transform(sample, initial), "Text transform must return synchronously");
      if (pose === undefined) return;
      if (typeof pose === "number") {
        const result = arena.value("NtResult");
        arena.setUnsigned(result, arena.api.size("NtResult"), BigInt(toI32(pose)));
        return frame.returnValue(result);
      }
      const out = arena.value(type);
      writeTextPose(arena, out, pose);
      const replacement = pose.glyph;
      // Replacement bytes belong to the outer paint call, including the native
      // validation and any trail batches after this callback returns.
      const bytes = replacement === glyph ? source + glyphField.offset : paintArena.byteView(replacement);
      arena.write(out + glyphField.offset, arena.read(bytes, glyphField.size));
      frame.writeOutput(1, out, arena.api.size(type));
    },
  });
}

function drawTextEffect(runtime: Runtime, arena: RawArena, name: string, target: number,
  at: Point, layout: TextLayout, options: TextPaintOptions, effect: TextEffect, beforeDraw?: () => void): void {
  const position = cellPointSlot(arena, at);
  const settings = textPaintOptions(runtime, arena, options);
  const native = textEffectAddress(arena, effect);
  const transform = effect.transform;
  let callback: RawCallbacks | undefined;
  try {
    if (transform !== undefined) {
      if (typeof transform !== "function") throw runtime.failure(1);
      callback = transformText(runtime, arena, transform);
      setNativeField(arena, native, "NtTextEffect", "transform", callback.function("NtTextTransform"));
      setNativeField(arena, native, "NtTextEffect", "context", callback.context);
    }
    OwnedTextLayout.use(runtime, layout, handle => {
      beforeDraw?.();
      arena.result(name, target, position, pointerSlot(arena, handle), settings, pointerSlot(arena, native));
    });
  } catch (error) {
    const failures = callback?.takeErrors();
    if (failures?.length) {
      throw new CallError(error instanceof NativeError ? error : null,
        failures.map(cause => ({ phase: "paint", cause })));
    }
    throw error;
  } finally {
    callback?.close();
  }
}

function controlStyle(arena: RawArena, style?: ControlStyle): number {
  if (style === undefined) return pointerSlot(arena, 0);
  const type = "NtControlStyle";
  const address = arena.value(type);
  for (const name of ["normal", "focused", "engaged"] as const) {
    writeCellStyle(arena, address + arena.api.field(type, name).offset, style[name]);
  }
  const width = address + arena.api.field(type, "width").offset;
  setNativeField(arena, width, "NtWidthPolicy", "ambiguous", style.ambiguousWidth ?? 1);
  setNativeField(arena, width, "NtWidthPolicy", "emoji", style.emojiWidth ?? 2);
  return pointerSlot(arena, address);
}

function notificationOptions(runtime: Runtime, arena: RawArena, notification: NotificationOptions): number {
  const type = "NtNotificationOptions";
  const options = arena.call("nt_notification_options_default");
  const { id, title, message, dismissLabel, actions = [], expiresAt = 0n, columns, messageRows,
    style, dismissible, dismissOnAction } = notification;
  const count = actions.length;
  if (count > Number(arena.api.constant("NT_NOTIFICATION_MAX_ACTIONS"))) throw runtime.failure(2);
  setNativeField(arena, options, type, "id", toU64(id));
  setNativeField(arena, options, type, "expires_at", toU64(expiresAt));
  for (const [name, text] of [["title", title], ["message", message], ["dismiss_label", dismissLabel]] as const) {
    if (text !== undefined) {
      const field = options + arena.api.field(type, name).offset;
      copyNativeValue(arena, "NtByteView", field, arena.byteView(text));
    }
  }
  if (columns !== undefined) setNativeField(arena, options, type, "columns", toU32(columns));
  if (messageRows !== undefined) setNativeField(arena, options, type, "message_rows", toU32(messageRows));
  if (dismissible !== undefined) setNativeField(arena, options, type, "dismissible", Number(dismissible));
  if (dismissOnAction !== undefined) setNativeField(arena, options, type, "dismiss_on_action", Number(dismissOnAction));
  if (style !== undefined) {
    const field = options + arena.api.field(type, "style").offset;
    copyNativeValue(arena, "NtControlStyle", field, arena.pointer(controlStyle(arena, style)));
  }
  const actionType = "NtNotificationAction";
  const entries = valueArray(arena, actionType, count);
  const size = arena.api.size(actionType);
  const labelOffset = arena.api.field(actionType, "label").offset;
  for (let index = 0; index < count; index++) {
    const action = actions[index];
    const entry = entries + index * size;
    setNativeField(arena, entry, actionType, "id", toU64(action.id));
    setNativeField(arena, entry, actionType, "disabled", action.disabled ? 1 : 0);
    copyNativeValue(arena, "NtByteView", entry + labelOffset, arena.byteView(action.label));
  }
  setNativeField(arena, options, type, "actions", entries);
  setNativeField(arena, options, type, "action_count", count);
  return pointerSlot(arena, options);
}

function unsignedNativeField(arena: RawArena, address: number, type: string, name: string): bigint {
  const { offset, size } = arena.api.field(type, name);
  return arena.unsigned(address + offset, size);
}

function choiceIndex(index?: number): number {
  if (index === undefined) return choiceNone;
  if (index === choiceNone) throw new RangeError("The maximum size_t is reserved for no selection");
  return toU32(index);
}

function choiceOptions(arena: RawArena, choice: ChoiceOptions): number {
  const type = "NtChoiceOptions";
  const address = arena.value(type);
  const entryType = "NtChoiceEntry";
  const entrySize = arena.api.size(entryType);
  const data = choice.entries;
  const count = data.length;
  const entries = count ? arena.allocate(count * entrySize) : 0;
  const label = arena.api.field(entryType, "label");
  for (let index = 0; index < count; index++) {
    const entry = data[index];
    const start = entries + index * entrySize;
    arena.write(start + label.offset, arena.read(arena.byteView(entry.label), label.size));
    setNativeField(arena, start, entryType, "disabled", entry.disabled ? 1 : 0);
  }
  setNativeField(arena, address, type, "kind", toU32(choiceKinds.indexOf(choice.kind)));
  setNativeField(arena, address, type, "entries", entries);
  setNativeField(arena, address, type, "count", count);
  setNativeField(arena, address, type, "selected", choiceIndex(choice.selected));
  setNativeField(arena, address, type, "visible_rows", toU32(choice.visibleRows ?? 0));
  setNativeField(arena, address, type, "style", arena.pointer(controlStyle(arena, choice.style)));
  return pointerSlot(arena, address);
}

function progressOptions(arena: RawArena, progress: ProgressOptions): number {
  const type = "NtProgressOptions";
  const address = arena.value(type);
  setNativeField(arena, address, type, "value", toU32(progress.value));
  setNativeField(arena, address, type, "maximum", toU32(progress.maximum));
  setNativeField(arena, address, type, "vertical", Number(Boolean(progress.vertical)));
  writeCellStyle(arena, address + arena.api.field(type, "filled").offset, progress.filled);
  writeCellStyle(arena, address + arena.api.field(type, "remaining").offset, progress.remaining);
  return address;
}

function timedIndicator(arena: RawArena, type: "NtCursorOptions" | "NtWaitOptions",
                        style: Style, phaseUs: bigint | undefined, flag: "visible" | "ascii", value: boolean): number {
  const address = arena.value(type);
  writeCellStyle(arena, address + arena.api.field(type, "style").offset, style);
  setNativeField(arena, address, type, "phase_us", toU64(phaseUs ?? 0n));
  setNativeField(arena, address, type, flag, value ? 1 : 0);
  return address;
}

function scrollbarOptions(arena: RawArena, bar: ScrollbarOptions): number {
  const type = "NtScrollBarOptions";
  const address = arena.value(type);
  setNativeField(arena, address, type, "content", toU32(bar.content));
  setNativeField(arena, address, type, "viewport", toU32(bar.viewport));
  setNativeField(arena, address, type, "offset", toU32(bar.offset));
  setNativeField(arena, address, type, "vertical", bar.vertical ? 1 : 0);
  writeCellStyle(arena, address + arena.api.field(type, "thumb").offset, bar.thumb);
  writeCellStyle(arena, address + arena.api.field(type, "track").offset, bar.track);
  return address;
}

function writeBoxItem(arena: RawArena, address: number, item?: BoxItem): void {
  const type = "NtBoxItem";
  writeExtent(arena, address + arena.api.field(type, "columns").offset, item?.columns);
  writeExtent(arena, address + arena.api.field(type, "rows").offset, item?.rows);
  setNativeField(arena, address, type, "grow_columns", item?.growColumns ? 1 : 0);
  setNativeField(arena, address, type, "grow_rows", item?.growRows ? 1 : 0);
}

function boxLayout(runtime: Runtime, arena: RawArena, layout: BoxLayout): number {
  const type = "NtBoxLayout", address = arena.value(type);
  setNativeField(arena, address, type, "kind", enumCode(runtime, boxKind, layout.kind ?? "row"));
  setNativeField(arena, address, type, "gap", toU32(layout.gap ?? 0));
  setNativeField(arena, address, type, "horizontal", enumCode(runtime, textAlignCode, layout.horizontal ?? "start"));
  setNativeField(arena, address, type, "vertical", enumCode(runtime, textAlignCode, layout.vertical ?? "start"));
  const padding = address + arena.api.field(type, "padding").offset;
  for (const side of ["left", "top", "right", "bottom"] as const) {
    setNativeField(arena, padding, "NtBoxInsets", side, toU32(layout.padding?.[side] ?? 0));
  }
  writeBoxItem(arena, address + arena.api.field(type, "fallback").offset, layout.fallback);
  const items = layout.items ?? [];
  const count = toU32(items.length);
  if (count) {
    const itemType = "NtBoxItem", size = arena.api.size(itemType);
    const data = valueArray(arena, itemType, count);
    for (let index = 0; index < count; index++) writeBoxItem(arena, data + index * size, items[index]);
    setNativeField(arena, address, type, "items", data);
    setNativeField(arena, address, type, "item_count", count);
  }
  return address;
}

function ruleOptions(arena: RawArena, rule: RuleOptions): number {
  const type = "NtRuleOptions", address = arena.value(type);
  setNativeField(arena, address, type, "vertical", rule.vertical ? 1 : 0);
  setNativeField(arena, address, type, "ascii", rule.ascii ? 1 : 0);
  writeCellStyle(arena, address + arena.api.field(type, "color").offset, rule.style ?? defaultStyle);
  return address;
}

function panelOptions(arena: RawArena, panel: PanelOptions): number {
  const type = "NtPanelOptions";
  const address = arena.value(type);
  if (panel.fill) setNativeField(arena, address, type, "fill", packedColor(panel.fill));
  setNativeField(arena, address, type, "framed", Number(Boolean(panel.framed)));
  setNativeField(arena, address, type, "ascii", Number(Boolean(panel.ascii)));
  if (panel.border) writeCellStyle(arena, address + arena.api.field(type, "border").offset, panel.border);
  const padding = address + arena.api.field(type, "padding").offset;
  for (const side of ["left", "top", "right", "bottom"] as const) {
    setNativeField(arena, padding, "NtBoxInsets", side, toU32(panel.padding?.[side] ?? 0));
  }
  if (panel.title !== undefined) {
    writePanelTitle(arena, address, panel.title, panel.titleStyle ?? defaultStyle);
  }
  return pointerSlot(arena, address);
}

function writePanelTitle(arena: RawArena, panel: number, value: Utf8, palette: Style): void {
  const title = panel + arena.api.field("NtPanelOptions", "title").offset;
  const text = arena.byteView(value);
  arena.write(title + arena.api.field("NtLabelOptions", "text").offset, arena.read(text, arena.api.size("NtByteView")));
  const style = title + arena.api.field("NtLabelOptions", "style").offset;
  const width = style + arena.api.field("NtLabelStyle", "width").offset;
  setNativeField(arena, width, "NtWidthPolicy", "ambiguous", 1);
  setNativeField(arena, width, "NtWidthPolicy", "emoji", 2);
  setNativeField(arena, style, "NtLabelStyle", "tab_stop", 4);
  writeCellStyle(arena, style + arena.api.field("NtLabelStyle", "color").offset, palette);
}

function viewportOptions(arena: RawArena, viewport: ViewportOptions): number {
  const type = "NtViewportOptions";
  const address = arena.value(type);
  setNativeField(arena, address, type, "scroll_columns", Number(Boolean(viewport.scrollColumns)));
  setNativeField(arena, address, type, "scroll_rows", Number(Boolean(viewport.scrollRows)));
  const offset = address + arena.api.field(type, "requested").offset;
  setNativeField(arena, offset, "NtCellSize", "columns", toU32(viewport.requested?.columns ?? 0));
  setNativeField(arena, offset, "NtCellSize", "rows", toU32(viewport.requested?.rows ?? 0));
  return address;
}

function writeWindowPlacement(arena: RawArena, address: number, placement: WindowPlacement): void {
  const type = "NtWindowPlacement";
  writeNativeBounds(arena, address + arena.api.field(type, "floating").offset, placement.floating);
  setNativeField(arena, address, type, "mode", toU32(windowModes.indexOf(placement.mode ?? "normal")));
  setNativeField(arena, address, type, "dock", toU32(windowDocks.indexOf(placement.dock ?? "float")));
  setNativeField(arena, address, type, "dock_extent", toU32(placement.dockExtent ?? 0));
}

function writePlotBounds(arena: RawArena, address: number, bounds: PlotBounds): void {
  for (const axis of ["x", "y"] as const) {
    const target = address + arena.api.field("NtPlotBounds", axis).offset;
    arena.setFloat64(target + arena.api.field("NtPlotDomain", "min").offset, bounds[axis].min);
    arena.setFloat64(target + arena.api.field("NtPlotDomain", "max").offset, bounds[axis].max);
  }
}

function writeContinuousPoint(arena: RawArena, address: number, point: Point): void {
  arena.setFloat64(address + arena.api.field("NtPoint", "x").offset, point.x);
  arena.setFloat64(address + arena.api.field("NtPoint", "y").offset, point.y);
}

function readContinuousPoint(arena: RawArena, address: number): Point {
  return { x: arena.float64(address + arena.api.field("NtPoint", "x").offset),
    y: arena.float64(address + arena.api.field("NtPoint", "y").offset) };
}

function nativePath(arena: RawArena, path: Path): number {
  const points = path.points;
  const count = toU32(points.length);
  const pointSize = arena.api.size("NtPoint");
  const array = count ? arena.allocate(count * pointSize) : 0;
  const values = Array.from({ length: count }, (_, index) => points[index]);
  for (const [index, point] of values.entries()) writeContinuousPoint(arena, array + index * pointSize, point);
  const type = "NtPath";
  const native = arena.value(type);
  setNativeField(arena, native, type, "points", array);
  setNativeField(arena, native, type, "count", count);
  setNativeField(arena, native, type, "closed", path.closed ? 1 : 0);
  setNativeField(arena, native, type, "boundary", toU32(path.boundary ?? Repeat.once));
  return native;
}

function writePanelPose(arena: RawArena, address: number, pose: PanelPose): void {
  const type = "NtPanelPose";
  for (const field of ["anchor", "offset", "scale"] as const) {
    writeContinuousPoint(arena, address + arena.api.field(type, field).offset, pose[field]);
  }
  arena.setFloat64(address + arena.api.field(type, "turns").offset, pose.turns);
  arena.setFloat64(address + arena.api.field(type, "opacity").offset, pose.opacity);
}

function readPanelPose(arena: RawArena, address: number): PanelPose {
  const type = "NtPanelPose";
  return {
    anchor: readContinuousPoint(arena, address + arena.api.field(type, "anchor").offset),
    offset: readContinuousPoint(arena, address + arena.api.field(type, "offset").offset),
    scale: readContinuousPoint(arena, address + arena.api.field(type, "scale").offset),
    turns: arena.float64(address + arena.api.field(type, "turns").offset),
    opacity: arena.float64(address + arena.api.field(type, "opacity").offset),
  };
}

function nativeSurfaceMask(arena: RawArena, mask: SurfaceMask): number {
  const type = "NtSurfaceMask";
  const out = arena.value(type);
  const kind = mask.kind;
  const codes = { rectangle: 0, ellipse: 1, diamond: 2, roundedRectangle: 3, alpha: 4 } as const;
  setNativeField(arena, out, type, "kind", toU32(codes[kind]));
  writeNativeBounds(arena, out + arena.api.field(type, "bounds").offset, mask.bounds);
  if (kind === "roundedRectangle") {
    arena.setFloat64(out + arena.api.field(type, "radius").offset, mask.radius);
  }
  if (kind === "alpha") {
    const bytes = mask.alpha;
    const count = bytes.length;
    const alpha = count ? arena.allocate(count) : 0;
    if (count) arena.write(alpha, bytes);
    setNativeField(arena, out, type, "alpha", alpha);
    setNativeField(arena, out, type, "stride", toU32(mask.stride));
    setNativeField(arena, out, type, "count", count);
  }
  return out;
}

function synchronousCompositionValue<T>(value: T, message: string): T {
  if (typeof value === "object" && value !== null && "then" in value && typeof value.then === "function") {
    void Promise.resolve(value).catch(() => {});
    throw new TypeError(message);
  }
  return value;
}

function panelRemapper(runtime: Runtime, remap: NonNullable<PanelLayer["remap"]>): RawCallbacks {
  return runtime.raw.callbacks({
    NtPanelRemap(frame) {
      const arena = frame.arena;
      const type = "NtPanelSample";
      const sample = arena.pointer(frame.argument(0));
      const time = arena.api.field(type, "elapsed_us");
      const pixel = synchronousCompositionValue(remap({
        destination: readContinuousPoint(arena, sample + arena.api.field(type, "destination").offset),
        source: readContinuousPoint(arena, sample + arena.api.field(type, "source").offset),
        viewportSize: nativeSize(arena, sample + arena.api.field(type, "viewport_size").offset),
        regionSize: nativeSize(arena, sample + arena.api.field(type, "region_size").offset),
        elapsedUs: arena.unsigned(sample + time.offset, time.size),
      }), "Panel remap must return a pixel synchronously");
      const outputType = "NtPanelPixel";
      const out = arena.value(outputType);
      writeContinuousPoint(arena, out + arena.api.field(outputType, "source").offset, pixel.source);
      arena.setFloat64(out + arena.api.field(outputType, "opacity").offset, pixel.opacity);
      frame.writeOutput(1, out, arena.api.size(outputType));
    },
  });
}

class FrameArguments {
  readonly #sources = new Map<object, { publication: object; frame: number }>();
  readonly #releases: (() => void)[] = [];
  readonly #callbacks: RawCallbacks[] = [];

  constructor(readonly runtime: Runtime, readonly arena: RawArena, private readonly destination?: OwnedTree) {}

  alreadyBorrowed(owner: OffscreenFrame, frame: number): void {
    this.#sources.set(owner, { publication: owner, frame });
  }

  frame(value: Frame): number {
    const source = frameSources.get(value);
    if (!source || source.runtime !== this.runtime) throw this.runtime.failure(1);
    let held = this.#sources.get(source.owner);
    if (!held) {
      const lease = source.claim(this.destination);
      if (lease.release) this.#releases.push(lease.release);
      const frame = lease.published
        ? this.arena.pointer(this.arena.call("nt_tree_frame", pointerSlot(this.arena, lease.handle)))
        : lease.handle;
      if (!frame) throw this.runtime.failure(5);
      held = { publication: source.publication, frame };
      this.#sources.set(source.owner, held);
    }
    if (held.publication !== source.publication) throw this.runtime.failure(5);
    return held.frame;
  }

  callback(handler: RawCallbacks): RawCallbacks {
    this.#callbacks.push(handler);
    return handler;
  }

  run<T>(name: string, destination: number, type: string, layers: readonly T[], write: (at: number, layer: T) => void): void {
    const arena = this.arena;
    const count = toU32(layers.length);
    const size = arena.api.size(type);
    const array = count ? arena.allocate(count * size) : 0;
    const values = Array.from({ length: count }, (_, index) => layers[index]);
    for (const [index, layer] of values.entries()) write(array + index * size, layer);
    const result = arena.call(name, pointerSlot(arena, destination), pointerSlot(arena, array), sizeSlot(arena, count));
    const status = Number(arena.unsigned(result, arena.api.size("NtResult"))) | 0;
    const failures = this.#callbacks.flatMap(callback =>
      callback.takeErrors().map(cause => ({ phase: "paint" as const, cause })));
    if (failures.length) throw new CallError(status ? this.runtime.failure(status) : null, failures);
    this.runtime.check(status);
  }

  close(): void {
    for (const callback of this.#callbacks) callback.close();
    for (const release of this.#releases) release();
  }
}

function writePanelLayer(batch: FrameArguments, out: number, layer: PanelLayer): void {
  const { arena, runtime } = batch;
  const type = "NtPanelLayer";
  setNativeField(arena, out, type, "source", batch.frame(layer.source));
  writeNativeBounds(arena, out + arena.api.field(type, "region").offset, layer.region);
  writeNativeBounds(arena, out + arena.api.field(type, "viewport").offset, layer.viewport);
  writePanelPose(arena, out + arena.api.field(type, "pose").offset, layer.pose);
  setNativeField(arena, out, type, "elapsed_us", toU64(layer.elapsedUs ?? 0n));
  const mask = layer.mask;
  if (mask) setNativeField(arena, out, type, "mask", nativeSurfaceMask(arena, mask));
  const remap = layer.remap;
  if (remap) {
    const handler = batch.callback(panelRemapper(runtime, remap));
    setNativeField(arena, out, type, "remap", handler.function("NtPanelRemap"));
    setNativeField(arena, out, type, "remap_context", handler.context);
  }
}

function writeSurfaceOwners(batch: FrameArguments, out: number, type: string, owners: SurfaceOwners = "clear"): void {
  const { arena, runtime } = batch;
  const policy = typeof owners === "object" && owners !== null ? 2
    : owners === "shared" ? 1 : owners === "clear" ? 0 : -1;
  if (policy < 0) throw runtime.failure(1);
  setNativeField(arena, out, type, "owners", policy);
  if (typeof owners !== "object" || owners === null) return;
  const map = owners.map;
  if (typeof map !== "function") throw runtime.failure(1);
  const handler = batch.callback(runtime.raw.callbacks({
    NtSurfaceMapOwner(frame) {
      const callbackArena = frame.arena;
      const size = callbackArena.api.size("NtOwnerToken");
      const owner = callbackArena.unsigned(frame.argument(0), size);
      const mapped = synchronousCompositionValue(map(owner), "Surface owner map must return synchronously");
      const value = callbackArena.value("NtOwnerToken");
      callbackArena.setUnsigned(value, size, toU64(mapped));
      frame.writeOutput(1, value, size);
    },
  }));
  setNativeField(arena, out, type, "map_owner", handler.function("NtSurfaceMapOwner"));
  setNativeField(arena, out, type, "owner_context", handler.context);
}

function projectPanelLayers(runtime: Runtime, destination: OwnedTree, paint: number, layers: readonly PanelLayer[]): void {
  runtime.raw.withArena(arena => {
    const batch = new FrameArguments(runtime, arena, destination);
    try {
      batch.run("nt_paint_project_panels", paint, "NtPanelLayer", layers, (out, layer) => writePanelLayer(batch, out, layer));
    } finally {
      batch.close();
    }
  });
}

function plotOptions(arena: RawArena, plot: PlotOptions): number {
  const type = "NtPlotOptions";
  const options = arena.value(type);
  const data = plot.data;
  const count = data.length;
  const datumSize = arena.api.size("NtPlotDatum");
  const entries = count ? arena.allocate(count * datumSize) : 0;
  const pointOffset = arena.api.field("NtPlotDatum", "point").offset;
  const axes = { x: arena.api.field("NtPoint", "x").offset, y: arena.api.field("NtPoint", "y").offset };
  for (let index = 0; index < count; index++) {
    const datum = data[index];
    const entry = entries + index * datumSize;
    arena.setFloat64(entry + pointOffset + axes.x, datum.x);
    arena.setFloat64(entry + pointOffset + axes.y, datum.y);
    if (datum.color) setNativeField(arena, entry, "NtPlotDatum", "color", packedColor(datum.color));
  }
  setNativeField(arena, options, type, "kind", toU32(plotKinds.indexOf(plot.kind)));
  setNativeField(arena, options, type, "data", entries);
  setNativeField(arena, options, type, "count", count);
  setNativeField(arena, options, type, "data_limit", toU32(plot.dataLimit ?? 0));
  setNativeField(arena, options, type, "sample_stride", toU32(plot.sampleStride ?? 0));
  const domain = plot.domain;
  setNativeField(arena, options, type, "auto_x", !domain || plot.autoX ? 1 : 0);
  setNativeField(arena, options, type, "auto_y", !domain || plot.autoY ? 1 : 0);
  if (domain) {
    writePlotBounds(arena, options + arena.api.field(type, "domain").offset, domain);
  }
  arena.setFloat64(options + arena.api.field(type, "baseline").offset, plot.baseline ?? 0);
  arena.setFloat64(options + arena.api.field(type, "hole_ratio").offset, plot.holeRatio ?? 0);
  arena.setFloat64(options + arena.api.field(type, "cell_aspect").offset, plot.cellAspect ?? 0);
  setNativeField(arena, options, type, "ascii", plot.ascii ? 1 : 0);
  setNativeField(arena, options, type, "equal_units", plot.equalUnits ? 1 : 0);
  setNativeField(arena, options, type, "sample_colors", plot.sampleColors ? 1 : 0);
  setNativeField(arena, options, type, "sampling", toU32(samplingModes.indexOf(plot.sampling ?? "cell")));
  setNativeField(arena, options, type, "foreground", packedColor(plot.foreground ?? defaultStyle.foreground));
  setNativeField(arena, options, type, "background", packedColor(plot.background ?? defaultStyle.background));
  if (plot.preferred) writeNativeSize(arena, options + arena.api.field(type, "preferred").offset, plot.preferred);
  if (plot.reveal) {
    const reveal = options + arena.api.field(type, "reveal").offset;
    setNativeField(arena, reveal, "NtPlotReveal", "start_us", toU64(plot.reveal.startUs ?? 0n));
    setNativeField(arena, reveal, "NtPlotReveal", "duration_us", toU64(plot.reveal.durationUs));
    setNativeField(arena, reveal, "NtPlotReveal", "easing", toU32(plot.reveal.easing ?? Easing.linear));
  }
  return pointerSlot(arena, options);
}

function digitsOptions(arena: RawArena, digits: DigitsOptions): number {
  const type = "NtDigitsOptions";
  const address = arena.value(type);
  const fractionalDigits = toU32(digits.fractionalDigits ?? 0);
  if (fractionalDigits > 255) throw new RangeError("Expected an unsigned 8-bit integer");
  setNativeField(arena, address, type, "value", toI64(digits.value));
  setNativeField(arena, address, type, "fractional_digits", fractionalDigits);
  setNativeField(arena, address, type, "ascii", digits.ascii ? 1 : 0);
  setNativeField(arena, address, type, "foreground", packedColor(digits.foreground ?? defaultStyle.foreground));
  setNativeField(arena, address, type, "background", packedColor(digits.background ?? defaultStyle.background));
  return pointerSlot(arena, address);
}

function particleEffectRecipePointer(arena: RawArena, recipe: ParticleRecipe): number {
  const options = arena.value("NtParticleEffectOptions");
  const slot = pointerSlot(arena, options);
  arena.result("nt_particle_effect_recipe", enumSlot(arena, "NtParticleRecipe", particleRecipes.indexOf(recipe)), slot);
  return slot;
}

function particleEffectOptions(arena: RawArena, effect: ParticleEffectOptions | ParticleEffect): number {
  if ("view" in effect) return authoredParticleEffectPointer(arena, effect, true);
  const type = "NtParticleEffectOptions";
  const slot = particleEffectRecipePointer(arena, effect.recipe);
  const options = arena.pointer(slot);
  const view = options + arena.api.field(type, "view").offset;
  const viewType = "NtParticleViewOptions";
  const emitter = view + arena.api.field(viewType, "emitter").offset;
  const emitterType = "NtParticleEmitterOptions";
  setNativeField(arena, emitter, emitterType, "seed", toU64(effect.seed ?? 0n));
  setNativeField(arena, emitter, emitterType, "stream", toU64(effect.stream ?? 0n));
  setNativeField(arena, emitter, emitterType, "start_us", toU64(effect.startUs ?? 0n));
  if (effect.preferred) writeNativeSize(arena, view + arena.api.field(viewType, "preferred").offset, effect.preferred);
  return slot;
}

function writeParticleRange(arena: RawArena, address: number, range: ParticleRange): void {
  writeContinuousPoint(arena, address + arena.api.field("NtParticleRange", "min").offset, range.min);
  writeContinuousPoint(arena, address + arena.api.field("NtParticleRange", "max").offset, range.max);
}

function particleEmitterOptions(arena: RawArena, options: ParticleEmitterOptions): number {
  const address = arena.call("nt_particle_emitter_options_default");
  const type = "NtParticleEmitterOptions";
  const times = [["seed", options.seed], ["stream", options.stream], ["start_us", options.startUs],
    ["period_us", options.periodUs], ["emission_limit", options.emissionLimit],
    ["lifetime_min_us", options.lifetimeMinUs], ["lifetime_max_us", options.lifetimeMaxUs]] as const;
  for (const [name, value] of times) {
    if (value !== undefined) setNativeField(arena, address, type, name, toU64(value));
  }
  for (const [name, value] of [["births_per_tick", options.birthsPerTick], ["capacity", options.capacity],
    ["color_easing", options.colorEasing]] as const) {
    if (value !== undefined) setNativeField(arena, address, type, name, toU32(value));
  }
  for (const [name, range] of [["origin", options.origin], ["velocity", options.velocity]] as const) {
    if (range === undefined) continue;
    writeParticleRange(arena, address + arena.api.field(type, name).offset, range);
  }
  const acceleration = options.acceleration;
  if (acceleration !== undefined) {
    writeContinuousPoint(arena, address + arena.api.field(type, "acceleration").offset, acceleration);
  }
  for (const [name, color] of [["birth_color", options.birthColor], ["end_color", options.endColor]] as const) {
    if (color !== undefined) {
      const { offset, size } = arena.api.field(type, name);
      arena.setUnsigned(address + offset, size, BigInt(packedColor(color)));
    }
  }
  return pointerSlot(arena, address);
}

function readParticleEmitterOptions(arena: RawArena, address: number): ParticleEmitterOptions {
  const type = "NtParticleEmitterOptions";
  const field = (name: string) => address + arena.api.field(type, name).offset;
  const time = (name: string) => unsignedNativeField(arena, address, type, name);
  const number = (name: string) => nativeField(arena, address, type, name);
  const range = (name: string) => {
    const start = field(name);
    return { min: readContinuousPoint(arena, start + arena.api.field("NtParticleRange", "min").offset),
      max: readContinuousPoint(arena, start + arena.api.field("NtParticleRange", "max").offset) };
  };
  return {
    seed: time("seed"), stream: time("stream"), startUs: time("start_us"), periodUs: time("period_us"),
    emissionLimit: time("emission_limit"), birthsPerTick: number("births_per_tick"), capacity: number("capacity"),
    lifetimeMinUs: time("lifetime_min_us"), lifetimeMaxUs: time("lifetime_max_us"),
    origin: range("origin"), velocity: range("velocity"), acceleration: readContinuousPoint(arena, field("acceleration")),
    birthColor: readRGBA(arena, field("birth_color")), endColor: readRGBA(arena, field("end_color")),
    colorEasing: number("color_easing") as ParticleEmitterOptions["colorEasing"],
  };
}

function readParticle(arena: RawArena, address: number): ParticleSample {
  const type = "NtParticleSample";
  return { id: unsignedNativeField(arena, address, type, "id"),
    bornUs: unsignedNativeField(arena, address, type, "born_us"),
    ageUs: unsignedNativeField(arena, address, type, "age_us"),
    lifetimeUs: unsignedNativeField(arena, address, type, "lifetime_us"),
    progress: arena.float64(address + arena.api.field(type, "progress").offset),
    position: readContinuousPoint(arena, address + arena.api.field(type, "position").offset),
    velocity: readContinuousPoint(arena, address + arena.api.field(type, "velocity").offset),
    color: readRGBA(arena, address + arena.api.field(type, "color").offset) };
}

function writeParticleTrail(arena: RawArena, address: number, options: ParticleTrailOptions): void {
  const type = "NtParticleTrailOptions";
  setNativeField(arena, address, type, "spacing_us", toU64(options.spacingUs));
  setNativeField(arena, address, type, "samples", toU32(options.samples));
}

function particlePaint(arena: RawArena, options: ParticlePaintOptions): number {
  const type = "NtParticlePaintOptions", address = arena.call("nt_particle_paint_options_default");
  const { glyph, width, trail, trailOpacity } = options;
  if (glyph !== undefined) {
    copyNativeValue(arena, "NtByteView", address + arena.api.field(type, "glyph").offset, arena.byteView(glyph));
  }
  if (width !== undefined) writeWidthPolicy(arena, address + arena.api.field(type, "width").offset, width);
  if (trail !== undefined) writeParticleTrail(arena, address + arena.api.field(type, "trail").offset, trail);
  if (trailOpacity !== undefined) arena.setFloat64(address + arena.api.field(type, "trail_opacity").offset, trailOpacity);
  return address;
}

function particleViewPointer(arena: RawArena, options: ParticleViewOptions): number {
  const type = "NtParticleViewOptions", address = arena.call("nt_particle_view_options_default");
  const { emitter, paint, preferred, frameIntervalUs } = options;
  if (emitter !== undefined) {
    copyNativeValue(arena, "NtParticleEmitterOptions", address + arena.api.field(type, "emitter").offset,
      arena.pointer(particleEmitterOptions(arena, emitter)));
  }
  if (paint !== undefined) {
    copyNativeValue(arena, "NtParticlePaintOptions", address + arena.api.field(type, "paint").offset,
      particlePaint(arena, paint));
  }
  if (preferred !== undefined) writeNativeSize(arena, address + arena.api.field(type, "preferred").offset, preferred);
  if (frameIntervalUs !== undefined) setNativeField(arena, address, type, "frame_interval_us", toU64(frameIntervalUs));
  return pointerSlot(arena, address);
}

function readParticlePaint(runtime: Runtime, arena: RawArena, address: number): ParticlePaintOptions {
  const field = (name: string) => address + arena.api.field("NtParticlePaintOptions", name).offset;
  const width = field("width"), trail = field("trail");
  return {
    glyph: readByteView(runtime, arena, field("glyph")),
    width: {
      ambiguous: nativeField(arena, width, "NtWidthPolicy", "ambiguous") as WidthPolicy["ambiguous"],
      emoji: nativeField(arena, width, "NtWidthPolicy", "emoji") as WidthPolicy["emoji"],
    },
    trail: {
      spacingUs: unsignedNativeField(arena, trail, "NtParticleTrailOptions", "spacing_us"),
      samples: nativeField(arena, trail, "NtParticleTrailOptions", "samples"),
    },
    trailOpacity: arena.float64(field("trail_opacity")),
  };
}

function readParticleEffect(runtime: Runtime, arena: RawArena, address: number): ParticleEffect {
  const type = "NtParticleEffectOptions";
  const view = address + arena.api.field(type, "view").offset;
  const viewType = "NtParticleViewOptions";
  const motion = address + arena.api.field(type, "motion").offset;
  const motionType = "NtParticleEffectMotion";
  const motionField = (name: string) => motion + arena.api.field(motionType, name).offset;
  return {
    view: {
      emitter: readParticleEmitterOptions(arena, view + arena.api.field(viewType, "emitter").offset),
      paint: readParticlePaint(runtime, arena, view + arena.api.field(viewType, "paint").offset),
      preferred: nativeSize(arena, view + arena.api.field(viewType, "preferred").offset),
      frameIntervalUs: unsignedNativeField(arena, view, viewType, "frame_interval_us"),
    },
    motion: {
      radial: nativeField(arena, motion, motionType, "radial") !== 0,
      directionTurns: arena.float64(motionField("direction_turns")), sweepTurns: arena.float64(motionField("sweep_turns")),
      speedMin: arena.float64(motionField("speed_min")), speedMax: arena.float64(motionField("speed_max")),
      swayAmplitude: readContinuousPoint(arena, motionField("sway_amplitude")),
      swayPeriodUs: unsignedNativeField(arena, motion, motionType, "sway_period_us"),
      glitterPeriodUs: unsignedNativeField(arena, motion, motionType, "glitter_period_us"),
      glitterMinAlpha: nativeField(arena, motion, motionType, "glitter_min_alpha"),
    },
  };
}

function authoredParticleEffectPointer(arena: RawArena, effect: ParticleEffect, mounted: boolean): number {
  const type = "NtParticleEffectOptions", address = arena.value(type);
  const viewType = "NtParticleViewOptions", view = address + arena.api.field(type, "view").offset;
  if (mounted) {
    copyNativeValue(arena, viewType, view, arena.pointer(particleViewPointer(arena, effect.view)));
  } else {
    copyNativeValue(arena, "NtParticleEmitterOptions", view + arena.api.field(viewType, "emitter").offset,
      arena.pointer(particleEmitterOptions(arena, effect.view.emitter ?? {})));
  }
  const motionType = "NtParticleEffectMotion", motion = address + arena.api.field(type, "motion").offset;
  const options = effect.motion;
  setNativeField(arena, motion, motionType, "radial", options.radial ? 1 : 0);
  for (const [name, value] of [["direction_turns", options.directionTurns], ["sweep_turns", options.sweepTurns],
    ["speed_min", options.speedMin], ["speed_max", options.speedMax]] as const) {
    arena.setFloat64(motion + arena.api.field(motionType, name).offset, value);
  }
  writeContinuousPoint(arena, motion + arena.api.field(motionType, "sway_amplitude").offset, options.swayAmplitude);
  setNativeField(arena, motion, motionType, "sway_period_us", toU64(options.swayPeriodUs));
  setNativeField(arena, motion, motionType, "glitter_period_us", toU64(options.glitterPeriodUs));
  const minAlpha = toU32(options.glitterMinAlpha);
  if (minAlpha > 255) throw new RangeError("Expected an unsigned 8-bit integer");
  setNativeField(arena, motion, motionType, "glitter_min_alpha", minAlpha);
  return pointerSlot(arena, address);
}

function visualBranchOptions(arena: RawArena, options: VisualBranchOptions): number {
  const type = "NtVisualBranchOptions", address = arena.call("nt_visual_branch_options_default");
  for (const [name, value] of [["seed", options.seed], ["stream", options.stream], ["start_us", options.startUs],
    ["lifetime_us", options.lifetimeUs], ["reveal_us", options.revealUs], ["flicker_us", options.flickerUs]] as const) {
    if (value !== undefined) setNativeField(arena, address, type, name, toU64(value));
  }
  for (const [name, value] of [["trunk_segments", options.trunkSegments], ["branch_count", options.branchCount],
    ["branch_segments", options.branchSegments], ["jitter_cells", options.jitterCells],
    ["branch_reach_cells", options.branchReachCells]] as const) {
    if (value !== undefined) setNativeField(arena, address, type, name, toU32(value));
  }
  for (const [name, point] of [["start", options.start], ["end", options.end]] as const) {
    if (point !== undefined) writeCellPoint(arena, address + arena.api.field(type, name).offset, point);
  }
  const color = options.color;
  if (color !== undefined) {
    copyNativeValue(arena, "NtRgba", address + arena.api.field(type, "color").offset, rgbaSlot(arena, color));
  }
  const alpha = options.flickerMinAlpha;
  if (alpha !== undefined) {
    if (toU32(alpha) > 255) throw new RangeError("Flicker minimum alpha must be an unsigned byte");
    setNativeField(arena, address, type, "flicker_min_alpha", alpha);
  }
  return pointerSlot(arena, address);
}

function depositsOptions(arena: RawArena, options: ParticleDepositsOptions): number {
  const type = "NtParticleDepositsOptions", address = arena.call("nt_particle_deposits_options_default");
  const { seed, stream, capacity, holdUs, release } = options;
  if (seed !== undefined) setNativeField(arena, address, type, "seed", toU64(seed));
  if (stream !== undefined) setNativeField(arena, address, type, "stream", toU64(stream));
  if (capacity !== undefined) setNativeField(arena, address, type, "capacity", toU32(capacity));
  if (holdUs !== undefined) setNativeField(arena, address, type, "hold_us", toU64(holdUs));
  if (release !== undefined) {
    const kind = "NtParticleReleaseOptions", native = address + arena.api.field(type, "release").offset;
    const { count, lifetimeMinUs, lifetimeMaxUs, velocity, acceleration, endColor, colorEasing } = release;
    if (count !== undefined) setNativeField(arena, native, kind, "count", toU32(count));
    if (lifetimeMinUs !== undefined) setNativeField(arena, native, kind, "lifetime_min_us", toU64(lifetimeMinUs));
    if (lifetimeMaxUs !== undefined) setNativeField(arena, native, kind, "lifetime_max_us", toU64(lifetimeMaxUs));
    if (velocity !== undefined) writeParticleRange(arena, native + arena.api.field(kind, "velocity").offset, velocity);
    if (acceleration !== undefined) {
      writeContinuousPoint(arena, native + arena.api.field(kind, "acceleration").offset, acceleration);
    }
    if (endColor !== undefined) {
      copyNativeValue(arena, "NtRgba", native + arena.api.field(kind, "end_color").offset, rgbaSlot(arena, endColor));
    }
    if (colorEasing !== undefined) setNativeField(arena, native, kind, "color_easing", toU32(colorEasing));
  }
  return pointerSlot(arena, address);
}

function depositContact(arena: RawArena, contact: ParticleContact): number {
  const type = "NtParticleContact", address = arena.value(type);
  const { cell, at, mass, color } = contact;
  writeCellPoint(arena, address + arena.api.field(type, "cell").offset, cell);
  setNativeField(arena, address, type, "at", toU64(at));
  setNativeField(arena, address, type, "mass", toU32(mass));
  copyNativeValue(arena, "NtRgba", address + arena.api.field(type, "color").offset, rgbaSlot(arena, color));
  return address;
}

function depositPaint(arena: RawArena, options: ParticleDepositPaint): number {
  const type = "NtParticleDepositPaint", address = arena.call("nt_particle_deposit_paint_default");
  const { pileGlyph, pileWidth, release } = options;
  if (pileGlyph !== undefined) {
    copyNativeValue(arena, "NtByteView", address + arena.api.field(type, "pile_glyph").offset, arena.byteView(pileGlyph));
  }
  if (pileWidth !== undefined) writeWidthPolicy(arena, address + arena.api.field(type, "pile_width").offset, pileWidth);
  if (release !== undefined) {
    copyNativeValue(arena, "NtParticlePaintOptions", address + arena.api.field(type, "release").offset,
      particlePaint(arena, release));
  }
  return pointerSlot(arena, address);
}

type UseParticleSource = <T>(call: (arena: RawArena, native: number) => T) => T;
type ParticleSource = { readonly runtime: Runtime; readonly use: UseParticleSource };
const particleEmitterSources = new WeakMap<ParticleEmitter, ParticleSource>();
const particleDepositSources = new WeakMap<ParticleDeposits, ParticleSource>();

function ownedNativeSource(runtime: Runtime, handle: number, destroy: string) {
  let busy = false;
  function use<T>(call: (arena: RawArena, native: number) => T): T {
    if (!handle) throw runtime.failure(5);
    if (busy) throw runtime.failure(6);
    busy = true;
    try {
      return runtime.raw.withArena(arena => call(arena, pointerSlot(arena, handle)));
    } finally { busy = false; }
  }
  function close(): void {
    if (!handle) return;
    use((arena, native) => { arena.call(destroy, native); });
    handle = 0;
  }
  return { runtime, use, close };
}

function readDecoderResult(runtime: Runtime, arena: RawArena, address: number): InputRead {
  const type = "NtInputRead";
  const status = nativeField(arena, address, type, "status");
  const consumed = nativeField(arena, address, type, "consumed");
  if (status !== 1) return { status: status === 2 ? "discarded" : "pending", consumed };
  const event = address + arena.api.field(type, "event").offset;
  // Decoder output contains host events only. Focus-out is a host cancellation.
  let input = readObservedInput(runtime, arena, event) as Input;
  if (input.kind === EventKind.key) {
    const text = readByteView(runtime, arena, address + arena.api.field(type, "key_text").offset);
    if (text.length) input = { ...input, text };
  }
  return { status: "ready", consumed, at: unsignedNativeField(arena, event, "NtEvent", "at"), input };
}

function ownedInputDecoder(runtime: Runtime, handle: number): InputDecoder {
  const { use, close } = ownedNativeSource(runtime, handle, "nt_input_destroy");
  return Object.freeze<InputDecoder>({
    read: (bytes, now) => use((arena, decoder) => {
      if (typedArrayTag.call(bytes) !== "Uint8Array") throw runtime.failure(1);
      const length = typedArrayLength.call(bytes);
      const input = length === 0 ? new Uint8Array() : new Uint8Array(
        typedArrayBuffer.call(bytes), typedArrayOffset.call(bytes), length);
      const out = arena.value("NtInputRead");
      try {
        arena.result("nt_input_read", decoder, arena.byteView(input), timeSlot(arena, now), pointerSlot(arena, out));
      } catch (error) {
        if (error instanceof NativeError && error.code === 3) {
          throw new InputReadError(error, readDecoderResult(runtime, arena, out));
        }
        throw error;
      }
      return readDecoderResult(runtime, arena, out);
    }),
    deadline: () => use((arena, decoder) => {
      const out = arena.value("NtTimeUs");
      const scheduled = arena.call("nt_input_deadline", decoder, pointerSlot(arena, out));
      return arena.unsigned(scheduled, arena.api.size("_Bool")) !== 0n
        ? arena.unsigned(out, arena.api.size("NtTimeUs")) : undefined;
    }),
    expectCursorReport: expected => use((arena, decoder) => {
      arena.result("nt_input_expect_cursor_report", decoder, boolSlot(arena, expected));
    }),
    cancel: (now, reason) => use((arena, decoder) => {
      const out = arena.value("NtEvent");
      arena.result("nt_input_cancel", decoder, timeSlot(arena, now), enumSlot(arena, "NtCancelReason", reason), pointerSlot(arena, out));
      return readObservedInput(runtime, arena, out) as Input;
    }),
    close,
  });
}

function particleDeadline(arena: RawArena, runtime: Runtime, native: number, operation: string,
  now: bigint, motion: Motion, frameIntervalUs: bigint): bigint | undefined {
  const type = "NtDeadline", out = arena.value(type);
  arena.result(operation, native, timeSlot(arena, now),
    enumSlot(arena, "NtMotionPolicy", enumCode(runtime, motionCode, motion)), timeSlot(arena, frameIntervalUs),
    pointerSlot(arena, out));
  return nativeField(arena, out, type, "scheduled") ? unsignedNativeField(arena, out, type, "at") : undefined;
}

function ownedParticleEmitter(runtime: Runtime, handle: number): ParticleEmitter {
  const source = ownedNativeSource(runtime, handle, "nt_particle_emitter_destroy");
  const { use, close } = source;
  const emitter = Object.freeze<ParticleEmitter>({
    sample: now => use((arena, native) => readNativeRows(arena, "nt_particle_emitter_sample", "NtParticleSample",
      [native, timeSlot(arena, now)], address => readParticle(arena, address))),
    particle: (id, now) => use((arena, native) => {
      const live = arena.value("_Bool"), out = arena.value("NtParticleSample");
      arena.result("nt_particle_emitter_particle", native, timeSlot(arena, id), timeSlot(arena, now),
        pointerSlot(arena, live), pointerSlot(arena, out));
      return arena.unsigned(live, arena.api.size("_Bool")) === 0n ? undefined : readParticle(arena, out);
    }),
    trail: (id, now, options) => use((arena, native) => {
      const type = "NtParticleTrailOptions", trail = arena.value(type);
      writeParticleTrail(arena, trail, options);
      return readNativeRows(arena, "nt_particle_emitter_trail", "NtParticleSample",
        [native, timeSlot(arena, id), timeSlot(arena, now), trail], address => readParticle(arena, address));
    }),
    deadline: (now, motion = "full", frameIntervalUs = 0n) => use((arena, native) =>
      particleDeadline(arena, runtime, native, "nt_particle_emitter_deadline", now, motion, frameIntervalUs)),
    close,
  });
  particleEmitterSources.set(emitter, source);
  return emitter;
}

function ownedParticleDeposits(runtime: Runtime, handle: number): ParticleDeposits {
  const source = ownedNativeSource(runtime, handle, "nt_particle_deposits_destroy");
  const { use, close } = source;
  const store = Object.freeze<ParticleDeposits>({
    add: contact => use((arena, native) => {
      const out = arena.value("uint64_t");
      arena.result("nt_particle_deposits_add", native, depositContact(arena, contact), pointerSlot(arena, out));
      return arena.unsigned(out, arena.api.size("uint64_t"));
    }),
    advance: now => use((arena, native) => { arena.result("nt_particle_deposits_advance", native, timeSlot(arena, now)); }),
    piles: now => use((arena, native) => {
      const type = "NtParticlePile";
      return readNativeRows(arena, "nt_particle_deposits_piles", type, [native, timeSlot(arena, now)], address => ({
        cell: nativeCellPoint(arena, address + arena.api.field(type, "cell").offset),
        contacts: nativeField(arena, address, type, "contacts"),
        mass: unsignedNativeField(arena, address, type, "mass"),
        color: readRGBA(arena, address + arena.api.field(type, "color").offset),
      }));
    }),
    releases: now => use((arena, native) => {
      const type = "NtParticleRelease";
      return readNativeRows(arena, "nt_particle_deposits_releases", type, [native, timeSlot(arena, now)], address => ({
        depositId: unsignedNativeField(arena, address, type, "deposit_id"),
        particle: readParticle(arena, address + arena.api.field(type, "particle").offset),
      }));
    }),
    deadline: (now, motion = "full", frameIntervalUs = 0n) => use((arena, native) =>
      particleDeadline(arena, runtime, native, "nt_particle_deposits_deadline", now, motion, frameIntervalUs)),
    close,
  });
  particleDepositSources.set(store, source);
  return store;
}

function colorAt(data: DataView, address: number): Color {
  return [data.getUint8(address), data.getUint8(address + 1), data.getUint8(address + 2), data.getUint8(address + 3)];
}

function storeInput(runtime: Runtime, address: number, now: bigint, input: Input): Utf8 {
  new Uint8Array(runtime.native.memory.buffer, address, 72).fill(0);
  const word = (offset: number, value: number) => runtime.view().setUint32(address + offset, toU32(value), true);
  const coordinate = (offset: number, value: number) => runtime.view().setInt32(address + offset, toI32(value), true);
  const identity = (offset: number, value: bigint) => runtime.view().setBigUint64(address + offset, toU64(value), true);
  identity(0, now);
  word(16, input.kind);
  switch (input.kind) {
    case EventKind.key:
      word(20, input.key.code);
      word(24, input.key.state ?? KeyState.press);
      word(28, input.key.modifiers ?? 0);
      return input.text ?? "";
    case EventKind.text:
    case EventKind.paste:
      return input.text;
    case EventKind.pointerDown:
    case EventKind.pointerMove:
    case EventKind.pointerUp: {
      const at = input.at;
      identity(8, input.pointer);
      word(28, input.modifiers ?? 0);
      word(32, input.button ?? 0);
      word(36, input.pressed ?? 0);
      if (at) {
        word(40, 1);
        coordinate(44, at.x);
        coordinate(48, at.y);
      }
      break;
    }
    case EventKind.pointerCancel:
      identity(8, input.pointer);
      word(68, input.reason ?? 0);
      break;
    case EventKind.hostCancel:
      word(68, input.reason ?? 0);
      break;
    case EventKind.wheel:
      word(28, input.modifiers ?? 0);
      coordinate(44, input.at.x);
      coordinate(48, input.at.y);
      coordinate(52, input.dx);
      coordinate(56, input.dy);
      break;
    case EventKind.resize:
      word(60, input.size.columns);
      word(64, input.size.rows);
      break;
  }
  return "";
}

type FrameLease = { readonly handle: number; readonly published?: true; readonly release?: () => void };
type FrameSource = {
  readonly owner: object;
  readonly runtime: Runtime;
  readonly publication: object;
  claim(destination?: OwnedTree): FrameLease;
};

const frameSources = new WeakMap<Frame, FrameSource>();

function ansiOptions(runtime: Runtime, arena: RawArena, options: AnsiOptions): number {
  const type = "NtAnsiPresenter", address = arena.value(type);
  const capsType = "NtTerminalCapabilities", caps = address + arena.api.field(type, "capabilities").offset;
  const settings = options.capabilities;
  setNativeField(arena, caps, capsType, "color", enumCode(runtime, terminalColors, settings.color ?? "mono"));
  for (const [field, value] of [["interactive", settings.interactive], ["utf8", settings.utf8],
    ["half_blocks", settings.halfBlocks], ["braille", settings.braille], ["pointer", settings.pointer],
    ["bracketed_paste", settings.bracketedPaste]] as const) {
    setNativeField(arena, caps, capsType, field, value ? 1 : 0);
  }
  writeWidthPolicy(arena, caps + arena.api.field(capsType, "width").offset, settings.width);
  if (options.matte) setNativeField(arena, address, type, "matte", packedColor(options.matte));
  const colors = options.palette;
  if (colors?.length) {
    const count = toU32(colors.length), colorType = "NtRgba";
    const size = arena.api.size(colorType), data = valueArray(arena, colorType, count);
    for (let index = 0; index < count; index++) {
      arena.setUnsigned(data + index * size, size, BigInt(packedColor(colors[index])));
    }
    const paletteType = "NtPaletteView", palette = address + arena.api.field(type, "palette").offset;
    setNativeField(arena, palette, paletteType, "colors", data);
    setNativeField(arena, palette, paletteType, "count", count);
  }
  return address;
}

function writeAnsi(runtime: Runtime, frame: Frame, now: bigint, options: AnsiOptions,
  write: (bytes: Uint8Array) => void): void {
  if (typeof write !== "function") throw runtime.failure(1);
  runtime.raw.withArena(arena => {
    const args = new FrameArguments(runtime, arena);
    try {
      const native = args.frame(frame), context = ansiOptions(runtime, arena, options);
      const handler = args.callback(runtime.raw.callbacks({
        NtOutputWrite(callback) {
          const bytes = readByteViewBytes(callback.arena, callback.argument(0));
          synchronousCompositionValue(write(bytes), "ANSI writer must complete synchronously");
        },
      }));
      setNativeField(arena, context, "NtAnsiPresenter", "write", handler.function("NtOutputWrite"));
      setNativeField(arena, context, "NtAnsiPresenter", "write_context", handler.context);
      try {
        arena.result("nt_ansi_present", pointerSlot(arena, context), pointerSlot(arena, native), timeSlot(arena, now));
      } catch (error) {
        const failures = handler.takeErrors();
        if (failures.length) throw failures[0];
        throw error;
      }
    } finally {
      args.close();
    }
  });
}

function ownerSlot(arena: RawArena, owner: bigint): number {
  const out = arena.value("NtOwnerToken");
  arena.setUnsigned(out, arena.api.size("NtOwnerToken"), toU64(owner));
  return out;
}

function glyphColumns(runtime: Runtime, arena: RawArena, columns: number): number {
  if (columns !== 1 && columns !== 2) throw runtime.failure(1);
  const out = arena.value("uint8_t");
  arena.setUnsigned(out, 1, BigInt(columns));
  return out;
}

type UseOffscreen = <T>(call: (frame: number) => T) => T;

function requireGraph(runtime: Runtime): void {
  if (!runtime.raw.signature("nt_graph_layout_create")) throw runtime.failure(7);
}

function graphInput(runtime: Runtime, arena: RawArena, graph: Graph): [number, number] {
  const { nodes, edges, layout, forceIterations } = graph;
  const nodeCount = toU32(nodes.length), edgeCount = toU32(edges.length);
  if (nodeCount > 512 || edgeCount > 4096) throw runtime.failure(2);
  const dataType = "NtGraphData", layoutType = "NtGraphLayoutOptions";
  const data = arena.value(dataType), options = arena.value(layoutType);
  const nodeType = "NtGraphNode", edgeType = "NtGraphEdge";
  const nodeArray = valueArray(arena, nodeType, nodeCount), edgeArray = valueArray(arena, edgeType, edgeCount);
  const nodeSize = arena.api.size(nodeType), edgeSize = arena.api.size(edgeType);
  for (let index = 0; index < nodeCount; index++) {
    const { id, pinned, position } = nodes[index];
    const address = nodeArray + index * nodeSize;
    setNativeField(arena, address, nodeType, "id", toU64(id));
    setNativeField(arena, address, nodeType, "pinned", pinned ? 1 : 0);
    writeContinuousPoint(arena, address + arena.api.field(nodeType, "position").offset,
      { x: position?.x ?? 0, y: position?.y ?? 0 });
  }
  for (let index = 0; index < edgeCount; index++) {
    const { id, source, target, directed } = edges[index];
    const address = edgeArray + index * edgeSize;
    setNativeField(arena, address, edgeType, "id", toU64(id));
    setNativeField(arena, address, edgeType, "source", toU64(source));
    setNativeField(arena, address, edgeType, "target", toU64(target));
    setNativeField(arena, address, edgeType, "directed", directed ? 1 : 0);
  }
  setNativeField(arena, data, dataType, "nodes", nodeArray);
  setNativeField(arena, data, dataType, "node_count", nodeCount);
  setNativeField(arena, data, dataType, "edges", edgeArray);
  setNativeField(arena, data, dataType, "edge_count", edgeCount);
  setNativeField(arena, options, layoutType, "kind", toU32(graphLayout[layout ?? "layered"]));
  setNativeField(arena, options, layoutType, "force_iterations", toU32(forceIterations ?? 0));
  return [data, options];
}

function graphViewOptions(runtime: Runtime, arena: RawArena, graph: Graph): number {
  const [data, layout] = graphInput(runtime, arena, graph);
  const type = "NtGraphViewOptions", address = arena.call("nt_graph_view_options_default");
  copyNativeValue(arena, "NtGraphData", address + arena.api.field(type, "data").offset, data);
  copyNativeValue(arena, "NtGraphLayoutOptions", address + arena.api.field(type, "layout").offset, layout);
  const { preferred, width, colors, labels } = graph;
  if (preferred) {
    const size = { columns: toU32(preferred.columns ?? 0), rows: toU32(preferred.rows ?? 0) };
    if (size.columns || size.rows) writeNativeSize(arena, address + arena.api.field(type, "preferred").offset, size);
  }
  if (width) writeWidthPolicy(arena, address + arena.api.field(type, "width").offset, width);
  if (colors) {
    for (const key of ["node", "pinned", "edge", "selected", "background"] as const) {
      const value = colors[key];
      if (value !== undefined) setNativeField(arena, address, type, key, packedColor(value));
    }
  }
  if (labels) {
    const count = toU32(labels.length), labelType = "NtGraphLabel";
    if (count > 512) throw runtime.failure(2);
    const start = valueArray(arena, labelType, count), size = arena.api.size(labelType);
    for (let index = 0; index < count; index++) {
      const { node, text } = labels[index], label = start + size * index;
      if (text.length > 256) throw runtime.failure(2);
      setNativeField(arena, label, labelType, "node", toU64(node));
      copyNativeValue(arena, "NtByteView", label + arena.api.field(labelType, "text").offset, arena.byteView(text));
    }
    setNativeField(arena, address, type, "labels", start);
    setNativeField(arena, address, type, "label_count", count);
  }
  return pointerSlot(arena, address);
}

function copyGraphRows<Row>(arena: RawArena, layout: number, operation: string, type: string,
  read: (address: number, type: string) => Row): Row[] {
  const count = arena.value("size_t");
  const start = arena.pointer(arena.call(operation, layout, pointerSlot(arena, count)));
  const length = Number(arena.unsigned(count, arena.api.size("size_t"))), stride = arena.api.size(type);
  return Array.from({ length }, (_, index) => read(start + index * stride, type));
}

function graphSnapshot(runtime: Runtime, graph: Graph): GraphSnapshot {
  requireGraph(runtime);
  return runtime.raw.withArena(arena => {
    const [data, layout] = graphInput(runtime, arena, graph), out = arena.value("pointer");
    arena.result("nt_graph_layout_create", pointerSlot(arena, 0), data, layout, pointerSlot(arena, out));
    try {
      const nodes = copyGraphRows(arena, out, "nt_graph_layout_nodes", "NtGraphNode", (address, type) => ({
        id: unsignedNativeField(arena, address, type, "id"),
        pinned: nativeField(arena, address, type, "pinned") !== 0,
        position: readContinuousPoint(arena, address + arena.api.field(type, "position").offset),
      }));
      const edges = copyGraphRows(arena, out, "nt_graph_layout_edges", "NtGraphEdge", (address, type) => ({
        id: unsignedNativeField(arena, address, type, "id"),
        source: unsignedNativeField(arena, address, type, "source"),
        target: unsignedNativeField(arena, address, type, "target"),
        directed: nativeField(arena, address, type, "directed") !== 0,
      }));
      return { nodes, edges };
    } finally {
      arena.call("nt_graph_layout_destroy", out);
    }
  });
}

function graphHit(arena: RawArena, address: number): GraphHit {
  return { kind: graphHitKinds[nativeField(arena, address, "NtGraphHit", "kind")],
    id: unsignedNativeField(arena, address, "NtGraphHit", "id") };
}

function samplingModeSlot(runtime: Runtime, arena: RawArena, mode: SamplingMode): number {
  const native = samplingModes.indexOf(mode);
  if (native < 0) throw runtime.failure(1);
  return enumSlot(arena, "NtSamplingMode", native);
}

function canvasInk(arena: RawArena, ink: CanvasInk): number {
  const type = "NtCanvasInk", address = arena.value(type);
  setNativeField(arena, address, type, "color", packedColor(ink.color));
  setNativeField(arena, address, type, "owner", toU64(ink.owner ?? 0n));
  setNativeField(arena, address, type, "composite", arena.pointer(compositePointer(arena, ink.composite)));
  return address;
}

function canvasPixel(arena: RawArena, address: number): CanvasPixel {
  const type = "NtCanvasPixel";
  return { color: readRGBA(arena, address + arena.api.field(type, "color").offset),
    owner: unsignedNativeField(arena, address, type, "owner"),
    covered: nativeField(arena, address, type, "covered") !== 0 };
}

function reduceCellSamples(runtime: Runtime, pixels: readonly CanvasPixel[],
  background: readonly Color[], mode: SamplingMode): SampledCell {
  if (pixels.length !== 8 || background.length !== 8) throw runtime.failure(1);
  return runtime.raw.withArena(arena => {
    const type = "NtCellSamples", input = arena.value(type), output = arena.value("NtSampledCell");
    const pixelStart = input + arena.api.field(type, "pixels").offset;
    const backgroundStart = input + arena.api.field(type, "background").offset;
    const pixelType = "NtCanvasPixel", pixelSize = arena.api.size(pixelType), colorSize = arena.api.size("NtRgba");
    for (let index = 0; index < 8; index++) {
      const pixel = pixels[index], address = pixelStart + index * pixelSize;
      setNativeField(arena, address, pixelType, "color", packedColor(pixel.color));
      setNativeField(arena, address, pixelType, "owner", toU64(pixel.owner));
      setNativeField(arena, address, pixelType, "covered", pixel.covered ? 1 : 0);
      arena.setUnsigned(backgroundStart + index * colorSize, colorSize, BigInt(packedColor(background[index])));
    }
    const outputType = "NtSampledCell";
    arena.result("nt_cell_sample", pointerSlot(arena, input), samplingModeSlot(runtime, arena, mode), pointerSlot(arena, output));
    return { glyph: runtime.decode(output + arena.api.field(outputType, "glyph").offset,
      nativeField(arena, output, outputType, "glyph_size")),
      style: readCellStyle(arena, output + arena.api.field(outputType, "style").offset),
      owner: unsignedNativeField(arena, output, outputType, "owner") };
  });
}

function ownedCanvas(runtime: Runtime, handle: number): Canvas {
  const { use, close } = ownedNativeSource(runtime, handle, "nt_canvas_destroy");
  function drawPoints(operation: string, points: readonly Point[], ink: CanvasInk): void {
    use((arena, canvas) => {
      arena.result(operation, canvas, ...points.map(point => cellPointSlot(arena, point)), canvasInk(arena, ink));
    });
  }
  function drawRect(operation: string, bounds: Bounds, ink: CanvasInk): void {
    use((arena, canvas) => {
      const rect = arena.value("NtCellRect");
      writeNativeBounds(arena, rect, bounds);
      arena.result(operation, canvas, rect, canvasInk(arena, ink));
    });
  }
  return Object.freeze<Canvas>({
    get size() { return use((arena, canvas) => nativeSize(arena, arena.call("nt_canvas_size", canvas))); },
    clear: background => use((arena, canvas) => { arena.result("nt_canvas_clear", canvas, rgbaSlot(arena, background)); }),
    pixel: at => use((arena, canvas) => {
      const output = arena.value("NtCanvasPixel");
      arena.result("nt_canvas_pixel", canvas, cellPointSlot(arena, at), pointerSlot(arena, output));
      return canvasPixel(arena, output);
    }),
    point: (at, ink) => drawPoints("nt_canvas_point", [at], ink),
    line: (from, to, ink) => drawPoints("nt_canvas_line", [from, to], ink),
    triangle: (a, b, c, ink) => drawPoints("nt_canvas_triangle", [a, b, c], ink),
    fill: (bounds, ink) => drawRect("nt_canvas_fill", bounds, ink),
    border: (bounds, ink) => drawRect("nt_canvas_border", bounds, ink),
    render: mode => use((arena, canvas) => {
      const output = arena.value("pointer");
      arena.result("nt_canvas_render", canvas, samplingModeSlot(runtime, arena, mode),
        pointerSlot(arena, 0), pointerSlot(arena, output));
      return new OwnedOffscreenFrame(runtime, arena.pointer(output));
    }),
    close,
  });
}

class OwnedOffscreenFrame implements OffscreenFrame {
  readonly #runtime: Runtime;
  #handle: number;
  #busy = false;

  constructor(runtime: Runtime, handle: number) {
    this.#runtime = runtime;
    this.#handle = handle;
    frameSources.set(this, {
      owner: this, runtime, publication: this,
      claim: () => ({ handle: this.#acquire(), release: () => { this.#busy = false; } }),
    });
  }

  #acquire(): number {
    if (!this.#handle) throw this.#runtime.failure(5);
    if (this.#busy) throw this.#runtime.failure(6);
    this.#busy = true;
    return this.#handle;
  }

  #use<T>(call: (frame: number) => T): T {
    const frame = this.#acquire();
    try {
      return call(frame);
    } finally {
      this.#busy = false;
    }
  }

  #call<T>(call: (arena: RawArena, pointer: number) => T): T {
    return this.#use(frame => this.#runtime.raw.withArena(arena => call(arena, pointerSlot(arena, frame))));
  }

  get size(): Size {
    return this.#call((arena, frame) => nativeSize(arena, arena.call("nt_frame_size", frame)));
  }

  cell(at: Point): Cell {
    return this.#call((arena, frame) => {
      const type = "NtCellView";
      const out = arena.value(type);
      arena.result("nt_frame_cell", frame, cellPointSlot(arena, at), pointerSlot(arena, out));
      const owner = arena.api.field(type, "owner");
      return {
        owner: arena.unsigned(out + owner.offset, owner.size),
        glyph: readByteView(this.#runtime, arena, out + arena.api.field(type, "glyph").offset),
        lead: nativeCellPoint(arena, out + arena.api.field(type, "lead").offset),
        columns: nativeField(arena, out, type, "glyph_columns"),
        style: readCellStyle(arena, out + arena.api.field(type, "style").offset),
      };
    });
  }

  background(at: Point): CellBackground {
    return this.#call((arena, frame) => {
      const type = "NtCellBackground";
      const out = arena.value(type);
      arena.result("nt_frame_background", frame, cellPointSlot(arena, at), pointerSlot(arena, out));
      const owner = arena.api.field(type, "owner");
      return { owner: arena.unsigned(out + owner.offset, owner.size),
        color: colorAt(this.#runtime.view(), out + arena.api.field(type, "color").offset) };
    });
  }

  resize(size: Size): void {
    this.#call((arena, frame) => {
      const bounds = arena.value("NtCellSize");
      writeNativeSize(arena, bounds, size);
      arena.result("nt_frame_resize", frame, bounds);
    });
  }

  setGlyph(at: Point, glyph: Utf8, columns: number, style: Style, owner = 0n): void {
    this.#call((arena, frame) => {
      const value = arena.value("NtCellStyle");
      writeCellStyle(arena, value, style);
      arena.result("nt_frame_set_glyph", frame, cellPointSlot(arena, at), arena.byteView(glyph),
        glyphColumns(this.#runtime, arena, columns), value, ownerSlot(arena, owner));
    });
  }

  clearGlyph(at: Point): void {
    this.#call((arena, frame) => arena.result("nt_frame_clear_glyph", frame, cellPointSlot(arena, at)));
  }

  surface(): Surface {
    return this.#call((arena, frame) => {
      const out = arena.value("NtSurfaceView");
      arena.result("nt_surface_root", frame, pointerSlot(arena, out));
      return new OwnedSurface(this.#runtime, this, call => this.#use(call), surfaceSnapshot(arena, out));
    });
  }

  close(): void {
    if (!this.#handle) return;
    this.#call((arena, frame) => {
      arena.call("nt_frame_destroy", frame);
      this.#handle = 0;
    });
  }
}

const surfaceType = "NtSurfaceView";

function surfaceMaskAddress(arena: RawArena, view: number): number {
  return view + arena.api.field(surfaceType, "mask").offset;
}

function surfaceSnapshot(arena: RawArena, view: number): Uint8Array {
  setNativeField(arena, view, surfaceType, "frame", 0);
  setNativeField(arena, view, surfaceType, "parent", 0);
  setNativeField(arena, surfaceMaskAddress(arena, view), "NtSurfaceMask", "alpha", 0);
  return arena.read(view, arena.api.size(surfaceType));
}

class OwnedSurface implements Surface {
  readonly #runtime: Runtime;
  readonly #owner: OffscreenFrame;
  readonly #useFrame: UseOffscreen;
  readonly #view: Uint8Array;
  readonly #alpha?: Uint8Array;
  readonly #parent?: OwnedSurface;

  constructor(runtime: Runtime, owner: OffscreenFrame, useFrame: UseOffscreen, view: Uint8Array,
    alpha?: Uint8Array, parent?: OwnedSurface) {
    this.#runtime = runtime;
    this.#owner = owner;
    this.#useFrame = useFrame;
    this.#view = view;
    this.#alpha = alpha;
    this.#parent = parent;
  }

  #call<T>(call: (arena: RawArena, surface: number, frame: number) => T): T {
    return this.#useFrame(frame => this.#runtime.raw.withArena(arena => {
      const ancestry: OwnedSurface[] = [];
      for (let view: OwnedSurface | undefined = this; view; view = view.#parent) ancestry.push(view);
      const size = arena.api.size(surfaceType);
      const native = arena.allocate(size * ancestry.length);
      for (const [index, view] of ancestry.entries()) {
        const at = native + size * index;
        arena.write(at, view.#view);
        setNativeField(arena, at, surfaceType, "frame", frame);
        if (index + 1 < ancestry.length) setNativeField(arena, at, surfaceType, "parent", at + size);
        if (view.#alpha?.length) {
          const bytes = arena.allocate(view.#alpha.length);
          arena.write(bytes, view.#alpha);
          setNativeField(arena, surfaceMaskAddress(arena, at), "NtSurfaceMask", "alpha", bytes);
        }
      }
      return call(arena, pointerSlot(arena, native), frame);
    }));
  }

  child(placement: Bounds, mask?: SurfaceMask): Surface {
    return this.#call((arena, parent) => {
      const bounds = arena.value("NtCellRect");
      writeNativeBounds(arena, bounds, placement);
      const nativeMask = mask ? nativeSurfaceMask(arena, mask) : 0;
      const out = arena.value(surfaceType);
      arena.result("nt_surface_child", parent, bounds, pointerSlot(arena, nativeMask), pointerSlot(arena, out));
      let alpha: Uint8Array | undefined;
      if (nativeMask) {
        const type = "NtSurfaceMask";
        const count = nativeField(arena, nativeMask, type, "count");
        if (count) alpha = arena.read(arena.pointer(nativeMask + arena.api.field(type, "alpha").offset), count);
      }
      return new OwnedSurface(this.#runtime, this.#owner, this.#useFrame, surfaceSnapshot(arena, out), alpha, this);
    });
  }

  get clip(): Bounds {
    return this.#call((arena, view) => {
      const out = arena.value("NtCellRect");
      arena.result("nt_surface_clip", view, pointerSlot(arena, out));
      return nativeBounds(arena, out);
    });
  }

  coverage(at: Point): number {
    return this.#call((arena, view) => {
      const out = arena.value("double");
      arena.result("nt_surface_coverage", view, cellPointSlot(arena, at), pointerSlot(arena, out));
      return arena.float64(out);
    });
  }

  fill(bounds: Bounds, color: Color, owner = 0n): void {
    this.#call((arena, view) => {
      const rect = arena.value("NtCellRect");
      writeNativeBounds(arena, rect, bounds);
      const rgba = arena.value("NtRgba");
      arena.setUnsigned(rgba, arena.api.size("NtRgba"), BigInt(packedColor(color)));
      arena.result("nt_surface_fill", view, rect, rgba, ownerSlot(arena, owner));
    });
  }

  glyph(at: Point, glyph: Utf8, columns: number, style: Style, owner = 0n): void {
    this.#call((arena, view) => {
      const value = arena.value("NtCellStyle");
      writeCellStyle(arena, value, style);
      arena.result("nt_surface_glyph", view, cellPointSlot(arena, at), arena.byteView(glyph),
        glyphColumns(this.#runtime, arena, columns), value, ownerSlot(arena, owner));
    });
  }

  eraseGlyph(at: Point): void {
    this.#call((arena, view) => arena.result("nt_surface_erase_glyph", view, cellPointSlot(arena, at)));
  }

  paintText(at: Point, layout: TextLayout, options: TextPaintOptions): void {
    this.#call((arena, view) => {
      const position = cellPointSlot(arena, at);
      const settings = textPaintOptions(this.#runtime, arena, options);
      OwnedTextLayout.use(this.#runtime, layout, handle =>
        arena.result("nt_text_paint", view, position, pointerSlot(arena, handle), settings));
    });
  }

  paintStyledText(at: Point, layout: TextLayout, options: TextPaintOptions, styles: TextStyles): void {
    this.#call((arena, view) => drawStyledText(this.#runtime, arena, "nt_style_text_paint",
      view, at, layout, options, styles));
  }

  paintTextEffect(at: Point, layout: TextLayout, options: TextPaintOptions, effect: TextEffect): void {
    this.#call((arena, view) => drawTextEffect(this.#runtime, arena, "nt_text_effect_paint", view,
      at, layout, options, effect));
  }

  #layers(call: (batch: FrameArguments, view: number) => void): void {
    this.#call((arena, view, frame) => {
      const batch = new FrameArguments(this.#runtime, arena);
      try {
        batch.alreadyBorrowed(this.#owner, frame);
        call(batch, arena.pointer(view));
      } finally {
        batch.close();
      }
    });
  }

  compose(layers: readonly SurfaceLayer[]): void {
    this.#layers((batch, view) => {
      const arena = batch.arena;
      const type = "NtSurfaceLayer";
      batch.run("nt_surface_compose", view, type, layers, (out, layer) => {
        setNativeField(arena, out, type, "source", batch.frame(layer.source));
        writeNativeBounds(arena, out + arena.api.field(type, "region").offset, layer.region);
        writeCellPoint(arena, out + arena.api.field(type, "position").offset, layer.position);
        arena.setFloat64(out + arena.api.field(type, "opacity").offset, layer.opacity ?? 1);
        const mask = layer.mask;
        if (mask) setNativeField(arena, out, type, "mask", nativeSurfaceMask(arena, mask));
        writeSurfaceOwners(batch, out, type, layer.owners);
      });
    });
  }

  projectPanels(layers: readonly SurfacePanelLayer[]): void {
    this.#layers((batch, view) => {
      batch.run("nt_surface_project_panels", view, "NtPanelLayer", layers, (out, layer) => {
        writePanelLayer(batch, out, layer);
        writeSurfaceOwners(batch, out, "NtPanelLayer", layer.owners);
      });
    });
  }
}

class PublishedFrame implements Frame {
  constructor(
    private readonly runtime: Runtime,
    private readonly useTree: <T>(call: (tree: number) => T) => T,
  ) {}

  get size(): Size {
    return this.useTree(tree => this.runtime.withBuffer(8, out => {
      this.runtime.native.nocturne_wasm_tree_frame_size(tree, out);
      const data = this.runtime.view();
      return { columns: data.getUint32(out, true), rows: data.getUint32(out + 4, true) };
    }));
  }

  cell(at: Point): Cell {
    return this.useTree(tree => this.runtime.withBuffer(40, out => {
      this.runtime.check(this.runtime.native.nocturne_wasm_tree_frame_cell(tree, toI32(at.x), toI32(at.y), out));
      const data = this.runtime.view();
      return {
        owner: data.getBigUint64(out, true),
        glyph: this.runtime.decode(data.getUint32(out + 8, true), data.getUint32(out + 12, true)),
        lead: { x: data.getInt32(out + 16, true), y: data.getInt32(out + 20, true) },
        columns: data.getUint32(out + 24, true),
        style: { foreground: colorAt(data, out + 28), background: colorAt(data, out + 32) },
      };
    }));
  }
}

class OwnedTree implements Tree {
  #handle: number;
  #busy = false;
  #publication: object = {};
  readonly #nodes = new WeakSet<Node>();
  readonly #editorViews = new Map<Node, EditorResource[]>();
  readonly #editorResources = new Map<EditorResource, number>();
  readonly #receivers = new Map<bigint, { owner: Node; handler: RawCallbacks }>();
  #failures: CallbackFailure[] = [];

  constructor(private readonly runtime: Runtime, handle: number) {
    this.#handle = handle;
  }

  #acquireTree(): number {
    if (this.#busy) throw this.runtime.failure(6);
    if (this.#handle === 0) throw this.runtime.failure(5);
    this.#busy = true;
    try {
      for (const resource of this.#editorResources.keys()) resource.borrow(this);
      return this.#handle;
    } catch (cause) {
      this.#releaseTree();
      throw cause;
    }
  }

  #releaseTree(): void {
    for (const resource of this.#editorResources.keys()) resource.release(this);
    this.#busy = false;
  }

  #withTree<T>(call: (tree: number) => T, reconcileViews = false): T {
    const tree = this.#acquireTree();
    try {
      return call(tree);
    } finally {
      try {
        if (reconcileViews) this.#pruneNodeResources(tree);
      } finally {
        this.#releaseTree();
      }
    }
  }

  #claimFrame(publication: object, destination?: OwnedTree): FrameLease {
    const sameTree = destination === this;
    const tree = sameTree ? this.#handle : this.#acquireTree();
    if (this.#publication !== publication) {
      if (!sameTree) this.#releaseTree();
      throw this.runtime.failure(5);
    }
    return sameTree ? { handle: tree, published: true } :
      { handle: tree, published: true, release: () => this.#releaseTree() };
  }

  #validated(node: Node): Node {
    if (!this.#nodes.has(node)) throw this.runtime.failure(1);
    return node;
  }

  sameNode(left: Node | undefined, right: Node | undefined): boolean {
    return left !== undefined && right !== undefined &&
      this.#nodes.has(left) && this.#nodes.has(right) &&
      left.slot === right.slot && left.generation === right.generation;
  }

  #node(data: DataView, address: number): Node | undefined {
    const generation = data.getUint32(address + 4, true);
    if (generation === 0) return undefined;
    const node = Object.freeze({ slot: data.getUint32(address, true), generation });
    this.#nodes.add(node);
    return node;
  }

  #nodeFromNative(arena: RawArena, address: number): Node {
    const node = Object.freeze({ slot: nativeField(arena, address, "NtNodeId", "slot"),
      generation: nativeField(arena, address, "NtNodeId", "generation") });
    this.#nodes.add(node);
    return node;
  }

  #writeNativeNode(arena: RawArena, address: number, tree: number, node: Node): void {
    const treeField = arena.api.field("NtNodeId", "tree");
    arena.setPointer(address + treeField.offset, tree);
    setNativeField(arena, address, "NtNodeId", "slot", node.slot);
    setNativeField(arena, address, "NtNodeId", "generation", node.generation);
  }

  #retainEditorResource(resource: EditorResource): number {
    const handle = resource.retainView(this);
    this.#editorResources.set(resource, (this.#editorResources.get(resource) ?? 0) + 1);
    return handle;
  }

  #dropEditorResource(resource: EditorResource): void {
    const count = (this.#editorResources.get(resource) ?? 0) - 1;
    if (count === 0) {
      this.#editorResources.delete(resource);
      resource.release(this);
    } else {
      this.#editorResources.set(resource, count);
    }
    resource.dropView();
  }

  #dropEditorView(node: Node, resources: EditorResource[]): void {
    this.#editorViews.delete(node);
    for (const resource of resources) this.#dropEditorResource(resource);
  }

  #dropReceiver(id: bigint): void {
    const receiver = this.#receivers.get(id);
    if (receiver) {
      receiver.handler.close();
      this.#receivers.delete(id);
    }
  }

  #pruneNodeResources(tree: number): void {
    if (this.#editorViews.size === 0 && this.#receivers.size === 0) return;
    this.runtime.raw.withArena(arena => {
      const handle = pointerSlot(arena, tree);
      const id = arena.value("NtNodeId");
      const out = pointerSlot(arena, arena.value("NtNodeGeometry"));
      const resultSize = arena.api.size("NtResult");
      const stale = (node: Node) => {
        this.#writeNativeNode(arena, id, tree, node);
        const result = arena.call("nt_tree_geometry", handle, id, out);
        return arena.unsigned(result, resultSize) === 5n;
      };
      for (const [node, resources] of this.#editorViews) {
        if (stale(node)) this.#dropEditorView(node, resources);
      }
      for (const [registration, receiver] of this.#receivers) {
        if (stale(receiver.owner)) this.#dropReceiver(registration);
      }
    });
  }

  eventNode(arena: RawArena, node: Node): number {
    const out = arena.value("NtNodeId");
    this.#writeNativeNode(arena, out, this.#handle, this.#validated(node));
    return out;
  }

  callbackFailure(phase: CallbackFailure["phase"], cause: unknown): void {
    this.#failures.push({ phase, cause });
  }

  #finish(status: number): void {
    const failures = this.#failures;
    this.#failures = [];
    if (failures.length) throw new CallError(status ? this.runtime.failure(status) : null, failures);
    this.runtime.check(status);
  }

  #mount(options: MountOptions, call: (tree: number, slot: number, generation: number, out: number) => number): Node {
    return this.#withTree(tree => this.runtime.withBuffer(8, out => {
      const parent = options.parent;
      const id = parent ? this.#validated(parent) : { slot: 0, generation: 0 };
      this.#finish(call(tree, id.slot, id.generation, out));
      const node = this.#node(this.runtime.view(), out);
      if (!node) throw this.runtime.failure(5);
      return node;
    }));
  }

  mountBox(kind: BoxKind, options: MountOptions = {}, gap = 0): Node {
    return this.#mount(options, (tree, slot, generation, out) =>
      this.runtime.native.nocturne_wasm_tree_mount_box(tree, slot, generation, mountFlags(options),
        toU32(boxKind[kind]), toU32(gap), out));
  }

  mountBoxLayout(layout: BoxLayout, options: MountOptions = {}): Node {
    return this.#mountStock("nt_box_layout_mount", options, arena =>
      [pointerSlot(arena, boxLayout(this.runtime, arena, layout))]);
  }

  setBoxLayout(node: Node, layout: BoxLayout): void {
    this.withNative(scope => {
      const arena = scope.arena;
      arena.result("nt_box_layout_set", scope.handle(), scope.node(node),
        pointerSlot(arena, boxLayout(this.runtime, arena, layout)));
    });
  }

  mountRule(rule: RuleOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_rule_mount", options, arena => [ruleOptions(arena, rule)]);
  }

  setRule(node: Node, rule: RuleOptions): void {
    this.withNative(scope => {
      const arena = scope.arena;
      arena.result("nt_rule_set", scope.handle(), scope.node(node), ruleOptions(arena, rule));
    });
  }

  mountLabel(value: Utf8, options: MountOptions = {}, style: Style = defaultStyle): Node {
    return this.#mount(options, (tree, slot, generation, out) => this.runtime.withBytes(value, (address, count) =>
      this.runtime.native.nocturne_wasm_tree_mount_label(tree, slot, generation, mountFlags(options), address, count,
        packedColor(style.foreground), packedColor(style.background), out)));
  }

  #mountStockResult(scope: RawTree, name: string, options: MountOptions, values: (arena: RawArena) => number[]): number {
    const arena = scope.arena;
    const flags = mountFlags(options);
    const type = "NtMountOptions";
    const nativeOptions = arena.value(type);
    setNativeField(arena, nativeOptions, type, "enabled", flags & 1);
    setNativeField(arena, nativeOptions, type, "focusable", (flags >> 1) & 1);
    setNativeField(arena, nativeOptions, type, "hit_testable", (flags >> 2) & 1);
    const out = arena.value("NtNodeId");
    arena.result(name, scope.handle(), scope.node(options.parent), nativeOptions, ...values(arena), pointerSlot(arena, out));
    return out;
  }

  #mountStock(name: string, options: MountOptions, values: (arena: RawArena) => number[]): Node {
    return this.withNative(scope => scope.adopt(this.#mountStockResult(scope, name, options, values)));
  }

  mountButton(label: Utf8, options: MountOptions = {}, style?: ControlStyle): Node {
    return this.#mountStock("nt_button_mount", options, arena => [arena.byteView(label), controlStyle(arena, style)]);
  }

  buttonPressed(node: Node): boolean {
    return this.#controlFlag("nt_button_pressed", node);
  }

  mountItem(label: Utf8, selected: boolean, options: MountOptions = {}, style?: ControlStyle): Node {
    return this.#mountStock("nt_item_mount", options, arena =>
      [arena.byteView(label), controlStyle(arena, style), boolSlot(arena, selected)]);
  }

  item(node: Node): boolean {
    return this.#controlFlag("nt_item_get", node);
  }

  setItem(node: Node, selected: boolean): void {
    this.#setControlFlag("nt_item_set", node, selected);
  }

  mountCheckbox(label: Utf8, checked: boolean, options: MountOptions = {}, style?: ControlStyle): Node {
    return this.#mountStock("nt_checkbox_mount", options, arena =>
      [arena.byteView(label), controlStyle(arena, style), boolSlot(arena, checked)]);
  }

  checkbox(node: Node): boolean {
    return this.#controlFlag("nt_checkbox_get", node);
  }

  setCheckbox(node: Node, checked: boolean): void {
    this.#setControlFlag("nt_checkbox_set", node, checked);
  }

  #controlFlag(getter: string, node: Node): boolean {
    return this.withNative(scope => {
      const arena = scope.arena;
      const out = arena.value("_Bool");
      arena.result(getter, scope.handle(), scope.node(node), pointerSlot(arena, out));
      return arena.unsigned(out, arena.api.size("_Bool")) !== 0n;
    });
  }

  #setControlFlag(setter: string, node: Node, value: boolean): void {
    this.withNative(scope => scope.arena.result(setter, scope.handle(), scope.node(node), boolSlot(scope.arena, value)));
  }

  mountChoice(choice: ChoiceOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_choice_mount", options, arena => [choiceOptions(arena, choice)]);
  }

  configureChoice(node: Node, choice: ChoiceOptions): void {
    this.withNative(scope => scope.arena.result("nt_choice_configure",
      scope.handle(), scope.node(node), choiceOptions(scope.arena, choice)));
  }

  choice(node: Node): ChoiceState {
    return this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtChoiceState";
      const out = arena.value(type);
      arena.result("nt_choice_get", scope.handle(), scope.node(node), pointerSlot(arena, out));
      const index = nativeField(arena, out, type, "selected");
      return { selected: index === choiceNone ? undefined : index,
        expanded: nativeField(arena, out, type, "expanded") !== 0 };
    });
  }

  setChoice(node: Node, selected?: number): void {
    this.withNative(scope => {
      const arena = scope.arena;
      const index = arena.value("size_t");
      arena.setUnsigned(index, arena.api.size("size_t"), BigInt(choiceIndex(selected)));
      arena.result("nt_choice_set", scope.handle(), scope.node(node), index);
    });
  }

  mountProgress(progress: ProgressOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_progress_mount", options, arena => [progressOptions(arena, progress)]);
  }

  setProgress(node: Node, progress: ProgressOptions): void {
    this.withNative(scope => scope.arena.result(
      "nt_progress_set", scope.handle(), scope.node(node), progressOptions(scope.arena, progress)));
  }

  progress(node: Node): ProgressOptions {
    return this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtProgressOptions";
      const out = arena.value(type);
      arena.result("nt_progress_get", scope.handle(), scope.node(node), pointerSlot(arena, out));
      return { value: nativeField(arena, out, type, "value"), maximum: nativeField(arena, out, type, "maximum"),
        filled: readCellStyle(arena, out + arena.api.field(type, "filled").offset),
        remaining: readCellStyle(arena, out + arena.api.field(type, "remaining").offset),
        vertical: nativeField(arena, out, type, "vertical") !== 0 };
    });
  }

  mountCursor(cursor: CursorOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_cursor_mount", options, arena =>
      [timedIndicator(arena, "NtCursorOptions", cursor.style, cursor.phaseUs, "visible", cursor.visible)]);
  }

  setCursor(node: Node, cursor: CursorOptions): void {
    this.withNative(scope => scope.arena.result("nt_cursor_set", scope.handle(), scope.node(node),
      timedIndicator(scope.arena, "NtCursorOptions", cursor.style, cursor.phaseUs, "visible", cursor.visible)));
  }

  mountWait(wait: WaitOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_wait_mount", options, arena =>
      [timedIndicator(arena, "NtWaitOptions", wait.style, wait.phaseUs, "ascii", wait.ascii ?? false)]);
  }

  setWait(node: Node, wait: WaitOptions): void {
    this.withNative(scope => scope.arena.result("nt_wait_set", scope.handle(), scope.node(node),
      timedIndicator(scope.arena, "NtWaitOptions", wait.style, wait.phaseUs, "ascii", wait.ascii ?? false)));
  }

  mountScrollbar(bar: ScrollbarOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_scrollbar_mount", options, arena => [scrollbarOptions(arena, bar)]);
  }

  setScrollbar(node: Node, bar: ScrollbarOptions): void {
    this.withNative(scope => scope.arena.result("nt_scrollbar_set", scope.handle(), scope.node(node),
      scrollbarOptions(scope.arena, bar)));
  }

  scrollbar(node: Node): ScrollbarOptions {
    return this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtScrollBarOptions";
      const out = arena.value(type);
      arena.result("nt_scrollbar_get", scope.handle(), scope.node(node), pointerSlot(arena, out));
      return { content: nativeField(arena, out, type, "content"), viewport: nativeField(arena, out, type, "viewport"),
        offset: nativeField(arena, out, type, "offset"),
        thumb: readCellStyle(arena, out + arena.api.field(type, "thumb").offset),
        track: readCellStyle(arena, out + arena.api.field(type, "track").offset),
        vertical: nativeField(arena, out, type, "vertical") !== 0 };
    });
  }

  mountEditorView(view: EditorViewOptions, options: MountOptions = {}): Node {
    return this.withNative(scope => {
      const editor = sameRuntimeResource(this.runtime, editorResources.get(view.editor));
      const keys = view.keys;
      const bank = keys?.registers;
      const registers = bank === undefined ? undefined : sameRuntimeResource(this.runtime, registerResources.get(bank));
      const resources: EditorResource[] = [];
      let mounted = false;
      try {
        const editorHandle = this.#retainEditorResource(editor);
        resources.push(editor);
        let registerHandle = 0;
        if (registers) {
          registerHandle = this.#retainEditorResource(registers);
          resources.push(registers);
        }
        const out = this.#mountStockResult(scope, "nt_editor_view_mount", options, arena => {
          const type = "NtEditorViewOptions";
          const native = arena.value(type);
          setNativeField(arena, native, type, "editor", editorHandle);
          setNativeField(arena, native, type, "keys", keys ? arena.pointer(editorKeyMap(arena, keys, registerHandle)) : 0);
          setNativeField(arena, native, type, "style", arena.pointer(controlStyle(arena, view.style)));
          setNativeField(arena, native, type, "wrap", toU32(wrapCode[view.wrap ?? "none"]));
          setNativeField(arena, native, type, "follow_caret", view.followCaret ? 1 : 0);
          setNativeField(arena, native, type, "show_caret", view.showCaret ? 1 : 0);
          setNativeField(arena, native, type, "submit_on_enter", view.submitOnEnter ? 1 : 0);
          setNativeField(arena, native, type, "scroll_keys", view.scrollKeys ? 1 : 0);
          return [pointerSlot(arena, native)];
        });
        const node = this.#nodeFromNative(scope.arena, out);
        this.#editorViews.set(node, resources);
        mounted = true;
        return node;
      } finally {
        if (!mounted) for (const resource of resources) this.#dropEditorResource(resource);
      }
    });
  }

  scrollEditorView(node: Node, requested: Size): void {
    this.withNative(scope => {
      const arena = scope.arena;
      const offset = arena.value("NtCellSize");
      writeNativeSize(arena, offset, requested);
      arena.result("nt_editor_view_scroll", scope.handle(), scope.node(node), offset);
    });
  }

  editorViewOffset(node: Node): Size {
    return this.withNative(scope => {
      const arena = scope.arena;
      const out = arena.value("NtCellSize");
      arena.result("nt_editor_view_offset", scope.handle(), scope.node(node), pointerSlot(arena, out));
      return nativeSize(arena, out);
    });
  }

  mountPanel(panel: PanelOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_panel_mount", options, arena => [panelOptions(arena, panel)]);
  }

  setPanel(node: Node, panel: PanelOptions): void {
    this.withNative(scope => scope.arena.result("nt_panel_set", scope.handle(), scope.node(node), panelOptions(scope.arena, panel)));
  }

  mountViewport(viewport: ViewportOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_viewport_mount", options, arena => [viewportOptions(arena, viewport)]);
  }

  setViewport(node: Node, viewport: ViewportOptions): void {
    this.withNative(scope => scope.arena.result(
      "nt_viewport_set", scope.handle(), scope.node(node), viewportOptions(scope.arena, viewport)));
  }

  viewport(node: Node): ViewportState {
    return this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtViewportView";
      const out = arena.value(type);
      arena.result("nt_viewport_view", scope.handle(), scope.node(node), pointerSlot(arena, out));
      return { placed: nativeField(arena, out, type, "placed") !== 0,
        child: nativeNode(scope, out + arena.api.field(type, "child").offset),
        viewport: nativeSize(arena, out + arena.api.field(type, "viewport").offset),
        content: nativeSize(arena, out + arena.api.field(type, "content").offset),
        effective: nativeSize(arena, out + arena.api.field(type, "effective").offset) };
    });
  }

  mountWindowWorkspace(preferred: Size, options: MountOptions = {}): Node {
    return this.#mountStock("nt_window_workspace_mount", options, arena => {
      const size = arena.value("NtCellSize");
      writeNativeSize(arena, size, preferred);
      return [size];
    });
  }

  mountWindow(window: WindowOptions, options: MountOptions & { readonly parent: Node }): Node {
    return this.#mountStock("nt_window_mount", options, arena => {
      const type = "NtWindowOptions";
      const spec = arena.call("nt_window_options_default");
      if (window.placement) {
        writeWindowPlacement(arena, spec + arena.api.field(type, "placement").offset, window.placement);
      }
      const panel = arena.api.field(type, "panel");
      if (window.panel) {
        const source = arena.pointer(panelOptions(arena, window.panel));
        arena.write(spec + panel.offset, arena.read(source, panel.size));
      }
      if (window.title !== undefined) {
        writePanelTitle(arena, spec + panel.offset, window.title, window.panel?.titleStyle ?? defaultStyle);
      }
      setNativeField(arena, spec, type, "draggable", window.disableDrag ? 0 : 1);
      setNativeField(arena, spec, type, "resizable", window.disableResize ? 0 : 1);
      return [pointerSlot(arena, spec)];
    });
  }

  setWindowPlacement(node: Node, placement: WindowPlacement): void {
    this.withNative(scope => {
      const value = scope.arena.value("NtWindowPlacement");
      writeWindowPlacement(scope.arena, value, placement);
      scope.arena.result("nt_window_set", scope.handle(), scope.node(node), value);
    });
  }

  setWindowPanel(node: Node, panel: PanelOptions): void {
    this.withNative(scope => scope.arena.result("nt_window_set_panel", scope.handle(), scope.node(node),
      panelOptions(scope.arena, panel)));
  }

  focusWindow(node: Node, target?: Node): void {
    this.withNative(scope => scope.arena.result("nt_window_focus", scope.handle(), scope.node(node), scope.node(target)));
  }

  animateWindow(node: Node, animation: WindowAnimation): void {
    this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtWindowAnimation";
      const value = arena.value(type);
      writeNativeBounds(arena, value + arena.api.field(type, "destination").offset, animation.destination);
      setNativeField(arena, value, type, "start", toU64(animation.startUs));
      setNativeField(arena, value, type, "duration", toU64(animation.durationUs));
      setNativeField(arena, value, type, "easing", toU32(animation.easing ?? Easing.linear));
      arena.result("nt_window_animate", scope.handle(), scope.node(node), value);
    });
  }

  window(node: Node): WindowState {
    return this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtWindowState";
      const out = arena.value(type);
      arena.result("nt_window_get", scope.handle(), scope.node(node), pointerSlot(arena, out));
      const placementType = "NtWindowPlacement";
      const placement = out + arena.api.field(type, "requested").offset;
      return {
        requested: { floating: nativeBounds(arena, placement + arena.api.field(placementType, "floating").offset),
          mode: windowModes[nativeField(arena, placement, placementType, "mode")],
          dock: windowDocks[nativeField(arena, placement, placementType, "dock")],
          dockExtent: nativeField(arena, placement, placementType, "dock_extent") },
        published: nativeGeometry(scope, out + arena.api.field(type, "published").offset),
        animating: nativeField(arena, out, type, "animating") !== 0,
      };
    });
  }

  mountNotification(notification: NotificationOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_notification_mount", options,
      arena => [notificationOptions(this.runtime, arena, notification)]);
  }

  setNotification(node: Node, notification: NotificationOptions): void {
    this.withNative(scope => {
      const value = notificationOptions(this.runtime, scope.arena, notification);
      scope.arena.result("nt_notification_set", scope.handle(), scope.node(node), value);
    });
  }

  notification(node: Node): NotificationState {
    return this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtNotificationState";
      const out = arena.value(type);
      arena.result("nt_notification_get", scope.handle(), scope.node(node), pointerSlot(arena, out));
      return { id: unsignedNativeField(arena, out, type, "id"),
        selectedAction: unsignedNativeField(arena, out, type, "selected_action"),
        expiresAt: unsignedNativeField(arena, out, type, "expires_at"),
        status: nativeField(arena, out, type, "status") as NotificationStatus };
    });
  }

  dismissNotification(node: Node): void {
    this.withNative(scope => scope.arena.result("nt_notification_dismiss", scope.handle(), scope.node(node)));
  }

  mountPlot(plot: PlotOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_plot_mount", options, arena => [plotOptions(arena, plot)]);
  }

  setPlot(node: Node, plot: PlotOptions): void {
    this.withNative(scope => scope.arena.result("nt_plot_set", scope.handle(), scope.node(node), plotOptions(scope.arena, plot)));
  }

  mountDigits(digits: DigitsOptions, options: MountOptions = {}): Node {
    return this.#mountStock("nt_digits_mount", options, arena => [digitsOptions(arena, digits)]);
  }

  setDigits(node: Node, digits: DigitsOptions): void {
    this.withNative(scope => scope.arena.result("nt_digits_set", scope.handle(), scope.node(node),
      digitsOptions(scope.arena, digits)));
  }

  mountParticleEffect(effect: ParticleEffectOptions | ParticleEffect, options: MountOptions = {}): Node {
    return this.#mountStock("nt_particle_effect_mount", options, arena => [particleEffectOptions(arena, effect)]);
  }

  setParticleEffect(node: Node, effect: ParticleEffectOptions | ParticleEffect): void {
    this.withNative(scope => scope.arena.result("nt_particle_effect_set",
      scope.handle(), scope.node(node), particleEffectOptions(scope.arena, effect)));
  }

  mountParticleView(view: ParticleViewOptions = {}, options: MountOptions = {}): Node {
    return this.#mountStock("nt_particle_view_mount", options, arena => [particleViewPointer(arena, view)]);
  }

  setParticleView(node: Node, view: ParticleViewOptions): void {
    this.withNative(scope => {
      const value = particleViewPointer(scope.arena, view);
      scope.arena.result("nt_particle_view_set", scope.handle(), scope.node(node), value);
    });
  }

  mountGraph(graph: Graph, options: MountOptions = {}): Node {
    return this.#mountStock("nt_graph_view_mount", options, arena => {
      requireGraph(this.runtime);
      return [graphViewOptions(this.runtime, arena, graph)];
    });
  }

  #withHostedComponent(component: Component, mount: (id: number) => number): number {
    const id = this.runtime.retain(component, this, failure => this.#failures.push(failure));
    let committed = false;
    try {
      const status = mount(id);
      committed = status === 0;
      return status;
    } finally {
      if (!committed) this.runtime.release(id);
    }
  }

  mountComponent(component: Component, options: MountOptions = {}): Node {
    return this.#mount(options, (tree, slot, generation, out) => {
      const flags = mountFlags(options);
      return this.#withHostedComponent(component, id =>
        this.runtime.native.nocturne_wasm_tree_mount_component(tree, slot, generation, flags, id, out));
    });
  }

  mountPanelContent(provider: PanelContentProvider, options: MountOptions = {}): Node {
    return this.#mount(options, (tree, slot, generation, out) => {
      const mount = this.runtime.native.nocturne_wasm_tree_mount_panel_content;
      if (!mount) throw this.runtime.failure(7);
      const { preferred, paint, update, event, dispose } = provider;
      if (typeof paint !== "function" || [update, event, dispose].some(callback =>
        callback !== undefined && typeof callback !== "function")) throw this.runtime.failure(1);
      const columns = toU32(preferred.columns), rows = toU32(preferred.rows), flags = mountFlags(options);
      const callbacks: Component = { paint: paint.bind(provider), update: update?.bind(provider),
        event: event?.bind(provider), dispose: dispose?.bind(provider) };
      return this.#withHostedComponent(callbacks, id =>
        mount(tree, slot, generation, flags, id, columns, rows, update ? 1 : 0, event ? 1 : 0, out));
    });
  }

  setLabel(node: Node, value: Utf8, style: Style = defaultStyle): void {
    this.#withTree(tree => this.runtime.withBytes(value, (address, count) => {
      const id = this.#validated(node);
      this.runtime.check(this.runtime.native.nocturne_wasm_tree_set_label(tree, id.slot, id.generation, address, count,
        packedColor(style.foreground), packedColor(style.background)));
    }));
  }

  setGraph(node: Node, graph: Graph): void {
    this.withNative(scope => {
      requireGraph(this.runtime);
      const id = scope.node(node);
      const options = graphViewOptions(this.runtime, scope.arena, graph);
      scope.arena.result("nt_graph_view_set", scope.handle(), id, options);
    });
  }

  graphHitAt(node: Node, at: Point): GraphHit {
    return this.withNative(scope => {
      requireGraph(this.runtime);
      const arena = scope.arena, out = arena.value("NtGraphHit");
      arena.result("nt_graph_view_hit", scope.handle(), scope.node(node), cellPointSlot(arena, at), pointerSlot(arena, out));
      return graphHit(arena, out);
    });
  }

  graphSelection(node: Node): GraphHit {
    return this.withNative(scope => {
      requireGraph(this.runtime);
      const arena = scope.arena, out = arena.value("NtGraphHit");
      arena.result("nt_graph_view_selection", scope.handle(), scope.node(node), pointerSlot(arena, out));
      return graphHit(arena, out);
    });
  }

  selectGraph(node: Node, selection: GraphHit): void {
    this.withNative(scope => {
      requireGraph(this.runtime);
      const arena = scope.arena, type = "NtGraphHit", value = arena.value(type);
      const kind = graphHitKinds.indexOf(selection.kind);
      if (kind < 0) throw this.runtime.failure(1);
      setNativeField(arena, value, type, "kind", kind);
      setNativeField(arena, value, type, "id", toU64(selection.id));
      arena.result("nt_graph_view_select", scope.handle(), scope.node(node), value);
    });
  }

  withNative<T>(call: (scope: RawTree) => T): T {
    return this.#withNative(call, true);
  }

  #withNative<T>(call: (scope: RawTree) => T, reconcile = false): T {
    return this.#withTree(tree => this.runtime.raw.withArena(arena => {
      const api = arena.api;
      const treeField = api.field("NtNodeId", "tree");
      const handle = () => {
        const address = arena.value("pointer");
        arena.setPointer(address, tree);
        return address;
      };
      const node = (value?: Node) => {
        const address = arena.value("NtNodeId");
        if (value !== undefined) this.#writeNativeNode(arena, address, tree, this.#validated(value));
        return address;
      };
      const adopt = (address: number) => {
        if (!arena.owns(address, api.size("NtNodeId"))) throw this.runtime.failure(2);
        if (arena.pointer(address + treeField.offset) !== tree) throw this.runtime.failure(1);
        const geometry = arena.value("NtNodeGeometry");
        const out = arena.value("pointer");
        arena.setPointer(out, geometry);
        arena.result("nt_tree_geometry", handle(), address, out);
        return this.#nodeFromNative(arena, address);
      };
      let value: T;
      try {
        value = call({ arena, handle, node, adopt });
      } catch (cause) {
        const failures = this.#failures;
        this.#failures = [];
        if (failures.length) {
          if (cause instanceof NativeError) throw new CallError(cause, failures);
          throw new AggregateError([cause, new CallError(null, failures)]);
        }
        throw cause;
      }
      this.#finish(0);
      return value;
    }), reconcile);
  }

  remove(node: Node): void {
    this.#withTree(tree => {
      const id = this.#validated(node);
      this.#finish(this.runtime.native.nocturne_wasm_tree_remove(tree, id.slot, id.generation));
    }, true);
  }

  move(node: Node, newParent?: Node): void {
    this.withNative(scope => scope.arena.result("nt_tree_move", scope.handle(), scope.node(node), scope.node(newParent)));
  }

  invalidate(node: Node, reason: Invalidation): void {
    this.withNative(scope => {
      const type = "NtInvalidation";
      const value = scope.arena.value(type);
      scope.arena.setUnsigned(value, scope.arena.api.size(type), BigInt(toU32(invalidationCode[reason])));
      scope.arena.result("nt_tree_invalidate", scope.handle(), scope.node(node), value);
    });
  }

  geometry(node: Node): Geometry {
    return this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtNodeGeometry";
      const out = arena.value(type);
      arena.result("nt_tree_geometry", scope.handle(), scope.node(node), pointerSlot(arena, out));
      return nativeGeometry(scope, out);
    });
  }

  hit(point: Point): Node | undefined {
    return this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtCellPoint";
      const position = arena.value(type);
      setNativeField(arena, position, type, "x", toI32(point.x));
      setNativeField(arena, position, type, "y", toI32(point.y));
      const out = arena.value("NtNodeId");
      arena.result("nt_tree_hit", scope.handle(), position, pointerSlot(arena, out));
      return nativeNode(scope, out);
    });
  }

  property(node: Node, id: number): PropertyValue {
    return this.withNative(scope => {
      const arena = scope.arena;
      const out = arena.value("NtValue");
      arena.result("nt_tree_property_get", scope.handle(), propertyReference(scope, node, id), pointerSlot(arena, out));
      return propertyOutput(this.runtime, arena, out);
    });
  }

  setProperty(node: Node, id: number, value: PropertyValue): void {
    this.withNative(scope => scope.arena.result("nt_tree_property_set", scope.handle(),
      propertyReference(scope, node, id), propertyInput(scope.arena, value)));
  }

  defineProperties(node: Node, descriptors: readonly PropertyDescriptor[]): void {
    this.#withNative(scope => {
      const arena = scope.arena;
      const entries = Array.from(descriptors);
      const type = "NtPropertyInfo";
      const size = arena.api.size(type);
      const declarations = valueArray(arena, type, entries.length);
      const callbacks = new Map<number, PropertyDescriptor>();
      for (const [i, entry] of entries.entries()) {
        const address = declarations + i * size;
        const id = toU32(entry.id);
        const { kind, read, prepare, finish } = entry;
        if (typeof read !== "function" || (prepare !== undefined && typeof prepare !== "function") ||
          (finish !== undefined && typeof finish !== "function") || (prepare && !finish)) throw this.runtime.failure(1);
        setNativeField(arena, address, type, "id", id);
        setNativeField(arena, address, type, "kind", enumCode(this.runtime, propertyKind, kind));
        setNativeField(arena, address, type, "writable", finish ? 1 : 0);
        setNativeField(arena, address, type, "invalidation", enumCode(this.runtime, invalidationCode, entry.invalidation ?? "none"));
        callbacks.set(id, { id, kind, read, prepare, finish });
      }
      const id = scope.node(node);
      const tree = scope.handle();
      const count = sizeSlot(arena, entries.length);
      const data = pointerSlot(arena, declarations);
      const providerType = "NtPropertyProvider";
      const provider = arena.value(providerType);
      const nativeProvider = pointerSlot(arena, provider);
      const handler = propertyProvider(this.runtime, callbacks, (phase, cause) => this.callbackFailure(phase, cause));
      try {
        setNativeField(arena, provider, providerType, "context", handler.context);
        setNativeField(arena, provider, providerType, "read", handler.function("NtPropertyProviderRead"));
        setNativeField(arena, provider, providerType, "prepare", handler.function("NtPropertyProviderPrepare"));
        setNativeField(arena, provider, providerType, "finish", handler.function("NtPropertyProviderFinish"));
        setNativeField(arena, provider, providerType, "release", handler.function("NtPropertyProviderRelease"));
        arena.result("nt_tree_define_property_provider", tree, id, data, count, nativeProvider);
      } catch (cause) {
        handler.close();
        throw cause;
      }
    });
  }

  properties(node: Node): readonly PropertyInfo[] {
    return this.#withNative(scope => {
      const arena = scope.arena;
      const tree = scope.handle();
      const id = scope.node(node);
      const width = arena.api.size("size_t");
      const count = sizeSlot(arena, 0);
      arena.result("nt_tree_property_count", tree, id, pointerSlot(arena, count));
      const type = "NtPropertyInfo";
      const out = arena.value(type);
      const pointer = pointerSlot(arena, out);
      const index = sizeSlot(arena, 0);
      return Array.from({ length: Number(arena.unsigned(count, width)) }, (_, i) => {
        arena.setUnsigned(index, width, BigInt(i));
        arena.result("nt_tree_property_at", tree, id, index, pointer);
        return {
          id: nativeField(arena, out, type, "id"),
          kind: propertyKindNames.get(nativeField(arena, out, type, "kind"))!,
          writable: nativeField(arena, out, type, "writable") !== 0,
          invalidation: propertyInvalidations.get(nativeField(arena, out, type, "invalidation"))!,
        };
      });
    });
  }

  applyProperties(assignments: readonly PropertyAssignment[]): void {
    this.withNative(scope => {
      const arena = scope.arena;
      const entries = Array.from(assignments);
      arena.result("nt_tree_properties_apply", scope.handle(),
        pointerSlot(arena, assignmentArray(scope, entries)), sizeSlot(arena, entries.length));
    });
  }

  captureState(properties: readonly PropertyRef[]): StateSnapshot {
    let snapshot: OwnedStateSnapshot | undefined;
    try {
      return this.#withNative(scope => {
        const arena = scope.arena;
        const entries = Array.from(properties);
        const refs = propertyRefArray(scope, entries);
        const out = arena.value("pointer");
        arena.result("nt_tree_state_capture", scope.handle(), pointerSlot(arena, refs), sizeSlot(arena, entries.length),
          pointerSlot(arena, 0), pointerSlot(arena, out));
        return snapshot = new OwnedStateSnapshot(this.runtime, arena.pointer(out));
      });
    } catch (cause) {
      snapshot?.close();
      throw cause;
    }
  }

  restoreState(properties: readonly PropertyRef[], snapshot: StateSnapshot): void {
    this.#withNative(scope => {
      const arena = scope.arena;
      const entries = Array.from(properties);
      const refs = propertyRefArray(scope, entries);
      OwnedStateSnapshot.use(this.runtime, snapshot, handle =>
        arena.result("nt_tree_state_restore", scope.handle(), pointerSlot(arena, refs), sizeSlot(arena, entries.length),
          pointerSlot(arena, handle)));
    });
  }

  applyStyles(sheets: readonly StyleSheet[], mounts: readonly StyleMount[], targets: readonly PropertyRef[]): void {
    this.withNative(scope => {
      const arena = scope.arena;
      const annotations = Array.from(mounts);
      const properties = Array.from(targets);
      const type = "NtStyleMount";
      const size = arena.api.size(type);
      const node = arena.api.field(type, "node").offset;
      const element = arena.api.field(type, "element").offset;
      const nativeMounts = valueArray(arena, type, annotations.length);
      for (const [i, mount] of annotations.entries()) {
        const entry = nativeMounts + i * size;
        copyNativeValue(arena, "NtNodeId", entry + node, scope.node(mount.node));
        writeStyleElement(arena, entry + element, mount.element);
      }
      const nativeTargets = propertyRefArray(scope, properties);
      const nativeSheets = arena.value("NtStyleSheets");
      withStyleSheets(this.runtime, arena, nativeSheets, sheets, () =>
        arena.result("nt_tree_apply_styles", scope.handle(), nativeSheets, pointerSlot(arena, nativeMounts),
          sizeSlot(arena, annotations.length), pointerSlot(arena, nativeTargets), sizeSlot(arena, properties.length)));
    });
  }

  createBindingGraph(values: readonly BindingValue[], targets: readonly BindingTarget[]): BindingGraph {
    const callbacks: RawCallbacks[] = [];
    let application: RawArena | undefined;
    let handle: number;
    try {
      handle = this.withNative(scope => {
        const arena = scope.arena;
        const sources = Array.from(values);
        const destinations = Array.from(targets);
        const nativeValues = bindingValueArray(scope, sources, compute => {
          const handler = bindingEvaluator(this.runtime, compute, () => {
            if (!application) throw this.runtime.failure(6);
            return application;
          });
          callbacks.push(handler);
          return handler;
        });
        const out = arena.value("pointer");
        arena.result("nt_binding_graph_create", scope.handle(), pointerSlot(arena, nativeValues),
          sizeSlot(arena, sources.length), pointerSlot(arena, bindingTargetArray(scope, destinations)),
          sizeSlot(arena, destinations.length), pointerSlot(arena, out));
        return arena.pointer(out);
      });
    } catch (cause) {
      for (const handler of callbacks) handler.close();
      throw cause;
    }

    return {
      apply: inputs => this.withNative(scope => {
        if (!handle) throw this.runtime.failure(5);
        const arena = scope.arena;
        const type = "NtValue";
        const size = arena.api.size(type);
        const provided = Array.from(inputs);
        const data = valueArray(arena, type, provided.length);
        for (const [index, input] of provided.entries()) {
          copyNativeValue(arena, type, data + index * size, propertyInput(arena, input));
        }
        application = arena;
        try {
          arena.result("nt_binding_graph_apply", pointerSlot(arena, handle),
            pointerSlot(arena, data), sizeSlot(arena, provided.length));
        } finally {
          application = undefined;
          for (const handler of callbacks) {
            for (const cause of handler.takeErrors()) this.#failures.push({ phase: "bindingCompute", cause });
          }
        }
      }),
      close: () => {
        if (this.#busy) throw this.runtime.failure(6);
        if (!handle) return;
        this.runtime.raw.withArena(arena => {
          arena.result("nt_binding_graph_destroy", pointerSlot(arena, handle));
          handle = 0;
          for (const handler of callbacks) handler.close();
          callbacks.length = 0;
        });
      },
    };
  }

  configureSchedule(limits: ScheduleLimits): void {
    this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtScheduleLimits";
      const native = arena.value(type);
      setNativeField(arena, native, type, "batches", toU32(limits.batches));
      setNativeField(arena, native, type, "subscriptions", toU32(limits.subscriptions));
      arena.result("nt_tree_schedule_configure", scope.handle(), native);
    });
  }

  scheduleBatch(owner: Node, deadline: bigint, assignments: readonly PropertyAssignment[]): bigint {
    return this.withNative(scope => {
      const arena = scope.arena;
      const out = arena.value("NtScheduleId");
      const entries = Array.from(assignments);
      arena.result("nt_tree_schedule_batch", scope.handle(), scope.node(owner), timeSlot(arena, deadline),
        pointerSlot(arena, assignmentArray(scope, entries)), sizeSlot(arena, entries.length),
        pointerSlot(arena, out));
      return arena.unsigned(out, arena.api.size("NtScheduleId"));
    });
  }

  cancelSchedule(id: bigint): void {
    this.#withTree(tree => this.runtime.raw.withArena(arena => {
      const status = arena.call("nt_tree_schedule_cancel", pointerSlot(arena, tree), timeSlot(arena, id));
      const code = Number(arena.unsigned(status, arena.api.size("NtResult"))) | 0;
      if (code === 0 || code === 5) this.#dropReceiver(id);
      this.runtime.check(code);
    }));
  }

  get nextScheduled(): bigint | undefined {
    return this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtDeadline";
      const out = arena.call("nt_tree_schedule_next", scope.handle());
      const { offset, size } = arena.api.field(type, "at");
      return nativeField(arena, out, type, "scheduled") ? arena.unsigned(out + offset, size) : undefined;
    });
  }

  runSchedule(now: bigint, budget: number): ScheduleRun {
    return this.withNative(scope => {
      const arena = scope.arena;
      const type = "NtScheduleRun";
      const out = arena.value(type);
      const status = arena.call("nt_tree_schedule_run", scope.handle(), timeSlot(arena, now),
        sizeSlot(arena, budget), pointerSlot(arena, out));
      const { offset, size } = arena.api.field(type, "failed");
      const report = { attempted: nativeField(arena, out, type, "attempted"), failed: arena.unsigned(out + offset, size) };
      const code = Number(arena.unsigned(status, arena.api.size("NtResult"))) | 0;
      if (code && report.failed) throw new ScheduleError(this.runtime.failure(code), report);
      this.runtime.check(code);
      return report;
    });
  }

  #intentSource(arena: RawArena, address: number): Node | undefined {
    return nativeField(arena, address, "NtNodeId", "tree") ? this.#nodeFromNative(arena, address) : undefined;
  }

  subscribeIntent(owner: Node, kind: number, receive: IntentReceiver): bigint {
    return this.#withTree(tree => this.runtime.raw.withArena(arena => {
      if (typeof receive !== "function") throw this.runtime.failure(1);
      const node = this.eventNode(arena, owner);
      const nativeKind = enumSlot(arena, "uint32_t", kind);
      const out = arena.value("NtScheduleId");
      const handler = this.runtime.raw.callbacks({
        NtIntentReceiver: frame => {
          try {
            const callbackArena = frame.arena;
            const callbackOwner = this.#nodeFromNative(callbackArena, frame.argument(0));
            const intent = readEventIntent(this.runtime, callbackArena, callbackArena.pointer(frame.argument(1)),
              address => this.#intentSource(callbackArena, address));
            this.runtime.withoutComponentContext(() =>
              synchronousCompositionValue(receive(callbackOwner, intent), "Intent receiver must return synchronously"));
          } catch (cause) {
            this.callbackFailure("intentReceive", cause);
          }
          return undefined;
        },
      });
      try {
        arena.result("nt_tree_schedule_subscribe", pointerSlot(arena, tree), node, nativeKind,
          pointerSlot(arena, handler.function("NtIntentReceiver")), pointerSlot(arena, handler.context),
          pointerSlot(arena, out));
        const id = arena.unsigned(out, arena.api.size("NtScheduleId"));
        this.#receivers.set(id, { owner, handler });
        return id;
      } catch (cause) {
        handler.close();
        throw cause;
      }
    }));
  }

  deliverIntents(budget: number): number {
    return this.#withTree(tree => this.runtime.raw.withArena(arena => {
      const out = sizeSlot(arena, 0);
      arena.result("nt_tree_schedule_deliver", pointerSlot(arena, tree),
        sizeSlot(arena, budget), pointerSlot(arena, out));
      const delivered = Number(arena.unsigned(out, arena.api.size("size_t")));
      const failures = this.#failures;
      this.#failures = [];
      if (failures.length) throw new IntentDeliveryError(delivered, failures);
      return delivered;
    }));
  }

  focusAdvance(reverse = false): void {
    this.withNative(scope => {
      const arena = scope.arena;
      const value = arena.value("_Bool");
      arena.setUnsigned(value, 1, reverse ? 1n : 0n);
      arena.result("nt_tree_focus_advance", scope.handle(), value);
    });
  }

  tick(now: bigint, motion: Motion = "full"): void {
    this.#withTree(tree => this.#finish(this.runtime.native.nocturne_wasm_tree_tick(tree,
      toU64(now), toU32(motionCode[motion]))));
  }

  get nextUpdate(): bigint | undefined {
    return this.#withTree(tree => this.runtime.withBuffer(4, out => {
      const next = this.runtime.native.nocturne_wasm_tree_next_update(tree, out);
      return this.runtime.view().getUint32(out, true) ? BigInt.asUintN(64, next) : undefined;
    }));
  }

  setFocus(node?: Node): void {
    this.#withTree(tree => {
      const id = node ? this.#validated(node) : { slot: 0, generation: 0 };
      this.#finish(this.runtime.native.nocturne_wasm_tree_set_focus(tree, id.slot, id.generation));
    });
  }

  get focus(): Node | undefined {
    return this.#withTree(tree => this.runtime.withBuffer(8, out => {
      this.runtime.native.nocturne_wasm_tree_focus(tree, out);
      return this.#node(this.runtime.view(), out);
    }));
  }

  capture(pointer: bigint): Node | undefined {
    return this.#withTree(tree => this.runtime.withBuffer(8, out => {
      this.runtime.native.nocturne_wasm_tree_capture(tree, toU64(pointer), out);
      return this.#node(this.runtime.view(), out);
    }));
  }

  dispatch(now: bigint, input: Input, policy: DispatchPolicy = {}): Delivery {
    return this.#withTree(tree => this.runtime.withBuffer(72, value => {
      const text = storeInput(this.runtime, value, now, input);
      const chords = (policy.reserved ?? []).map(chord => [toU32(chord.code), toU32(chord.modifiers ?? 0)] as const);
      return this.runtime.withBytes(text, (bytes, count) => this.runtime.withBuffer(8 * chords.length, reserved => {
        const entries = this.runtime.view();
        chords.forEach(([key, modifiers], index) => {
          entries.setUint32(reserved + index * 8, key, true);
          entries.setUint32(reserved + index * 8 + 4, modifiers, true);
        });
        return this.runtime.withBuffer(24, out => {
          const status = this.runtime.native.nocturne_wasm_tree_dispatch(tree, value, bytes, count, reserved,
            chords.length, Number(Boolean(policy.traverseFocus)), out);
          const data = this.runtime.view();
          const delivery: Delivery = {
            admitted: data.getUint32(out, true) !== 0,
            target: this.#node(data, out + 4),
            reserved: data.getUint32(out + 12, true) !== 0,
            consumed: data.getUint32(out + 16, true) !== 0,
            textDelivered: data.getUint32(out + 20, true) !== 0,
          };
          try {
            this.#finish(status);
          } catch (cause) {
            throw new DispatchError(cause, delivery);
          }
          return delivery;
        });
      }));
    }));
  }

  peekIntent(): Intent | undefined {
    return this.#withTree(tree => this.runtime.withBuffer(20, out => {
      if (!this.runtime.native.nocturne_wasm_tree_intent(tree, out)) return undefined;
      const data = this.runtime.view();
      const source = this.#node(data, out);
      const kind = data.getUint32(out + 8, true);
      const bytes = new Uint8Array(data.buffer, data.getUint32(out + 12, true), data.getUint32(out + 16, true));
      return { source, kind, payload: bytes.slice() };
    }));
  }

  peekEventIntent(): EventIntent | undefined {
    return this.#withTree(tree => this.runtime.raw.withArena(arena => {
      const out = arena.value("NtEventIntentView");
      const present = arena.call("nt_tree_peek_event_intent", pointerSlot(arena, tree), pointerSlot(arena, out));
      if (!arena.unsigned(present, arena.api.size("_Bool"))) return undefined;
      return readEventIntent(this.runtime, arena, out, address => this.#intentSource(arena, address));
    }));
  }

  popIntent(): void {
    this.#withTree(tree => this.runtime.native.nocturne_wasm_tree_pop_intent(tree));
  }

  publish(size: Size): void {
    this.#withTree(tree => {
      const status = this.runtime.native.nocturne_wasm_tree_publish(tree, toU32(size.columns), toU32(size.rows));
      if (status === 0) this.#publication = {};
      this.#finish(status);
    });
  }

  get frame(): Frame | undefined {
    return this.#withTree(tree => {
      if (this.runtime.native.nocturne_wasm_tree_has_frame(tree) === 0) return undefined;
      const publication = this.#publication;
      const frame = new PublishedFrame(this.runtime, call => this.#withTree(tree => {
        if (this.#publication !== publication) throw this.runtime.failure(5);
        return call(tree);
      }));
      frameSources.set(frame, { owner: this, runtime: this.runtime, publication,
        claim: destination => this.#claimFrame(publication, destination) });
      return frame;
    });
  }

  close(): void {
    if (this.#handle !== 0) {
      this.#withTree(tree => {
        this.runtime.native.nocturne_wasm_tree_destroy(tree);
        this.#handle = 0;
        for (const [node, resources] of this.#editorViews) this.#dropEditorView(node, resources);
        for (const id of this.#receivers.keys()) this.#dropReceiver(id);
        this.#finish(0);
      });
    }
  }
}

/** Send the visible glyphs of a published frame to a line-oriented host. */
export function writeTerminalRows(frame: Frame, write: (row: string, index: number) => void): void {
  const size = frame.size;
  for (let y = 0; y < size.rows; y++) {
    const row: string[] = [];
    for (let x = 0; x < size.columns;) {
      const cell = frame.cell({ x, y });
      row.push(cell.columns ? cell.glyph : " ");
      x += Math.max(1, cell.columns);
    }
    write(row.join(""), y);
  }
}

function hostCallbacks(getRuntime: () => Runtime) {
  return {
    measure(id: number, context: number, minColumns: number, maxColumns: number, columnsBounded: number,
      minRows: number, maxRows: number, rowsBounded: number, out: number): number {
      const runtime = getRuntime();
      return runtime.invoke(id, "measure", (component, borrow) => {
        const measure: Measure = {
          get childCount() {
            borrow();
            return runtime.native.nocturne_wasm_measure_count(context) >>> 0;
          },
          child(index, limits) {
            borrow();
            return runtime.sizeCall(result => runtime.native.nocturne_wasm_measure_child(context, toU32(index),
              toU32(limits.columns.min), toU32(limits.columns.max ?? 0), Number(limits.columns.max !== undefined),
              toU32(limits.rows.min), toU32(limits.rows.max ?? 0), Number(limits.rows.max !== undefined), result));
          },
        };
        const limits = {
          columns: { min: minColumns >>> 0, max: columnsBounded ? maxColumns >>> 0 : undefined },
          rows: { min: minRows >>> 0, max: rowsBounded ? maxRows >>> 0 : undefined },
        };
        const preferred = component.measure?.(measure, limits) ?? { columns: limits.columns.min, rows: limits.rows.min };
        const columns = toU32(preferred.columns);
        const rows = toU32(preferred.rows);
        const data = runtime.view();
        data.setUint32(out >>> 0, columns, true);
        data.setUint32((out >>> 0) + 4, rows, true);
      });
    },
    layout(id: number, context: number, columns: number, rows: number): number {
      const runtime = getRuntime();
      return runtime.invoke(id, "layout", (component, borrow) => {
        const layout: Layout = {
          get childCount() {
            borrow();
            return runtime.native.nocturne_wasm_layout_count(context) >>> 0;
          },
          childSize(index) {
            borrow();
            return runtime.sizeCall(out => runtime.native.nocturne_wasm_layout_size(context, toU32(index), out));
          },
          placeClipped(index, bounds, clip, depth = 0) {
            borrow();
            runtime.raw.withArena(arena => {
              const layout = pointerSlot(arena, context);
              const child = arena.value("NtNodeId");
              const assigned = arena.value("NtCellRect"), visible = arena.value("NtCellRect");
              const z = arena.value("int32_t");
              writeNativeBounds(arena, assigned, bounds);
              writeNativeBounds(arena, visible, clip);
              arena.setUnsigned(z, arena.api.size("int32_t"), BigInt(toI32(depth)));
              arena.result("nt_layout_child_at", layout, sizeSlot(arena, toU32(index)), pointerSlot(arena, child));
              arena.result("nt_layout_place_clipped", layout, child, assigned, z, visible);
            });
          },
          place(index, bounds, depth = 0) {
            borrow();
            runtime.check(runtime.native.nocturne_wasm_layout_place(context, toU32(index),
              toI32(bounds.origin.x), toI32(bounds.origin.y), toU32(bounds.size.columns), toU32(bounds.size.rows),
              toI32(depth)));
          },
        };
        component.layout?.(layout, { columns: columns >>> 0, rows: rows >>> 0 });
      });
    },
    update(id: number, now: bigint, elapsed: bigint, motion: number, out: number): number {
      const runtime = getRuntime();
      return runtime.invoke(id, "update", component => {
        const reply = component.update?.({ now: BigInt.asUintN(64, now), elapsed: BigInt.asUintN(64, elapsed),
          motion: motion === 0 ? "full" : "reduced" });
        const next = reply?.next;
        runtime.native.nocturne_wasm_update_reply(out, toU32(invalidationCode[reply?.invalidate ?? "none"]),
          Number(next !== undefined), toU64(next ?? 0n));
      });
    },
    paint(id: number, context: number, columns: number, rows: number): number {
      const runtime = getRuntime();
      return runtime.invoke(id, "paint", (component, borrow, owner) => {
        let preparing = false;
        const writable = () => {
          borrow();
          if (preparing) throw runtime.failure(6);
        };
        const paint: Paint = {
          get clip() {
            writable();
            return runtime.raw.withArena(arena =>
              nativeBounds(arena, arena.call("nt_paint_clip", pointerSlot(arena, context))));
          },
          eraseGlyph(at) {
            writable();
            runtime.raw.withArena(arena =>
              arena.result("nt_paint_erase_glyph", pointerSlot(arena, context), cellPointSlot(arena, at)));
          },
          glyph(at, value, columns, style) {
            writable();
            runtime.withBytes(value, (address, count) => {
              runtime.check(runtime.native.nocturne_wasm_paint_glyph(context, toI32(at.x), toI32(at.y), address, count,
                toU32(columns), packedColor(style.foreground), packedColor(style.background)));
            });
          },
          fill(bounds, fill) {
            writable();
            runtime.check(runtime.native.nocturne_wasm_paint_fill(context, toI32(bounds.origin.x), toI32(bounds.origin.y),
              toU32(bounds.size.columns), toU32(bounds.size.rows), packedColor(fill)));
          },
          paintText(at, layout, options) {
            writable();
            preparing = true;
            try {
              runtime.raw.withArena(arena => {
                const position = cellPointSlot(arena, at);
                const settings = textPaintOptions(runtime, arena, options);
                const styles = pointerSlot(arena, arena.value("NtTextStyles"));
                borrow();
                OwnedTextLayout.use(runtime, layout, handle =>
                  arena.result("nt_paint_styled_text", pointerSlot(arena, context), position,
                    pointerSlot(arena, handle), settings, styles));
              });
            } finally {
              preparing = false;
            }
          },
          paintStyledText(at, layout, options, styles) {
            writable();
            preparing = true;
            try {
              runtime.raw.withArena(arena => drawStyledText(runtime, arena, "nt_paint_styled_text",
                pointerSlot(arena, context), at, layout, options, styles, borrow));
            } finally {
              preparing = false;
            }
          },
          paintTextEffect(at, layout, options, effect) {
            writable();
            preparing = true;
            try {
              runtime.raw.withArena(arena =>
                drawTextEffect(runtime, arena, "nt_paint_text_effect", pointerSlot(arena, context),
                  at, layout, options, effect, borrow));
            } finally {
              preparing = false;
            }
          },
          drawField(bounds, field, ink, now, motion = "full") {
            writable();
            preparing = true;
            try {
              runtime.raw.withArena(arena => {
                const rect = arena.value("NtCellRect");
                writeNativeBounds(arena, rect, bounds);
                const descriptor = fieldPointer(runtime, arena, field);
                const style = fieldInkPointer(arena, ink);
                const at = timeSlot(arena, now);
                const policy = enumSlot(arena, "NtMotionPolicy", enumCode(runtime, motionCode, motion));
                borrow();
                arena.result("nt_field_draw", descriptor, style, pointerSlot(arena, context), rect, at, policy);
              });
            } finally {
              preparing = false;
            }
          },
          drawVisualBranch(options, now, glyph, width, motion = "full") {
            writable();
            preparing = true;
            try {
              runtime.raw.withArena(arena => {
                const spec = visualBranchOptions(arena, options);
                const at = timeSlot(arena, now);
                const policy = enumSlot(arena, "NtMotionPolicy", enumCode(runtime, motionCode, motion));
                const ink = arena.byteView(glyph);
                const glyphWidth = arena.value("NtWidthPolicy");
                writeWidthPolicy(arena, glyphWidth, width);
                borrow();
                arena.result("nt_visual_branch_draw", spec, pointerSlot(arena, context), at, policy, ink, glyphWidth);
              });
            } finally {
              preparing = false;
            }
          },
          drawParticleDeposits(store, now, options = {}, motion = "full") {
            writable();
            const source = particleDepositSources.get(store);
            if (source?.runtime !== runtime) throw runtime.failure(1);
            preparing = true;
            try {
              source.use((arena, native) => {
                const style = depositPaint(arena, options);
                const at = timeSlot(arena, now);
                const policy = enumSlot(arena, "NtMotionPolicy", enumCode(runtime, motionCode, motion));
                borrow();
                arena.result("nt_particle_deposits_draw", native, pointerSlot(arena, context), at, policy, style);
              });
            } finally {
              preparing = false;
            }
          },
          drawParticles(emitter, now, options = {}, motion = "full") {
            writable();
            const source = particleEmitterSources.get(emitter);
            if (source?.runtime !== runtime) throw runtime.failure(1);
            preparing = true;
            try {
              source.use((arena, native) => {
                const style = particlePaint(arena, options);
                const at = timeSlot(arena, now);
                const policy = enumSlot(arena, "NtMotionPolicy", enumCode(runtime, motionCode, motion));
                borrow();
                arena.result("nt_particle_emitter_draw", native, pointerSlot(arena, context), at, policy,
                  pointerSlot(arena, style));
              });
            } finally {
              preparing = false;
            }
          },
          projectPanels(layers) {
            writable();
            preparing = true;
            try {
              projectPanelLayers(runtime, owner, context, layers);
            } finally {
              preparing = false;
            }
          },
          paintRaster(raster, origin, mode) {
            writable();
            preparing = true;
            try {
              runtime.spatialMeshes.paint(raster, context, origin, mode, borrow);
            } finally {
              preparing = false;
            }
          },
        };
        component.paint?.(paint, { columns: columns >>> 0, rows: rows >>> 0 });
      });
    },
    event(id: number, context: number, input: number, reply: number, kind: number, at: bigint): number {
      const runtime = getRuntime();
      let staged = 0;
      let stagedCause: unknown;
      const status = runtime.invoke(id, "event", (component, borrow, owner) => {
        let preparing = false;
        const writable = () => {
          borrow();
          if (preparing) throw runtime.failure(6);
        };
        const stage = <T>(call: () => T): T => {
          writable();
          if (staged) throw stagedCause;
          preparing = true;
          try {
            return call();
          } catch (cause) {
            if (cause instanceof NativeError) {
              staged = cause.code;
            } else if (cause instanceof RangeError || cause instanceof TypeError) {
              staged = 1;
            } else {
              staged = 9;
              owner.callbackFailure("event", cause);
            }
            stagedCause = cause;
            throw cause;
          } finally {
            preparing = false;
          }
        };
        const phase = () => runtime.raw.withArena(arena => {
          const result = arena.call("nt_event_phase", pointerSlot(arena, context));
          return Number(arena.unsigned(result, arena.api.size("NtEventPhase"))) as EventPhaseValue;
        });
        const observedInput = () => runtime.raw.withArena(arena => readObservedInput(runtime, arena, input));
        const localPosition = () => runtime.withBuffer(8, out => {
          if (!runtime.native.nocturne_wasm_event_local(context, out)) return undefined;
          const data = runtime.view();
          return { x: data.getInt32(out, true), y: data.getInt32(out + 4, true) };
        });
        const clock = BigInt.asUintN(64, at);
        const ruleScope: EventScope = {
          context,
          observe: () => ({ input: observedInput(), at: clock, phase: phase(), localPosition: localPosition() }),
          node: (arena, node) => owner.eventNode(arena, node),
          fail: (phase, cause) => owner.callbackFailure(phase, cause),
        };
        const event: Event = {
          kind, at: clock,
          get input() { borrow(); return observedInput(); },
          get phase() { borrow(); return phase(); },
          get key() {
            borrow();
            return kind === EventKind.key ? (observedInput() as Extract<Input, { kind: 0 }>).key : undefined;
          },
          get text() {
            borrow();
            if (kind !== EventKind.text && kind !== EventKind.paste) return undefined;
            return (observedInput() as { text: string }).text;
          },
          get localPosition() { borrow(); return localPosition(); },
          emit(kind, value) {
            stage(() => runtime.withBytes(value, (bytes, count) =>
              runtime.check(runtime.native.nocturne_wasm_event_emit(context, toU32(kind), bytes, count))));
          },
          emitFields(kind, fields) {
            stage(() => runtime.raw.withArena(arena => {
              const entries = Array.from(fields);
              const nativeKind = enumSlot(arena, "uint32_t", kind);
              const nativeFields = intentFieldArray(arena, entries);
              arena.result("nt_event_emit_fields", pointerSlot(arena, context), nativeKind,
                pointerSlot(arena, nativeFields), sizeSlot(arena, entries.length));
            }));
          },
          applyRules(rules) {
            return stage(() => applyEventRules(runtime, ruleScope, rules));
          },
          consume() {
            writable();
            runtime.native.nocturne_wasm_event_consume(reply);
          },
          invalidate(reason = "paint") {
            writable();
            runtime.native.nocturne_wasm_event_invalidate(reply, toU32(invalidationCode[reason]));
          },
          capture(request) {
            writable();
            runtime.native.nocturne_wasm_event_capture(reply, toU32(captureCode[request]));
          },
          requestFocus() {
            writable();
            const focus = runtime.raw.field("NtEventReply", "focus");
            runtime.view().setUint8(reply + focus.offset, 1);
          },
        };
        try {
          component.event?.(event);
        } catch (cause) {
          if (!staged || !Object.is(cause, stagedCause)) throw cause;
        }
      });
      return status || staged;
    },
    dispose(id: number): void {
      const runtime = getRuntime();
      try {
        runtime.invoke(id, "dispose", component => component.dispose?.());
      } finally {
        runtime.release(id);
      }
    },
  };
}

/** Instantiate the core SDK with browser-compatible WebAssembly imports. */
export async function loadNocturne(source: BufferSource | WebAssembly.Module): Promise<Nocturne> {
  const module = source instanceof WebAssembly.Module ? source : await WebAssembly.compile(source);
  let memory: WebAssembly.Memory | undefined;
  let runtime: Runtime;
  const imports = {
    nocturne: {
      ...hostCallbacks(() => runtime),
      raw_callback(token: number, id: number, result: number, args: number, count: number): void {
        runtime.raw.invokeCallback(token, id, result, args, count);
      },
    },
    wasi_snapshot_preview1: {
      random_get(offset: number, length: number): number {
        if (!memory) return 21;
        const address = offset >>> 0;
        const count = length >>> 0;
        if (address + count > memory.buffer.byteLength) return 21;
        if (!globalThis.crypto) return 52;
        const destination = new Uint8Array(memory.buffer, address, count);
        for (let begin = 0; begin < count; begin += 65536) {
          crypto.getRandomValues(destination.subarray(begin, begin + 65536));
        }
        return 0;
      },
    },
  };
  const instance = await WebAssembly.instantiate(module, imports);
  const native = instance.exports as NativeWasm;
  memory = native.memory;
  native._initialize();
  runtime = new Runtime(native);
  const abiVersion = native.nocturne_wasm_abi_version();
  if (abiVersion !== 1) throw runtime.failure(7);
  const availableComponents: NocturneModule[] = ["nocturne"];
  if (native.nocturne_wasm_tree_mount_graph && native.nocturne_wasm_tree_set_graph) availableComponents.push("graph");
  if (native.nocturne_wasm_vector_cross && native.nocturne_wasm_vector_normalize) availableComponents.push("spatial");
  if (native.nocturne_wasm_water_sample) availableComponents.push("simulation");
  return {
    abiVersion,
    get unicodeVersion() {
      return runtime.raw.withArena(arena => {
        const version = runtime.decodeCString(arena.pointer(arena.call("nt_unicode_version")));
        if (version === null) throw runtime.failure(7);
        return version;
      });
    },
    availableComponents: Object.freeze(availableComponents),
    get raw() { return runtime.raw; },
    spatial: Object.freeze({ ...spatialGeometry(runtime.raw, code => runtime.failure(code)), ...runtime.spatialMeshes.methods }),
    ...flowSimulation(runtime.raw, code => runtime.failure(code)),
    ...surfaceTransportSimulation(runtime.raw, code => runtime.failure(code)),
    ...bodySimulation(runtime.raw, code => runtime.failure(code)),
    createText(value) {
      return createText(runtime, value);
    },
    lineBreaks(value) {
      return runtime.raw.withArena(arena => {
        const type = "NtLineBreak";
        return readNativeRows(arena, "nt_line_breaks", type, [pointerSlot(arena, 0), arena.byteView(value)], row =>
          ({ offset: nativeField(arena, row, type, "offset"), mandatory: nativeField(arena, row, type, "mandatory") !== 0 }));
      });
    },
    scriptRuns(value) {
      return runtime.raw.withArena(arena => {
        const type = "NtScriptRun";
        const script = arena.api.field(type, "script").offset;
        return readNativeRows(arena, "nt_script_runs", type, [pointerSlot(arena, 0), arena.byteView(value)], row =>
          ({ ...nativeSourceSpan(arena, row, type), script: runtime.decode(row + script, 4) }));
      });
    },
    createBidi(value, direction = "auto") {
      if (!Object.hasOwn(bidiDirection, direction)) throw runtime.failure(1);
      return runtime.raw.withArena(arena => {
        const base = arena.value("NtDirection");
        arena.setUnsigned(base, arena.api.size("NtDirection"), BigInt(bidiDirection[direction]));
        const out = arena.value("pointer");
        arena.result("nt_bidi_create", pointerSlot(arena, 0), arena.byteView(value), base, pointerSlot(arena, out));
        return new OwnedBidi(runtime, arena.pointer(out));
      });
    },
    measureGlyph(glyph, width) {
      return runtime.raw.withArena(arena => {
        const policy = arena.value("NtWidthPolicy");
        writeWidthPolicy(arena, policy, width);
        const type = "NtGlyphMetrics";
        const out = arena.value(type);
        arena.result("nt_glyph_measure", arena.byteView(glyph), policy, pointerSlot(arena, out));
        return { presentation: nativeField(arena, out, type, "presentation") as GlyphMetrics["presentation"],
          columns: nativeField(arena, out, type, "columns") };
      });
    },
    createTextLayout(text, options = {}) {
      return createTextLayout(runtime, text, options);
    },
    layoutText(value, options = {}) {
      const text = createText(runtime, value);
      try {
        return createTextLayout(runtime, text, options);
      } finally {
        text.close();
      }
    },
    createStyleSheet(rules) {
      return createStyleSheet(runtime, rules);
    },
    resolveStyle(sheets, subject, property) {
      return resolveStyle(runtime, sheets, subject, property);
    },
    importStateSnapshot(bytes) {
      return importStateSnapshot(runtime, bytes);
    },
    colorFromRGBA(color) {
      return runtime.raw.withArena(arena =>
        readPaintColor(arena, arena.call("nt_color_from_rgba", rgbaSlot(arena, color))));
    },
    colorToRGBA(color, gamut = GamutPolicy.clip) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("NtRgba");
        arena.result("nt_color_to_rgba", paintColorSlot(arena, color),
          enumSlot(arena, "NtGamutPolicy", gamut), pointerSlot(arena, out));
        return readRGBA(arena, out);
      });
    },
    convertColor(color, space, gamut = GamutPolicy.clip) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("NtColor");
        arena.result("nt_color_convert", paintColorSlot(arena, color), enumSlot(arena, "NtColorSpace", space),
          enumSlot(arena, "NtGamutPolicy", gamut), pointerSlot(arena, out));
        return readPaintColor(arena, out);
      });
    },
    mixColor(a, b, progress, interpolation) {
      return runtime.raw.withArena(arena => {
        const spec = arena.value("NtColorInterpolation");
        writeColorInterpolation(arena, spec, interpolation);
        const out = arena.value("NtColor");
        arena.result("nt_color_mix", paintColorSlot(arena, a), paintColorSlot(arena, b),
          floatSlot(arena, progress), spec, pointerSlot(arena, out));
        return readPaintColor(arena, out);
      });
    },
    parseBlendMode(name) {
      return parseNativeName(runtime, name, "NtBlendMode", "nt_blend_mode_parse") as BlendMode;
    },
    parseCompositeOperator(name) {
      return parseNativeName(runtime, name, "NtCompositeOperator", "nt_composite_operator_parse") as CompositeOperator;
    },
    compositeColor(backdrop, source, options) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("NtColor");
        arena.result("nt_color_composite", paintColorSlot(arena, backdrop), paintColorSlot(arena, source),
          compositePointer(arena, options), pointerSlot(arena, out));
        return readPaintColor(arena, out);
      });
    },
    compositeRGBA(backdrop, source, options) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("NtRgba");
        arena.result("nt_rgba_composite", rgbaSlot(arena, backdrop), rgbaSlot(arena, source),
          compositePointer(arena, options), pointerSlot(arena, out));
        return readRGBA(arena, out);
      });
    },
    lookupPalette(palette, index) {
      return runtime.raw.withArena(arena => {
        const colors = arena.value("NtPaletteView");
        writePalette(arena, colors, palette);
        const out = arena.value("NtRgba");
        arena.result("nt_palette_lookup", colors, sizeSlot(arena, index), pointerSlot(arena, out));
        return readRGBA(arena, out);
      });
    },
    nearestPalette(palette, color, gamut = GamutPolicy.clip) {
      return runtime.raw.withArena(arena => {
        const colors = arena.value("NtPaletteView");
        writePalette(arena, colors, palette);
        const out = arena.value("size_t");
        arena.result("nt_palette_nearest", colors, paintColorSlot(arena, color), enumSlot(arena, "NtGamutPolicy", gamut),
          pointerSlot(arena, out));
        return Number(arena.unsigned(out, arena.api.size("size_t")));
      });
    },
    sampleGradient(gradient, coordinate) {
      return runtime.raw.withArena(arena => {
        const stops = arena.value("NtGradient");
        writeGradient(arena, stops, gradient);
        const out = arena.value("NtColor");
        arena.result("nt_gradient_sample", pointerSlot(arena, stops), floatSlot(arena, coordinate),
          pointerSlot(arena, out));
        return readPaintColor(arena, out);
      });
    },
    samplePalette(palette, coordinate) {
      return runtime.raw.withArena(arena => {
        const ramp = arena.value("NtPaletteRamp");
        writePaletteRamp(arena, ramp, palette);
        const out = arena.value("NtColor");
        arena.result("nt_palette_sample", pointerSlot(arena, ramp), floatSlot(arena, coordinate), pointerSlot(arena, out));
        return readPaintColor(arena, out);
      });
    },
    opticalFalloff(falloff, distance) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("double");
        arena.result("nt_optical_falloff", enumSlot(arena, "NtOpticalFalloff", falloff), floatSlot(arena, distance),
          pointerSlot(arena, out));
        return arena.float64(out);
      });
    },
    defaultRippleField() {
      return runtime.raw.withArena(arena => readRippleField(arena, arena.call("nt_ripple_field_default")));
    },
    defaultWaveField() {
      return runtime.raw.withArena(arena => readWaveField(arena, arena.call("nt_wave_field_default")));
    },
    sampleField(field, query) {
      return runtime.raw.withArena(arena => {
        const type = "NtFieldSample", out = arena.value(type);
        arena.result("nt_field_sample", fieldPointer(runtime, arena, field), fieldQuerySlot(runtime, arena, query),
          pointerSlot(arena, out));
        return { value: arena.float64(out + arena.api.field(type, "value").offset),
          coverage: arena.float64(out + arena.api.field(type, "coverage").offset) };
      });
    },
    fieldColor(field, ink, query) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("NtRgba");
        arena.result("nt_field_color", fieldPointer(runtime, arena, field), fieldInkPointer(arena, ink),
          fieldQuerySlot(runtime, arena, query), pointerSlot(arena, out));
        return readRGBA(arena, out);
      });
    },
    fieldDeadline(field, now, frameIntervalUs, motion = "full") {
      return runtime.raw.withArena(arena => {
        const type = "NtDeadline", out = arena.value(type);
        arena.result("nt_field_deadline", fieldPointer(runtime, arena, field), timeSlot(arena, now),
          enumSlot(arena, "NtMotionPolicy", enumCode(runtime, motionCode, motion)), timeSlot(arena, frameIntervalUs),
          pointerSlot(arena, out));
        const at = arena.api.field(type, "at");
        return nativeField(arena, out, type, "scheduled") ? arena.unsigned(out + at.offset, at.size) : undefined;
      });
    },
    paintCoordinate(field, sample) {
      return runtime.raw.withArena(arena => {
        const descriptor = arena.value("NtPaintField");
        writePaintField(arena, descriptor, field);
        const out = arena.value("double");
        arena.result("nt_paint_coordinate", pointerSlot(arena, descriptor), paintSampleSlot(runtime, arena, sample),
          pointerSlot(arena, out));
        return arena.float64(out);
      });
    },
    samplePaint(paint, sample) {
      return runtime.raw.withArena(arena => {
        const descriptor = paintSourceAddress(runtime, arena, paint);
        const out = arena.value("NtColor");
        arena.result("nt_paint_sample", pointerSlot(arena, descriptor), paintSampleSlot(runtime, arena, sample),
          pointerSlot(arena, out));
        return readPaintColor(arena, out);
      });
    },
    samplePaintRGBA(paint, sample, gamut = GamutPolicy.clip) {
      return runtime.raw.withArena(arena => {
        const descriptor = paintSourceAddress(runtime, arena, paint);
        const out = arena.value("NtRgba");
        arena.result("nt_paint_sample_rgba", pointerSlot(arena, descriptor), paintSampleSlot(runtime, arena, sample),
          enumSlot(arena, "NtGamutPolicy", gamut), pointerSlot(arena, out));
        return readRGBA(arena, out);
      });
    },
    textPoseMotion(sample, pose, motion) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("NtTextPose");
        writeTextPose(arena, out, pose);
        arena.result("nt_text_pose_motion", textEffectClock(arena, sample), textMotionSlot(arena, motion),
          pointerSlot(arena, out));
        return readTextPose(arena, out, pose.glyph);
      });
    },
    textPoseNoise(sample, pose, extent, intervalUs, smooth = false) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("NtTextPose"), axes = arena.value("NtPoint");
        writeTextPose(arena, out, pose);
        writeContinuousPoint(arena, axes, extent);
        arena.result("nt_text_pose_noise", textEffectClock(arena, sample), axes, timeSlot(arena, intervalUs),
          boolSlot(arena, smooth), pointerSlot(arena, out));
        return readTextPose(arena, out, pose.glyph);
      });
    },
    createEditor(value, options = {}) {
      return runtime.raw.withArena(arena => {
        const type = "NtEditorOptions";
        const nativeOptions = arena.value(type);
        setNativeField(arena, nativeOptions, type, "lines", options.singleLine ? 1 : 0);
        setNativeField(arena, nativeOptions, type, "read_only", options.readOnly ? 1 : 0);
        const editor = arena.value("pointer");
        arena.result("nt_editor_create", pointerSlot(arena, 0), arena.byteView(value),
          pointerSlot(arena, nativeOptions), pointerSlot(arena, editor));
        return new OwnedEditor(runtime, arena.pointer(editor));
      });
    },
    createEditorRegisters(slots) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("pointer");
        arena.result("nt_editor_registers_create", pointerSlot(arena, 0), sizeSlot(arena, slots), pointerSlot(arena, out));
        return new OwnedEditorRegisters(runtime, arena.pointer(out));
      });
    },
    createTree() {
      return runtime.withBuffer(4, out => {
        runtime.check(native.nocturne_wasm_tree_create(out));
        return new OwnedTree(runtime, runtime.view().getUint32(out, true));
      });
    },
    resolveScroll(options, viewport, content) {
      return runtime.raw.withArena(arena => {
        const visible = arena.value("NtCellSize"), total = arena.value("NtCellSize");
        writeNativeSize(arena, visible, viewport);
        writeNativeSize(arena, total, content);
        return nativeSize(arena, arena.call("nt_scroll_resolve", viewportOptions(arena, options), visible, total));
      });
    },
    anchorRect(parent, child, horizontal, vertical, offset) {
      return runtime.raw.withArena(arena => {
        const available = arena.value("NtCellSize"), occupied = arena.value("NtCellSize");
        writeNativeSize(arena, available, parent);
        writeNativeSize(arena, occupied, child);
        const x = arena.value("NtBoxAlign"), y = arena.value("NtBoxAlign");
        const size = arena.api.size("NtBoxAlign");
        arena.setUnsigned(x, size, BigInt(enumCode(runtime, textAlignCode, horizontal)));
        arena.setUnsigned(y, size, BigInt(enumCode(runtime, textAlignCode, vertical)));
        const out = arena.value("NtCellRect");
        arena.result("nt_anchor_rect", available, occupied, x, y, cellPointSlot(arena, offset), pointerSlot(arena, out));
        return nativeBounds(arena, out);
      });
    },
    xterm256Palette() {
      return runtime.raw.withArena(arena => {
        const count = 256, type = "NtRgba", size = arena.api.size(type), out = valueArray(arena, type, count);
        arena.result("nt_xterm256_palette", pointerSlot(arena, out), sizeSlot(arena, count));
        return Array.from({ length: count }, (_, index) => readRGBA(arena, out + index * size));
      });
    },
    writeAnsi(frame, now, options, write) {
      writeAnsi(runtime, frame, now, options, write);
    },
    createInputDecoder(options) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("pointer");
        let address = 0;
        if (options !== undefined) {
          const type = "NtInputOptions";
          address = arena.value(type);
          setNativeField(arena, address, type, "escape_delay", toU64(options.escapeDelayUs ?? 25000n));
          setNativeField(arena, address, type, "paste_limit", toU32(options.pasteLimit ?? 1048576));
          setNativeField(arena, address, type, "keyboard", enumCode(runtime, keyboardProtocols, options.keyboard ?? "xterm"));
        }
        arena.result("nt_input_create", pointerSlot(arena, 0), pointerSlot(arena, address), pointerSlot(arena, out));
        return ownedInputDecoder(runtime, arena.pointer(out));
      });
    },
    createCanvas(size) {
      return runtime.raw.withArena(arena => {
        const samples = arena.value("NtCellSize"), output = arena.value("pointer");
        writeNativeSize(arena, samples, size);
        arena.result("nt_canvas_create", pointerSlot(arena, 0), samples, pointerSlot(arena, output));
        return ownedCanvas(runtime, arena.pointer(output));
      });
    },
    parseSamplingMode(name) {
      return samplingModes[parseNativeName(runtime, name, "NtSamplingMode", "nt_sampling_mode_parse")];
    },
    reduceCellSamples(pixels, background, mode) {
      return reduceCellSamples(runtime, pixels, background, mode);
    },
    createOffscreenFrame(size) {
      return runtime.raw.withArena(arena => {
        const nativeSize = arena.value("NtCellSize");
        writeNativeSize(arena, nativeSize, size);
        const out = arena.value("pointer");
        arena.result("nt_frame_create", pointerSlot(arena, 0), nativeSize, pointerSlot(arena, out));
        return new OwnedOffscreenFrame(runtime, arena.pointer(out));
      });
    },
    frameFromCells(size, cells) {
      return runtime.raw.withArena(arena => {
        const count = toU32(cells.length), bounds = arena.value("NtCellSize"), out = arena.value("pointer");
        writeNativeSize(arena, bounds, size);
        arena.result("nt_frame_from_cells", pointerSlot(arena, 0), bounds,
          contentCells(runtime, arena, cells, count), sizeSlot(arena, count), pointerSlot(arena, out));
        return new OwnedOffscreenFrame(runtime, arena.pointer(out));
      });
    },
    sampleImage(image, u, v, sampling = {}) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("NtRgba");
        arena.result("nt_image_sample", imageView(arena, image), imageSampling(arena, sampling),
          floatSlot(arena, u), floatSlot(arena, v), pointerSlot(arena, out));
        return readRGBA(arena, out);
      });
    },
    rasterizeImage(image, extent, options = {}) {
      return runtime.raw.withArena(arena => {
        const bounds = arena.value("NtCellSize"), out = arena.value("pointer");
        writeNativeSize(arena, bounds, extent);
        const mode = samplingModes.indexOf(options.sampling ?? "cell");
        if (mode < 0) throw runtime.failure(1);
        const nativeMode = arena.value("NtSamplingMode");
        arena.setUnsigned(nativeMode, arena.api.size("NtSamplingMode"), BigInt(mode));
        arena.result("nt_image_rasterize", imageView(arena, image), imageSampling(arena, options), bounds,
          nativeMode, ownerSlot(arena, options.owner ?? 0n), pointerSlot(arena, 0), pointerSlot(arena, out));
        return new OwnedOffscreenFrame(runtime, arena.pointer(out));
      });
    },
    sampleVisualBranch(options, now, motion = "full") {
      return runtime.raw.withArena(arena => {
        const type = "NtVisualBranchSegment";
        const spec = visualBranchOptions(arena, options);
        const policy = enumSlot(arena, "NtMotionPolicy", enumCode(runtime, motionCode, motion));
        return readNativeRows(arena, "nt_visual_branch_sample", type, [spec, timeSlot(arena, now), policy], address => ({
          id: nativeField(arena, address, type, "id"),
          parentId: nativeField(arena, address, type, "parent_id"),
          start: nativeCellPoint(arena, address + arena.api.field(type, "start").offset),
          end: nativeCellPoint(arena, address + arena.api.field(type, "end").offset),
          color: readRGBA(arena, address + arena.api.field(type, "color").offset),
        }));
      });
    },
    visualBranchDeadline(options, now, motion = "full", frameIntervalUs = 0n) {
      return runtime.raw.withArena(arena => particleDeadline(arena, runtime, visualBranchOptions(arena, options),
        "nt_visual_branch_deadline", now, motion, frameIntervalUs));
    },
    createParticleDeposits(options = {}) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("pointer");
        arena.result("nt_particle_deposits_create", pointerSlot(arena, 0),
          depositsOptions(arena, options), pointerSlot(arena, out));
        return ownedParticleDeposits(runtime, arena.pointer(out));
      });
    },
    particleEffectRecipe(recipe) {
      return runtime.raw.withArena(arena => readParticleEffect(runtime, arena,
        arena.pointer(particleEffectRecipePointer(arena, recipe))));
    },
    createParticleEffectEmitter(effect) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("pointer");
        arena.result("nt_particle_effect_emitter_create", pointerSlot(arena, 0),
          authoredParticleEffectPointer(arena, effect, false), pointerSlot(arena, out));
        return ownedParticleEmitter(runtime, arena.pointer(out));
      });
    },
    createParticleEmitter(options = {}) {
      return runtime.raw.withArena(arena => {
        const out = arena.value("pointer");
        arena.result("nt_particle_emitter_create", pointerSlot(arena, 0),
          particleEmitterOptions(arena, options), pointerSlot(arena, out));
        return ownedParticleEmitter(runtime, arena.pointer(out));
      });
    },
    createTimeline(clips, options = {}) {
      const count = toU32(clips.length);
      if (count === 0) throw runtime.failure(1);
      return compileTimeline(runtime, options, (arena, root) => {
        const type = "NtTimelineNode";
        const size = arena.api.size(type);
        const children = arena.allocate(count * size);
        const values = Array.from({ length: count }, (_, index) => clips[index]);
        for (const [index, clip] of values.entries()) {
          writeTimelineNode(runtime, arena, children + index * size, {
            kind: "clip", delayUs: clip.delayUs, channel: clip.channel, spanUs: clip.spanUs,
            source: { kind: "tween", tween: clip.tween },
          }, 1);
        }
        const layouts = { sequence: 1, parallel: 2 } as const;
        setNativeField(arena, root, type, "kind", toU32(layouts[options.layout ?? "sequence"]));
        setNativeField(arena, root, type, "data.group.children", children);
        setNativeField(arena, root, type, "data.group.count", count);
      });
    },
    createTimelineTree(root, options = {}) {
      return compileTimeline(runtime, options, (arena, node) => writeTimelineNode(runtime, arena, node, root, 0));
    },
    limitTrackRate(requested, minimum, maximum) {
      return runtime.raw.withArena(arena => {
        const type = "NtTrackRate";
        const request = arena.value(type), lower = arena.value(type), upper = arena.value(type), out = arena.value(type);
        writeTrackRate(arena, request, requested);
        writeTrackRate(arena, lower, minimum);
        writeTrackRate(arena, upper, maximum);
        arena.result("nt_track_rate_limit", request, lower, upper, pointerSlot(arena, out));
        return { numerator: nativeField(arena, out, type, "numerator"),
          denominator: nativeField(arena, out, type, "denominator") };
      });
    },
    mapTrackTime(timing, elapsedUs) {
      return runtime.raw.withArena(arena => {
        const native = arena.value("NtTrackTime");
        writeTrackTime(arena, native, timing);
        const out = arena.value("NtTimeUs");
        arena.result("nt_track_time", pointerSlot(arena, native), timeSlot(arena, elapsedUs), pointerSlot(arena, out));
        return arena.unsigned(out, arena.api.size("NtTimeUs"));
      });
    },
    sampleRandomTrack(track, atUs) {
      return runtime.raw.withArena(arena => {
        const native = arena.value("NtRandomTrack");
        writeRandomTrack(arena, native, track);
        const out = arena.value("double");
        arena.result("nt_random_track_sample", pointerSlot(arena, native), timeSlot(arena, atUs), pointerSlot(arena, out));
        return arena.float64(out);
      });
    },
    scheduleTrack(schedule, rank, item, spanUs) {
      return runtime.raw.withArena(arena => {
        const native = arena.value("NtTrackSchedule");
        writeTrackSchedule(arena, native, schedule);
        const out = arena.value("NtTrackSlot");
        arena.result("nt_track_schedule", pointerSlot(arena, native), timeSlot(arena, rank),
          timeSlot(arena, item), timeSlot(arena, spanUs), pointerSlot(arena, out));
        const delay = arena.api.field("NtTrackSlot", "delay_us"), span = arena.api.field("NtTrackSlot", "span_us");
        return { delayUs: arena.unsigned(out + delay.offset, delay.size), spanUs: arena.unsigned(out + span.offset, span.size) };
      });
    },
    ease(easing, progress) {
      return runtime.raw.withArena(arena => {
        const mode = arena.value("NtEasing");
        arena.setUnsigned(mode, arena.api.size("NtEasing"), BigInt(toU32(easing)));
        const out = arena.value("double");
        animationCall(runtime, arena, "nt_ease", mode, floatSlot(arena, progress), pointerSlot(arena, out));
        return arena.float64(out);
      });
    },
    sampleTween(tween, elapsedUs) {
      const sample = native.nocturne_wasm_tween_sample;
      if (!sample) throw runtime.failure(7);
      const { from, to } = tween;
      if (!Number.isFinite(from) || !Number.isFinite(to)) throw runtime.failure(1);
      const duration = toU64(tween.durationUs);
      const delay = toU64(tween.delayUs ?? 0n);
      const easing = toU32(tween.easing ?? Easing.linear);
      const repeat = toU32(tween.repeat ?? Repeat.once);
      const elapsed = toU64(elapsedUs);
      return runtime.withBuffer(32, out => {
        runtime.check(sample(from, to, duration, delay, easing, repeat, elapsed, out));
        const data = runtime.view();
        return {
          value: data.getFloat64(out, true),
          progress: data.getFloat64(out + 8, true),
          iteration: data.getBigUint64(out + 16, true),
          phase: data.getInt32(out + 24, true) as AnimationSample["phase"],
          direction: data.getInt32(out + 28, true) as AnimationSample["direction"],
        };
      });
    },
    sampleKeyframes(track, elapsedUs) {
      return runtime.raw.withArena(arena => {
        const nativeTrack = arena.value("NtKeyframeTrack");
        writeKeyframeTrack(arena, nativeTrack, track);
        const sample = "NtAnimationSample";
        const out = arena.value(sample);
        animationCall(runtime, arena, "nt_keyframes_sample", pointerSlot(arena, nativeTrack),
          timeSlot(arena, elapsedUs), pointerSlot(arena, out));
        const iteration = arena.api.field(sample, "iteration");
        return {
          value: arena.float64(out + arena.api.field(sample, "value").offset),
          progress: arena.float64(out + arena.api.field(sample, "progress").offset),
          iteration: arena.unsigned(out + iteration.offset, iteration.size),
          phase: nativeField(arena, out, sample, "phase") as AnimationSample["phase"],
          direction: nativeField(arena, out, sample, "direction", true) as AnimationSample["direction"],
        };
      });
    },
    createPlayback(nowUs) {
      return withAnimationValue(runtime, "NtPlayback", undefined, (arena, value, pointer) => {
        animationCall(runtime, arena, "nt_playback_init", pointer, timeSlot(arena, nowUs));
        return playbackFromNative(runtime, arena, value);
      });
    },
    createSpring({ tuning, value, velocity = 0, target, startUs }) {
      return withAnimationValue(runtime, "NtSpring", undefined, (arena, spring, pointer) => {
        const type = "NtSpringTuning";
        const settings = arena.value(type);
        setNativeField(arena, settings, type, "response_us", toU64(tuning.responseUs));
        arena.setFloat64(settings + arena.api.field(type, "position_tolerance").offset, tuning.positionTolerance);
        arena.setFloat64(settings + arena.api.field(type, "velocity_tolerance").offset, tuning.velocityTolerance);
        animationCall(runtime, arena, "nt_spring_init", pointer, settings, floatSlot(arena, value),
          floatSlot(arena, velocity), floatSlot(arena, target), timeSlot(arena, startUs));
        return springFromNative(runtime, arena, spring);
      });
    },
    pathLength(path) {
      return runtime.raw.withArena(arena => {
        const native = nativePath(arena, path);
        const out = arena.value("double");
        animationCall(runtime, arena, "nt_path_length", pointerSlot(arena, native), pointerSlot(arena, out));
        return arena.float64(out);
      });
    },
    samplePath(path, distance) {
      return runtime.raw.withArena(arena => {
        const native = nativePath(arena, path);
        const type = "NtPathSample";
        const out = arena.value(type);
        animationCall(runtime, arena, "nt_path_sample", pointerSlot(arena, native), floatSlot(arena, distance),
          pointerSlot(arena, out));
        return {
          position: readContinuousPoint(arena, out + arena.api.field(type, "position").offset),
          tangent: readContinuousPoint(arena, out + arena.api.field(type, "tangent").offset),
          distance: arena.float64(out + arena.api.field(type, "distance").offset),
          length: arena.float64(out + arena.api.field(type, "length").offset),
        };
      });
    },
    projectPanelPoint(pose, point) {
      return runtime.raw.withArena(arena => {
        const native = arena.value("NtPanelPose");
        writePanelPose(arena, native, pose);
        const input = arena.value("NtPoint"), out = arena.value("NtPoint");
        writeContinuousPoint(arena, input, point);
        arena.result("nt_panel_project_point", pointerSlot(arena, native), input, pointerSlot(arena, out));
        return readContinuousPoint(arena, out);
      });
    },
    samplePanelPose(transition, elapsedUs, motion = "full") {
      return runtime.raw.withArena(arena => {
        const type = "NtPanelTransition";
        const native = arena.value(type);
        writePanelPose(arena, native + arena.api.field(type, "from").offset, transition.from);
        writePanelPose(arena, native + arena.api.field(type, "to").offset, transition.to);
        setNativeField(arena, native, type, "delay_us", toU64(transition.delayUs ?? 0n));
        setNativeField(arena, native, type, "duration_us", toU64(transition.durationUs));
        setNativeField(arena, native, type, "easing", toU32(transition.easing ?? Easing.linear));
        const policy = arena.value("NtMotionPolicy");
        arena.setUnsigned(policy, arena.api.size("NtMotionPolicy"), BigInt(toU32(motionCode[motion])));
        const out = arena.value("NtPanelPose");
        arena.result("nt_panel_pose_at", pointerSlot(arena, native), timeSlot(arena, elapsedUs), policy,
          pointerSlot(arena, out));
        return readPanelPose(arena, out);
      });
    },
    projectPlot(view, point) {
      return runtime.raw.withArena(arena => {
        const type = "NtPlotViewport";
        const viewport = arena.value(type);
        writePlotBounds(arena, viewport + arena.api.field(type, "domain").offset, view.domain);
        writeNativeBounds(arena, viewport + arena.api.field(type, "cells").offset, view.cells);
        arena.setFloat64(viewport + arena.api.field(type, "cell_aspect").offset, view.cellAspect ?? 0);
        setNativeField(arena, viewport, type, "equal_units", view.equalUnits ? 1 : 0);
        const input = arena.value("NtPoint"), out = arena.value("NtPoint");
        writeContinuousPoint(arena, input, point);
        arena.result("nt_plot_project", pointerSlot(arena, viewport), input, pointerSlot(arena, out));
        return readContinuousPoint(arena, out);
      });
    },
    clipPlotSegment(domain, segment) {
      return runtime.raw.withArena(arena => {
        const bounds = arena.value("NtPlotBounds");
        const first = arena.value("NtPoint"), last = arena.value("NtPoint");
        const visible = arena.value("_Bool");
        const outFirst = arena.value("NtPoint"), outLast = arena.value("NtPoint");
        writePlotBounds(arena, bounds, domain);
        writeContinuousPoint(arena, first, segment.first);
        writeContinuousPoint(arena, last, segment.last);
        arena.result("nt_plot_clip_segment", bounds, first, last, pointerSlot(arena, visible),
          pointerSlot(arena, outFirst), pointerSlot(arena, outLast));
        return arena.unsigned(visible, arena.api.size("_Bool")) === 0n ? undefined :
          { first: readContinuousPoint(arena, outFirst), last: readContinuousPoint(arena, outLast) };
      });
    },
    decodeControlIntent(intent) {
      return runtime.raw.withArena(arena => {
        const type = "NtIntentView";
        const native = arena.value(type), out = arena.value("NtControlEvent");
        setNativeField(arena, native, type, "kind", toU32(intent.kind));
        const payload = arena.api.field(type, "payload");
        arena.write(native + payload.offset, arena.read(arena.byteView(intent.payload), payload.size));
        arena.result("nt_control_event_decode", native, pointerSlot(arena, out));
        const value = arena.api.field("NtControlEvent", "value");
        return { kind: nativeField(arena, out, "NtControlEvent", "kind") as ControlEvent["kind"],
          value: arena.unsigned(out + value.offset, value.size) };
      });
    },
    layoutGraph(graph) {
      return graphSnapshot(runtime, graph);
    },
    decodeGraphIntent(intent) {
      requireGraph(runtime);
      if (intent?.kind !== GraphSelectionIntent || intent.payloadKind !== "fields") throw runtime.failure(1);
      const entries = intent.fields;
      if (entries.length !== 2) throw runtime.failure(1);
      const fields = Array.from({ length: 2 }, (_, index) => {
        const { id, value } = entries[index];
        if (value.kind !== "unsigned") throw runtime.failure(1);
        return { id, value: { kind: "unsigned", value: value.value } } as const;
      });
      return runtime.raw.withArena(arena => {
        const type = "NtEventIntentView", native = arena.value(type), out = arena.value("NtGraphHit");
        const view = native + arena.api.field(type, "intent").offset;
        setNativeField(arena, view, "NtIntentView", "kind", GraphSelectionIntent);
        setNativeField(arena, native, type, "payload_kind", arena.api.constant("NT_INTENT_FIELDS")!);
        setNativeField(arena, native, type, "fields", intentFieldArray(arena, fields));
        setNativeField(arena, native, type, "field_count", fields.length);
        arena.result("nt_graph_event_decode", native, pointerSlot(arena, out));
        return graphHit(arena, out);
      });
    },
    decodeNotificationIntent(intent) {
      if (intent.kind !== NotificationIntentKind || intent.payloadKind !== "fields") throw runtime.failure(1);
      const entries = intent.fields;
      if (entries.length !== 3) throw runtime.failure(1);
      const fields = Array.from({ length: 3 }, (_, index) => {
        const { id, value } = entries[index];
        if (value.kind !== "unsigned") throw runtime.failure(1);
        return { id, value: { kind: "unsigned", value: value.value } } as const;
      });
      return runtime.raw.withArena(arena => {
        const type = "NtEventIntentView", resultType = "NtNotificationEvent";
        const native = arena.value(type), out = arena.value(resultType);
        const view = native + arena.api.field(type, "intent").offset;
        setNativeField(arena, view, "NtIntentView", "kind", NotificationIntentKind);
        setNativeField(arena, native, type, "payload_kind", arena.api.constant("NT_INTENT_FIELDS")!);
        setNativeField(arena, native, type, "fields", intentFieldArray(arena, fields));
        setNativeField(arena, native, type, "field_count", fields.length);
        arena.result("nt_notification_event_decode", native, pointerSlot(arena, out));
        return { kind: nativeField(arena, out, resultType, "kind") as NotificationEventKind,
          notification: unsignedNativeField(arena, out, resultType, "notification"),
          action: unsignedNativeField(arena, out, resultType, "action") };
      });
    },
    crossVector(left, right) {
      const cross = native.nocturne_wasm_vector_cross;
      if (!cross) throw runtime.failure(7);
      return runtime.withBuffer(24, out => {
        runtime.check(cross(left.x, left.y, left.z, right.x, right.y, right.z, out));
        return vectorAt(runtime.view(), out);
      });
    },
    normalizeVector(vector) {
      const normalize = native.nocturne_wasm_vector_normalize;
      if (!normalize) throw runtime.failure(7);
      return runtime.withBuffer(24, out => {
        runtime.check(normalize(vector.x, vector.y, vector.z, out));
        return vectorAt(runtime.view(), out);
      });
    },
    sampleWater(waves, x, z, seconds) {
      const sample = native.nocturne_wasm_water_sample;
      if (!sample) throw runtime.failure(7);
      const count = toU32(waves.length);
      if (count > 32) throw runtime.failure(2);
      const values = Array.from({ length: count }, (_, index) => {
        const { amplitude, cyclesPerX, cyclesPerZ, cyclesPerSecond, phase } = waves[index];
        return [amplitude, cyclesPerX, cyclesPerZ, cyclesPerSecond, phase];
      });
      return runtime.withBuffer(40 * count + 64, address => {
        const input = runtime.view();
        for (const [index, wave] of values.entries()) {
          wave.forEach((value, field) => input.setFloat64(address + 40 * index + 8 * field, value, true));
        }
        const out = address + 40 * count;
        runtime.check(sample(count ? address : 0, count, x, z, seconds, out));
        const data = runtime.view();
        return {
          height: data.getFloat64(out, true),
          gradientX: data.getFloat64(out + 8, true),
          gradientZ: data.getFloat64(out + 16, true),
          normal: vectorAt(data, out + 24),
          verticalVelocity: data.getFloat64(out + 48, true),
          laplacian: data.getFloat64(out + 56, true),
        };
      });
    },
    sampleFlowSection(section, rate) {
      if (!native.nocturne_wasm_water_sample) throw runtime.failure(7);
      return runtime.raw.withArena(arena => {
        const type = "NtFlowSection";
        const input = arena.value(type);
        arena.setFloat64(input + arena.api.field(type, "width").offset, section.width);
        arena.setFloat64(input + arena.api.field(type, "speed").offset, section.speed);
        arena.setFloat64(input + arena.api.field(type, "maximum_depth").offset, section.maximumDepth);
        const sample = "NtFlowSectionSample";
        const out = arena.value(sample);
        arena.result("nt_flow_section_sample", pointerSlot(arena, input), floatSlot(arena, rate), pointerSlot(arena, out));
        return { depth: arena.float64(out + arena.api.field(sample, "depth").offset),
          carriedRate: arena.float64(out + arena.api.field(sample, "carried_rate").offset),
          excessRate: arena.float64(out + arena.api.field(sample, "excess_rate").offset) };
      });
    },
    exchangeHeat(stores, conductance, elapsedUs) {
      if (!native.nocturne_wasm_water_sample) throw runtime.failure(7);
      return runtime.raw.withArena(arena => {
        const type = "NtThermalPair";
        const input = arena.value(type);
        const first = arena.api.field(type, "first").offset;
        const second = arena.api.field(type, "second").offset;
        for (const [offset, store] of [[first, stores.first], [second, stores.second]] as const) {
          arena.setFloat64(input + offset + arena.api.field("NtThermalStore", "heat_capacity").offset, store.heatCapacity);
          arena.setFloat64(input + offset + arena.api.field("NtThermalStore", "energy").offset, store.energy);
        }
        const elapsed = arena.value("NtTimeUs");
        arena.setUnsigned(elapsed, arena.api.size("NtTimeUs"), toU64(elapsedUs));
        const out = arena.value(type);
        arena.result("nt_thermal_exchange", pointerSlot(arena, input), floatSlot(arena, conductance),
          elapsed, pointerSlot(arena, out));
        return { first: thermalStore(arena, out + first), second: thermalStore(arena, out + second) };
      });
    },
  };
}
