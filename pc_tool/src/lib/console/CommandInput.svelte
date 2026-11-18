<script lang="ts">
  import { errorText, serial } from "$lib/ipc/invoke";
  import { logStore } from "$lib/stores/logStore.svelte";
  import { boardStore } from "$lib/stores/boardStore.svelte";

  let text = $state("");
  let historyIndex = $state(-1);
  let errorMessage = $state("");
  let input = $state<HTMLInputElement | null>(null);

  const suggestions = $derived(
    text.length > 0 ? logStore.knownCommands.filter((command) => command.startsWith(text) && command !== text).slice(0, 8) : [],
  );

  async function send() {
    const command = text.trim();
    if (!command) {
      try {
        await serial.sendText("");
      } catch {
        /* ignore */
      }
      return;
    }
    errorMessage = "";
    logStore.rememberCommand(command);
    historyIndex = -1;
    text = "";
    try {
      if (command === "help") {
        const reply = await serial.runCommand(command, 4000);
        logStore.learnCommandsFromHelp(reply.lines);
      } else {
        await serial.sendText(command);
      }
    } catch (error) {
      errorMessage = errorText(error);
    }
  }

  function onKey(event: KeyboardEvent) {
    const history = logStore.commandHistory;
    if (event.key === "Enter") {
      event.preventDefault();
      void send();
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      if (history.length === 0) return;
      historyIndex = historyIndex < 0 ? history.length - 1 : Math.max(0, historyIndex - 1);
      text = history[historyIndex];
    } else if (event.key === "ArrowDown") {
      event.preventDefault();
      if (historyIndex < 0) return;
      historyIndex = historyIndex + 1 >= history.length ? -1 : historyIndex + 1;
      text = historyIndex < 0 ? "" : history[historyIndex];
    } else if (event.key === "Tab" && suggestions.length > 0) {
      event.preventDefault();
      text = suggestions[0];
    } else if (event.key === "Escape") {
      text = "";
    }
  }

  $effect(() => {
    const focusConsole = (event: KeyboardEvent) => {
      if (event.ctrlKey && event.key === "l") {
        event.preventDefault();
        input?.focus();
      }
    };
    window.addEventListener("keydown", focusConsole);
    return () => window.removeEventListener("keydown", focusConsole);
  });
</script>

<div class="command-input">
  <span class="prompt mono">face&gt;</span>
  <input
    class="mono"
    bind:this={input}
    bind:value={text}
    onkeydown={onKey}
    placeholder={boardStore.isConnected ? "command (Tab completes, ↑↓ history, Ctrl+L focus)" : "connect first"}
    disabled={!boardStore.isConnected}
    autocomplete="off"
    spellcheck="false"
  />
  <button onclick={send} disabled={!boardStore.isConnected}>send</button>
  {#if suggestions.length > 0}
    <div class="suggestions mono">
      {#each suggestions as suggestion (suggestion)}
        <button onclick={() => (text = suggestion)}>{suggestion}</button>
      {/each}
    </div>
  {/if}
  {#if errorMessage}<span class="error-text">{errorMessage}</span>{/if}
</div>

<style>
  .command-input {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 4px 8px;
    border-top: 1px solid var(--border);
    position: relative;
  }
  .prompt {
    color: var(--green);
  }
  input {
    flex: 1;
    background: #0d0f15;
  }
  .suggestions {
    position: absolute;
    bottom: 100%;
    left: 60px;
    display: flex;
    gap: 4px;
    padding: 4px;
    background: var(--panel-raised);
    border: 1px solid var(--border);
    border-radius: 4px;
  }
  .suggestions button {
    padding: 1px 6px;
    font-size: 12px;
  }
</style>
