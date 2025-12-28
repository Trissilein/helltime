#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};
use tauri::{Emitter, Listener, Manager, State};
use tauri::menu::CheckMenuItem;
use tokio::sync::Mutex;
use std::sync::Mutex as StdMutex;

const SCHEDULE_URL: &str = "https://helltides.com/api/schedule";
const CACHE_TTL: Duration = Duration::from_secs(30);

// ============================================================================
// WINDOW STATE MANAGER - Provides deterministic, serialized window operations
// ============================================================================

/// Tracks the logical state of the main window for deterministic behavior
#[derive(Debug, Clone, Copy, PartialEq)]
enum WindowVisibility {
    Visible,    // Window is shown and in taskbar
    Hidden,     // Window is hidden, only in tray (user closed/minimized)
}

/// Thread-safe window state manager with debouncing
struct WindowStateManager {
    /// Current logical visibility state
    visibility: StdMutex<WindowVisibility>,
    /// Last tray action timestamp for debouncing
    last_tray_action: StdMutex<Instant>,
    /// Lock to serialize window operations
    operation_lock: StdMutex<()>,
    /// Flag to prevent recursive event handling
    in_transition: AtomicBool,
}

impl WindowStateManager {
    fn new() -> Self {
        Self {
            visibility: StdMutex::new(WindowVisibility::Visible),
            last_tray_action: StdMutex::new(Instant::now() - Duration::from_secs(10)),
            operation_lock: StdMutex::new(()),
            in_transition: AtomicBool::new(false),
        }
    }

    /// Check if we should process a tray action (debounce rapid clicks)
    fn should_process_tray_action(&self) -> bool {
        let mut last = self.last_tray_action.lock().unwrap();
        let now = Instant::now();
        // Use 50ms debounce - filters double-events but allows normal clicks
        if now.duration_since(*last) < Duration::from_millis(50) {
            // Silent debounce for very rapid events (Tauri quirk)
            return false;
        }
        *last = now;
        true
    }

    /// Get current visibility state
    fn get_visibility(&self) -> WindowVisibility {
        *self.visibility.lock().unwrap()
    }

    /// Set visibility state
    fn set_visibility(&self, state: WindowVisibility) {
        let mut vis = self.visibility.lock().unwrap();
        eprintln!("📍 Window state: {:?} → {:?}", *vis, state);
        *vis = state;
    }

    /// Acquire operation lock (serialize window operations)
    fn acquire_lock(&self) -> std::sync::MutexGuard<'_, ()> {
        self.operation_lock.lock().unwrap()
    }

    /// Check/set transition flag to prevent recursive handling
    fn begin_transition(&self) -> bool {
        !self.in_transition.swap(true, Ordering::SeqCst)
    }

    fn end_transition(&self) {
        self.in_transition.store(false, Ordering::SeqCst);
    }
}

// Global window state manager
static WINDOW_STATE: std::sync::OnceLock<WindowStateManager> = std::sync::OnceLock::new();

fn get_window_state() -> &'static WindowStateManager {
    WINDOW_STATE.get_or_init(WindowStateManager::new)
}

/// Restore window to visible state (show + taskbar + focus)
fn restore_window(window: &tauri::WebviewWindow) {
    let state = get_window_state();

    // Skip if already visible (optimization)
    if state.get_visibility() == WindowVisibility::Visible {
        // Still focus the window even if already visible
        let _ = window.set_focus();
        return;
    }

    let _lock = state.acquire_lock();

    if !state.begin_transition() {
        eprintln!("⚠ Restore skipped - already in transition");
        return;
    }

    eprintln!("🔼 Restoring window...");

    // Order matters: show first, then configure
    let _ = window.show();
    let _ = window.unminimize();
    let _ = window.set_skip_taskbar(false);
    let _ = window.set_focus();

    state.set_visibility(WindowVisibility::Visible);
    state.end_transition();

    eprintln!("✅ Window restored");
}

/// Hide window to tray using Window type (from on_window_event)
fn hide_window_to_tray_v2(window: &tauri::Window, app_handle: &tauri::AppHandle) {
    let state = get_window_state();
    let _lock = state.acquire_lock();

    if !state.begin_transition() {
        eprintln!("⚠ Hide skipped - already in transition");
        return;
    }

    eprintln!("🔽 Hiding window to tray...");

    // Hide main window
    let _ = window.hide();
    let _ = window.set_skip_taskbar(true);

    // Also hide overlay
    if let Some(overlay) = app_handle.get_webview_window("overlay") {
        let _ = overlay.hide();
    }

    state.set_visibility(WindowVisibility::Hidden);
    state.end_transition();

    eprintln!("✅ Window hidden to tray");
}

/// Hide window to tray using WebviewWindow type (from tray click)
fn hide_window_to_tray(window: &tauri::WebviewWindow, app_handle: &tauri::AppHandle) {
    let state = get_window_state();

    // Skip if already hidden
    if state.get_visibility() == WindowVisibility::Hidden {
        return;
    }

    let _lock = state.acquire_lock();

    if !state.begin_transition() {
        eprintln!("⚠ Hide skipped - already in transition");
        return;
    }

    eprintln!("🔽 Hiding window to tray...");

    // Hide main window
    let _ = window.hide();
    let _ = window.set_skip_taskbar(true);

    // Also hide overlay
    if let Some(overlay) = app_handle.get_webview_window("overlay") {
        let _ = overlay.hide();
    }

    state.set_visibility(WindowVisibility::Hidden);
    state.end_transition();

    eprintln!("✅ Window hidden to tray");
}

/// Toggle window visibility (for tray click)
fn toggle_window(window: &tauri::WebviewWindow, app_handle: &tauri::AppHandle) {
    let state = get_window_state();
    let current = state.get_visibility();

    eprintln!("🔄 Toggle requested, current state: {:?}", current);

    match current {
        WindowVisibility::Visible => hide_window_to_tray(window, app_handle),
        WindowVisibility::Hidden => restore_window(window),
    }
}

// ============================================================================

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

fn try_load_tray_icon(icon_path: &std::path::Path) -> Option<tauri::image::Image<'static>> {
  use tauri::image::Image;

  match std::fs::read(icon_path) {
    Ok(png_data) => {
      match image::load_from_memory_with_format(&png_data, image::ImageFormat::Png) {
        Ok(img) => {
          let rgba_img = img.to_rgba8();
          let (width, height) = rgba_img.dimensions();
          eprintln!("✓ Loaded icon: {:?} ({}x{})", icon_path, width, height);
          return Some(Image::new_owned(rgba_img.into_raw(), width, height));
        }
        Err(e) => {
          eprintln!("✗ Failed to decode PNG at {:?}: {}", icon_path, e);
        }
      }
    }
    Err(e) => {
      eprintln!("✗ Icon file not found at {:?}: {}", icon_path, e);
    }
  }
  None
}

fn create_fallback_icon() -> tauri::image::Image<'static> {
  use tauri::image::Image;
  eprintln!("⚠ Using fallback hourglass icon");

  let mut pixels = vec![0u8; 64 * 64 * 4]; // 64x64 RGBA

  // Fill with reddish-orange (helltime brand color)
  for i in (0..pixels.len()).step_by(4) {
    pixels[i] = 200;     // R
    pixels[i + 1] = 80;  // G
    pixels[i + 2] = 20;  // B
    pixels[i + 3] = 255; // A (fully opaque)
  }

  Image::new_owned(pixels, 64, 64)
}

fn main() {
  tauri::Builder::default()
    .manage(AppState {
      cache: Mutex::new(Cache::default()),
      http: reqwest::Client::new(),
    })
    .plugin(tauri_plugin_notification::init())
    .plugin(tauri_plugin_shell::init())
    .setup(|app| {
      // Debug: print current working directory
      if let Ok(cwd) = std::env::current_dir() {
        eprintln!("Current working directory: {:?}", cwd);
      }

      // Try to load the actual PNG icon from various paths
      let icon = {
        let paths = [
          std::path::PathBuf::from("icons/icon.png"),
          std::path::PathBuf::from("src-tauri/icons/icon.png"),
          std::path::PathBuf::from("../icons/icon.png"),
          std::path::PathBuf::from("../../icons/icon.png"),
          std::path::PathBuf::from("./icons/icon.png"),
        ];

        eprintln!("Attempting to load tray icon...");
        let mut loaded_icon = None;
        for path in &paths {
          if let Some(icon) = try_load_tray_icon(path) {
            loaded_icon = Some(icon);
            break;
          }
        }

        loaded_icon.unwrap_or_else(create_fallback_icon)
      };

      let _tray_icon = tauri::tray::TrayIconBuilder::new()
        .icon(icon)
        .tooltip("Helltime")
        .on_tray_icon_event(|tray, event| {
          use tauri::tray::{TrayIconEvent, MouseButton};
          match event {
            // Left click: toggle window visibility
            TrayIconEvent::Click { button: MouseButton::Left, .. } => {
              let state = get_window_state();

              // Debounce rapid clicks
              if !state.should_process_tray_action() {
                return;
              }

              if let Some(window) = tray.app_handle().get_webview_window("main") {
                toggle_window(&window, tray.app_handle());
              }
            }
            _ => {}
          }
        })
        .on_menu_event(|app, event| {
          match event.id.as_ref() {
            "restore" => {
              if let Some(window) = app.get_webview_window("main") {
                restore_window(&window);
              }
            }
            "toggle-overlay" => {
              let _ = app.emit("menu:toggle-overlay", ());
            }
            "toggle-reminder" => {
              let _ = app.emit("menu:toggle-reminder", ());
            }
            "quit" => {
              app.exit(0);
            }
            _ => {}
          }
        })
        .menu({
          let overlay_item = CheckMenuItem::with_id(app, "toggle-overlay", "Overlay", true, true, None::<&str>)?;
          let reminder_item = CheckMenuItem::with_id(app, "toggle-reminder", "Reminder", true, true, None::<&str>)?;

          let overlay_item_clone = overlay_item.clone();
          let reminder_item_clone = reminder_item.clone();

          app.listen("menu:update-overlay-state", move |event| {
            let payload_str = event.payload();
            let checked = payload_str == "true";
            let _ = overlay_item_clone.set_checked(checked);
          });

          app.listen("menu:update-reminder-state", move |event| {
            let payload_str = event.payload();
            let checked = payload_str == "true";
            let _ = reminder_item_clone.set_checked(checked);
          });

          &tauri::menu::Menu::with_items(
            app,
            &[
              &tauri::menu::MenuItem::with_id(app, "restore", "Restore", true, None::<&str>)?,
              &tauri::menu::PredefinedMenuItem::separator(app)?,
              &overlay_item,
              &reminder_item,
              &tauri::menu::PredefinedMenuItem::separator(app)?,
              &tauri::menu::MenuItem::with_id(app, "quit", "Exit", true, None::<&str>)?,
            ],
          )?
        })
        .show_menu_on_left_click(false)
        .build(app);

      Ok(())
    })
    .on_window_event(|window, event| {
      // Only handle main window events
      if window.label() != "main" {
        return;
      }

      match event {
        // Close button → hide to tray (don't actually close)
        tauri::WindowEvent::CloseRequested { api, .. } => {
          api.prevent_close();
          hide_window_to_tray_v2(window, window.app_handle());
        }

        // Window focused → ensure visible state is correct
        tauri::WindowEvent::Focused(focused) => {
          if *focused {
            let state = get_window_state();
            // If we're focused, we should be visible
            if state.get_visibility() == WindowVisibility::Hidden {
              eprintln!("📍 Focus received while hidden - updating state");
              state.set_visibility(WindowVisibility::Visible);
              let _ = window.set_skip_taskbar(false);
            }
          }
        }

        // Resized with zero size often indicates minimize on Windows
        tauri::WindowEvent::Resized(size) => {
          // When minimized, Windows reports size as 0,0 or very small
          if size.width == 0 && size.height == 0 {
            eprintln!("📥 Window minimized (size 0x0 detected)");
            hide_window_to_tray_v2(window, window.app_handle());
          }
        }

        _ => {}
      };
    })
    .invoke_handler(tauri::generate_handler![
      fetch_schedule,
    ])
    .build(tauri::generate_context!())
    .expect("error while building tauri application")
    .run(|_app_handle, _event| {
      // Tauri handles exit gracefully by default
    });
}
