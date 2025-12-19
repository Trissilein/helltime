import { isTauri } from "@tauri-apps/api/core";

export async function notify(title: string, body: string): Promise<void> {
  if (isTauri()) {
    try {
      const { sendNotification } = await import("@tauri-apps/plugin-notification");
      await sendNotification({ title, body });
      return;
    } catch {
      // fall back to Web Notifications below
    }
  }

  if (!("Notification" in window)) return;
  if (Notification.permission === "default") {
    try {
      await Notification.requestPermission();
    } catch {
      return;
    }
  }
  if (Notification.permission !== "granted") return;
  new Notification(title, { body });
}
