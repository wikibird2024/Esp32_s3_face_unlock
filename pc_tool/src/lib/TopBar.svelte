<script lang="ts">
  import { onMount } from "svelte";
  import { camera, errorText, isTauri, serial } from "./ipc/invoke";
  import { refreshAfterReady } from "./ipc/events";
  import type { SerialPortInfo } from "./ipc/types";
  import { boardStore } from "./stores/boardStore.svelte";
  import { settingsStore } from "./stores/settingsStore.svelte";

  let ports = $state<SerialPortInfo[]>([]);
  let errorMessage = $state("");
  let busy = $state(false);
  const settings = settingsStore.values;
  const baudRates = [115200, 230400, 460800, 921600];

  async function refreshPorts() {
    if (!isTauri) return;
    try {
      ports = await serial.listPorts();
      if (!settings.serialPortPath && ports.length > 0) settings.serialPortPath = ports[0].path;
    } catch (error) {
      errorMessage = errorText(error);
    }
  }

  onMount(() => {
    void refreshPorts();
    const timer = setInterval(() => void refreshPorts(), 5000);
    return () => clearInterval(timer);
  });

  async function toggleConnect() {
    errorMessage = "";
    busy = true;
    try {
      if (boardStore.isConnected || boardStore.serial.state === "reconnecting") {
        await serial.disconnect();
      } else {
        await serial.connect(settings.serialPortPath, settings.baudRate);
        await settingsStore.save();
        // The board may already be up: ask for status + faces now.
        setTimeout(() => void refreshAfterReady(), 300);
      }
    } catch (error) {
      errorMessage = errorText(error);
    } finally {
      busy = false;
    }
  }

  async function resetBoard() {
    try {
      await serial.resetBoard();
    } catch (error) {
      errorMessage = errorText(error);
    }
  }

  async function toggleCamera() {
    errorMessage = "";
    try {
      if (boardStore.cameraState === "stopped") {
        const ip = settings.boardIp || boardStore.boardIp;
        if (!ip) throw new Error("no board address yet (Wi-Fi IP from the log, or type one)");
        await camera.start(ip);
        await settingsStore.save();
      } else {
        await camera.stop();
      }
    } catch (error) {
      errorMessage = errorText(error);
    }
  }

  const stateColour = $derived(
    boardStore.serial.state === "connected" ? "green" : boardStore.serial.state === "reconnecting" ? "yellow" : "grey",
  );
  const kindLabel = (port: SerialPortInfo) =>
    port.kind === "uartBridge" ? "UART" : port.kind === "usbJtag" ? "USB-JTAG (log only)" : "";
</script>

<header class="topbar">
  <div class="row">
    <input
      class="mono"
      list="port-list"
      bind:value={settings.serialPortPath}
      placeholder="/dev/ttyUSB0 or /tmp/face_unlock_tty"
      style="width: 230px"
    />
    <datalist id="port-list">
      {#each ports as port (port.path)}
        <option value={port.path}>{kindLabel(port)} {port.productName ?? ""}</option>
      {/each}
    </datalist>
    <select bind:value={settings.baudRate}>
      {#each baudRates as rate (rate)}
        <option value={rate}>{rate}</option>
      {/each}
    </select>
    <button class:primary={!boardStore.isConnected} onclick={toggleConnect} disabled={busy || !isTauri}>
      {boardStore.isConnected || boardStore.serial.state === "reconnecting" ? "Disconnect" : "Connect"}
    </button>
    <span class={`badge ${stateColour}`}>{boardStore.serial.state}</span>
    <button onclick={resetBoard} disabled={!boardStore.isConnected} title="DTR/RTS pulse (ignored on a pty)">Reset</button>
  </div>
  <div class="row">
    <input class="mono" bind:value={settings.boardIp} placeholder={boardStore.boardIp || "board IP"} style="width: 160px" />
    <button onclick={toggleCamera} disabled={!isTauri}>
      {boardStore.cameraState === "stopped" ? "Stream on" : "Stream off"}
    </button>
    <span class={`badge ${boardStore.cameraState === "streaming" ? "green" : boardStore.cameraState === "stopped" ? "grey" : "yellow"}`}>
      {boardStore.cameraState}{boardStore.cameraState === "streaming" ? ` ${boardStore.cameraFps.toFixed(1)} fps` : ""}
    </span>
    <span class="dim">firmware</span>
    <span class="badge mono" title="version.app from status json, or the boot banner">{boardStore.flashedVersion || "?"}</span>
    {#if errorMessage}
      <span class="error-text">{errorMessage}</span>
    {:else if boardStore.serial.errorText}
      <span class="error-text">{boardStore.serial.errorText}</span>
    {/if}
  </div>
</header>

<style>
  .topbar {
    display: flex;
    justify-content: space-between;
    gap: 16px;
    padding: 6px 10px;
    background: var(--panel);
    border-bottom: 1px solid var(--border);
    height: 44px;
    align-items: center;
  }
</style>
