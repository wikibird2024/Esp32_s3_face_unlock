// Wire types shared with the Rust side (bench_core::events, board_protocol).
// Keys are camelCase on both sides.

export type LogLevel = "E" | "W" | "I" | "D" | "V";
export type LineClass = "log" | "telemetry" | "jsonReply" | "panic" | "console";

export interface LogLine {
  sequence: number;
  tsMs: number;
  boardMs: number | null;
  level: LogLevel | null;
  tag: string | null;
  message: string;
  raw: string;
  class: LineClass;
}

export interface SerialLinesBatch {
  lines: LogLine[];
  droppedLineCount: number;
}

export type SerialConnectionState = "disconnected" | "connecting" | "connected" | "reconnecting";

export interface SerialStatePayload {
  state: SerialConnectionState;
  portPath: string;
  baudRate: number;
  errorText: string | null;
}

export type SerialPortKind = "uartBridge" | "usbJtag" | "other";

export interface SerialPortInfo {
  path: string;
  kind: SerialPortKind;
  vendorId: number | null;
  productId: number | null;
  productName: string | null;
  serialNumber: string | null;
}

export interface ConsoleReply {
  command: string;
  lines: string[];
  isError: boolean;
  errorText: string | null;
  timedOut: boolean;
}

export interface BoardBootPayload {
  version: string;
  idfVersion: string;
  tsMs: number;
}

export type BoardEventKind =
  | "doorOpened"
  | "doorClosed"
  | "faceRejected"
  | "enrollStarted"
  | "faceEnrolled"
  | "enrollFailed"
  | "approvalRequested"
  | "approvalDenied"
  | "approvalTimeout";

export interface BoardEventPayload {
  kind: BoardEventKind;
  faceId: number;
  name: string;
  similarity: number;
  requestId: number;
  tsMs: number;
}

export interface TelemetrySamplePayload {
  tsMs: number;
  values: Record<string, number>;
}

export interface TelemetryBoxPayload {
  x: number;
  y: number;
  width: number;
  height: number;
  faceId: number;
  similarity: number;
  tsMs: number;
}

export interface BacktraceFrame {
  pc: number;
  sp: number;
}

export interface BoardPanicPayload {
  reason: string;
  core: number | null;
  frames: BacktraceFrame[];
  isCorrupted: boolean;
  elfSha256: string | null;
  rawLines: string[];
  tsMs: number;
}

export interface BoardStatus {
  state: string;
  stateId: number;
  frames: number;
  framesWithFace: number;
  inferenceMs: number;
  lastMatch: { id: number; similarity: number; threshold: number };
  approval: { requestId: number } | null;
  door: "open" | "locked" | string;
  wifi: { enabled: boolean; connected: boolean; ip: string | null };
  faces: { count: number; capacity: number; skipped: number } | null;
  heap: { internal: number; psram: number; internalMin: number };
  version: {
    app: string;
    idf: string;
    compileDate: string;
    compileTime: string;
    elfSha256Short: string;
  };
  uptimeMs: number;
}

export interface FaceEntry {
  id: number;
  name: string;
}

export interface TaskRow {
  name: string;
  state: string;
  priority: number;
  stackFreeWords: number;
  taskNumber: number;
  core: number | null;
}

export interface CameraFramePayload {
  sequence: number;
  byteCount: number;
  receivedFps: number;
}

export type CameraConnectionState = "stopped" | "connecting" | "streaming" | "offline";

export interface CameraStatePayload {
  state: CameraConnectionState;
  boardIp: string;
  errorText: string | null;
}

export type FlashStage = "connecting" | "erasing" | "writing" | "verifying" | "done" | "failed";

export interface FlashProgressPayload {
  stage: FlashStage;
  file: string | null;
  percent: number;
  message: string | null;
}

export interface RepoIdentity {
  repoPath: string;
  gitDescribe: string;
  isDirty: boolean;
  headShortHash: string;
  branch: string;
}

export interface Partition {
  name: string;
  kind: string;
  subtype: string;
  offset: number;
  sizeBytes: number;
}

export interface BuildIdentity {
  buildDirPath: string;
  project: {
    projectName: string;
    projectVersion: string;
    projectPath: string;
    idfPath: string;
    buildDir: string;
    appElf: string;
    appBin: string;
    target: string;
    gitRevision: string;
    monitorBaud: string;
    cCompiler: string;
  };
  flasher: {
    chip: string;
    flashMode: string;
    flashSize: string;
    flashFreq: string;
    flashFiles: { offset: number; path: string }[];
  };
  flashFiles: { offset: number; path: string; byteCount: number; exists: boolean }[];
  elfPath: string;
  elfSha256: string | null;
  elfModifiedAtMs: number | null;
  partitions: Partition[];
}

export interface FrameLocation {
  address: number;
  function: string | null;
  file: string | null;
  line: number | null;
  isInlined: boolean;
}

export interface DecodedBacktrace {
  frames: FrameLocation[];
  fallbackLines: string[];
}

export interface DefaultPaths {
  repoPath: string;
  buildDirPath: string;
  idfPath: string;
  esptoolPythonPath: string;
}
