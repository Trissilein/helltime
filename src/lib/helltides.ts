import { invoke } from "@tauri-apps/api/core";
import type { ScheduleResponse } from "./types";

export async function fetchSchedule(): Promise<ScheduleResponse> {
  return await invoke<ScheduleResponse>("fetch_schedule");
}

