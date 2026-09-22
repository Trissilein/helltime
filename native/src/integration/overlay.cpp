#include "overlay.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>

namespace helltime::integration {
namespace {

constexpr wchar_t kClassName[] = L"HelltimeNativeOverlay";

template <typename T> void release(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

std::wstring appDataFile() {
    wchar_t buffer[32768]{};
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    return size == 0 || size >= std::size(buffer)
        ? std::wstring{}
        : std::wstring(buffer, size) + L"\\HelltimeNative\\overlay-position.txt";
}

std::array<float, 4> colorFromHex(const std::string& hex) {
    unsigned int value = 0;
    if (hex.size() == 7 && sscanf_s(hex.c_str() + 1, "%06x", &value) == 1) {
        return {((value >> 16) & 255) / 255.0f,
                ((value >> 8) & 255) / 255.0f,
                (value & 255) / 255.0f,
                1.0f};
    }
    return {0.043f, 0.071f, 0.125f, 1.0f};
}

} // namespace

OverlayWindow::~OverlayWindow() { Destroy(); }

bool OverlayWindow::Create(HINSTANCE instance) {
    instance_ = instance;
    diagnostics_ = {};
    diagnostics_.hwnd = nullptr;

    WNDCLASSEXW klass{sizeof(klass)};
    klass.hInstance = instance;
    klass.lpfnWndProc = WindowProc;
    klass.lpszClassName = kClassName;
    klass.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    klass.hbrBackground = nullptr;
    SetLastError(ERROR_SUCCESS);
    if (!RegisterClassExW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        RecordError(L"RegisterClassExW");
        return false;
    }

    LoadPosition();
    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, L"Helltime overlay", WS_POPUP,
        position_.x, position_.y, width_, height_, nullptr, nullptr, instance, this);
    if (!window_) {
        RecordError(L"CreateWindowExW");
        return false;
    }

    diagnostics_.created = true;
    diagnostics_.hwnd = window_;
    if (!SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_,
                      SWP_NOACTIVATE | SWP_HIDEWINDOW)) {
        RecordError(L"SetWindowPos(create)");
        DestroyWindow(window_);
        window_ = nullptr;
        diagnostics_.created = false;
        return false;
    }
    RefreshDiagnosticsBounds();
    RecordEvent(L"created");
    return true;
}

void OverlayWindow::Destroy() {
    if (window_) {
        if (!DestroyWindow(window_)) RecordError(L"DestroyWindow");
        window_ = nullptr;
    }
    diagnostics_.visible = false;
    diagnostics_.showSucceeded = false;
    diagnostics_.hwnd = nullptr;
    if (instance_) UnregisterClassW(kClassName, instance_);
}

void OverlayWindow::Hide() {
    if (!window_) return;
    ShowWindow(window_, SW_HIDE);
    diagnostics_.showSucceeded = false;
    diagnostics_.visible = false;
    RefreshDiagnosticsBounds();
}

void OverlayWindow::ShowToast(const std::wstring& title, const std::wstring& body,
                              const std::wstring& category, ULONGLONG durationMs) {
    reminderTitle_ = title;
    reminderBody_ = body;
    reminderCategory_ = category;
    reminderUntil_ = GetTickCount64() + std::max<ULONGLONG>(1, durationMs);
    RecordEvent(L"toast queued");
}

void OverlayWindow::ShowReminderToast(const std::wstring& title, const std::wstring& body) {
    ShowToast(title, body, {}, 5200);
}

void OverlayWindow::ShowPreviewToast(const std::wstring& title, const std::wstring& body) {
    ShowToast(title, body, {}, 8000);
}

bool OverlayWindow::SetClickThrough(bool enabled) {
    if (!window_) return false;
    SetLastError(ERROR_SUCCESS);
    const auto previous = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
        RecordError(L"GetWindowLongPtrW");
        return false;
    }
    const auto next = enabled ? previous | WS_EX_TRANSPARENT
                              : previous & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    SetLastError(ERROR_SUCCESS);
    if (SetWindowLongPtrW(window_, GWL_EXSTYLE, next) == 0 && GetLastError() != ERROR_SUCCESS) {
        RecordError(L"SetWindowLongPtrW");
        return false;
    }
    // Do not show here. A layered window may only become visible after a good frame.
    if (!SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_,
                      SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOZORDER)) {
        RecordError(L"SetWindowPos(click-through)");
        return false;
    }
    return true;
}

void OverlayWindow::BeginMove() {
    if (!window_) return;
    dragging_ = false;
    if (!SetClickThrough(false)) {
        positioning_ = false;
        positioningUntil_ = 0;
        RecordEvent(L"positioning unavailable");
        return;
    }
    positioning_ = true;
    positioningUntil_ = GetTickCount64() + 15'000;
    SetForegroundWindow(window_);
    if (!SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_, SWP_NOACTIVATE)) {
        RecordError(L"SetWindowPos(positioning)");
        positioning_ = false;
        positioningUntil_ = 0;
        SetClickThrough(true);
        return;
    }
    RecordEvent(L"positioning started");
    RefreshDiagnosticsBounds();
}

void OverlayWindow::ResetPosition() {
    position_ = {40, 40};
    ClampPosition();
    SavePosition();
    if (window_ && !SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_, SWP_NOACTIVATE | SWP_NOSIZE)) {
        RecordError(L"SetWindowPos(reset)");
    }
    RecordEvent(L"position reset");
    RefreshDiagnosticsBounds();
}

void OverlayWindow::ClampPosition() {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int maxX = std::max(left, right - width_);
    const int maxY = std::max(top, bottom - height_);
    position_.x = std::clamp(position_.x, static_cast<LONG>(left), static_cast<LONG>(maxX));
    position_.y = std::clamp(position_.y, static_cast<LONG>(top), static_cast<LONG>(maxY));
}

void OverlayWindow::LoadPosition() {
    const auto path = appDataFile();
    if (path.empty()) return;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rt") == 0 && file) {
        fscanf_s(file, "%d %d", &position_.x, &position_.y);
        std::fclose(file);
    }
    ClampPosition();
}

void OverlayWindow::SavePosition() const {
    const auto path = appDataFile();
    if (path.empty()) return;
    const auto directory = path.substr(0, path.find_last_of(L'\\'));
    CreateDirectoryW(directory.c_str(), nullptr);
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wt") == 0 && file) {
        std::fwprintf(file, L"%d %d\n", position_.x, position_.y);
        std::fclose(file);
    }
}

void OverlayWindow::Update(const ui::UiState& state, const domain::Settings& settings,
                           const domain::Schedule& schedule, std::int64_t nowMs) {
    if (positioning_ && GetTickCount64() >= positioningUntil_) {
        positioning_ = false;
        dragging_ = false;
        if (GetCapture() == window_) ReleaseCapture();
        SetClickThrough(true);
        RecordEvent(L"positioning timeout");
    }

    const auto tick = GetTickCount64();
    const bool reminderActive = reminderUntil_ > tick;
    const bool toastMode = state.overlayMode == ui::OverlayMode::Toast;
    const bool hasOverviewRows = std::any_of(
        state.overlayCategories.begin(), state.overlayCategories.end(),
        [&state, index = std::size_t{0}](bool enabled) mutable {
            const bool visible = enabled && state.categories[index].enabled;
            ++index;
            return visible;
        });

    diagnostics_.mode = state.overlayMode;
    diagnostics_.positioning = positioning_;
    diagnostics_.gatePanicStop = state.panicStop;
    diagnostics_.gateOverlayEnabled = state.overlayEnabled;
    diagnostics_.gateSettingsEnabled = settings.overlayWindowEnabled;
    diagnostics_.gateOverviewRows = hasOverviewRows;
    diagnostics_.gateToastMode = toastMode;
    diagnostics_.gateReminderActive = reminderActive;
    if (!window_) return;

    if (state.panicStop || !state.overlayEnabled || !settings.overlayWindowEnabled) {
        Hide();
        return;
    }

    const bool toast = toastMode || reminderActive;

    // Idle toast mode is intentionally hidden. Positioning is the only exception.
    if ((toastMode && !reminderActive && !positioning_) || (!toast && !hasOverviewRows && !positioning_)) {
        Hide();
        return;
    }

    const float scaleX = std::clamp(state.overlayScaleX, 0.25f, 4.0f);
    const float scaleY = std::clamp(state.overlayScaleY, 0.25f, 4.0f);
    width_ = std::clamp(static_cast<int>(std::lround(304.0f * scaleX)), 160, 1200);

    int visibleRows = 0;
    for (std::size_t index = 0; index < state.overlayCategories.size(); ++index) {
        if (state.overlayCategories[index] && state.categories[index].enabled) ++visibleRows;
    }
    const int positioningHeight = positioning_ ? 32 : 0;
    const int logicalHeight = (toast
        ? 84
        : std::max(1, 8 + visibleRows * 38 + std::max(0, visibleRows - 1) * 5)) + positioningHeight;
    height_ = std::clamp(static_cast<int>(std::lround(logicalHeight * scaleY)), 52, 720);
    ClampPosition();
    if (!SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_, SWP_NOACTIVATE | SWP_NOSENDCHANGING)) {
        RecordError(L"SetWindowPos(update)");
        Hide();
        return;
    }

    if (!Render(state, settings, schedule, nowMs)) {
        Hide();
        return;
    }

    ShowWindow(window_, SW_SHOWNOACTIVATE);
    diagnostics_.showSucceeded = IsWindowVisible(window_) != FALSE;
    diagnostics_.visible = diagnostics_.showSucceeded;
    if (!diagnostics_.showSucceeded) RecordError(L"ShowWindow");
    RefreshDiagnosticsBounds();
}

bool OverlayWindow::Render(const ui::UiState& state, const domain::Settings& settings,
                           const domain::Schedule&, std::int64_t) {
    ID2D1Factory* d2d = nullptr;
    IDWriteFactory* write = nullptr;
    ID2D1DCRenderTarget* target = nullptr;
    IDWriteTextFormat* title = nullptr;
    IDWriteTextFormat* body = nullptr;
    ID2D1SolidColorBrush* background = nullptr;
    ID2D1SolidColorBrush* panel = nullptr;
    ID2D1SolidColorBrush* text = nullptr;
    ID2D1SolidColorBrush* muted = nullptr;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HBITMAP oldBitmap = nullptr;
    bool rendered = false;
    HRESULT result = S_OK;

    const D2D1_RENDER_TARGET_PROPERTIES properties{
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT};

    do {
    result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d);
    if (FAILED(result)) {
        RecordError(L"D2D factory", result);
        break;
    }
    result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&write));
    if (FAILED(result)) {
        RecordError(L"DirectWrite factory", result);
        break;
    }
    result = d2d->CreateDCRenderTarget(&properties, &target);
    if (FAILED(result)) {
        RecordError(L"D2D DC target", result);
        break;
    }
    result = write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                     DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                     16.0f, L"", &title);
    if (SUCCEEDED(result)) {
        result = write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         12.0f, L"", &body);
    }
    if (FAILED(result)) {
        RecordError(L"text format", result);
        break;
    }

    dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        RecordError(L"CreateCompatibleDC");
        break;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width_;
    info.bmiHeader.biHeight = -height_; // top-down BGRA for D2D and layered windows
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* dibBits = nullptr;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    if (!bitmap || !dibBits) {
        RecordError(L"CreateDIBSection");
        break;
    }
    oldBitmap = static_cast<HBITMAP>(SelectObject(dc, bitmap));
    if (!oldBitmap || oldBitmap == HGDI_ERROR) {
        RecordError(L"SelectObject(DIB)");
        break;
    }

    RECT rect{0, 0, width_, height_};
    result = target->BindDC(dc, &rect);
    if (FAILED(result)) {
        RecordError(L"D2D BindDC", result);
        break;
    }
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0, 0));
    const auto bg = colorFromHex(settings.overlayBgHex);
    const bool positioning = positioning_;
    const auto backgroundColor = positioning
        ? D2D1::ColorF(1.0f - bg[0], 1.0f - bg[1], 1.0f - bg[2], 1.0f)
        : D2D1::ColorF(bg[0], bg[1], bg[2], static_cast<float>(settings.overlayBgOpacity));
    result = target->CreateSolidColorBrush(
        backgroundColor, &background);
    if (SUCCEEDED(result)) {
        result = target->CreateSolidColorBrush(
            D2D1::ColorF(0.28f, 0.03f, 0.03f, static_cast<float>(settings.overlayLineBgOpacity)), &panel);
    }
    if (SUCCEEDED(result)) result = target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &text);
    if (SUCCEEDED(result)) result = target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.62f), &muted);
    if (FAILED(result)) {
        RecordError(L"brush", result);
        break;
    }

    target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0, 0, static_cast<float>(width_), static_cast<float>(height_)), 8, 8),
        background);
    if (positioning) {
        target->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(1, 1, width_ - 1.0f, height_ - 1.0f), 8, 8), text, 2.0f);
        target->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(4, 4, width_ - 4.0f, 29.0f), 6, 6), panel);
        target->DrawTextW(L"Ziehen zum Verschieben", 21, body,
                          D2D1::RectF(12, 7, width_ - 12.0f, 26), text);
    }

    const auto tick = GetTickCount64();
    const bool reminderToast = reminderUntil_ > tick;
    const bool toast = state.overlayMode == ui::OverlayMode::Toast || reminderToast;
    const int count = toast
        ? 1
        : std::max(1, static_cast<int>(std::count_if(
              state.overlayCategories.begin(), state.overlayCategories.end(),
              [&state, index = std::size_t{0}](bool enabled) mutable {
                  const bool visible = enabled && state.categories[index].enabled;
                  ++index;
                  return visible;
              })));
    const float gap = 5.0f;
    const float contentTop = positioning ? 34.0f : 4.0f;
    const float row = (height_ - contentTop - 4.0f - gap * (count - 1)) / count;
    int drawn = 0;
    if (reminderToast) {
        target->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(4, contentTop, width_ - 4.0f, height_ - 4.0f), 5, 5), panel);
        target->DrawTextW(reminderTitle_.c_str(), static_cast<UINT32>(reminderTitle_.size()), title,
                          D2D1::RectF(13, contentTop + 7.0f, width_ - 13.0f, contentTop + row * 0.52f), text);
        target->DrawTextW(reminderBody_.c_str(), static_cast<UINT32>(reminderBody_.size()), body,
                          D2D1::RectF(13, contentTop + row * 0.52f, width_ - 13.0f, height_ - 8.0f), muted);
    } else if (toast && positioning) {
        target->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(4, contentTop, width_ - 4.0f, height_ - 4.0f), 5, 5), panel);
        target->DrawTextW(L"Overlay", 7, title,
                          D2D1::RectF(13, contentTop + 7.0f, width_ * 0.60f, height_ - 8.0f), text);
        target->DrawTextW(L"ziehen", 6, title,
                          D2D1::RectF(width_ * 0.60f, contentTop + 7.0f, width_ - 13.0f, height_ - 8.0f), text);
    }
    for (int i = 0; !toast && i < 3 && drawn < count; ++i) {
        if (!state.overlayCategories[static_cast<std::size_t>(i)] ||
            !state.categories[static_cast<std::size_t>(i)].enabled) {
            continue;
        }
        const auto& category = state.categories[static_cast<std::size_t>(i)];
        const float y = contentTop + drawn * (row + gap);
        target->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(4, y, width_ - 4.0f, y + row), 5, 5), panel);
        const auto titleRect = D2D1::RectF(13, y + 5, width_ * 0.54f, y + 28);
        target->DrawTextW(category.title.c_str(), static_cast<UINT32>(category.title.size()), title,
                          titleRect, text);
        target->DrawTextW(category.countdown.c_str(), static_cast<UINT32>(category.countdown.size()), title,
                          D2D1::RectF(width_ * 0.55f, y + 5, width_ - 13.0f, y + 28), text);
        target->DrawTextW(category.eventTime.c_str(), static_cast<UINT32>(category.eventTime.size()), body,
                          D2D1::RectF(13, y + row - 23, width_ - 13.0f, y + row - 5), muted);
        ++drawn;
    }
    result = target->EndDraw();
    if (FAILED(result)) {
        RecordError(L"D2D EndDraw", result);
        break;
    }
    if (!GdiFlush()) {
        RecordError(L"GdiFlush");
        break;
    }
    {
        const auto* pixels = static_cast<const std::uint8_t*>(dibBits);
        const auto pixelCount = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
        std::uint64_t nonZeroAlpha = 0;
        std::uint64_t nonZeroColor = 0;
        std::uint8_t maxAlpha = 0;
        for (std::size_t index = 0; index < pixelCount; ++index) {
            // 32-bit DIB_RGB_COLORS is BGRA on Windows. D2D writes premultiplied BGRA.
            const auto offset = index * 4;
            const auto color = static_cast<std::uint8_t>(pixels[offset] | pixels[offset + 1] | pixels[offset + 2]);
            const auto alpha = pixels[offset + 3];
            if (color != 0) ++nonZeroColor;
            if (alpha != 0) ++nonZeroAlpha;
            maxAlpha = std::max(maxAlpha, alpha);
        }
        // Some WIC/GDI paths preserve D2D color channels but clear the DIB alpha
        // byte. Recover those pixels before UpdateLayeredWindow rather than
        // presenting a visibly existing but fully transparent HWND.
        if (nonZeroAlpha == 0 && nonZeroColor != 0) {
            auto* writablePixels = static_cast<std::uint8_t*>(dibBits);
            for (std::size_t index = 0; index < pixelCount; ++index) {
                const auto offset = index * 4;
                if ((writablePixels[offset] | writablePixels[offset + 1] | writablePixels[offset + 2]) != 0) {
                    writablePixels[offset + 3] = 0xFF;
                }
            }
            nonZeroAlpha = nonZeroColor;
            maxAlpha = 0xFF;
            RecordEvent(L"alpha recovered from DIB color");
        }
        diagnostics_.lastNonZeroAlphaPixels = nonZeroAlpha;
        diagnostics_.lastNonZeroColorPixels = nonZeroColor;
        diagnostics_.lastMaxAlpha = maxAlpha;
        diagnostics_.lastFrameHadAlpha = nonZeroAlpha != 0;
        if (!diagnostics_.lastFrameHadAlpha) {
            RecordError(L"frame alpha is fully transparent", E_FAIL);
            break;
        }
    }
    {
        POINT topLeft{position_.x, position_.y};
        SIZE size{width_, height_};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        diagnostics_.updateLayeredWindowSucceeded = false;
        if (!UpdateLayeredWindow(window_, nullptr, &topLeft, &size, dc, nullptr, 0, &blend, ULW_ALPHA)) {
            RecordError(L"UpdateLayeredWindow");
            break;
        }
        diagnostics_.updateLayeredWindowSucceeded = true;
    }
    rendered = true;
    diagnostics_.renderSucceeded = true;
    diagnostics_.lastHresult = S_OK;
    diagnostics_.lastWin32Error = ERROR_SUCCESS;
    diagnostics_.lastError.clear();
    diagnostics_.lastSuccessfulFrameTick = GetTickCount64();
    RecordEvent(L"frame rendered");
    } while (false);

    if (oldBitmap && dc) SelectObject(dc, oldBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    release(background);
    release(panel);
    release(text);
    release(muted);
    release(title);
    release(body);
    release(target);
    release(write);
    release(d2d);
    if (!rendered) diagnostics_.renderSucceeded = false;
    return rendered;
}

void OverlayWindow::RecordEvent(const wchar_t* event) {
    if (!event) return;
    if (diagnostics_.recentEventCount > 0 && diagnostics_.recentEvents[diagnostics_.recentEventCount - 1] == event) return;
    if (diagnostics_.recentEventCount < diagnostics_.recentEvents.size()) {
        diagnostics_.recentEvents[diagnostics_.recentEventCount++] = event;
    } else {
        for (std::size_t index = 1; index < diagnostics_.recentEvents.size(); ++index) {
            diagnostics_.recentEvents[index - 1] = std::move(diagnostics_.recentEvents[index]);
        }
        diagnostics_.recentEvents.back() = event;
    }
}

void OverlayWindow::RecordError(const wchar_t* step, HRESULT result, DWORD win32Error) {
    if (!step) return;
    if (win32Error == ERROR_SUCCESS && SUCCEEDED(result)) win32Error = GetLastError();
    diagnostics_.lastHresult = result;
    diagnostics_.lastWin32Error = win32Error;
    wchar_t message[256]{};
    if (FAILED(result)) {
        swprintf_s(message, L"%s failed (HRESULT 0x%08X)", step, static_cast<unsigned>(result));
    } else {
        swprintf_s(message, L"%s failed (Win32 %lu)", step, static_cast<unsigned long>(win32Error));
    }
    diagnostics_.lastError = message;
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
    RecordEvent(message);
}

void OverlayWindow::RefreshDiagnosticsBounds() {
    diagnostics_.hwnd = window_;
    diagnostics_.positioning = positioning_;
    diagnostics_.visible = window_ && IsWindowVisible(window_) != FALSE;
    if (window_) {
        if (!GetWindowRect(window_, &diagnostics_.bounds)) RecordError(L"GetWindowRect");
    } else {
        diagnostics_.bounds = {};
    }
}

OverlayDiagnostics OverlayWindow::GetDiagnostics() const {
    auto result = diagnostics_;
    result.hwnd = window_;
    result.visible = window_ && IsWindowVisible(window_) != FALSE;
    result.positioning = positioning_;
    if (window_) GetWindowRect(window_, &result.bounds);
    return result;
}

void OverlayWindow::ClearDiagnostics() {
    diagnostics_.recentEvents = {};
    diagnostics_.recentEventCount = 0;
    diagnostics_.lastError.clear();
    diagnostics_.lastHresult = S_OK;
    diagnostics_.lastWin32Error = ERROR_SUCCESS;
    RefreshDiagnosticsBounds();
}

LRESULT CALLBACK OverlayWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }
    return self ? self->HandleMessage(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
        if (positioning_) {
            dragging_ = true;
            POINT cursor{};
            GetCursorPos(&cursor);
            dragOffset_ = {cursor.x - position_.x, cursor.y - position_.y};
            SetCapture(window_);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (dragging_ && (wParam & MK_LBUTTON)) {
            POINT cursor{};
            GetCursorPos(&cursor);
            position_ = {cursor.x - dragOffset_.x, cursor.y - dragOffset_.y};
            ClampPosition();
            if (!SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_, SWP_NOACTIVATE)) {
                RecordError(L"SetWindowPos(drag)");
            }
            RefreshDiagnosticsBounds();
        }
        return 0;
    case WM_LBUTTONUP:
        if (dragging_) {
            dragging_ = false;
            positioning_ = false;
            positioningUntil_ = 0;
            if (GetCapture() == window_) ReleaseCapture();
            SavePosition();
            SetClickThrough(true);
            RecordEvent(L"position saved");
            RefreshDiagnosticsBounds();
        }
        return 0;
    case WM_CANCELMODE:
        if (dragging_) {
            dragging_ = false;
            positioning_ = false;
            positioningUntil_ = 0;
            if (GetCapture() == window_) ReleaseCapture();
            SavePosition();
            SetClickThrough(true);
            RecordEvent(L"position cancelled");
        }
        return 0;
    case WM_NCHITTEST:
        return positioning_ ? HTCLIENT : HTTRANSPARENT;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

} // namespace helltime::integration
