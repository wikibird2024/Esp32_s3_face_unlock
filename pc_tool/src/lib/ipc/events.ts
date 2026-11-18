// Subscribes once to every backend event and updates the stores.
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { boardStore } from "$lib/stores/boardStore.svelte";
import { logStore } from "$lib/stores/logStore.svelte";
import { telemetryStore } from "$lib/stores/telemetryStore.svelte";
import { settingsStore } from "$lib/stores/settingsStore.svelte";
import { board, build, errorText } from "./invoke";
import type {
  BoardBootPayload,
  BoardEventPayload,
  BoardPanicPayload,
  BoardStatus,
  CameraFramePayload,
  CameraStatePayload,
  SerialLinesBatch,
  SerialStatePayload,
  TelemetryBoxPayload,
  TelemetrySamplePayload,
} from "./types";

export const frameListeners = new Set<(sequence: number) => void>();
export const flashListeners = new Set<(payload: import("./types").FlashProgressPayload) => void>();
export const buildOutputListeners = new Set<(line: string | null, exitCode?: number) => void>();

let unlisteners: UnlistenFn[] = [];

export async function subscribeToBackend() {
  if (unlisteners.length > 0) return;
  unlisteners = await Promise.all([
    listen<SerialLinesBatch>("serial:lines", (event) =>
      logStore.append(event.payload.lines, event.payload.droppedLineCount),
    ),
    listen<SerialStatePayload>("serial:state", (event) => {
      boardStore.serial = event.payload;
    }),
    listen<BoardBootPayload>("board:boot", (event) => {
      boardStore.bootVersion = event.payload.version;
      boardStore.bootIdfVersion = event.payload.idfVersion;
      boardStore.lastBootTsMs = event.payload.tsMs;
      boardStore.resetAfterBoot();
    }),
    listen<{ tsMs: number }>("board:ready", () => {
      void refreshAfterReady();
    }),
    listen<{ ip: string }>("board:ip", (event) => {
      boardStore.boardIp = event.payload.ip;
      if (!settingsStore.values.boardIp) settingsStore.values.boardIp = event.payload.ip;
    }),
    listen<BoardEventPayload>("board:event", (event) => {
      boardStore.applyEvent(event.payload);
      if (event.payload.kind === "faceEnrolled") void refreshFaces();
      void refreshStatus();
    }),
    listen<BoardStatus & { tsMs: number }>("board:status", (event) => {
      boardStore.applyStatus(event.payload, event.payload.tsMs);
    }),
    listen<TelemetrySamplePayload>("telemetry:sample", (event) => {
      telemetryStore.add(event.payload.tsMs, event.payload.values);
      boardStore.telemetryOn = true;
    }),
    listen<TelemetryBoxPayload>("telemetry:box", (event) => {
      boardStore.lastBox = event.payload;
    }),
    listen<BoardPanicPayload>("board:panic", (event) => {
      const record = { ...event.payload, decoded: null, decodeError: null };
      boardStore.panics = [record, ...boardStore.panics].slice(0, 10);
      void decodePanic(record);
    }),
    listen<CameraFramePayload>("camera:frame", (event) => {
      boardStore.cameraFrameSequence = event.payload.sequence;
      boardStore.cameraFps = event.payload.receivedFps;
      boardStore.cameraByteCount = event.payload.byteCount;
      for (const listener of frameListeners) listener(event.payload.sequence);
    }),
    listen<CameraStatePayload>("camera:state", (event) => {
      boardStore.cameraState = event.payload.state;
      boardStore.cameraError = event.payload.errorText ?? "";
    }),
    listen<import("./types").FlashProgressPayload>("flash:progress", (event) => {
      for (const listener of flashListeners) listener(event.payload);
    }),
    listen<{ line: string }>("build:output", (event) => {
      for (const listener of buildOutputListeners) listener(event.payload.line);
    }),
    listen<{ exitCode: number }>("build:done", (event) => {
      for (const listener of buildOutputListeners) listener(null, event.payload.exitCode);
    }),
  ]);
}

export async function refreshStatus() {
  if (!boardStore.isConnected) return;
  try {
    const status = await board.readStatus();
    boardStore.applyStatus(status, Date.now());
  } catch (error) {
    boardStore.statusError = errorText(error);
  }
}

export async function refreshFaces() {
  if (!boardStore.isConnected) return;
  try {
    boardStore.faces = await board.readFaces();
    boardStore.facesError = "";
  } catch (error) {
    boardStore.facesError = errorText(error);
  }
}

/** After a boot or a fresh connection: status, faces, telemetry, help. */
export async function refreshAfterReady() {
  await refreshStatus();
  await refreshFaces();
  if (settingsStore.values.telemetryOnConnect) {
    try {
      await board.startTelemetry(settingsStore.values.telemetryPeriodMs);
      boardStore.telemetryOn = true;
    } catch {
      /* firmware without stats */
    }
  }
}

async function decodePanic(record: import("$lib/stores/boardStore.svelte").PanicRecord) {
  const elfPath = settingsStore.elfPathOrDefault();
  if (!elfPath || record.frames.length === 0) return;
  try {
    const decoded = await build.decodeBacktrace(
      elfPath,
      record.frames.map((frame) => frame.pc),
    );
    boardStore.panics = boardStore.panics.map((item) => (item === record ? { ...item, decoded } : item));
  } catch (error) {
    boardStore.panics = boardStore.panics.map((item) =>
      item === record ? { ...item, decodeError: errorText(error) } : item,
    );
  }
}
