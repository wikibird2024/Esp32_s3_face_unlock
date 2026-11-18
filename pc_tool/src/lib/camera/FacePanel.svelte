<script lang="ts">
  import { errorText, serial } from "$lib/ipc/invoke";
  import { refreshFaces, refreshStatus } from "$lib/ipc/events";
  import { boardStore } from "$lib/stores/boardStore.svelte";
  import { formatAgo } from "$lib/format";
  import { onMount } from "svelte";

  const NAME_RULE = /^[A-Za-z0-9_-]{1,31}$/;
  let enrollName = $state("");
  let message = $state("");
  let clearText = $state("");
  let confirmDeleteId = $state(0);
  let nowMs = $state(Date.now());

  onMount(() => {
    const clock = setInterval(() => (nowMs = Date.now()), 1000);
    return () => clearInterval(clock);
  });

  async function run(command: string, after?: () => Promise<void>) {
    message = "";
    try {
      const reply = await serial.runCommand(command);
      if (reply.isError) {
        message = reply.errorText ?? "failed";
      } else if (reply.lines.length > 0) {
        message = reply.lines.join(" · ");
      }
      if (after) await after();
    } catch (error) {
      message = errorText(error);
    }
  }

  const status = $derived(boardStore.status);
  const similarity = $derived(boardStore.lastEvent?.similarity ?? status?.lastMatch.similarity ?? 0);
  const threshold = $derived(status?.lastMatch.threshold ?? 0.55);
  const eventLabel: Record<string, string> = {
    doorOpened: "OPEN",
    doorClosed: "locked",
    faceRejected: "REJECT",
    enrollStarted: "enrolling",
    faceEnrolled: "enrolled",
    enrollFailed: "enroll failed",
    approvalRequested: "WAIT approval",
    approvalDenied: "denied",
    approvalTimeout: "approval timeout",
    doorFault: "DOOR FAULT",
  };
  const eventColour: Record<string, string> = {
    doorOpened: "green",
    faceRejected: "red",
    approvalRequested: "yellow",
    enrollFailed: "red",
    doorFault: "red",
    faceEnrolled: "green",
  };
</script>

<div class="face-panel">
  <div class="panel">
    <h3>Board</h3>
    <div class="row">
      <span class={`badge ${status?.state === "unlocked" ? "green" : status?.state === "waiting approval" ? "yellow" : "grey"}`}>
        {status?.state ?? "?"}
      </span>
      <span class={`badge ${status?.door === "open" ? "green" : "grey"}`}>door {status?.door ?? "?"}</span>
      {#if status?.faces}
        <span class="badge grey">{status.faces.count}/{status.faces.capacity} faces</span>
      {/if}
      <button onclick={() => void refreshStatus()} disabled={!boardStore.isConnected}>refresh</button>
    </div>
    {#if status}
      <div class="dim" style="margin-top: 4px">
        frames {status.frames} (with face {status.framesWithFace}) · inference {status.inferenceMs} ms · {boardStore.statusTsMs ? formatAgo(boardStore.statusTsMs, nowMs) : ""}
      </div>
    {:else if boardStore.statusError}
      <div class="error-text">{boardStore.statusError}</div>
    {/if}
  </div>

  <div class="panel">
    <h3>Last event</h3>
    {#if boardStore.lastEvent}
      {@const event = boardStore.lastEvent}
      <div class="row">
        <span class={`badge ${eventColour[event.kind] ?? "grey"}`}>{eventLabel[event.kind] ?? event.kind}</span>
        <strong>{event.name || (event.faceId ? `#${event.faceId}` : "unknown")}</strong>
        <span class="dim">{formatAgo(event.tsMs, nowMs)}</span>
      </div>
    {:else}
      <div class="dim">nothing yet</div>
    {/if}
    <div class="similarity">
      <div class="bar">
        <div class="fill" style:width={`${Math.min(100, similarity * 100)}%`} class:known={similarity >= threshold}></div>
        <div class="threshold" style:left={`${threshold * 100}%`}></div>
      </div>
      <span class="mono">{similarity.toFixed(3)} / need {threshold.toFixed(2)}</span>
    </div>
    {#if boardStore.waitingRequestId}
      <div class="row" style="margin-top: 6px">
        <span>approval #{boardStore.waitingRequestId}</span>
        <button class="primary" onclick={() => run(`approve ${boardStore.waitingRequestId}`, refreshStatus)}>Open</button>
        <button class="danger" onclick={() => run(`deny ${boardStore.waitingRequestId}`, refreshStatus)}>Deny</button>
      </div>
    {/if}
    <div class="row" style="margin-top: 6px">
      <button onclick={() => run("unlock", refreshStatus)} disabled={!boardStore.isConnected}>unlock (manual)</button>
    </div>
  </div>

  <div class="panel">
    <h3>Enroll</h3>
    <div class="row">
      <input bind:value={enrollName} placeholder="name: letters, digits, _ -" maxlength="31" style="width: 170px" />
      <button
        class="primary"
        disabled={!boardStore.isConnected || !NAME_RULE.test(enrollName)}
        onclick={() => run(`enroll ${enrollName}`, async () => { enrollName = ""; })}
      >
        enroll
      </button>
      <button onclick={() => run("cancel", refreshStatus)} disabled={!boardStore.isConnected}>cancel</button>
    </div>
    {#if enrollName && !NAME_RULE.test(enrollName)}
      <div class="dim">1–31 chars from A–Z a–z 0–9 _ -</div>
    {/if}
  </div>

  <div class="panel faces">
    <div class="row" style="justify-content: space-between">
      <h3 style="margin: 0">Faces</h3>
      <button onclick={() => void refreshFaces()} disabled={!boardStore.isConnected}>refresh</button>
    </div>
    {#if boardStore.facesError}
      <div class="error-text">{boardStore.facesError}</div>
    {/if}
    <table>
      <thead><tr><th>id</th><th>name</th><th></th></tr></thead>
      <tbody>
        {#each boardStore.faces as face (face.id)}
          <tr>
            <td class="mono">{face.id}</td>
            <td>{face.name}</td>
            <td style="text-align: right">
              {#if confirmDeleteId === face.id}
                <button class="danger" onclick={() => run(`delete ${face.id}`, async () => { confirmDeleteId = 0; await refreshFaces(); })}>confirm</button>
                <button onclick={() => (confirmDeleteId = 0)}>no</button>
              {:else}
                <button onclick={() => (confirmDeleteId = face.id)}>delete</button>
              {/if}
            </td>
          </tr>
        {:else}
          <tr><td colspan="3" class="dim">no faces enrolled</td></tr>
        {/each}
      </tbody>
    </table>
    <div class="row" style="margin-top: 6px">
      <input bind:value={clearText} placeholder="type yes to clear all" style="width: 150px" />
      <button class="danger" disabled={clearText !== "yes"} onclick={() => run("clear yes", async () => { clearText = ""; await refreshFaces(); })}>clear all</button>
    </div>
  </div>
  {#if message}<div class="dim">{message}</div>{/if}
</div>

<style>
  .face-panel {
    display: flex;
    flex-direction: column;
    gap: 8px;
    overflow: auto;
  }
  .similarity {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-top: 8px;
  }
  .bar {
    position: relative;
    flex: 1;
    height: 10px;
    background: var(--panel-raised);
    border-radius: 5px;
    overflow: hidden;
  }
  .fill {
    height: 100%;
    background: var(--yellow);
  }
  .fill.known {
    background: var(--green);
  }
  .threshold {
    position: absolute;
    top: 0;
    width: 2px;
    height: 100%;
    background: var(--text);
  }
  .faces table {
    max-height: 200px;
  }
</style>
