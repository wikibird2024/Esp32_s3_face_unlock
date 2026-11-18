<script lang="ts">
  // Virtualized list: only the rows inside the viewport exist in the DOM.
  import { logStore } from "$lib/stores/logStore.svelte";
  import { formatBoardMs, formatClock } from "$lib/format";
  import type { LogLine } from "$lib/ipc/types";

  const ROW_HEIGHT = 18;
  const OVERSCAN = 10;
  let container = $state<HTMLDivElement | null>(null);
  let scrollTop = $state(0);
  let viewportHeight = $state(300);

  const lines = $derived(logStore.visibleLines);
  const totalHeight = $derived(lines.length * ROW_HEIGHT);
  const firstIndex = $derived(Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - OVERSCAN));
  const lastIndex = $derived(Math.min(lines.length, Math.ceil((scrollTop + viewportHeight) / ROW_HEIGHT) + OVERSCAN));
  const slice = $derived(lines.slice(firstIndex, lastIndex));

  function onScroll() {
    if (!container) return;
    scrollTop = container.scrollTop;
    const atBottom = container.scrollHeight - container.scrollTop - container.clientHeight < ROW_HEIGHT * 2;
    logStore.isScrollLocked = !atBottom;
  }

  $effect(() => {
    // Follow the tail unless the user scrolled up.
    lines.length;
    if (container && !logStore.isScrollLocked) {
      container.scrollTop = container.scrollHeight;
    }
  });

  $effect(() => {
    if (!container) return;
    const observer = new ResizeObserver(() => {
      viewportHeight = container?.clientHeight ?? 300;
    });
    observer.observe(container);
    return () => observer.disconnect();
  });

  function rowClass(line: LogLine): string {
    if (line.class === "panic") return "level-panic";
    if (line.class === "telemetry") return "level-telemetry";
    if (line.class === "console" || line.class === "jsonReply") return "level-console";
    return `level-${line.level ?? "I"}`;
  }
</script>

<div class="log-list mono" bind:this={container} onscroll={onScroll}>
  <div style:height={`${totalHeight}px`} style="position: relative">
    {#each slice as line, offset (line.sequence)}
      <div class={`log-row ${rowClass(line)}`} style:top={`${(firstIndex + offset) * ROW_HEIGHT}px`} title={line.raw}>
        <span class="time">{formatClock(line.tsMs)}</span>
        <span class="board-ms">{formatBoardMs(line.boardMs)}</span>
        <span class="level">{line.level ?? (line.class === "console" ? ">" : line.class === "jsonReply" ? "@" : "")}</span>
        <span class="tag">{line.tag ?? ""}</span>
        <span class="message">{line.message}</span>
      </div>
    {/each}
  </div>
</div>

<style>
  .log-list {
    flex: 1;
    overflow: auto;
    font-size: 12px;
    background: #0d0f15;
  }
  .log-row {
    position: absolute;
    left: 0;
    right: 0;
    height: 18px;
    line-height: 18px;
    white-space: nowrap;
    overflow: hidden;
    padding: 0 8px;
    display: grid;
    grid-template-columns: 92px 70px 14px 110px 1fr;
    gap: 8px;
  }
  .time,
  .board-ms {
    color: var(--text-dim);
  }
  .tag {
    color: #9bb4d6;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .level-E {
    color: var(--red);
  }
  .level-W {
    color: var(--yellow);
  }
  .level-I {
    color: var(--green);
  }
  .level-D,
  .level-V {
    color: var(--text-dim);
  }
  .level-console {
    color: var(--text);
  }
  .level-telemetry {
    color: #7f8fa6;
  }
  .level-panic {
    color: #ff8a80;
    background: #2a1414;
  }
  .message {
    overflow: hidden;
    text-overflow: ellipsis;
  }
</style>
