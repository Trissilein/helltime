import { isTauri } from "@tauri-apps/api/core";

export async function openExternalUrl(url: string): Promise<void> {
  try {
    if (isTauri()) {
      const { open } = await import("@tauri-apps/plugin-shell");
      await open(url);
      return;
    }
  } catch (e) {
    // eslint-disable-next-line no-console
    console.warn("openExternalUrl: tauri open failed", e);
  }

  try {
    window.open(url, "_blank", "noreferrer");
  } catch (e) {
    // eslint-disable-next-line no-console
    console.warn("openExternalUrl: window.open failed", e);
  }
}
