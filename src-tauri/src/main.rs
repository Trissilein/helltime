#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use std::time::{Duration, Instant};
use tauri::{Manager, State};
use tokio::sync::Mutex;

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

fn main() {
  tauri::Builder::default()
    .manage(AppState {
      cache: Mutex::new(Cache::default()),
      http: reqwest::Client::new(),
    })
    .plugin(tauri_plugin_notification::init())
    .plugin(tauri_plugin_shell::init())
    .on_window_event(|window, event| {
      if let tauri::WindowEvent::CloseRequested { .. } = event {
        if window.label() == "main" {
          if let Some(overlay) = window.app_handle().get_webview_window("overlay") {
            let _ = overlay.close();
          }
          // Ensure the process terminates even if the overlay was the last window alive.
          window.app_handle().exit(0);
        }
      }
    })
    .invoke_handler(tauri::generate_handler![
      fetch_schedule,
    ])
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
}
