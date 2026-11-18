<script lang="ts">
  import "./app.css";
  import { onMount } from "svelte";
  import TopBar from "./TopBar.svelte";
  import ConsoleDock from "./console/ConsoleDock.svelte";
  import CameraTab from "./camera/CameraTab.svelte";
  import BuildTab from "./build/BuildTab.svelte";
  import InspectorTab from "./inspector/InspectorTab.svelte";
  import SettingsTab from "./settings/SettingsTab.svelte";
  import { isTauri } from "./ipc/invoke";
  import { subscribeToBackend } from "./ipc/events";
  import { settingsStore } from "./stores/settingsStore.svelte";

  type Tab = "camera" | "build" | "inspector" | "settings";
  const tabs: { id: Tab; label: string; key: string }[] = [
    { id: "camera", label: "Camera", key: "1" },
    { id: "build", label: "Build", key: "2" },
    { id: "inspector", label: "Inspector", key: "3" },
    { id: "settings", label: "Settings", key: "4" },
  ];
  let activeTab = $state<Tab>("camera");
  let dockHeightPercent = $state(42);
  let dragging = false;

  onMount(() => {
    void (async () => {
      await settingsStore.load();
      if (isTauri) await subscribeToBackend();
    })();
    const handleKey = (event: KeyboardEvent) => {
      if (event.altKey && !event.ctrlKey) {
        const tab = tabs.find((item) => item.key === event.key);
        if (tab) {
          activeTab = tab.id;
          event.preventDefault();
        }
      }
    };
    window.addEventListener("keydown", handleKey);
    return () => window.removeEventListener("keydown", handleKey);
  });

  function startDrag(event: PointerEvent) {
    dragging = true;
    (event.target as HTMLElement).setPointerCapture(event.pointerId);
  }

  function onDrag(event: PointerEvent) {
    if (!dragging) return;
    const total = window.innerHeight;
    const fromBottom = total - event.clientY;
    dockHeightPercent = Math.min(80, Math.max(15, (fromBottom / total) * 100));
  }

  function endDrag() {
    dragging = false;
  }
</script>

<div class="shell">
  <TopBar />
  <nav class="tabs">
    {#each tabs as tab (tab.id)}
      <button class:active={activeTab === tab.id} onclick={() => (activeTab = tab.id)} title={`Alt+${tab.key}`}>
        {tab.label}
      </button>
    {/each}
    {#if !isTauri}
      <span class="dim">browser preview: no backend</span>
    {/if}
  </nav>
  <main class="content" style:height={`calc(100% - 78px - ${dockHeightPercent}%)`}>
    {#if activeTab === "camera"}
      <CameraTab />
    {:else if activeTab === "build"}
      <BuildTab />
    {:else if activeTab === "inspector"}
      <InspectorTab />
    {:else}
      <SettingsTab />
    {/if}
  </main>
  <div class="divider" onpointerdown={startDrag} onpointermove={onDrag} onpointerup={endDrag} role="separator" aria-orientation="horizontal"></div>
  <section class="dock" style:height={`${dockHeightPercent}%`}>
    <ConsoleDock />
  </section>
</div>

<style>
  .shell {
    display: flex;
    flex-direction: column;
    height: 100vh;
  }
  .tabs {
    display: flex;
    gap: 2px;
    align-items: center;
    padding: 4px 8px 0;
    border-bottom: 1px solid var(--border);
    background: var(--panel);
  }
  .tabs button {
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    background: transparent;
    padding: 5px 12px;
    color: var(--text-dim);
  }
  .tabs button.active {
    color: var(--text);
    border-bottom-color: var(--accent);
  }
  .tabs .dim {
    margin-left: auto;
    font-size: 11px;
  }
  .content {
    overflow: auto;
    padding: 10px;
  }
  .divider {
    height: 6px;
    cursor: row-resize;
    background: var(--border);
  }
  .divider:hover {
    background: var(--accent);
  }
  .dock {
    min-height: 120px;
    display: flex;
    flex-direction: column;
  }
</style>
