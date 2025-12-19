#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use std::time::{Duration, Instant};
use tauri::State;
use tokio::sync::Mutex;

mod overlay;

const SCHEDULE_URL: &str = "https://helltides.com/api/schedule";
const CACHE_TTL: Duration = Duration::from_secs(30);

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub struct ScheduleResponse {
  #[serde(default)]
  pub world_boss: Vec<serde_json::Value>,
  #[serde(default)]
  pub legion: Vec<serde_json::Value>,
  #[serde(default)]
  pub helltide: Vec<serde_json::Value>,
}

#[derive(Default)]
struct Cache {
  last_fetch: Option<Instant>,
  value: Option<ScheduleResponse>,
}

struct AppState {
  cache: Mutex<Cache>,
  http: reqwest::Client,
  overlay: overlay::OverlayManager,
}

#[tauri::command]
async fn fetch_schedule(state: State<'_, AppState>) -> Result<ScheduleResponse, String> {
  {
    let cache = state.inner().cache.lock().await;
    if let (Some(at), Some(value)) = (cache.last_fetch, cache.value.clone()) {
      if at.elapsed() < CACHE_TTL {
        return Ok(value);
      }
    }
  }

  let resp = state
    .inner()
    .http
    .get(SCHEDULE_URL)
    .header(
      reqwest::header::USER_AGENT,
      "helltime/0.1 (+https://github.com/)",
    )
    .timeout(Duration::from_secs(10))
    .send()
    .await
    .map_err(|e| format!("request failed: {e}"))?;

  if !resp.status().is_success() {
    return Err(format!("bad status: {}", resp.status()));
  }

  let json = resp
    .json::<ScheduleResponse>()
    .await
    .map_err(|e| format!("invalid json: {e}"))?;

  let mut cache = state.inner().cache.lock().await;
  cache.last_fetch = Some(Instant::now());
  cache.value = Some(json.clone());

  Ok(json)
}

#[tauri::command]
fn overlay_status(state: State<'_, AppState>) -> overlay::OverlayStatus {
  state.inner().overlay.status()
}

#[tauri::command]
fn overlay_show(
  state: State<'_, AppState>,
  payload: overlay::OverlayPayload,
  position: Option<overlay::OverlayPosition>,
) -> Result<(), String> {
  state.inner().overlay.show(payload, position)
}

#[tauri::command]
fn overlay_hide(state: State<'_, AppState>) -> Result<(), String> {
  state.inner().overlay.hide()
}

#[tauri::command]
fn overlay_enter_config(state: State<'_, AppState>, position: Option<overlay::OverlayPosition>) -> Result<(), String> {
  state.inner().overlay.enter_config(position)
}

#[tauri::command]
fn overlay_exit_config(state: State<'_, AppState>) -> Result<(), String> {
  state.inner().overlay.exit_config()
}

#[tauri::command]
fn overlay_get_position(state: State<'_, AppState>) -> Option<overlay::OverlayPosition> {
  state.inner().overlay.get_position()
}

#[tauri::command]
fn overlay_set_position(state: State<'_, AppState>, position: overlay::OverlayPosition) -> Result<(), String> {
  state.inner().overlay.set_position(position)
}

fn main() {
  tauri::Builder::default()
    .manage(AppState {
      cache: Mutex::new(Cache::default()),
      http: reqwest::Client::new(),
      overlay: overlay::OverlayManager::new(),
    })
    .plugin(tauri_plugin_notification::init())
    .plugin(tauri_plugin_shell::init())
    .invoke_handler(tauri::generate_handler![
      fetch_schedule,
      overlay_status,
      overlay_show,
      overlay_hide,
      overlay_enter_config,
      overlay_exit_config,
      overlay_get_position,
      overlay_set_position
    ])
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
}
