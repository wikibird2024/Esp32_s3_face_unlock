<script lang="ts">
  import CameraView from "./CameraView.svelte";
  import FacePanel from "./FacePanel.svelte";
</script>

<div class="camera-tab">
  <CameraView />
  <FacePanel />
</div>

<style>
  .camera-tab {
    display: grid;
    grid-template-columns: minmax(320px, 1fr) 360px;
    gap: 10px;
    height: 100%;
  }
</style>
