<script lang="ts">
  import { onMount } from "svelte";
  import TelemetryChart from "./TelemetryChart.svelte";
  import { board, build, errorText, isTauri } from "$lib/ipc/invoke";
  import type { Partition, TaskRow } from "$lib/ipc/types";
  import { boardStore } from "$lib/stores/boardStore.svelte";
  import { settingsStore } from "$lib/stores/settingsStore.svelte";
  import { telemetryStore } from "$lib/stores/telemetryStore.svelte";
  import { formatBytes, formatHex, formatUptime } from "$lib/format";

  const charts = [
    { key: "fps", label: "frames / s", colour: "#4cc38a", unit: "" },
    { key: "infer_ms", label: "inference", colour: "#4fa3ff", unit: " ms" },
    { key: "heap_free", label: "internal heap free", colour: "#e5b84a", unit: " B" },
    { key: "psram_free", label: "PSRAM free", colour: "#c792ea", unit: " B" },
    { key: "rssi", label: "Wi-Fi RSSI", colour: "#ff8a80", unit: " dBm" },
  ];
  let tasks = $state<TaskRow[]>([]);
  let tasksError = $state("");
  let partitions = $state<Partition[]>([]);
  let autoRefresh = $state(true);
  let message = $state("");

  async function refreshTasks() {
    if (!boardStore.isConnected) return;
    try {
      tasks = await board.readTasks();
      tasksError = tasks.length === 0 ? "no task rows (firmware without `tasks`?)" : "";
    } catch (error) {
      tasksError = errorText(error);
    }
  }

  async function loadPartitions() {
    if (!isTauri || !settingsStore.values.buildDirPath) return;
    try {
      const identity = await build.readBuildIdentity(settingsStore.values.buildDirPath, settingsStore.values.repoPath || null);
      partitions = identity.partitions;
    } catch {
      partitions = [];
    }
  }

  async function toggleTelemetry() {
    message = "";
    try {
      if (boardStore.telemetryOn) {
        await board.stopTelemetry();
        boardStore.telemetryOn = false;
      } else {
        await board.startTelemetry(settingsStore.values.telemetryPeriodMs);
        boardStore.telemetryOn = true;
      }
    } catch (error) {
      message = errorText(error);
    }
  }

  onMount(() => {
    void refreshTasks();
    void loadPartitions();
    const timer = setInterval(() => {
      if (autoRefresh) void refreshTasks();
    }, 5000);
    return () => clearInterval(timer);
  });

  const latest = $derived(telemetryStore.latest);
  const stateNames = ["idle", "confirming", "unlocked", "cooldown", "enrolling", "waiting approval"];
  const taskState: Record<string, string> = { X: "running", B: "blocked", R: "ready", S: "suspended", D: "deleted" };
</script>

<div class="inspector">
  <div class="row">
    <button onclick={toggleTelemetry} disabled={!boardStore.isConnected} class:primary={!boardStore.telemetryOn}>
      {boardStore.telemetryOn ? "telemetry off" : `telemetry on (${settingsStore.values.telemetryPeriodMs} ms)`}
    </button>
    <button onclick={() => (telemetryStore.isPaused = !telemetryStore.isPaused)}>{telemetryStore.isPaused ? "resume charts" : "pause charts"}</button>
    <button onclick={() => telemetryStore.clear()}>clear</button>
    {#if latest.uptime_s !== undefined}<span class="dim">uptime {formatUptime(latest.uptime_s * 1000)}</span>{/if}
    {#if latest.state !== undefined}<span class="badge grey">{stateNames[latest.state] ?? latest.state}</span>{/if}
    {#if message}<span class="error-text">{message}</span>{/if}
  </div>

  <div class="gauges">
    {#each charts as chart (chart.key)}
      <div class="gauge panel">
        <div class="dim">{chart.label}</div>
        <div class="value mono" style:color={chart.colour}>
          {latest[chart.key] !== undefined ? (chart.key.includes("heap") || chart.key.includes("psram") ? formatBytes(latest[chart.key]) : `${latest[chart.key]}${chart.unit}`) : "–"}
        </div>
      </div>
    {/each}
    {#if boardStore.status}
      <div class="gauge panel">
        <div class="dim">min internal heap</div>
        <div class="value mono">{formatBytes(boardStore.status.heap.internalMin)}</div>
      </div>
    {/if}
  </div>

  <div class="charts">
    {#each charts as chart (chart.key)}
      <TelemetryChart seriesKey={chart.key} label={chart.label} colour={chart.colour} unit={chart.unit} />
    {/each}
  </div>

  <div class="tables">
    <div class="panel">
      <div class="row" style="justify-content: space-between">
        <h3 style="margin: 0">Tasks</h3>
        <span>
          <label><input type="checkbox" bind:checked={autoRefresh} /> auto 5 s</label>
          <button onclick={refreshTasks} disabled={!boardStore.isConnected}>refresh</button>
        </span>
      </div>
      {#if tasksError}<div class="dim">{tasksError}</div>{/if}
      <table>
        <thead><tr><th>task</th><th>state</th><th>prio</th><th>stack free</th><th>core</th></tr></thead>
        <tbody>
          {#each tasks as task (task.name + task.taskNumber)}
            <tr>
              <td class="mono">{task.name}</td>
              <td>{taskState[task.state] ?? task.state}</td>
              <td>{task.priority}</td>
              <td class:low={task.stackFreeWords < 128}>{task.stackFreeWords} words</td>
              <td>{task.core === null || task.core < 0 ? "any" : task.core}</td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
    <div class="panel">
      <h3>Partitions (partitions.csv)</h3>
      <table>
        <thead><tr><th>name</th><th>type</th><th>offset</th><th>size</th></tr></thead>
        <tbody>
          {#each partitions as partition (partition.name)}
            <tr>
              <td class="mono">{partition.name}</td>
              <td>{partition.kind}/{partition.subtype}</td>
              <td class="mono">{formatHex(partition.offset, 6)}</td>
              <td>{formatBytes(partition.sizeBytes)}</td>
            </tr>
          {:else}
            <tr><td colspan="4" class="dim">set the build directory in Settings</td></tr>
          {/each}
        </tbody>
      </table>
    </div>
  </div>
</div>

<style>
  .inspector {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }
  .gauges {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 8px;
  }
  .gauge .value {
    font-size: 20px;
    margin-top: 2px;
  }
  .charts {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(380px, 1fr));
    gap: 8px;
  }
  .tables {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
  }
  td.low {
    color: var(--red);
  }
</style>
