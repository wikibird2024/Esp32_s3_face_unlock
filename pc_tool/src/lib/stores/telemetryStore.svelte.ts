// Time series from `>k:v` lines, capped to a window.
const WINDOW_MS = 10 * 60 * 1000;

export interface Series {
  times: number[];
  values: number[];
}

class TelemetryStore {
  private series = new Map<string, Series>();
  version = $state(0);
  latest = $state<Record<string, number>>({});
  latestTsMs = $state(0);
  isPaused = $state(false);
  minMax = $state<Record<string, { min: number; max: number }>>({});

  add(tsMs: number, values: Record<string, number>) {
    this.latest = values;
    this.latestTsMs = tsMs;
    for (const [key, value] of Object.entries(values)) {
      const range = this.minMax[key];
      this.minMax[key] = range
        ? { min: Math.min(range.min, value), max: Math.max(range.max, value) }
        : { min: value, max: value };
    }
    if (this.isPaused) return;
    const cutoff = tsMs - WINDOW_MS;
    for (const [key, value] of Object.entries(values)) {
      let one = this.series.get(key);
      if (!one) {
        one = { times: [], values: [] };
        this.series.set(key, one);
      }
      one.times.push(tsMs / 1000);
      one.values.push(value);
      let drop = 0;
      while (drop < one.times.length && one.times[drop] * 1000 < cutoff) drop++;
      if (drop > 0) {
        one.times.splice(0, drop);
        one.values.splice(0, drop);
      }
    }
    this.version++;
  }

  get(key: string): Series | undefined {
    this.version;
    return this.series.get(key);
  }

  keys(): string[] {
    this.version;
    return [...this.series.keys()];
  }

  clear() {
    this.series.clear();
    this.minMax = {};
    this.version++;
  }
}

export const telemetryStore = new TelemetryStore();
