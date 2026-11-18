<script lang="ts">
  import FilterBar from "./FilterBar.svelte";
  import LogList from "./LogList.svelte";
  import CommandInput from "./CommandInput.svelte";
  import PanicCard from "./PanicCard.svelte";
  import { boardStore } from "$lib/stores/boardStore.svelte";

  let showPanics = $state(true);
</script>

<div class="dock-layout">
  <FilterBar />
  <div class="body">
    <LogList />
    {#if showPanics && boardStore.panics.length > 0}
      <aside class="panics">
        <div class="row" style="justify-content: space-between">
          <strong>Crashes ({boardStore.panics.length})</strong>
          <button onclick={() => (showPanics = false)}>hide</button>
        </div>
        {#each boardStore.panics as panic, index (panic.tsMs + "-" + index)}
          <PanicCard {panic} />
        {/each}
      </aside>
    {:else if boardStore.panics.length > 0}
      <button class="show-panics" onclick={() => (showPanics = true)}>crashes ({boardStore.panics.length})</button>
    {/if}
  </div>
  <CommandInput />
</div>

<style>
  .dock-layout {
    display: flex;
    flex-direction: column;
    height: 100%;
    background: var(--panel);
  }
  .body {
    flex: 1;
    display: flex;
    min-height: 0;
  }
  .panics {
    width: 380px;
    overflow: auto;
    border-left: 1px solid var(--border);
    padding: 6px;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .show-panics {
    align-self: flex-start;
    margin: 6px;
    color: var(--red);
  }
</style>
