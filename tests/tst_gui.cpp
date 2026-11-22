// Drives the real MainWindow (offscreen): new project, Build, a broken build, clicking a problem.
// Needs a ZPdevtools checkout with the assemblers and one C compiler built, next to this repo
// (or ZPSTUDIO_DEVTOOLS=...) and a ZPgameTemplate beside it. Skips itself otherwise.

#include "assetpanel.h"
#include "codeeditor.h"
#include "emulatorpane.h"
#include "mainwindow.h"
#include "mapeditor.h"
#include "newmapdialog.h"
#include "tilesetview.h"
#include "problemspanel.h"
#include "projectcreator.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QDir>
#include <QImage>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
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
    QStringList answerYes_;  // questions a test expects and wants answered Yes
    QTimer watchdog_;

    static QAction* action(QWidget& w, const QString& text)
    {
        for (QAction* a : w.findChildren<QAction*>())
            if (a->text() == text) return a;
        return nullptr;
    }

    static QPushButton* button(QWidget& w, const QString& text)
    {
        for (QPushButton* b : w.findChildren<QPushButton*>())
            if (b->text() == text) return b;
        return nullptr;
    }

    // Builds with the window's Build action and waits for it to finish; returns the ROM bytes.
    QByteArray buildAndRead(MainWindow& w, const ProjectConfig& pr)
    {
        QAction* build = action(w, "Build");
        build->trigger();
        QElapsedTimer waited;                                      // (a helper that returns a value cannot use QTRY_*)
        waited.start();
        while (!build->isEnabled() && waited.elapsed() < 60000) QTest::qWait(20);
        auto* problems = w.findChild<ProblemsPanel*>();
        if (!build->isEnabled() || problems->errorCount() != 0) return {};
        QFile f(pr.romPath());
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    // A 32x16 indexed sheet: 8 tiles, one flat colour each.
    static void writeSheet(const QString& path)
    {
        QImage img(32, 16, QImage::Format_Indexed8);
        QList<QRgb> t;
        for (int i = 0; i < 8; ++i) t << qRgb(40 + i * 24, 20 + i * 8, 200 - i * 20);
        img.setColorTable(t);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 32; ++x) img.setPixel(x, y, (x / 8) + (y / 8) * 4);
        QVERIFY(img.save(path));
    }

private slots:
    void init()
    {
        modalSeen_.clear();
        answerYes_.clear();
    }

    void initTestCase()
    {
        // Never depend on the machine's sound card: it can be slow or wedged, and a CI runner has none.
        if (qEnvironmentVariableIsEmpty("SDL_AUDIODRIVER")) qputenv("SDL_AUDIODRIVER", "dummy");
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

        // A modal box would hang the test forever. A question a test expects (answerYes_) is answered Yes; anything
        // else is closed and recorded so the test fails loudly instead.
        connect(&watchdog_, &QTimer::timeout, this, [this] {
            QWidget* m = QApplication::activeModalWidget();
            if (!m) return;
            if (auto* box = qobject_cast<QMessageBox*>(m))
                for (const QString& want : answerYes_)
                    if (box->windowTitle().contains(want) || box->text().contains(want)) {
                        box->button(QMessageBox::Yes)->click();
                        return;
                    }
            modalSeen_ = m->windowTitle() + m->metaObject()->className();
            m->close();
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
        QVERIFY(pane);
        QVERIFY2(!pane->screen(), "the emulator (and its audio device) is only created by the first Run");
        QVERIFY(!pane->running());

        QAction* run = action(w, "▶ Run");
        QAction* stop = action(w, "■ Stop");
        QVERIFY(run && stop && !stop->isEnabled());
        run->trigger();                                            // build, then load it into the pane
        QTRY_VERIFY_WITH_TIMEOUT(pane->running(), 60000);
        QVERIFY(pane->screen());
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

    // ---- the tile map editor ---------------------------------------------------------------------------------

    void mapEditorPaintsFillsErasesPicksAndUndoes()
    {
        QTemporaryDir d;
        writeSheet(d.path() + "/sheet.png");
        TileMap m = TileMap::createEmpty(d.path() + "/m.tmj", "sheet.png", {32, 16}, 16, 16, "bg");
        QString err;
        QVERIFY(m.save(&err));

        MapEditor ed(m);
        ed.resize(1100, 700);
        ed.show();
        TileCanvas* c = ed.canvas();
        TileMapDocument* doc = ed.document();
        auto at = [&](int x, int y) { return QPoint((x * 8 + 4) * c->zoom(), (y * 8 + 4) * c->zoom()); };
        const quint32 tile = 4;                                              // the fourth tile of the sheet (gid 4)
        ed.tilesetView()->selectGid(tile);
        QVERIFY(!ed.documentModified());

        // a click paints one cell
        QTest::mouseClick(c, Qt::LeftButton, {}, at(2, 3));
        QCOMPARE(doc->map().cell(0, 2, 3), tile);
        QVERIFY(ed.documentModified());

        // a drag paints an unbroken line, and the whole drag is one undo step
        QTest::mousePress(c, Qt::LeftButton, {}, at(0, 10));
        QTest::mouseMove(c, at(6, 10));
        QTest::mouseRelease(c, Qt::LeftButton, {}, at(6, 10));
        for (int x = 0; x <= 6; ++x) QCOMPARE(doc->map().cell(0, x, 10), tile);
        doc->undoStack()->undo();
        for (int x = 0; x <= 6; ++x) QCOMPARE(doc->map().cell(0, x, 10), quint32(0));
        QCOMPARE(doc->map().cell(0, 2, 3), tile);                            // the earlier click is its own step

        // fill: everything connected to the click, but not the cell painted above
        ed.setTool(TileCanvas::Tool::Fill);
        QTest::mouseClick(c, Qt::LeftButton, {}, at(10, 10));
        int filled = 0;
        for (quint32 g : doc->map().layers()[0].data) filled += (g == tile);
        QCOMPARE(filled, 16 * 16);                                           // the lone painted cell was already `tile`
        doc->undoStack()->undo();

        // rectangle
        ed.setTool(TileCanvas::Tool::Rect);
        QTest::mousePress(c, Qt::LeftButton, {}, at(1, 1));
        QTest::mouseMove(c, at(3, 2));
        QTest::mouseRelease(c, Qt::LeftButton, {}, at(3, 2));
        QCOMPARE(doc->map().cell(0, 1, 1), tile);
        QCOMPARE(doc->map().cell(0, 3, 2), tile);
        QCOMPARE(doc->map().cell(0, 4, 2), quint32(0));

        // eraser, and right-click as a quick eraser with the pencil
        ed.setTool(TileCanvas::Tool::Eraser);
        QTest::mouseClick(c, Qt::LeftButton, {}, at(2, 3));
        QCOMPARE(doc->map().cell(0, 2, 3), quint32(0));
        ed.setTool(TileCanvas::Tool::Pencil);
        QTest::mouseClick(c, Qt::LeftButton, {}, at(8, 8));
        QCOMPARE(doc->map().cell(0, 8, 8), tile);
        QTest::mouseClick(c, Qt::RightButton, {}, at(8, 8));
        QCOMPARE(doc->map().cell(0, 8, 8), quint32(0));

        // flip toggles mirror what is painted; the picker takes tile AND mirroring back off the map
        action(ed, "Flip H")->trigger();
        QVERIFY(ed.flipH());
        QTest::mouseClick(c, Qt::LeftButton, {}, at(12, 12));
        QCOMPARE(doc->map().cell(0, 12, 12), quint32(tile | kFlipH));
        action(ed, "Flip H")->trigger();                                     // off again
        ed.tilesetView()->selectGid(1);
        ed.setTool(TileCanvas::Tool::Picker);
        QTest::mouseClick(c, Qt::LeftButton, {}, at(12, 12));
        QVERIFY(ed.flipH());                                                 // restored from the cell
        QCOMPARE(ed.tilesetView()->brush().gids[0], tile);
        QTest::mouseClick(c, Qt::LeftButton, {}, at(13, 12));                // the picker switched back to the pencil
        QCOMPARE(doc->map().cell(0, 13, 12), quint32(tile | kFlipH));

        // clicks outside the map or on nothing new do no harm
        const int steps = doc->undoStack()->count();
        QTest::mouseClick(c, Qt::LeftButton, {}, QPoint(-5, -5));
        QTest::mouseClick(c, Qt::LeftButton, {}, at(13, 12));                // already that tile
        QCOMPARE(doc->undoStack()->count(), steps);

        // save, then it is on disk and clean
        QVERIFY2(ed.saveDocument(&err), qPrintable(err));
        QVERIFY(!ed.documentModified());
        TileMap back;
        QVERIFY2(TileMap::load(d.path() + "/m.tmj", &back, &err), qPrintable(err));
        QCOMPARE(back.cell(0, 12, 12), quint32(tile | kFlipH));
        QCOMPARE(back.cell(0, 1, 1), tile);
    }

    void mapEditorWarnsAboutWhatTheBuildWouldRefuse()
    {
        QTemporaryDir d;
        writeSheet(d.path() + "/sheet.png");
        TileMap m = TileMap::createEmpty(d.path() + "/m.tmj", "sheet.png", {32, 16}, 8, 8, "bg");
        m.setCell(0, 0, 0, 2 | kFlipD);                                      // a rotated tile: the console cannot draw it
        MapEditor ed(m);
        ed.show();
        QLabel* warn = nullptr;
        for (QLabel* l : ed.findChildren<QLabel*>())
            if (l->text().contains("refuse")) warn = l;
        QVERIFY(warn);
        QVERIFY(!warn->isHidden());
        QVERIFY(warn->text().contains("rotated"));
    }

    void newTilemapDialogValidatesItsFields()
    {
        QTemporaryDir d;
        writeSheet(d.path() + "/sheet.png");
        QVERIFY(QFile(d.path() + "/taken.tmj").open(QIODevice::WriteOnly));
        QImage odd(10, 10, QImage::Format_RGB32);
        odd.fill(Qt::red);
        QVERIFY(odd.save(d.path() + "/odd.png"));

        NewMapDialog dlg(d.path());
        QPushButton* ok = dlg.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        const auto result = dlg.result();
        QCOMPARE(result.width, 64);
        QCOMPARE(result.name, QStringLiteral("level"));
        QVERIFY(result.tilesetFile.endsWith(".png"));

        // the first sheet in the list is odd.png (10x10): refused until a good one is chosen
        auto* sheets = dlg.findChild<QComboBox*>("sheetBox");
        sheets->setCurrentIndex(sheets->findText("odd.png"));
        QVERIFY(!ok->isEnabled());
        sheets->setCurrentIndex(sheets->findText("sheet.png"));
        QVERIFY(ok->isEnabled());

        auto* name = dlg.findChild<QLineEdit*>("mapName");
        name->setText("taken");                                              // taken.tmj exists
        QVERIFY(!ok->isEnabled());
        name->setText("fresh");
        QVERIFY(ok->isEnabled());
    }

    void turningABackgroundIntoATilemapChangesNothingUntilYouEditIt()
    {
        if (!EmulatorPane::available()) QSKIP("built without the embedded emulator");
        CreateOptions o;
        o.templateDir = tc_.templateDir;
        o.targetDir = workDir_.path() + "/maps";
        o.title = "MAP GAME";
        ProjectConfig pr;
        QString err;
        QVERIFY2(createProject(o, &pr, &err), qPrintable(err));

        MainWindow w;
        w.resize(1280, 800);
        w.show();
        QVERIFY(w.openProject(pr.dir));
        const QByteArray original = buildAndRead(w, pr);
        QVERIFY2(original.size() > 100000, "the plain build works");

        auto* assets = w.findChild<AssetPanel*>();
        auto* list = assets->findChild<QListWidget*>();
        auto budgetText = [&] {
            QStringList t;
            for (QProgressBar* b : assets->findChildren<QProgressBar*>()) t << b->format();
            return t;
        };
        const QStringList budgetsBefore = budgetText();
        QVERIFY(list->item(0)->text().startsWith("Background 0"));
        list->setCurrentRow(0);

        // Edit Tilemap on a picture: converts it (after a question), opens the map in the editor
        answerYes_ << "becomes a map";
        QPushButton* edit = button(*assets, "Edit Tilemap");
        QVERIFY(edit);
        edit->click();
        QVERIFY2(modalSeen_.isEmpty(), qPrintable("unexpected dialog: " + modalSeen_));
        QVERIFY(QFile::exists(pr.assetsDir() + "/ground.tmj"));
        QVERIFY(QFile::exists(pr.assetsDir() + "/ground.png"));              // the picture is kept
        AssetManifest manifest;
        QVERIFY(AssetManifest::load(pr.manifestPath(), &manifest, &err));
        QCOMPARE(manifest.json().value("bg0").toObject().value("tiled").toString(), QStringLiteral("ground.tmj"));
        QVERIFY(!manifest.json().value("bg0").toObject().contains("image"));
        auto* editor = w.findChild<MapEditor*>();
        QVERIFY2(editor, "the map opens in an editor tab");
        QVERIFY(!editor->documentModified());

        // The Art panel costs the map exactly like the picture, and the ROM is the very same bytes.
        QCOMPARE(budgetText(), budgetsBefore);
        QCOMPARE(buildAndRead(w, pr), original);

        // Paint something visible near the top-left of the screen with a tile from the soil, save, build, play.
        const QPoint soil(10, 60);
        const quint32 soilGid = editor->document()->map().cell(0, soil.x(), soil.y());
        editor->tilesetView()->selectGid(soilGid);
        TileCanvas* c = editor->canvas();
        auto at = [&](int x, int y) { return QPoint((x * 8 + 4) * c->zoom(), (y * 8 + 4) * c->zoom()); };
        editor->setTool(TileCanvas::Tool::Rect);
        QTest::mousePress(c, Qt::LeftButton, {}, at(2, 2));
        QTest::mouseMove(c, at(20, 8));
        QTest::mouseRelease(c, Qt::LeftButton, {}, at(20, 8));
        QVERIFY(editor->documentModified());

        const QByteArray edited = buildAndRead(w, pr);                       // Build saves the open map first
        QVERIFY(!editor->documentModified());
        QVERIFY2(!edited.isEmpty() && edited != original, "the edit reached the ROM");

        action(w, "▶ Run")->trigger();
        auto* pane = w.findChild<EmulatorPane*>();
        QTRY_VERIFY_WITH_TIMEOUT(pane->running(), 60000);
        QTest::qWait(1500);
        QSet<QRgb> seen;
        const QImage img = pane->screen()->grab().toImage();
        for (int y = 0; y < img.height(); y += 3)
            for (int x = 0; x < img.width(); x += 3) seen.insert(img.pixel(x, y));
        QVERIFY2(seen.size() >= 4, "the game still draws a picture");
        action(w, "■ Stop")->trigger();
        QVERIFY2(modalSeen_.isEmpty(), qPrintable("unexpected dialog: " + modalSeen_));
    }

    void closingAWindowWithAnUnsavedMapDoesNotCrash()
    {
        // A modified map is torn down with its window (the user chose Discard): the undo stack announces the change
        // in state while it is destroyed, which used to reach a half-destroyed tab widget.
        QTemporaryDir d;
        writeSheet(d.path() + "/sheet.png");
        TileMap m = TileMap::createEmpty(d.path() + "/m.tmj", "sheet.png", {32, 16}, 8, 8, "bg");
        QString err;
        QVERIFY(m.save(&err));
        {
            MapEditor lone(m);
            lone.document()->edit(0, {{1, 1, 2}});
            QVERIFY(lone.documentModified());
        }                                                              // destroyed while modified
        QVERIFY(true);

        CreateOptions o;
        o.templateDir = tc_.templateDir;
        o.targetDir = workDir_.path() + "/discard";
        ProjectConfig pr;
        QVERIFY2(createProject(o, &pr, &err), qPrintable(err));
        auto* w = new MainWindow;
        w->show();
        QVERIFY(w->openProject(pr.dir));
        auto* assets = w->findChild<AssetPanel*>();
        assets->findChild<QListWidget*>()->setCurrentRow(0);
        answerYes_ << "becomes a map";
        button(*assets, "Edit Tilemap")->click();
        auto* ed = w->findChild<MapEditor*>();
        QVERIFY(ed);
        QVERIFY(ed->document()->edit(0, {{3, 3, 2}}));
        QVERIFY(ed->documentModified());
        delete w;                                                       // with unsaved changes: must not crash
        QVERIFY2(modalSeen_.isEmpty(), qPrintable("unexpected dialog: " + modalSeen_));
    }

    // Not a check: with ZPSTUDIO_SCREENSHOT_MAP=/path/out.png it saves the window with a tile map open (for the README).
    void screenshotMapEditorForDocs()
    {
        const QString out = qEnvironmentVariable("ZPSTUDIO_SCREENSHOT_MAP");
        if (out.isEmpty()) QSKIP("set ZPSTUDIO_SCREENSHOT_MAP=<file.png> to take one");
        CreateOptions o;
        o.templateDir = tc_.templateDir;
        o.targetDir = workDir_.path() + "/mapshot";
        o.title = "TILEMAP GAME";
        ProjectConfig pr;
        QString err;
        QVERIFY2(createProject(o, &pr, &err), qPrintable(err));
        theme::apply(*qApp);
        MainWindow w;
        w.resize(1360, 860);
        w.show();
        QVERIFY(w.openProject(pr.dir));
        auto* assets = w.findChild<AssetPanel*>();
        assets->findChild<QListWidget*>()->setCurrentRow(0);
        answerYes_ << "becomes a map";
        button(*assets, "Edit Tilemap")->click();
        auto* ed = w.findChild<MapEditor*>();
        QVERIFY(ed);
        TileCanvas* c = ed->canvas();
        auto at = [&](int x, int y) { return QPoint((x * 8 + 4) * c->zoom(), (y * 8 + 4) * c->zoom()); };
        ed->tilesetView()->selectGid(ed->document()->map().cell(0, 10, 60));
        ed->setTool(TileCanvas::Tool::Rect);
        QTest::mousePress(c, Qt::LeftButton, {}, at(4, 6));
        QTest::mouseMove(c, at(18, 10));
        QTest::mouseRelease(c, Qt::LeftButton, {}, at(18, 10));
        QTest::mouseMove(c, at(24, 14));
        QTest::qWait(400);
        QVERIFY(w.grab().save(out));
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
