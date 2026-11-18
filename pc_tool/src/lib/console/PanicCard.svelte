<script lang="ts">
  import type { PanicRecord } from "$lib/stores/boardStore.svelte";
  import { settingsStore } from "$lib/stores/settingsStore.svelte";
  import { errorText, misc } from "$lib/ipc/invoke";
  import { formatClock, formatHex } from "$lib/format";

  let { panic }: { panic: PanicRecord } = $props();
  let showRaw = $state(false);
  let message = $state("");

  async function open(file: string, line: number) {
    try {
      await misc.openInEditor(settingsStore.values.editorCommand, file, line);
    } catch (error) {
      message = errorText(error);
    }
  }

  function shortFile(file: string): string {
    const repo = settingsStore.values.repoPath.replace(/\/$/, "");
    return repo && file.startsWith(repo) ? file.slice(repo.length + 1) : file;
  }
</script>

<div class="panic-card">
  <div class="reason">{panic.reason}</div>
  <div class="dim">
    {formatClock(panic.tsMs)} · core {panic.core ?? "?"} · {panic.frames.length} frames{panic.isCorrupted ? " · corrupted" : ""}
    {#if panic.elfSha256}· ELF {panic.elfSha256.slice(0, 8)}{/if}
  </div>
  {#if panic.decoded}
    <ol class="frames mono">
      {#each panic.decoded.frames as frame, index (index)}
        <li class:inlined={frame.isInlined}>
          <span class="dim">{formatHex(frame.address)}</span>
          {frame.function ?? "?"}
          {#if frame.file && frame.line}
            <button class="link" onclick={() => open(frame.file!, frame.line!)}>{shortFile(frame.file)}:{frame.line}</button>
          {/if}
        </li>
      {/each}
      {#each panic.decoded.fallbackLines as line, index (index)}
        <li>{line}</li>
      {/each}
    </ol>
  {:else if panic.decodeError}
    <div class="error-text">decode: {panic.decodeError}</div>
  {:else if panic.frames.length > 0}
    <div class="dim">decoding against {settingsStore.elfPathOrDefault() || "(no ELF path set)"}…</div>
  {/if}
  <button onclick={() => (showRaw = !showRaw)}>{showRaw ? "hide raw" : "raw"}</button>
  {#if showRaw}
    <pre class="mono">{panic.rawLines.join("\n")}</pre>
  {/if}
  {#if message}<div class="error-text">{message}</div>{/if}
</div>

<style>
  .panic-card {
    border: 1px solid var(--red);
    border-radius: 6px;
    padding: 6px 8px;
    background: #1d1416;
    font-size: 12px;
  }
  .reason {
    color: #ff8a80;
    font-weight: 600;
    white-space: pre-wrap;
  }
  .frames {
    margin: 6px 0;
    padding-left: 18px;
  }
  .frames li.inlined {
    color: var(--text-dim);
  }
  .link {
    border: none;
    background: none;
    color: var(--accent);
    padding: 0;
    text-decoration: underline;
  }
  pre {
    max-height: 200px;
    overflow: auto;
    font-size: 11px;
    white-space: pre-wrap;
  }
</style>
