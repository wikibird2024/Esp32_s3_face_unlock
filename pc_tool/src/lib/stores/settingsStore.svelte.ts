// Persistent settings (tauri-plugin-store, file `settings.json` in the app
// config dir). Falls back to memory when not running inside Tauri.
import { isTauri, misc } from "$lib/ipc/invoke";

export interface Settings {
  serialPortPath: string;
  baudRate: number;
  boardIp: string;
  repoPath: string;
  buildDirPath: string;
  elfPath: string;
  idfPath: string;
  esptoolPythonPath: string;
  flashBaudRate: number;
  editorCommand: string;
  telemetryPeriodMs: number;
  telemetryOnConnect: boolean;
  logFileDirectory: string;
}

const defaults: Settings = {
  serialPortPath: "",
  baudRate: 115200,
  boardIp: "",
  repoPath: "",
  buildDirPath: "",
  elfPath: "",
  idfPath: "",
  esptoolPythonPath: "",
  flashBaudRate: 460800,
  editorCommand: "",
  telemetryPeriodMs: 500,
  telemetryOnConnect: true,
  logFileDirectory: "",
};

type StoreHandle = { get<T>(key: string): Promise<T | null | undefined>; set(key: string, value: unknown): Promise<void>; save(): Promise<void> };

class SettingsStore {
  values = $state<Settings>({ ...defaults });
  loaded = $state(false);
  private store: StoreHandle | null = null;

  async load() {
    if (isTauri) {
      try {
        const { load } = await import("@tauri-apps/plugin-store");
        this.store = (await load("settings.json")) as unknown as StoreHandle;
        const saved = await this.store.get<Partial<Settings>>("settings");
        if (saved) this.values = { ...defaults, ...saved };
      } catch (error) {
        console.warn("settings store unavailable", error);
      }
      if (!this.values.repoPath || !this.values.buildDirPath || !this.values.idfPath) {
        try {
          const paths = await misc.defaultPaths();
          this.values.repoPath ||= paths.repoPath;
          this.values.buildDirPath ||= paths.buildDirPath;
          this.values.idfPath ||= paths.idfPath;
          this.values.esptoolPythonPath ||= paths.esptoolPythonPath;
        } catch {
          /* not in tauri */
        }
      }
      if (!this.values.editorCommand) {
        try {
          this.values.editorCommand = await misc.defaultEditorTemplate();
        } catch {
          /* ignore */
        }
      }
    }
    this.loaded = true;
  }

  async save() {
    if (!this.store) return;
    try {
      await this.store.set("settings", $state.snapshot(this.values));
      await this.store.save();
    } catch (error) {
      console.warn("settings save failed", error);
    }
  }

  /// ELF path: explicit setting, else `<buildDir>/face_unlock_esp32_s3.elf`.
  elfPathOrDefault(appElf = "face_unlock_esp32_s3.elf"): string {
    if (this.values.elfPath) return this.values.elfPath;
    if (this.values.buildDirPath) return `${this.values.buildDirPath.replace(/\/$/, "")}/${appElf}`;
    return "";
  }
}

export const settingsStore = new SettingsStore();
