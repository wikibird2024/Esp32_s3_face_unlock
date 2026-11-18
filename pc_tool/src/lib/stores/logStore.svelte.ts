// Console log buffer + filters. The buffer itself is not reactive (20k rows
// would be slow as a proxy); `version` ticks on every change and the
// visible slice is derived from it.
import type { LineClass, LogLevel, LogLine } from "$lib/ipc/types";

export const MAX_LINES = 20000;
const DROP_CHUNK = 1000;
const levelRank: Record<LogLevel, number> = { E: 0, W: 1, I: 2, D: 3, V: 4 };

export type MinimumLevel = LogLevel | "all";

class LogStore {
  private buffer: LogLine[] = [];
  version = $state(0);
  droppedTotal = $state(0);
  isPaused = $state(false);
  isScrollLocked = $state(false);
  showTelemetry = $state(false);
  filterText = $state("");
  minimumLevel = $state<MinimumLevel>("all");
  selectedTags = $state<Set<string>>(new Set());
  knownTags = $state<string[]>([]);
  commandHistory = $state<string[]>([]);
  knownCommands = $state<string[]>([
    "help", "status", "status json", "list", "list json", "enroll ", "cancel", "delete ", "clear yes",
    "unlock", "approve ", "deny ", "stats 500", "stats 0", "free", "restart", "log ", "tasks",
  ]);

  get count() {
    this.version;
    return this.buffer.length;
  }

  visibleLines = $derived.by(() => {
    this.version;
    const text = this.filterText.trim().toLowerCase();
    const minimum = this.minimumLevel;
    const tags = this.selectedTags;
    const showTelemetry = this.showTelemetry;
    return this.buffer.filter((line) => {
      if (line.class === "telemetry" && !showTelemetry) return false;
      if (minimum !== "all" && line.level && levelRank[line.level] > levelRank[minimum]) return false;
      if (tags.size > 0 && (!line.tag || !tags.has(line.tag))) return false;
      if (text && !line.raw.toLowerCase().includes(text)) return false;
      return true;
    });
  });

  append(lines: LogLine[], droppedLineCount: number) {
    if (this.isPaused) return;
    let newTag = false;
    for (const line of lines) {
      if (line.tag && !this.knownTags.includes(line.tag)) {
        this.knownTags.push(line.tag);
        newTag = true;
      }
    }
    if (newTag) this.knownTags.sort();
    this.buffer.push(...lines);
    if (this.buffer.length > MAX_LINES) {
      this.buffer.splice(0, this.buffer.length - MAX_LINES + DROP_CHUNK);
    }
    this.droppedTotal += droppedLineCount;
    this.version++;
  }

  clear() {
    this.buffer = [];
    this.version++;
  }

  allText(): string {
    return this.buffer.map((line) => line.raw).join("\n");
  }

  visibleText(): string {
    return this.visibleLines.map((line) => line.raw).join("\n");
  }

  rememberCommand(text: string) {
    const trimmed = text.trim();
    if (!trimmed) return;
    this.commandHistory = [...this.commandHistory.filter((item) => item !== trimmed), trimmed].slice(-100);
  }

  /** `help` replies: "<command>  <help text>" blocks; keep the first word. */
  learnCommandsFromHelp(lines: string[]) {
    const learned = new Set(this.knownCommands);
    for (const line of lines) {
      const match = /^([a-z_]+)\s{2,}/.exec(line) ?? /^([a-z_]+)$/.exec(line);
      if (match) learned.add(match[1]);
    }
    this.knownCommands = [...learned].sort();
  }

  toggleTag(tag: string) {
    const next = new Set(this.selectedTags);
    if (next.has(tag)) next.delete(tag);
    else next.add(tag);
    this.selectedTags = next;
  }
}

export const logStore = new LogStore();

export function classLabel(lineClass: LineClass): string {
  return lineClass === "jsonReply" ? "json" : lineClass;
}
