// Thin typed wrappers around every Rust command. Names match
// `src-tauri/src/lib.rs`; argument keys are camelCase (Tauri converts).
import { invoke } from "@tauri-apps/api/core";
import type {
  BoardStatus,
  BuildIdentity,
  ConsoleReply,
  DecodedBacktrace,
  DefaultPaths,
  FaceEntry,
  RepoIdentity,
  SerialPortInfo,
  SerialStatePayload,
  TaskRow,
} from "./types";

export const isTauri = typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;

export const serial = {
  listPorts: () => invoke<SerialPortInfo[]>("list_serial_ports"),
  connect: (portPath: string, baudRate: number) => invoke<void>("connect_serial", { portPath, baudRate }),
  disconnect: () => invoke<void>("disconnect_serial"),
  state: () => invoke<SerialStatePayload>("serial_state"),
  sendText: (text: string) => invoke<void>("send_console_text", { text }),
  runCommand: (text: string, timeoutMs = 3000) =>
    invoke<ConsoleReply>("run_console_command", { text, timeoutMs }),
  resetBoard: () => invoke<void>("reset_board"),
  startLogFile: (path: string) => invoke<void>("start_log_file", { path }),
  stopLogFile: () => invoke<void>("stop_log_file"),
};

export const board = {
  readStatus: () => invoke<BoardStatus>("read_board_status"),
  readFaces: () => invoke<FaceEntry[]>("read_face_list"),
  readTasks: () => invoke<TaskRow[]>("read_task_list"),
  startTelemetry: (periodMs: number) => invoke<void>("start_telemetry", { periodMs }),
  stopTelemetry: () => invoke<void>("stop_telemetry"),
};

export const camera = {
  start: (boardIp: string) => invoke<void>("start_camera_stream", { boardIp }),
  stop: () => invoke<void>("stop_camera_stream"),
  readLatestFrame: () => invoke<ArrayBuffer>("read_latest_camera_frame"),
  saveSnapshot: (path: string) => invoke<void>("save_camera_snapshot", { path }),
};

export const build = {
  readRepoIdentity: (repoPath: string) => invoke<RepoIdentity>("read_repo_identity", { repoPath }),
  readBuildIdentity: (buildDirPath: string, repoPath: string | null) =>
    invoke<BuildIdentity>("read_build_identity", { buildDirPath, repoPath }),
  runIdfBuild: (repoPath: string, idfPath: string, extraArgs: string[] = []) =>
    invoke<void>("run_idf_build", { repoPath, idfPath, extraArgs }),
  stopIdfBuild: () => invoke<void>("stop_idf_build"),
  decodeBacktrace: (elfPath: string, addresses: number[]) =>
    invoke<DecodedBacktrace>("decode_backtrace", { elfPath, addresses }),
};

export const flash = {
  flashBuild: (portPath: string, buildDirPath: string, baudRate: number, chip: string, pythonPath: string) =>
    invoke<void>("flash_build", { portPath, buildDirPath, baudRate, chip, pythonPath }),
  eraseFlash: (portPath: string, chip: string, pythonPath: string) =>
    invoke<void>("erase_flash", { portPath, chip, pythonPath }),
};

export const misc = {
  writeTextFile: (path: string, text: string) => invoke<void>("write_text_file", { path, text }),
  openInEditor: (template: string, file: string, line: number) =>
    invoke<void>("open_in_editor", { template, file, line }),
  defaultEditorTemplate: () => invoke<string>("default_editor_template"),
  defaultPaths: () => invoke<DefaultPaths>("default_paths"),
};

export function errorText(error: unknown): string {
  if (typeof error === "string") return error;
  if (error instanceof Error) return error.message;
  return JSON.stringify(error);
}
