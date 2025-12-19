use super::{OverlayPayload, OverlayPosition, OverlayStatus, Shared};
use std::ffi::c_void;
use std::ptr::null_mut;
use std::sync::atomic::{AtomicBool, AtomicIsize, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;
use windows::core::{w, PCWSTR};
use windows::Win32::Foundation::{COLORREF, HWND, LPARAM, LRESULT, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::{
  BeginPaint, CreateFontW, CreateSolidBrush, DeleteObject, DrawTextW, EndPaint, FillRect, GetDeviceCaps, InvalidateRect,
  SelectObject, SetBkMode, SetTextColor, DT_END_ELLIPSIS, DT_LEFT, DT_NOPREFIX, DT_TOP, HDC, HGDIOBJ, HFONT, PAINTSTRUCT,
  TRANSPARENT,
};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::System::Threading::GetCurrentThreadId;
use windows::Win32::UI::HiDpi::{SetProcessDpiAwarenessContext, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2};
use windows::Win32::UI::WindowsAndMessaging::{
  CreateWindowExW, DefWindowProcW, DispatchMessageW, GetMessageW, GetWindowLongPtrW, GetWindowRect, KillTimer, LoadCursorW,
  PostMessageW, PostQuitMessage, RegisterClassW, SendMessageW, SetLayeredWindowAttributes, SetTimer, SetWindowLongPtrW,
  SetWindowPos, ShowWindow, TranslateMessage, CS_HREDRAW, CS_VREDRAW, CW_USEDEFAULT, GWL_EXSTYLE, GWLP_USERDATA, HMENU,
  HTCAPTION, IDC_ARROW, LWA_ALPHA, MSG, SW_HIDE, SW_SHOWNOACTIVATE, WM_APP, WM_CLOSE, WM_DESTROY, WM_ERASEBKGND,
  WM_LBUTTONDOWN, WM_MOVE, WM_NCCREATE, WM_PAINT, WM_TIMER, WNDCLASSW, WS_CLIPSIBLINGS, WS_EX_LAYERED, WS_EX_NOACTIVATE,
  WS_EX_TOOLWINDOW, WS_EX_TOPMOST, WS_EX_TRANSPARENT, WS_POPUP, HWND_TOPMOST, SWP_NOACTIVATE, SWP_NOMOVE, SWP_NOSIZE,
};

const WM_OVERLAY_SHOW: u32 = WM_APP + 41;
const WM_OVERLAY_HIDE: u32 = WM_APP + 42;
const WM_OVERLAY_ENTER_CONFIG: u32 = WM_APP + 43;
const WM_OVERLAY_EXIT_CONFIG: u32 = WM_APP + 44;
const WM_OVERLAY_SET_POS: u32 = WM_APP + 45;
const TIMER_HIDE: usize = 1;

#[derive(Clone)]
pub struct OverlayManager {
  shared: Shared,
  hwnd_raw: Arc<AtomicIsize>,
  started: Arc<AtomicBool>,
  init_lock: Arc<Mutex<()>>,
}

impl OverlayManager {
  pub fn new() -> Self {
    unsafe {
      let _ = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    Self {
      shared: Shared::default(),
      hwnd_raw: Arc::new(AtomicIsize::new(0)),
      started: Arc::new(AtomicBool::new(false)),
      init_lock: Arc::new(Mutex::new(())),
    }
  }

  pub fn status(&self) -> OverlayStatus {
    let last_error = self.shared.last_error.lock().ok().and_then(|g| g.clone());
    let visible = self.shared.visible.lock().ok().map(|g| *g).unwrap_or(false);
    let config_mode = self.shared.config_mode.lock().ok().map(|g| *g).unwrap_or(false);
    let position = self.shared.position.lock().ok().and_then(|p| p.clone());
    let running = self.hwnd_raw.load(Ordering::SeqCst) != 0;

    OverlayStatus {
      supported: true,
      running,
      visible,
      config_mode,
      last_error,
      position,
    }
  }

  pub fn show(&self, payload: OverlayPayload, position: Option<OverlayPosition>) -> Result<(), String> {
    *self.shared.toast.lock().map_err(|_| "toast lock poisoned")? = Some(payload);
    if let Some(p) = position {
      *self.shared.position.lock().map_err(|_| "pos lock poisoned")? = Some(p);
    }
    let hwnd = self.ensure_window()?;
    unsafe {
      PostMessageW(Some(hwnd), WM_OVERLAY_SHOW, WPARAM(0), LPARAM(0))
        .map_err(|e| format!("PostMessageW(WM_OVERLAY_SHOW): {e:?}"))?;
    }
    Ok(())
  }

  pub fn hide(&self) -> Result<(), String> {
    let hwnd = self.ensure_window()?;
    unsafe {
      PostMessageW(Some(hwnd), WM_OVERLAY_HIDE, WPARAM(0), LPARAM(0))
        .map_err(|e| format!("PostMessageW(WM_OVERLAY_HIDE): {e:?}"))?;
    }
    Ok(())
  }

  pub fn enter_config(&self, position: Option<OverlayPosition>) -> Result<(), String> {
    if let Some(p) = position {
      *self.shared.position.lock().map_err(|_| "pos lock poisoned")? = Some(p);
    }
    *self.shared.config_mode.lock().map_err(|_| "cfg lock poisoned")? = true;
    let hwnd = self.ensure_window()?;
    unsafe {
      PostMessageW(Some(hwnd), WM_OVERLAY_ENTER_CONFIG, WPARAM(0), LPARAM(0))
        .map_err(|e| format!("PostMessageW(WM_OVERLAY_ENTER_CONFIG): {e:?}"))?;
    }
    Ok(())
  }

  pub fn exit_config(&self) -> Result<(), String> {
    *self.shared.config_mode.lock().map_err(|_| "cfg lock poisoned")? = false;
    let hwnd = self.ensure_window()?;
    unsafe {
      PostMessageW(Some(hwnd), WM_OVERLAY_EXIT_CONFIG, WPARAM(0), LPARAM(0))
        .map_err(|e| format!("PostMessageW(WM_OVERLAY_EXIT_CONFIG): {e:?}"))?;
    }
    Ok(())
  }

  pub fn get_position(&self) -> Option<OverlayPosition> {
    self.shared.position.lock().ok().and_then(|p| p.clone())
  }

  pub fn set_position(&self, pos: OverlayPosition) -> Result<(), String> {
    *self.shared.position.lock().map_err(|_| "pos lock poisoned")? = Some(pos);
    let hwnd = self.ensure_window()?;
    unsafe {
      PostMessageW(Some(hwnd), WM_OVERLAY_SET_POS, WPARAM(0), LPARAM(0))
        .map_err(|e| format!("PostMessageW(WM_OVERLAY_SET_POS): {e:?}"))?;
    }
    Ok(())
  }

  fn ensure_window(&self) -> Result<HWND, String> {
    let raw = self.hwnd_raw.load(Ordering::SeqCst);
    if raw != 0 {
      return Ok(HWND(raw as *mut c_void));
    }

    // serialize init attempts
    let _guard = self.init_lock.lock().map_err(|_| "init lock poisoned")?;

    let raw = self.hwnd_raw.load(Ordering::SeqCst);
    if raw != 0 {
      return Ok(HWND(raw as *mut c_void));
    }

    if self.started.swap(true, Ordering::SeqCst) {
      // another thread started it; wait a bit for hwnd
      for _ in 0..50 {
        let raw = self.hwnd_raw.load(Ordering::SeqCst);
        if raw != 0 {
          return Ok(HWND(raw as *mut c_void));
        }
        thread::sleep(Duration::from_millis(20));
      }
      return Err("overlay init timed out".into());
    }

    let shared = self.shared.clone();
    let hwnd_raw = self.hwnd_raw.clone();
    let (tx, rx) = std::sync::mpsc::channel::<Result<(), String>>();

    thread::spawn(move || {
      let _ = run_overlay_thread(shared, hwnd_raw, tx);
    });

    rx.recv_timeout(Duration::from_secs(2))
      .unwrap_or_else(|_| Err("overlay init timed out".into()))?;

    let raw = self.hwnd_raw.load(Ordering::SeqCst);
    if raw == 0 {
      return Err("overlay init failed".into());
    }
    Ok(HWND(raw as *mut c_void))
  }
}

fn set_last_error(shared: &Shared, msg: impl Into<String>) {
  if let Ok(mut g) = shared.last_error.lock() {
    *g = Some(msg.into());
  }
}

fn clear_last_error(shared: &Shared) {
  if let Ok(mut g) = shared.last_error.lock() {
    *g = None;
  }
}

fn run_overlay_thread(shared: Shared, hwnd_raw: Arc<AtomicIsize>, ready: std::sync::mpsc::Sender<Result<(), String>>) -> Result<(), String> {
  unsafe {
    // ensure message queue exists
    let _ = GetCurrentThreadId();

    let hinstance = GetModuleHandleW(PCWSTR::null())
      .map_err(|e| format!("GetModuleHandleW: {e:?}"))?
      .into();
    let class_name = w!("helltime_native_overlay");

    let wc = WNDCLASSW {
      style: CS_HREDRAW | CS_VREDRAW,
      lpfnWndProc: Some(wndproc),
      hInstance: hinstance,
      lpszClassName: class_name,
      hCursor: LoadCursorW(None, IDC_ARROW).unwrap_or_default(),
      ..Default::default()
    };
    let _ = RegisterClassW(&wc);

    let ctx = Box::new(WindowCtx {
      shared,
      font_title: None,
      font_body: None,
    });
    let ctx_ptr = Box::into_raw(ctx);

    let ex_style = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    let style = WS_POPUP | WS_CLIPSIBLINGS;

    let hwnd = CreateWindowExW(
      ex_style,
      class_name,
      w!("helltime"),
      style,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      280,
      110,
      None,
      None,
      Some(hinstance),
      Some(ctx_ptr as *const c_void),
    )
    .map_err(|e| format!("CreateWindowExW: {e:?}"))?;

    hwnd_raw.store(hwnd.0 as isize, Ordering::SeqCst);
    let _ = ready.send(Ok(()));

    let _ = SetLayeredWindowAttributes(hwnd, COLORREF(0), 245, LWA_ALPHA);
    ShowWindow(hwnd, SW_HIDE);

    // also store initial position
    let mut rect = RECT::default();
    if GetWindowRect(hwnd, &mut rect).is_ok() {
      if let Some(ctx) = get_ctx(hwnd) {
        if let Ok(mut p) = ctx.shared.position.lock() {
          *p = Some(OverlayPosition { x: rect.left, y: rect.top });
        }
      }
    }

    let mut msg = MSG::default();
    while GetMessageW(&mut msg, None, 0, 0).into() {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    Ok(())
  }
}

#[derive(Clone)]
struct WindowCtx {
  shared: Shared,
  font_title: Option<HFONT>,
  font_body: Option<HFONT>,
}

unsafe extern "system" fn wndproc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
  match msg {
    WM_NCCREATE => {
      let cs = &*(lparam.0 as *const windows::Win32::UI::WindowsAndMessaging::CREATESTRUCTW);
      let ctx_ptr = cs.lpCreateParams as *mut WindowCtx;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, ctx_ptr as isize);
      LRESULT(1)
    }
    WM_DESTROY => {
      let ctx_ptr = GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut WindowCtx;
      if !ctx_ptr.is_null() {
        let ctx = Box::from_raw(ctx_ptr);
        if let Some(f) = ctx.font_title {
          let _ = DeleteObject(HGDIOBJ(f.0));
        }
        if let Some(f) = ctx.font_body {
          let _ = DeleteObject(HGDIOBJ(f.0));
        }
      }
      PostQuitMessage(0);
      LRESULT(0)
    }
    WM_ERASEBKGND => LRESULT(1),
    WM_MOVE => {
      if let Some(ctx) = get_ctx(hwnd) {
        let mut rect = RECT::default();
        if GetWindowRect(hwnd, &mut rect).is_ok() {
          if let Ok(mut p) = ctx.shared.position.lock() {
            *p = Some(OverlayPosition { x: rect.left, y: rect.top });
          }
        }
      }
      LRESULT(0)
    }
    WM_LBUTTONDOWN => {
      if let Some(ctx) = get_ctx(hwnd) {
        let cfg = ctx.shared.config_mode.lock().ok().map(|g| *g).unwrap_or(false);
        if cfg {
          let _ = SendMessageW(
            hwnd,
            windows::Win32::UI::WindowsAndMessaging::WM_NCLBUTTONDOWN,
            Some(WPARAM(HTCAPTION as usize)),
            Some(LPARAM(0)),
          );
        }
      }
      LRESULT(0)
    }
    WM_TIMER => {
      if wparam.0 == TIMER_HIDE {
        let _ = KillTimer(Some(hwnd), TIMER_HIDE);
        set_visible(hwnd, false, get_ctx(hwnd));
      }
      LRESULT(0)
    }
    WM_OVERLAY_SET_POS => {
      if let Some(ctx) = get_ctx(hwnd) {
        apply_position(hwnd, &ctx.shared);
      }
      LRESULT(0)
    }
    WM_OVERLAY_ENTER_CONFIG => {
      if let Some(ctx) = get_ctx(hwnd) {
        clear_last_error(&ctx.shared);
        // make interactive: remove click-through + noactivate
        let mut ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE) as u32;
        ex &= !(WS_EX_TRANSPARENT.0 as u32);
        ex &= !(WS_EX_NOACTIVATE.0 as u32);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex as isize);
        apply_position(hwnd, &ctx.shared);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        let _ = SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        InvalidateRect(Some(hwnd), None, false.into());
        if let Ok(mut v) = ctx.shared.visible.lock() {
          *v = true;
        }
      }
      LRESULT(0)
    }
    WM_OVERLAY_EXIT_CONFIG => {
      if let Some(ctx) = get_ctx(hwnd) {
        // restore click-through + noactivate
        let mut ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE) as u32;
        ex |= WS_EX_TRANSPARENT.0 as u32;
        ex |= WS_EX_NOACTIVATE.0 as u32;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex as isize);
        InvalidateRect(Some(hwnd), None, false.into());
      }
      LRESULT(0)
    }
    WM_OVERLAY_HIDE => {
      set_visible(hwnd, false, get_ctx(hwnd));
      LRESULT(0)
    }
    WM_OVERLAY_SHOW => {
      if let Some(ctx) = get_ctx(hwnd) {
        clear_last_error(&ctx.shared);
        apply_position(hwnd, &ctx.shared);
        set_visible(hwnd, true, Some(ctx));
        InvalidateRect(Some(hwnd), None, false.into());
        let _ = SetTimer(Some(hwnd), TIMER_HIDE, 5200, None);
      }
      LRESULT(0)
    }
    WM_PAINT => {
      paint(hwnd);
      LRESULT(0)
    }
    WM_CLOSE => {
      // hide instead of destroy
      set_visible(hwnd, false, get_ctx(hwnd));
      LRESULT(0)
    }
    _ => DefWindowProcW(hwnd, msg, wparam, lparam),
  }
}

unsafe fn get_ctx(hwnd: HWND) -> Option<&'static mut WindowCtx> {
  let ptr = GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut WindowCtx;
  if ptr.is_null() { None } else { Some(&mut *ptr) }
}

unsafe fn set_visible(hwnd: HWND, visible: bool, ctx: Option<&'static mut WindowCtx>) {
  if visible {
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    let _ = SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
  } else {
    ShowWindow(hwnd, SW_HIDE);
    let _ = KillTimer(Some(hwnd), TIMER_HIDE);
  }

  if let Some(ctx) = ctx {
    if let Ok(mut v) = ctx.shared.visible.lock() {
      *v = visible;
    }
  }
}

unsafe fn apply_position(hwnd: HWND, shared: &Shared) {
  let pos = shared.position.lock().ok().and_then(|p| p.clone());
  if let Some(p) = pos {
    let _ = SetWindowPos(hwnd, HWND_TOPMOST, p.x, p.y, 280, 110, SWP_NOACTIVATE);
  }
}

unsafe fn paint(hwnd: HWND) {
  let mut ps = PAINTSTRUCT::default();
  let hdc: HDC = BeginPaint(hwnd, &mut ps);

  let ctx = get_ctx(hwnd);
  if ctx.is_none() {
    EndPaint(hwnd, &ps);
    return;
  }
  let ctx = ctx.unwrap();

  // fonts (lazy)
  if ctx.font_title.is_none() || ctx.font_body.is_none() {
    let dpi = GetDeviceCaps(Some(hdc), windows::Win32::Graphics::Gdi::LOGPIXELSY);
    let title_px = -mul_div(14, dpi, 72);
    let body_px = -mul_div(12, dpi, 72);
    ctx.font_title = Some(CreateFontW(
      title_px,
      0,
      0,
      0,
      700,
      0,
      0,
      0,
      windows::Win32::Graphics::Gdi::DEFAULT_CHARSET,
      windows::Win32::Graphics::Gdi::OUT_DEFAULT_PRECIS,
      windows::Win32::Graphics::Gdi::CLIP_DEFAULT_PRECIS,
      windows::Win32::Graphics::Gdi::DEFAULT_QUALITY,
      windows::Win32::Graphics::Gdi::DEFAULT_PITCH | windows::Win32::Graphics::Gdi::FF_DONTCARE,
      w!("Segoe UI"),
    ));
    ctx.font_body = Some(CreateFontW(
      body_px,
      0,
      0,
      0,
      500,
      0,
      0,
      0,
      windows::Win32::Graphics::Gdi::DEFAULT_CHARSET,
      windows::Win32::Graphics::Gdi::OUT_DEFAULT_PRECIS,
      windows::Win32::Graphics::Gdi::CLIP_DEFAULT_PRECIS,
      windows::Win32::Graphics::Gdi::DEFAULT_QUALITY,
      windows::Win32::Graphics::Gdi::DEFAULT_PITCH | windows::Win32::Graphics::Gdi::FF_DONTCARE,
      w!("Segoe UI"),
    ));
  }

  let mut rect = RECT::default();
  let _ = GetWindowRect(hwnd, &mut rect);
  let width = (rect.right - rect.left).max(1);
  let height = (rect.bottom - rect.top).max(1);
  let client = RECT {
    left: 0,
    top: 0,
    right: width,
    bottom: height,
  };

  // background
  let bg = CreateSolidBrush(COLORREF(0x20120b)); // BGR
  FillRect(hdc, &client, bg);
  let _ = DeleteObject(HGDIOBJ(bg.0));

  let padding = 10;
  let mut title_rect = client;
  title_rect.left += padding;
  title_rect.right -= padding;
  title_rect.top += 8;
  title_rect.bottom = title_rect.top + 22;

  let mut body_rect = client;
  body_rect.left += padding;
  body_rect.right -= padding;
  body_rect.top = title_rect.bottom + 2;
  body_rect.bottom -= 8;

  let cfg = ctx.shared.config_mode.lock().ok().map(|g| *g).unwrap_or(false);
  let toast = ctx.shared.toast.lock().ok().and_then(|t| t.clone());

  let (title, body) = if cfg {
    ("Toast Position".to_string(), "Zieh mich an die gewünschte Stelle.".to_string())
  } else if let Some(t) = toast {
    (t.title, t.body)
  } else {
    ("helltime".to_string(), "—".to_string())
  };

  SetBkMode(hdc, TRANSPARENT);
  if let Some(f) = ctx.font_title {
    let _ = SelectObject(hdc, HGDIOBJ(f.0));
  }
  SetTextColor(hdc, COLORREF(0xEDEDED));
  let mut title_w: Vec<u16> = title.encode_utf16().collect();
  let _ = DrawTextW(
    hdc,
    title_w.as_mut_slice(),
    &mut title_rect,
    DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_NOPREFIX,
  );

  if let Some(f) = ctx.font_body {
    let _ = SelectObject(hdc, HGDIOBJ(f.0));
  }
  SetTextColor(hdc, COLORREF(0xC8C8C8));
  let mut body_w: Vec<u16> = body.encode_utf16().collect();
  let _ = DrawTextW(
    hdc,
    body_w.as_mut_slice(),
    &mut body_rect,
    DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_NOPREFIX,
  );

  EndPaint(hwnd, &ps);
}

fn mul_div(number: i32, numerator: i32, denominator: i32) -> i32 {
  ((number as i64 * numerator as i64) / denominator as i64) as i32
}
