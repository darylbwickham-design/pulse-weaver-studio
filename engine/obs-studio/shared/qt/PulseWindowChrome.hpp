#pragma once
#include <QApplication>
#include <QLibrary>
#include <QWidget>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace PulseVisual {
// Theme only this application's native caption; retain Windows window controls.
inline void updateWindowChrome(QWidget *window)
{
#ifdef Q_OS_WIN
    if (!window || !window->isWindow() || !window->isVisible() ||
        window->windowType() == Qt::Popup || window->windowType() == Qt::ToolTip) return;
    static QLibrary library("dwmapi");
    using SetAttribute = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    static auto setAttribute = reinterpret_cast<SetAttribute>(library.resolve("DwmSetWindowAttribute"));
    if (!setAttribute) return;
    HIGHCONTRAST contrast{sizeof(HIGHCONTRAST), 0, nullptr};
    const bool highContrast = SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON);
    const QColor background = window->palette().color(QPalette::Window);
    const QColor foreground = window->palette().color(QPalette::WindowText);
    const COLORREF caption = highContrast ? DWMWA_COLOR_DEFAULT : RGB(background.red(), background.green(), background.blue());
    const COLORREF text = highContrast ? DWMWA_COLOR_DEFAULT : RGB(foreground.red(), foreground.green(), foreground.blue());
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    // Older Windows versions safely decline unsupported caption attributes.
    setAttribute(handle, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    setAttribute(handle, DWMWA_TEXT_COLOR, &text, sizeof(text));
#else
    Q_UNUSED(window);
#endif
}

class WindowChrome final : public QObject {
    bool eventFilter(QObject *target, QEvent *event) override
    {
        if (event->type() == QEvent::Show || event->type() == QEvent::PaletteChange)
            updateWindowChrome(qobject_cast<QWidget *>(target));
        return false;
    }
public:
    explicit WindowChrome(QObject *parent) : QObject(parent) { qApp->installEventFilter(this); }
};
} // namespace PulseVisual
