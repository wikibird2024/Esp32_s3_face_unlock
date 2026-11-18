<script lang="ts">
  import { logStore, type MinimumLevel } from "$lib/stores/logStore.svelte";
  import { misc, serial, errorText, isTauri } from "$lib/ipc/invoke";
  import { settingsStore } from "$lib/stores/settingsStore.svelte";

  let logFileOn = $state(false);
  let message = $state("");
  let showTags = $state(false);
  const levels: { value: MinimumLevel; label: string }[] = [
    { value: "all", label: "all levels" },
    { value: "E", label: "errors" },
    { value: "W", label: "warn+" },
    { value: "I", label: "info+" },
    { value: "D", label: "debug+" },
  ];

  function fileStamp() {
    const now = new Date();
    const pad = (value: number) => String(value).padStart(2, "0");
    return `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`;
  }

  async function toggleLogFile() {
    message = "";
    try {
      if (logFileOn) {
        await serial.stopLogFile();
        logFileOn = false;
      } else {
        const directory = settingsStore.values.logFileDirectory || settingsStore.values.repoPath || ".";
        const path = `${directory.replace(/\/$/, "")}/board-${fileStamp()}.log`;
        await serial.startLogFile(path);
        logFileOn = true;
        message = `logging to ${path}`;
      }
    } catch (error) {
      message = errorText(error);
    }
  }

  async function saveVisible() {
    message = "";
    try {
      const { save } = await import("@tauri-apps/plugin-dialog");
      const path = await save({ defaultPath: `board-${fileStamp()}.txt`, filters: [{ name: "Text", extensions: ["txt", "log"] }] });
      if (!path) return;
      await misc.writeTextFile(path, logStore.visibleText());
      message = `saved ${path}`;
    } catch (error) {
      message = errorText(error);
    }
  }
</script>

<div class="filter-bar">
  <input bind:value={logStore.filterText} placeholder="filter text" style="width: 180px" />
  <select bind:value={logStore.minimumLevel}>
    {#each levels as level (level.value)}
      <option value={level.value}>{level.label}</option>
    {/each}
  </select>
  <button onclick={() => (showTags = !showTags)} class:active={logStore.selectedTags.size > 0}>
    tags{logStore.selectedTags.size > 0 ? ` (${logStore.selectedTags.size})` : ""}
  </button>
  {#if showTags}
    <div class="tag-list">
      {#each logStore.knownTags as tag (tag)}
        <label><input type="checkbox" checked={logStore.selectedTags.has(tag)} onchange={() => logStore.toggleTag(tag)} /> {tag}</label>
      {/each}
      {#if logStore.knownTags.length === 0}<span class="dim">no tags yet</span>{/if}
      <button onclick={() => (logStore.selectedTags = new Set())}>clear</button>
    </div>
  {/if}
  <label><input type="checkbox" bind:checked={logStore.showTelemetry} /> telemetry rows</label>
  <button onclick={() => (logStore.isPaused = !logStore.isPaused)} class:active={logStore.isPaused}>
    {logStore.isPaused ? "resume" : "pause"}
  </button>
  <button onclick={() => (logStore.isScrollLocked = false)} disabled={!logStore.isScrollLocked}>follow</button>
  <button onclick={() => logStore.clear()}>clear</button>
  <button onclick={toggleLogFile} disabled={!isTauri} class:active={logFileOn}>{logFileOn ? "stop log file" : "log to file"}</button>
  <button onclick={saveVisible} disabled={!isTauri}>save view</button>
  <span class="dim">{logStore.visibleLines.length} / {logStore.count}{logStore.droppedTotal ? ` (dropped ${logStore.droppedTotal})` : ""}</span>
  {#if message}<span class="dim">{message}</span>{/if}
</div>

<style>
  .filter-bar {
    display: flex;
    align-items: center;
    gap: 6px;
    flex-wrap: wrap;
    padding: 4px 8px;
    border-bottom: 1px solid var(--border);
    font-size: 12px;
  }
  .tag-list {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    padding: 2px 6px;
    border: 1px solid var(--border);
    border-radius: 4px;
  }
  button.active {
    border-color: var(--accent);
    color: var(--accent);
  }
</style>
