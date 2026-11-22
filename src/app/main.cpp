#include "mainwindow.h"
#include "theme.h"

#include <QApplication>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ZeroPoint"));
    QApplication::setApplicationName(QStringLiteral("Studio"));   // shared with zpstudio-cli: same saved settings
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    theme::apply(app);

    MainWindow w;
    w.show();
    // `zpstudio <project folder>` opens it straight away.
    const QStringList args = app.arguments();
    if (args.size() > 1) w.openProject(args[1]);
    return app.exec();
}
