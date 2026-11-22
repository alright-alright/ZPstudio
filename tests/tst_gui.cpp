// Drives the real MainWindow (offscreen): new project, Build, a broken build, clicking a problem.
// Needs a ZPdevtools checkout with the assemblers and one C compiler built, next to this repo
// (or ZPSTUDIO_DEVTOOLS=...) and a ZPgameTemplate beside it. Skips itself otherwise.

#include "assetpanel.h"
#include "codeeditor.h"
#include "emulatorpane.h"
#include "mainwindow.h"
#include "problemspanel.h"
#include "projectcreator.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QProgressBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace zps;

class TstGui : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDir_, workDir_;
    Toolchain tc_;
    QString modalSeen_;      // text of any dialog that popped up (nothing in these flows should)
    QTimer watchdog_;

    static QAction* action(QWidget& w, const QString& text)
    {
        for (QAction* a : w.findChildren<QAction*>())
            if (a->text() == text) return a;
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        // Keep the user's real settings out of it.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        QCoreApplication::setOrganizationName("ZeroPointTest");
        QCoreApplication::setApplicationName("StudioGuiTest");

        const QString envDev = qEnvironmentVariable("ZPSTUDIO_DEVTOOLS");
        tc_ = Toolchain::detect(envDev.isEmpty() ? QDir(ZPSTUDIO_SOURCE_DIR).filePath("../ZPdevtools") : envDev);
        if (!tc_.buildProblems().isEmpty()) QSKIP(qPrintable("toolchain not ready: " + tc_.buildProblems().join(" | ")));
        if (tc_.templateDir.isEmpty()) QSKIP("no ZPgameTemplate next to ZPdevtools");
        tc_.emulator.clear();          // never start a real emulator window from a test
        tc_.save();

        // A modal box would hang the test forever: close it and fail loudly instead.
        connect(&watchdog_, &QTimer::timeout, this, [this] {
            if (QWidget* m = QApplication::activeModalWidget()) {
                modalSeen_ = m->windowTitle();
                m->close();
            }
        });
        watchdog_.start(100);
    }

    void buildsTheTemplateFromTheWindow()
    {
        CreateOptions o;
        o.templateDir = tc_.templateDir;
        o.targetDir = workDir_.path() + "/game";
        ProjectConfig pr;
        QString err;
        QVERIFY2(createProject(o, &pr, &err), qPrintable(err));

        MainWindow w;
        w.show();
        QVERIFY(w.openProject(pr.dir));
        QVERIFY(w.windowTitle().contains("ZeroPoint Studio"));
        auto* editor = w.findChild<CodeEditor*>();
        QVERIFY2(editor, "src/main.c should open in an editor tab");
        QVERIFY(editor->filePath().endsWith("src/main.c"));

        QAction* build = action(w, "Build");
        QVERIFY(build && build->isEnabled());
        build->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(pr.romPath()), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 5000);      // the run is over, the button is back

        auto* problems = w.findChild<ProblemsPanel*>();
        QCOMPARE(problems->errorCount(), 0);
        QVERIFY2(modalSeen_.isEmpty(), qPrintable("unexpected dialog: " + modalSeen_));
        QVERIFY(QFileInfo(pr.romPath()).size() > 100000);
    }

    void artPanelShowsTheTemplatesBudgets()
    {
        ProjectConfig pr;
        QString err;
        QVERIFY(ProjectConfig::load(workDir_.path() + "/game", &pr, &err));
        MainWindow w;
        QVERIFY(w.openProject(pr.dir));
        auto* assets = w.findChild<AssetPanel*>();
        QVERIFY(assets);
        const auto bars = assets->findChildren<QProgressBar*>();
        QCOMPARE(bars.size(), 6);                                  // colours, 4 tile banks, sprite cells
        QVERIFY2(bars[0]->format().endsWith("/ 240"), qPrintable(bars[0]->format()));
        QVERIFY(bars[0]->value() > 0 && bars[0]->value() <= 240);
        QVERIFY(!bars[0]->property("over").toBool());
        qInfo() << "colour estimate for the template art:" << bars[0]->format();
    }

    void aBrokenBuildGivesAClickableProblemOnTheRightLine()
    {
        ProjectConfig pr;
        QString err;
        QVERIFY(ProjectConfig::load(workDir_.path() + "/game", &pr, &err));
        MainWindow w;
        w.show();
        QVERIFY(w.openProject(pr.dir));
        auto* editor = w.findChild<CodeEditor*>();
        QVERIFY(editor);
        editor->appendPlainText("\nint broken( { ");
        const int brokenLine = editor->blockCount();               // the new last line
        QVERIFY(editor->document()->isModified());

        QAction* build = action(w, "Build");
        build->trigger();                                          // saves the edit, then builds
        auto* problems = w.findChild<ProblemsPanel*>();
        QTRY_VERIFY_WITH_TIMEOUT(problems->errorCount() > 0, 60000);
        QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 5000);
        QVERIFY(!editor->document()->isModified());                // Build saved it

        QTreeWidgetItem* row = nullptr;
        for (int i = 0; i < problems->topLevelItemCount() && !row; ++i)
            if (problems->topLevelItem(i)->text(1).startsWith("main.c:")) row = problems->topLevelItem(i);
        QVERIFY2(row, "an error with a main.c location");
        QCOMPARE(row->text(1), QStringLiteral("main.c:%1").arg(brokenLine));

        // Clicking it puts the cursor on that line.
        editor->moveCursor(QTextCursor::Start);
        emit problems->itemActivated(row, 0);
        QCOMPARE(editor->textCursor().blockNumber() + 1, brokenLine);
        QVERIFY(modalSeen_.isEmpty());
    }

    void runsTheGameInsideTheWindow()
    {
        if (!EmulatorPane::available()) QSKIP("built without the embedded emulator");
        CreateOptions o;
        o.templateDir = tc_.templateDir;
        o.targetDir = workDir_.path() + "/play";
        ProjectConfig pr;
        QString err;
        QVERIFY2(createProject(o, &pr, &err), qPrintable(err));

        MainWindow w;
        w.show();
        QVERIFY(w.openProject(pr.dir));
        auto* pane = w.findChild<EmulatorPane*>();
        QVERIFY(pane && pane->screen());
        QVERIFY(!pane->running());

        QAction* run = action(w, "▶ Run");
        QAction* stop = action(w, "■ Stop");
        QVERIFY(run && stop && !stop->isEnabled());
        run->trigger();                                            // build, then load it into the pane
        QTRY_VERIFY_WITH_TIMEOUT(pane->running(), 60000);
        QVERIFY(stop->isEnabled());
        QVERIFY2(modalSeen_.isEmpty(), qPrintable("unexpected dialog: " + modalSeen_));

        // The picture must actually appear: a template game paints sky, ground and sprites, so the frame holds many
        // colours (a black or frozen screen has one).
        auto colours = [&] {
            QSet<QRgb> seen;
            const QImage img = pane->screen()->grab().toImage();
            for (int y = 0; y < img.height(); y += 3)
                for (int x = 0; x < img.width(); x += 3) seen.insert(img.pixel(x, y));
            return seen.size();
        };
        QTRY_VERIFY2_WITH_TIMEOUT(colours() >= 6, qPrintable(QString("only %1 colours on screen").arg(colours())), 30000);

        // The picture moves: the template scrolls its backgrounds, so two frames a moment apart differ.
        const QImage a = pane->screen()->grab().toImage();
        QTest::qWait(500);
        QVERIFY2(pane->screen()->grab().toImage() != a, "the frame never changed: the game is not running");

        stop->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!pane->running(), 10000);
        QVERIFY(!stop->isEnabled());

        // Run again on the same window: a new machine each time, and no leftovers.
        run->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(pane->running(), 60000);
        stop->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!pane->running(), 10000);
    }

    // Not a check: with ZPSTUDIO_SCREENSHOT=/path/out.png it saves what the window looks like (for the README).
    void screenshotForDocs()
    {
        const QString out = qEnvironmentVariable("ZPSTUDIO_SCREENSHOT");
        if (out.isEmpty()) QSKIP("set ZPSTUDIO_SCREENSHOT=<file.png> to take one");
        // Its own fresh project: the other tests leave a broken main.c behind in /game.
        CreateOptions o;
        o.templateDir = tc_.templateDir;
        o.targetDir = workDir_.path() + "/shot";
        o.title = "SCREENSHOT GAME";
        ProjectConfig pr;
        QString err;
        QVERIFY2(createProject(o, &pr, &err), qPrintable(err));
        theme::apply(*qApp);
        MainWindow w;
        w.resize(1280, 800);
        w.show();
        QVERIFY(w.openProject(pr.dir));
        action(w, "▶ Run")->trigger();                      // builds, then plays inside the Game panel
        auto* pane = w.findChild<EmulatorPane*>();
        QTRY_VERIFY_WITH_TIMEOUT(pane->running() || !EmulatorPane::available(), 60000);
        QTest::qWait(2500);                                  // let the game get going
        QVERIFY(w.grab().save(out));
        action(w, "■ Stop")->trigger();
    }

    void cancelledBuildLeavesTheWindowUsable()
    {
        ProjectConfig pr;
        QString err;
        QVERIFY(ProjectConfig::load(workDir_.path() + "/game", &pr, &err));
        MainWindow w;
        QVERIFY(w.openProject(pr.dir));
        auto* editor = w.findChild<CodeEditor*>();
        editor->setPlainText(QStringLiteral("int main() { return 0; }\n"));   // restore a valid program
        QAction* build = action(w, "Build");
        QAction* stop = action(w, "■ Stop");
        QVERIFY(build && stop);
        QVERIFY(!stop->isEnabled());
        build->trigger();
        QVERIFY(stop->isEnabled());                                // a build is running
        stop->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 10000);
        QVERIFY(!stop->isEnabled());
    }
};

QTEST_MAIN(TstGui)
#include "tst_gui.moc"
