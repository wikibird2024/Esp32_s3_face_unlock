// Everything we know about the board right now.
import type {
  BoardEventPayload,
  BoardPanicPayload,
  BoardStatus,
  CameraConnectionState,
  DecodedBacktrace,
  FaceEntry,
  SerialStatePayload,
  TelemetryBoxPayload,
} from "$lib/ipc/types";

export interface PanicRecord extends BoardPanicPayload {
  decoded: DecodedBacktrace | null;
  decodeError: string | null;
}

class BoardStore {
  serial = $state<SerialStatePayload>({ state: "disconnected", portPath: "", baudRate: 115200, errorText: null });
  boardIp = $state("");
  bootVersion = $state("");
  bootIdfVersion = $state("");
  lastBootTsMs = $state(0);
  status = $state<BoardStatus | null>(null);
  statusTsMs = $state(0);
  statusError = $state("");
  lastEvent = $state<BoardEventPayload | null>(null);
  recentEvents = $state<BoardEventPayload[]>([]);
  waitingRequestId = $state(0);
  faces = $state<FaceEntry[]>([]);
  facesError = $state("");
  lastBox = $state<TelemetryBoxPayload | null>(null);
  panics = $state<PanicRecord[]>([]);
  cameraState = $state<CameraConnectionState>("stopped");
  cameraError = $state("");
  cameraFps = $state(0);
  cameraFrameSequence = $state(0);
  cameraByteCount = $state(0);
  telemetryOn = $state(false);

  get isConnected() {
    return this.serial.state === "connected";
  }

  get flashedVersion(): string {
    return this.status?.version.app || this.bootVersion;
  }

  get flashedElfShaShort(): string {
    return this.status?.version.elfSha256Short ?? "";
  }

  applyEvent(event: BoardEventPayload) {
    this.lastEvent = event;
    this.recentEvents = [event, ...this.recentEvents].slice(0, 50);
    if (event.kind === "approvalRequested") this.waitingRequestId = event.requestId;
    if (event.kind === "doorOpened" || event.kind === "approvalDenied" || event.kind === "approvalTimeout") {
      this.waitingRequestId = 0;
    }
  }

  applyStatus(status: BoardStatus, tsMs: number) {
    this.status = status;
    this.statusTsMs = tsMs;
    this.statusError = "";
    this.waitingRequestId = status.approval?.requestId ?? 0;
    if (status.wifi.ip && !this.boardIp) this.boardIp = status.wifi.ip;
  }

  resetAfterBoot() {
    this.status = null;
    this.waitingRequestId = 0;
    this.lastBox = null;
    this.telemetryOn = false;
  }
}

export const boardStore = new BoardStore();
