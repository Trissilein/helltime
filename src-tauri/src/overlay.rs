use serde::{Deserialize, Serialize};
use std::sync::{Arc, Mutex};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OverlayPosition {
  pub x: i32,
  pub y: i32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OverlayPayload {
  pub title: String,
  pub body: String,
  pub kind: Option<String>,
  #[serde(rename = "type")]
  pub event_type: Option<String>,
  pub bg_rgb: Option<u32>,
}

#[derive(Debug, Clone, Serialize)]
pub struct OverlayStatus {
  pub supported: bool,
  pub running: bool,
  pub visible: bool,
  pub config_mode: bool,
  pub last_error: Option<String>,
  pub position: Option<OverlayPosition>,
}

#[cfg(windows)]
mod win32;

#[cfg(windows)]
pub use win32::OverlayManager;

#[cfg(not(windows))]
pub struct OverlayManager {
  _noop: (),
}

#[cfg(not(windows))]
impl OverlayManager {
  pub fn new() -> Self {
    Self { _noop: () }
  }

  pub fn status(&self) -> OverlayStatus {
    OverlayStatus {
      supported: false,
      running: false,
      visible: false,
      config_mode: false,
      last_error: None,
      position: None,
    }
  }

  pub fn show(&self, _payload: OverlayPayload, _position: Option<OverlayPosition>) -> Result<(), String> {
    Ok(())
  }

  pub fn hide(&self) -> Result<(), String> {
    Ok(())
  }

  pub fn enter_config(&self, _position: Option<OverlayPosition>) -> Result<(), String> {
    Ok(())
  }

  pub fn exit_config(&self) -> Result<(), String> {
    Ok(())
  }

  pub fn get_position(&self) -> Option<OverlayPosition> {
    None
  }

  pub fn set_position(&self, _pos: OverlayPosition) -> Result<(), String> {
    Ok(())
  }
}

#[derive(Clone, Default)]
pub(crate) struct Shared {
  pub toast: Arc<Mutex<Option<OverlayPayload>>>,
  pub visible: Arc<Mutex<bool>>,
  pub config_mode: Arc<Mutex<bool>>,
  pub position: Arc<Mutex<Option<OverlayPosition>>>,
  pub last_error: Arc<Mutex<Option<String>>>,
}
