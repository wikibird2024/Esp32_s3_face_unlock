<script lang="ts">
  // uPlot wrapper: one chart per telemetry key, 10 minute window.
  import uPlot from "uplot";
  import "uplot/dist/uPlot.min.css";
  import { telemetryStore } from "$lib/stores/telemetryStore.svelte";

  let { seriesKey, label, colour = "#4fa3ff", unit = "" }: { seriesKey: string; label: string; colour?: string; unit?: string } = $props();
  let container = $state<HTMLDivElement | null>(null);
  let chart: uPlot | null = null;

  function makeChart(element: HTMLDivElement) {
    const options: uPlot.Options = {
      width: element.clientWidth || 400,
      height: 140,
      cursor: { sync: { key: "telemetry" } },
      legend: { show: false },
      scales: { x: { time: true } },
      axes: [
        { stroke: "#8a90a0", grid: { stroke: "#2a2f3d" }, ticks: { stroke: "#2a2f3d" } },
        { stroke: "#8a90a0", grid: { stroke: "#2a2f3d" }, ticks: { stroke: "#2a2f3d" }, size: 56 },
      ],
      series: [{}, { label, stroke: colour, width: 1.5, points: { show: false } }],
    };
    chart = new uPlot(options, [[], []], element);
    const observer = new ResizeObserver(() => {
      if (chart && element.clientWidth > 0) chart.setSize({ width: element.clientWidth, height: 140 });
    });
    observer.observe(element);
    return () => {
      observer.disconnect();
      chart?.destroy();
      chart = null;
    };
  }

  $effect(() => {
    if (!container) return;
    return makeChart(container);
  });

  $effect(() => {
    const series = telemetryStore.get(seriesKey);
    if (chart && series) chart.setData([series.times, series.values]);
  });

  const latest = $derived(telemetryStore.latest[seriesKey]);
  const range = $derived(telemetryStore.minMax[seriesKey]);
</script>

<div class="chart panel">
  <div class="row" style="justify-content: space-between">
    <span>{label}</span>
    <span class="mono">
      {latest !== undefined ? `${Number.isInteger(latest) ? latest : latest.toFixed(1)}${unit}` : "–"}
      {#if range}<span class="dim"> · min {range.min}{unit} max {range.max}{unit}</span>{/if}
    </span>
  </div>
  <div bind:this={container}></div>
</div>

<style>
  .chart {
    padding: 6px 8px;
  }
  :global(.uplot) {
    font-family: var(--mono);
    font-size: 10px;
  }
</style>
