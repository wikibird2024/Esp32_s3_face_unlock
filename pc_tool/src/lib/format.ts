export function formatBytes(bytes: number | null | undefined): string {
  if (bytes == null) return "–";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

export function formatClock(tsMs: number): string {
  const date = new Date(tsMs);
  const pad = (value: number, width = 2) => String(value).padStart(width, "0");
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}.${pad(date.getMilliseconds(), 3)}`;
}

export function formatBoardMs(boardMs: number | null): string {
  if (boardMs == null) return "";
  return `${(boardMs / 1000).toFixed(3)}s`;
}

export function formatHex(value: number, width = 8): string {
  return `0x${value.toString(16).padStart(width, "0")}`;
}

export function formatAgo(tsMs: number, nowMs: number): string {
  const seconds = Math.max(0, Math.round((nowMs - tsMs) / 1000));
  if (seconds < 60) return `${seconds}s ago`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m ago`;
  return `${Math.floor(minutes / 60)}h ago`;
}

export function formatUptime(uptimeMs: number): string {
  const total = Math.floor(uptimeMs / 1000);
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const seconds = total % 60;
  return hours > 0 ? `${hours}h ${minutes}m ${seconds}s` : `${minutes}m ${seconds}s`;
}
