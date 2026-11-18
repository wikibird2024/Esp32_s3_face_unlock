<script lang="ts">
  // The newest JPEG comes from Rust (one HTTP connection to the board);
  // the <img> swaps blob URLs. Box overlay from `>box:` telemetry.
  import { onMount } from "svelte";
  import { camera, errorText, isTauri } from "$lib/ipc/invoke";
  import { frameListeners } from "$lib/ipc/events";
  import { boardStore } from "$lib/stores/boardStore.svelte";

  const FRAME_WIDTH = 320;
  const FRAME_HEIGHT = 240;
  const BOX_MAX_AGE_MS = 500;

  let imageUrl = $state("");
  let message = $state("");
  let nowMs = $state(Date.now());
  let fetching = false;
  let pendingSequence = 0;

  async function pullFrame(sequence: number) {
    pendingSequence = sequence;
    if (fetching) return;
    fetching = true;
    try {
      while (pendingSequence > 0) {
        pendingSequence = 0;
        const bytes = await camera.readLatestFrame();
        if (bytes.byteLength === 0) continue;
        const next = URL.createObjectURL(new Blob([bytes], { type: "image/jpeg" }));
        const previous = imageUrl;
        imageUrl = next;
        if (previous) URL.revokeObjectURL(previous);
      }
    } catch (error) {
      message = errorText(error);
    } finally {
      fetching = false;
    }
  }

  onMount(() => {
    const listener = (sequence: number) => void pullFrame(sequence);
    frameListeners.add(listener);
    if (boardStore.cameraFrameSequence > 0) void pullFrame(boardStore.cameraFrameSequence);
    const clock = setInterval(() => (nowMs = Date.now()), 100);
    return () => {
      frameListeners.delete(listener);
      clearInterval(clock);
      if (imageUrl) URL.revokeObjectURL(imageUrl);
    };
  });

  async function saveSnapshot() {
    message = "";
    try {
      const { save } = await import("@tauri-apps/plugin-dialog");
      const path = await save({ defaultPath: `snapshot-${Date.now()}.jpg`, filters: [{ name: "JPEG", extensions: ["jpg"] }] });
      if (!path) return;
      await camera.saveSnapshot(path);
      message = `saved ${path}`;
    } catch (error) {
      message = errorText(error);
    }
  }

  const box = $derived(boardStore.lastBox && nowMs - boardStore.lastBox.tsMs < BOX_MAX_AGE_MS ? boardStore.lastBox : null);
  const threshold = $derived(boardStore.status?.lastMatch.threshold ?? 0.55);
</script>

<div class="camera-view panel">
  <div class="row" style="justify-content: space-between">
    <h3 style="margin: 0">Live view</h3>
    <span class="dim">
      {boardStore.cameraState}
      {#if boardStore.cameraState === "streaming"}· {boardStore.cameraFps.toFixed(1)} fps · {(boardStore.cameraByteCount / 1024).toFixed(1)} KB/frame{/if}
      {#if boardStore.cameraError}· {boardStore.cameraError}{/if}
    </span>
    <button onclick={saveSnapshot} disabled={!isTauri || !imageUrl}>snapshot</button>
  </div>
  <div class="frame">
    {#if imageUrl}
      <img src={imageUrl} alt="board camera" />
      <svg viewBox={`0 0 ${FRAME_WIDTH} ${FRAME_HEIGHT}`} preserveAspectRatio="none">
        {#if box}
          <rect x={box.x} y={box.y} width={box.width} height={box.height} class={box.similarity >= threshold ? "known" : "unknown"} />
          <text x={box.x} y={Math.max(10, box.y - 4)}>{box.faceId > 0 ? `#${box.faceId}` : "?"} {box.similarity.toFixed(2)}</text>
        {/if}
      </svg>
    {:else}
      <div class="placeholder dim">
        {#if boardStore.cameraState === "stopped"}
          Press “Stream on” in the top bar. The board serves one viewer at a time.
        {:else}
          waiting for the first frame…
        {/if}
      </div>
    {/if}
  </div>
  {#if message}<div class="dim">{message}</div>{/if}
</div>

<style>
  .camera-view {
    display: flex;
    flex-direction: column;
    gap: 8px;
    min-height: 0;
  }
  .frame {
    position: relative;
    flex: 1;
    min-height: 240px;
    background: #000;
    border-radius: 4px;
    overflow: hidden;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  img {
    width: 100%;
    height: 100%;
    object-fit: contain;
    image-rendering: auto;
  }
  svg {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    pointer-events: none;
  }
  rect {
    fill: none;
    stroke-width: 2;
  }
  rect.known {
    stroke: var(--green);
  }
  rect.unknown {
    stroke: var(--yellow);
  }
  text {
    fill: #fff;
    font-size: 11px;
    font-family: var(--mono);
    paint-order: stroke;
    stroke: #000;
    stroke-width: 2px;
  }
  .placeholder {
    padding: 20px;
    text-align: center;
  }
</style>
