#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace theme {

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const QColor window(0x14, 0x15, 0x2e), base(0x0e, 0x0f, 0x24), alt(0x1a, 0x1b, 0x3a);
    const QColor text(0xe8, 0xe8, 0xf5), disabled(0x6b, 0x6e, 0x92), accent(kAccent);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x22, 0x23, 0x4a));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase, alt);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0xb4, 0x9b, 0xff));
    p.setColor(QPalette::BrightText, QColor(kBad));
    for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, r, disabled);
    app.setPalette(p);

    app.setStyleSheet(QStringLiteral(
        "QToolBar { border: 0; spacing: 4px; padding: 4px; background: #1a1b3a; }"
        "QToolButton { padding: 5px 10px; border-radius: 5px; }"
        "QToolButton:hover { background: #2b2c5c; }"
        "QToolButton:disabled { color: #6b6e92; }"
        "QDockWidget::title { background: #1a1b3a; padding: 6px; }"
        "QTabBar::tab { padding: 6px 12px; background: #14152e; border: 0; }"
        "QTabBar::tab:selected { background: #22234a; border-bottom: 2px solid %1; }"
        "QProgressBar { border: 0; border-radius: 4px; background: #0e0f24; text-align: center; height: 14px; }"
        "QProgressBar::chunk { border-radius: 4px; background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 %1, stop:1 #e0579b); }"
        "QProgressBar[over=\"true\"]::chunk { background: %2; }"
        "QPushButton { padding: 6px 14px; border-radius: 5px; background: #2b2c5c; }"
        "QPushButton:hover { background: #3a3b78; }"
        "QPushButton#primary { background: %1; color: white; }"
        "QStatusBar { background: #1a1b3a; }").arg(QString::fromLatin1(kAccent), QString::fromLatin1(kBad)));
}

} // namespace theme
