import { invoke, isTauri } from "@tauri-apps/api/core";

export type OverlayPosition = { x: number; y: number };

export type OverlayPayload = {
  title: string;
  body: string;
  kind?: string;
  type?: "helltide" | "legion" | "world_boss";
  bg_rgb?: number;
  scale?: number;
  bg_a?: number;
};

export type OverlayStatus = {
  supported: boolean;
  running: boolean;
  visible: boolean;
  config_mode: boolean;
  last_error: string | null;
  position: OverlayPosition | null;
};

export async function overlayStatus(): Promise<OverlayStatus | null> {
  if (!isTauri()) return null;
  try {
    return (await invoke("overlay_status")) as OverlayStatus;
  } catch {
    return null;
  }
}

export async function overlayShow(payload: OverlayPayload, position?: OverlayPosition | null): Promise<void> {
  if (!isTauri()) return;
  try {
    await invoke("overlay_show", { payload, position: position ?? null });
  } catch (e) {
    console.warn("overlay_show failed", e);
  }
}

export async function overlayHide(): Promise<void> {
  if (!isTauri()) return;
  try {
    await invoke("overlay_hide");
  } catch (e) {
    console.warn("overlay_hide failed", e);
  }
}

export async function overlayEnterConfig(position?: OverlayPosition | null): Promise<void> {
  if (!isTauri()) return;
  try {
    await invoke("overlay_enter_config", { position: position ?? null });
  } catch (e) {
    console.warn("overlay_enter_config failed", e);
  }
}

export async function overlayExitConfig(): Promise<void> {
  if (!isTauri()) return;
  try {
    await invoke("overlay_exit_config");
  } catch (e) {
    console.warn("overlay_exit_config failed", e);
  }
}

export async function overlayGetPosition(): Promise<OverlayPosition | null> {
  if (!isTauri()) return null;
  try {
    return (await invoke("overlay_get_position")) as OverlayPosition | null;
  } catch (e) {
    console.warn("overlay_get_position failed", e);
    return null;
  }
}

export async function overlaySetPosition(position: OverlayPosition): Promise<void> {
  if (!isTauri()) return;
  try {
    await invoke("overlay_set_position", { position });
  } catch (e) {
    console.warn("overlay_set_position failed", e);
  }
}
