#pragma once

#include <QEvent>
#include <QLibrary>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Native frame implementation extracted from Horizon frontier/main.cpp.
// Keep native snap, sizing, system buttons and accessibility behavior.
inline void ApplyFrontierNativeFrame(QWidget* window)
{
#ifdef Q_OS_WIN
    if (!window) return;
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    // A recreated native window can lose its shell icon while Qt still owns
    // a non-null QIcon. Repair that exact HWND from our compiled PE resource.
    struct AppIcons {
        HICON small=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0));
        HICON large=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),0));
        ~AppIcons(){if(small)DestroyIcon(small);if(large)DestroyIcon(large);}
    };
    static AppIcons icons;
    if(!SendMessageW(handle,WM_GETICON,ICON_SMALL,0)&&icons.small)SendMessageW(handle,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(icons.small));
    if(!SendMessageW(handle,WM_GETICON,ICON_BIG,0)&&icons.large)SendMessageW(handle,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icons.large));
    window->setProperty("pgn_native_small_icon",bool(SendMessageW(handle,WM_GETICON,ICON_SMALL,0)));
    window->setProperty("pgn_native_large_icon",bool(SendMessageW(handle,WM_GETICON,ICON_BIG,0)));
    QLibrary dwm(QStringLiteral("dwmapi"));
    using SetWindowAttribute = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto setAttribute = reinterpret_cast<SetWindowAttribute>(
        dwm.resolve("DwmSetWindowAttribute"));
    if (!setAttribute) return;
    const BOOL dark = TRUE;
    constexpr DWORD useImmersiveDarkMode = 20;
    constexpr DWORD useImmersiveDarkModeLegacy = 19;
    if (FAILED(setAttribute(handle, useImmersiveDarkMode, &dark, sizeof(dark)))) {
        setAttribute(handle, useImmersiveDarkModeLegacy, &dark, sizeof(dark));
    }
    constexpr DWORD borderColorAttribute = 34;
    constexpr DWORD captionColorAttribute = 35;
    constexpr DWORD textColorAttribute = 36;
    const COLORREF border = RGB(23, 70, 83);
    const COLORREF caption = RGB(1, 10, 15);
    const COLORREF text = RGB(219, 255, 249);
    setAttribute(handle, borderColorAttribute, &border, sizeof(border));
    setAttribute(handle, captionColorAttribute, &caption, sizeof(caption));
    setAttribute(handle, textColorAttribute, &text, sizeof(text));
#else
    Q_UNUSED(window);
#endif
}

// QMainWindow adapters use Horizon's same lifecycle triggers without having
// to replace their own window class or any mining lifecycle behavior.
class HorizonNativeFrameTheme final : public QObject {
public:
    explicit HorizonNativeFrameTheme(QWidget* window) : QObject(window), window_(window)
    {
        window->installEventFilter(this);
        schedule();
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == window_) {
            switch (event->type()) {
            case QEvent::Show:
            case QEvent::WinIdChange:
            case QEvent::WindowActivate:
            case QEvent::WindowStateChange:
                schedule();
                break;
            default:
                break;
            }
        }
        return QObject::eventFilter(watched, event);
    }
private:
    void schedule()
    {
        if (pending_) return;
        pending_ = true;
        QTimer::singleShot(0, this, [this] {
            pending_ = false;
            if (window_) ApplyFrontierNativeFrame(window_);
        });
    }
    QPointer<QWidget> window_;
    bool pending_{};
};
