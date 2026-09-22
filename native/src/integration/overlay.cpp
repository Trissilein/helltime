#include "overlay.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <iterator>

namespace helltime::integration {
namespace {

constexpr wchar_t kClassName[] = L"HelltimeNativeOverlay";

template <typename T> void release(T*& value) { if (value) { value->Release(); value = nullptr; } }

void logOverlayFailure(const wchar_t* step, HRESULT result = S_OK) {
    wchar_t message[160]{};
    if (FAILED(result)) swprintf_s(message, L"Helltime overlay: %s failed (HRESULT 0x%08X)\n", step, static_cast<unsigned>(result));
    else swprintf_s(message, L"Helltime overlay: %s failed (Win32 %lu)\n", step, GetLastError());
    OutputDebugStringW(message);
}

std::wstring appDataFile() {
    wchar_t buffer[32768]{};
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    return size == 0 || size >= std::size(buffer) ? std::wstring{} : std::wstring(buffer, size) + L"\\HelltimeNative\\overlay-position.txt";
}

std::array<float, 4> colorFromHex(const std::string& hex) {
    unsigned int value = 0;
    if (hex.size() == 7 && std::sscanf(hex.c_str() + 1, "%06x", &value) == 1) {
        return {((value >> 16) & 255) / 255.0f, ((value >> 8) & 255) / 255.0f, (value & 255) / 255.0f, 1.0f};
    }
    return {0.043f, 0.071f, 0.125f, 1.0f};
}

std::wstring toWide(const std::wstring& value) { return value; }

} // namespace

OverlayWindow::~OverlayWindow() { Destroy(); }

bool OverlayWindow::Create(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSEXW klass{sizeof(klass)};
    klass.hInstance = instance;
    klass.lpfnWndProc = WindowProc;
    klass.lpszClassName = kClassName;
    klass.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    klass.hbrBackground = nullptr;
    RegisterClassExW(&klass);
    LoadPosition();
    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, L"Helltime overlay", WS_POPUP,
        position_.x, position_.y, width_, height_, nullptr, nullptr, instance, this);
    if (!window_) return false;
    SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_, SWP_NOACTIVATE | SWP_HIDEWINDOW);
    return true;
}

void OverlayWindow::Destroy() {
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
    if (instance_) UnregisterClassW(kClassName, instance_);
}

void OverlayWindow::Hide() { if (window_) ShowWindow(window_, SW_HIDE); }

void OverlayWindow::ShowReminderToast(const std::wstring& title, const std::wstring& body) {
    reminderTitle_ = title;
    reminderBody_ = body;
    reminderUntil_ = GetTickCount64() + 5200;
}

void OverlayWindow::SetClickThrough(bool enabled) {
    if (!window_) return;
    auto style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    if (enabled) style |= WS_EX_TRANSPARENT;
    else style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    SetWindowLongPtrW(window_, GWL_EXSTYLE, style);
    SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
}

void OverlayWindow::BeginMove() {
    if (!window_) return;
    positioning_ = true;
    positioningUntil_ = GetTickCount64() + 15'000;
    SetClickThrough(false);
    SetForegroundWindow(window_);
    SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void OverlayWindow::ResetPosition() {
    position_ = {40, 40};
    ClampPosition();
    SavePosition();
    if (window_) SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_, SWP_NOACTIVATE | SWP_NOSIZE);
}

void OverlayWindow::ClampPosition() {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    position_.x = std::clamp(position_.x, static_cast<LONG>(left - width_ + 24), static_cast<LONG>(right - 24));
    position_.y = std::clamp(position_.y, static_cast<LONG>(top - height_ + 24), static_cast<LONG>(bottom - 24));
}

void OverlayWindow::LoadPosition() {
    const auto path = appDataFile();
    if (path.empty()) return;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rt") == 0 && file) {
        std::fscanf(file, "%d %d", &position_.x, &position_.y);
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
    if (!window_) return;
    if (positioning_ && GetTickCount64() >= positioningUntil_) {
        positioning_ = false;
        SetClickThrough(true);
    }
    if (state.panicStop || !state.overlayEnabled || !settings.overlayWindowEnabled) {
        Hide();
        return;
    }
    const bool reminderActive = reminderUntil_ > GetTickCount64();
    const bool toast = state.overlayMode == ui::OverlayMode::Toast || reminderActive;
    const bool hasOverviewRows = std::any_of(state.overlayCategories.begin(), state.overlayCategories.end(),
        [&state, index = std::size_t{0}](bool enabled) mutable {
            const bool visible = enabled && state.categories[index].enabled;
            ++index;
            return visible;
        });
    if ((toast && !reminderActive && !positioning_) || (!toast && !hasOverviewRows)) {
        Hide();
        return;
    }
    width_ = static_cast<int>(std::lround((toast ? 360.0f : 390.0f) * state.overlayScaleX));
    height_ = static_cast<int>(std::lround((toast ? 84.0f : 150.0f) * state.overlayScaleY));
    width_ = std::clamp(width_, 200, 900);
    height_ = std::clamp(height_, 52, 360);
    ClampPosition();
    SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_, SWP_NOACTIVATE);
    Render(state, settings, schedule, nowMs);
    ShowWindow(window_, SW_SHOWNOACTIVATE);
}

void OverlayWindow::Render(const ui::UiState& state, const domain::Settings& settings,
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

    D2D1_RENDER_TARGET_PROPERTIES properties{
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT};
    HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d);
    if (FAILED(result)) logOverlayFailure(L"D2D factory", result);
    if (SUCCEEDED(result)) {
        result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&write));
        if (FAILED(result)) logOverlayFailure(L"DirectWrite factory", result);
    }
    if (SUCCEEDED(result)) {
        result = d2d->CreateDCRenderTarget(&properties, &target);
        if (FAILED(result)) logOverlayFailure(L"D2D DC target", result);
    }
    const bool ready = SUCCEEDED(result);
    if (ready) {
        result = write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"", &title);
        if (SUCCEEDED(result)) result = write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                                 DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"", &body);
        if (FAILED(result)) logOverlayFailure(L"text format", result);
        if (FAILED(result)) goto cleanup;
        dc = CreateCompatibleDC(nullptr);
        if (!dc) logOverlayFailure(L"compatible DC");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width_;
        info.bmiHeader.biHeight = -height_;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        if (dc) bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, nullptr, nullptr, 0);
        if (!bitmap) logOverlayFailure(L"DIB section");
        if (bitmap) oldBitmap = static_cast<HBITMAP>(SelectObject(dc, bitmap));
        if (!oldBitmap || oldBitmap == HGDI_ERROR) logOverlayFailure(L"DIB selection");
        RECT rect{0, 0, width_, height_};
        if (oldBitmap && oldBitmap != HGDI_ERROR) result = target->BindDC(dc, &rect);
        else result = E_FAIL;
        if (FAILED(result)) logOverlayFailure(L"D2D BindDC", result);
        if (FAILED(result)) goto cleanup;
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0, 0));
        const auto bg = colorFromHex(settings.overlayBgHex);
        result = target->CreateSolidColorBrush(D2D1::ColorF(bg[0], bg[1], bg[2], static_cast<float>(settings.overlayBgOpacity)), &background);
        if (SUCCEEDED(result)) result = target->CreateSolidColorBrush(D2D1::ColorF(0.28f, 0.03f, 0.03f, static_cast<float>(settings.overlayLineBgOpacity)), &panel);
        if (SUCCEEDED(result)) result = target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.96f), &text);
        if (SUCCEEDED(result)) result = target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.62f), &muted);
        if (FAILED(result)) {
            logOverlayFailure(L"brush", result);
            goto cleanup;
        }
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, static_cast<float>(width_), static_cast<float>(height_)), 8, 8), background);

        const bool reminderToast = reminderUntil_ > GetTickCount64();
        const bool toast = state.overlayMode == ui::OverlayMode::Toast || reminderToast;
        const int count = toast ? 1 : std::max(1, static_cast<int>(std::count_if(
            state.overlayCategories.begin(), state.overlayCategories.end(),
            [&state, index = std::size_t{0}](bool enabled) mutable {
                const bool visible = enabled && state.categories[index].enabled;
                ++index;
                return visible;
            })));
        const float gap = 5.0f;
        const float row = (height_ - 8.0f - gap * (count - 1)) / count;
        int drawn = 0;
        if (reminderToast) {
            target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(4, 4, width_ - 4.0f, height_ - 4.0f), 5, 5), panel);
            target->DrawTextW(reminderTitle_.c_str(), static_cast<UINT32>(reminderTitle_.size()), title,
                              D2D1::RectF(13, 11, width_ - 13.0f, height_ * 0.52f), text);
            target->DrawTextW(reminderBody_.c_str(), static_cast<UINT32>(reminderBody_.size()), body,
                              D2D1::RectF(13, height_ * 0.52f, width_ - 13.0f, height_ - 11.0f), muted);
        }
        if (positioning_) {
            target->DrawTextW(L"Overlay ziehen", 13, body,
                              D2D1::RectF(13, 7, width_ - 13.0f, 25), muted);
        }
        for (int i = 0; !toast && i < 3 && drawn < count; ++i) {
            if (!state.overlayCategories[static_cast<std::size_t>(i)] || !state.categories[static_cast<std::size_t>(i)].enabled) continue;
            const auto& category = state.categories[static_cast<std::size_t>(i)];
            const float y = 4.0f + drawn * (row + gap);
            target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(4, y, width_ - 4.0f, y + row), 5, 5), panel);
            const auto titleRect = D2D1::RectF(13, y + 5, width_ * 0.54f, y + 28);
            target->DrawTextW(category.title.c_str(), static_cast<UINT32>(category.title.size()), title, titleRect, text);
            target->DrawTextW(category.countdown.c_str(), static_cast<UINT32>(category.countdown.size()), title,
                              D2D1::RectF(width_ * 0.55f, y + 5, width_ - 13.0f, y + 28), text);
            target->DrawTextW(category.eventTime.c_str(), static_cast<UINT32>(category.eventTime.size()), body,
                              D2D1::RectF(13, y + row - 23, width_ - 13.0f, y + row - 5), muted);
            ++drawn;
        }
        result = target->EndDraw();
        if (FAILED(result)) {
            logOverlayFailure(L"D2D EndDraw", result);
            goto cleanup;
        }
        POINT topLeft{position_.x, position_.y};
        SIZE size{width_, height_};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (!UpdateLayeredWindow(window_, nullptr, &topLeft, &size, dc, nullptr, 0, &blend, ULW_ALPHA)) {
            logOverlayFailure(L"UpdateLayeredWindow");
        }
    }

cleanup:
    if (oldBitmap && dc) SelectObject(dc, oldBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    release(background); release(panel); release(text); release(muted);
    release(title); release(body); release(target); release(write); release(d2d);
}

LRESULT CALLBACK OverlayWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
        if (positioning_) {
            dragging_ = true;
            POINT cursor{}; GetCursorPos(&cursor);
            dragOffset_ = {cursor.x - position_.x, cursor.y - position_.y};
            SetCapture(window_);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (dragging_ && (wParam & MK_LBUTTON)) {
            POINT cursor{}; GetCursorPos(&cursor);
            position_ = {cursor.x - dragOffset_.x, cursor.y - dragOffset_.y};
            ClampPosition();
            SetWindowPos(window_, HWND_TOPMOST, position_.x, position_.y, width_, height_, SWP_NOACTIVATE);
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
        }
        return 0;
    case WM_NCHITTEST: return positioning_ ? HTCLIENT : HTTRANSPARENT;
    default: return DefWindowProcW(window_, message, wParam, lParam);
    }
}

} // namespace helltime::integration
