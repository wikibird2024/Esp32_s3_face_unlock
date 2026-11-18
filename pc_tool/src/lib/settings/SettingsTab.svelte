<script lang="ts">
  import { settingsStore } from "$lib/stores/settingsStore.svelte";
  import { isTauri } from "$lib/ipc/invoke";

  const settings = settingsStore.values;
  let saved = $state("");

  async function save() {
    await settingsStore.save();
    saved = `saved ${new Date().toLocaleTimeString()}`;
  }

  const fields: { key: keyof typeof settings; label: string; hint: string; type?: "number" | "checkbox" }[] = [
    { key: "repoPath", label: "repo path", hint: "git describe is read here; partitions.csv too" },
    { key: "buildDirPath", label: "build directory", hint: "project_description.json, flasher_args.json, the ELF" },
    { key: "elfPath", label: "ELF path (optional)", hint: "empty = <build dir>/face_unlock_esp32_s3.elf; used to decode crashes" },
    { key: "idfPath", label: "ESP-IDF path", hint: "export.sh is sourced from here for idf.py build" },
    { key: "esptoolPythonPath", label: "python with esptool", hint: "empty = newest ~/.espressif/python_env/*/bin/python" },
    { key: "flashBaudRate", label: "flash baud", hint: "460800 is safe for CP210x; 921600 for faster bridges", type: "number" },
    { key: "editorCommand", label: "editor command", hint: "{file} and {line} placeholders, e.g. code -g {file}:{line}" },
    { key: "telemetryPeriodMs", label: "telemetry period ms", hint: "stats <period> sent on connect (min 100)", type: "number" },
    { key: "telemetryOnConnect", label: "telemetry on connect", hint: "send stats automatically after boot/connect", type: "checkbox" },
    { key: "logFileDirectory", label: "log file directory", hint: "where 'log to file' writes board-<date>.log" },
  ];
</script>

<div class="settings panel">
  <h3>Settings</h3>
  {#if !isTauri}<div class="dim">browser preview: settings are not persisted</div>{/if}
  <div class="grid">
    {#each fields as field (field.key)}
      <label for={`setting-${field.key}`}>{field.label}</label>
      {#if field.type === "checkbox"}
        <input id={`setting-${field.key}`} type="checkbox" bind:checked={settings[field.key] as boolean} />
      {:else if field.type === "number"}
        <input id={`setting-${field.key}`} type="number" bind:value={settings[field.key] as number} class="mono" />
      {:else}
        <input id={`setting-${field.key}`} bind:value={settings[field.key] as string} class="mono" />
      {/if}
      <span class="dim">{field.hint}</span>
    {/each}
  </div>
  <div class="row" style="margin-top: 10px">
    <button class="primary" onclick={save} disabled={!isTauri}>save</button>
    <span class="dim">{saved}</span>
  </div>
  <p class="dim">
    Shortcuts: Alt+1..4 tabs · Ctrl+L focus the console · Tab completes a command · ↑↓ command history.
    Fake board: <code>python3 mobile_app/tool/fake_board.py --port 8080 --pty-link /tmp/face_unlock_tty</code>, then port
    <code>/tmp/face_unlock_tty</code> and board IP <code>127.0.0.1:8080</code>.
  </p>
</div>

<style>
  .grid {
    display: grid;
    grid-template-columns: 170px minmax(260px, 480px) 1fr;
    gap: 6px 10px;
    align-items: center;
  }
  code {
    font-family: var(--mono);
    color: var(--accent);
  }
</style>
