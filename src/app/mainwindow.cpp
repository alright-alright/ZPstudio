#include "mainwindow.h"

#include "assetpanel.h"
#include "codeeditor.h"
#include "emulatorpane.h"
#include "newprojectdialog.h"
#include "problemspanel.h"
#include "projectcreator.h"
#include "settingsdialog.h"
#include "theme.h"

#include <algorithm>

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

using namespace zps;

namespace {
const QStringList kTextSuffixes{QStringLiteral("c"), QStringLiteral("h"), QStringLiteral("asm"), QStringLiteral("json"),
                                QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("inc")};
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), tc_(Toolchain::load())
{
    setWindowTitle(tr("ZeroPoint Studio"));
    resize(1280, 800);
    setAcceptDrops(false);

    buildMenusAndToolbar();
    buildCentral();
    buildDocks();

    status_ = new QLabel(this);
    progress_ = new QProgressBar(this);
    progress_->setMaximumWidth(180);
    progress_->setVisible(false);
    statusBar()->addWidget(status_, 1);
    statusBar()->addPermanentWidget(progress_);

    connect(&runner_, &BuildRunner::stepStarted, this, [this](const QString& name, int i, int total) {
        progress_->setRange(0, total);
        progress_->setValue(i);
        progress_->setFormat(QStringLiteral("%1 / %2").arg(i).arg(total));
        status_->setText(tr("Building: %1").arg(name));
        log(QStringLiteral("— %1").arg(name));
    });
    connect(&runner_, &BuildRunner::output, this, &MainWindow::log);
    connect(&runner_, &BuildRunner::diagnostic, problems_, &ProblemsPanel::add);
    connect(&runner_, &BuildRunner::finished, this, &MainWindow::onBuildFinished);
    connect(&emulator_, &EmulatorLauncher::output, this, [this](const QString& l) { log(QStringLiteral("[emulator] ") + l); });
    connect(&emulator_, &EmulatorLauncher::stopped, this, [this](int code) {
        status_->setText(tr("Emulator closed (exit code %1)").arg(code));
        updateActions();
    });
    connect(problems_, &ProblemsPanel::openRequested, this, &MainWindow::openFileAt);
    connect(problems_, &ProblemsPanel::countsChanged, this, [this](int e, int w) {
        bottom_->setTabText(0, e || w ? tr("Problems (%1)").arg(e + w) : tr("Problems"));
    });

    updateWelcome();
    updateActions();
    if (!tc_.buildProblems().isEmpty()) status_->setText(tr("The tools are not set up yet: open Settings."));
}

// ---------------------------------------------------------------- setup

void MainWindow::buildMenusAndToolbar()
{
    actNew_ = new QAction(tr("New Game…"), this);
    actNew_->setShortcut(QKeySequence::New);
    actOpen_ = new QAction(tr("Open Project…"), this);
    actOpen_->setShortcut(QKeySequence::Open);
    actSave_ = new QAction(tr("Save All"), this);
    actSave_->setShortcut(QKeySequence::Save);
    actBuild_ = new QAction(tr("Build"), this);
    actBuild_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    actRun_ = new QAction(tr("▶ Run"), this);
    actRun_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    actRun_->setToolTip(tr("Build, then play the game in the Game panel (Ctrl+R)"));
    actRunWindow_ = new QAction(tr("Run in Separate Window"), this);
    actRunWindow_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    actRunWindow_->setToolTip(tr("Build, then play in the standalone emulator window (needs the emulator set in Settings)"));
    actStop_ = new QAction(tr("■ Stop"), this);
    actStop_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Period));
    actSettings_ = new QAction(tr("Settings…"), this);
    actSettings_->setMenuRole(QAction::PreferencesRole);

    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addActions({actNew_, actOpen_, actSave_});
    file->addSeparator();
    QAction* quit = file->addAction(tr("Quit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);
    QMenu* buildMenu = menuBar()->addMenu(tr("&Build"));
    buildMenu->addActions({actBuild_, actRun_, actRunWindow_, actStop_});
    menuBar()->addMenu(tr("&Tools"))->addAction(actSettings_);

    QToolBar* tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb->addActions({actNew_, actOpen_, actSave_});
    tb->addSeparator();
    tb->addActions({actBuild_, actRun_, actStop_});
    tb->addSeparator();
    tb->addAction(actSettings_);

    connect(actNew_, &QAction::triggered, this, &MainWindow::newProject);
    connect(actOpen_, &QAction::triggered, this, &MainWindow::chooseProject);
    connect(actSave_, &QAction::triggered, this, [this] { saveAll(); });
    connect(actBuild_, &QAction::triggered, this, [this] { build(RunMode::None); });
    connect(actRun_, &QAction::triggered, this, [this] { build(RunMode::InWindow); });
    connect(actRunWindow_, &QAction::triggered, this, [this] { build(RunMode::SeparateWindow); });
    connect(actStop_, &QAction::triggered, this, &MainWindow::stop);
    connect(actSettings_, &QAction::triggered, this, [this] { showSettings(); });
}

void MainWindow::buildCentral()
{
    stack_ = new QStackedWidget(this);

    // Welcome page: the first thing a new user sees.
    welcome_ = new QWidget(stack_);
    auto* wl = new QVBoxLayout(welcome_);
    wl->addStretch(1);
    auto* title = new QLabel(tr("<h1>ZeroPoint Studio</h1><p>Make a game for the ZeroPoint console: art, code, build and play in one place.</p>"));
    title->setAlignment(Qt::AlignHCenter);
    wl->addWidget(title);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* newBtn = new QPushButton(tr("New Game…"));
    newBtn->setObjectName(QStringLiteral("primary"));
    auto* openBtn = new QPushButton(tr("Open Project…"));
    row->addWidget(newBtn);
    row->addWidget(openBtn);
    row->addStretch(1);
    wl->addLayout(row);
    connect(newBtn, &QPushButton::clicked, this, &MainWindow::newProject);
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::chooseProject);

    recent_ = new QListWidget(welcome_);
    recent_->setMaximumWidth(520);
    recent_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    wl->addWidget(new QLabel(tr("Recent projects")), 0, Qt::AlignHCenter);
    wl->addWidget(recent_, 0, Qt::AlignHCenter);
    connect(recent_, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) { openProject(it->data(Qt::UserRole).toString()); });

    welcomeStatus_ = new QLabel(welcome_);
    welcomeStatus_->setAlignment(Qt::AlignHCenter);
    welcomeStatus_->setWordWrap(true);
    wl->addWidget(welcomeStatus_);
    wl->addStretch(2);

    tabs_ = new QTabWidget(stack_);
    tabs_->setTabsClosable(true);
    tabs_->setDocumentMode(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });

    stack_->addWidget(welcome_);
    stack_->addWidget(tabs_);
    setCentralWidget(stack_);
}

void MainWindow::buildDocks()
{
    // Project files (left)
    fileModel_ = new QFileSystemModel(this);
    files_ = new QTreeView(this);
    files_->setModel(fileModel_);
    files_->setHeaderHidden(true);
    for (int c = 1; c < fileModel_->columnCount(); ++c) files_->hideColumn(c);
    connect(files_, &QTreeView::activated, this, [this](const QModelIndex& i) {
        const QString path = fileModel_->filePath(i);
        if (QFileInfo(path).isFile() && kTextSuffixes.contains(QFileInfo(path).suffix().toLower())) openFile(path);
    });
    auto* filesDock = new QDockWidget(tr("Project"), this);
    filesDock->setWidget(files_);
    addDockWidget(Qt::LeftDockWidgetArea, filesDock);

    // Game (right, on top) and Art (right, below it). The right column runs the full height of the window.
    game_ = new EmulatorPane(this);
    gameDock_ = new QDockWidget(tr("Game"), this);
    gameDock_->setWidget(game_);
    addDockWidget(Qt::RightDockWidgetArea, gameDock_);
    assets_ = new AssetPanel(this);
    auto* assetDock = new QDockWidget(tr("Art"), this);
    assetDock->setWidget(assets_);
    addDockWidget(Qt::RightDockWidgetArea, assetDock);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    resizeDocks({gameDock_, assetDock}, {400, 330}, Qt::Vertical);
    resizeDocks({gameDock_}, {430}, Qt::Horizontal);
    connect(game_, &EmulatorPane::stateChanged, this, &MainWindow::updateActions);

    // Problems / Output (bottom)
    problems_ = new ProblemsPanel(this);
    output_ = new QPlainTextEdit(this);
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(5000);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    bottom_ = new QTabWidget(this);
    bottom_->setDocumentMode(true);
    bottom_->addTab(problems_, tr("Problems"));
    bottom_->addTab(output_, tr("Output"));
    auto* bottomDock = new QDockWidget(tr("Build"), this);
    bottomDock->setWidget(bottom_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
    resizeDocks({bottomDock}, {200}, Qt::Vertical);
}

// ---------------------------------------------------------------- project

QStringList MainWindow::recentProjects() const
{
    QStringList list = QSettings().value(QStringLiteral("recentProjects")).toStringList();
    list.erase(std::remove_if(list.begin(), list.end(), [](const QString& d) { return !QFileInfo::exists(d); }), list.end());
    return list;
}

void MainWindow::rememberProject(const QString& dir)
{
    QStringList list = recentProjects();
    list.removeAll(dir);
    list.prepend(dir);
    while (list.size() > 8) list.removeLast();
    QSettings().setValue(QStringLiteral("recentProjects"), list);
}

void MainWindow::updateWelcome()
{
    recent_->clear();
    for (const QString& d : recentProjects()) {
        auto* it = new QListWidgetItem(QStringLiteral("%1\n%2").arg(QFileInfo(d).fileName(), QDir::toNativeSeparators(d)), recent_);
        it->setData(Qt::UserRole, d);
    }
    recent_->setVisible(recent_->count() > 0);
    const QStringList problems = tc_.buildProblems();
    welcomeStatus_->setText(problems.isEmpty() ? QString() : tr("Setup needed: %1\n\nOpen Tools → Settings.").arg(problems.first()));
    welcomeStatus_->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromLatin1(theme::kWarn)));
}

void MainWindow::newProject()
{
    if (tc_.templateDir.isEmpty()) {
        showSettings(tr("New Game starts from a game template (the ZPgameTemplate folder). Tell Studio where it is."));
        if (tc_.templateDir.isEmpty()) return;
    }
    NewProjectDialog dlg(tc_.templateDir, this);
    if (dlg.exec() != QDialog::Accepted) return;
    ProjectConfig pr;
    QString err;
    if (!createProject(dlg.options(), &pr, &err)) {
        QMessageBox::critical(this, tr("Could not create the game"), err);
        return;
    }
    showProject(pr);
}

void MainWindow::chooseProject()
{
    const QString start = recentProjects().value(0, QDir::homePath());
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open a game folder"), start);
    if (!dir.isEmpty()) openProject(dir);
}

bool MainWindow::openProject(const QString& dir)
{
    ProjectConfig pr;
    QString err;
    if (ProjectConfig::load(dir, &pr, &err)) { showProject(pr); return true; }
    if (ProjectConfig::looksLikeTemplate(dir)) { adoptOrWarn(dir, err); return project_.has_value(); }
    QMessageBox::warning(this, tr("Not a Studio project"), err);
    return false;
}

void MainWindow::adoptOrWarn(const QString& dir, const QString&)
{
    const auto answer = QMessageBox::question(this, tr("Use this folder as a project?"),
        tr("%1 looks like a ZeroPoint game (it has src/main.c and assets/assets.json) but has no zpstudio.json.\n\n"
           "Studio can add one. Nothing else in the folder is changed.").arg(QDir::toNativeSeparators(dir)));
    if (answer != QMessageBox::Yes) return;
    ProjectConfig pr;
    QString err;
    if (!adoptProject(dir, &pr, &err)) { QMessageBox::critical(this, tr("Could not set up the project"), err); return; }
    showProject(pr);
}

void MainWindow::showProject(const ProjectConfig& pr)
{
    // Close the previous project's editors first (and let the user keep unsaved work).
    if (project_ && !saveAllOrAsk()) return;
    closeAllTabs();                // already saved or deliberately discarded: do not ask again per tab
    runner_.cancel();
    emulator_.stop();
    game_->stopGame();

    project_ = pr;
    rememberProject(pr.dir);
    fileModel_->setRootPath(pr.dir);
    files_->setRootIndex(fileModel_->index(pr.dir));
    assets_->setProject(pr);
    problems_->clearAll();
    output_->clear();
    setWindowTitle(tr("%1 — ZeroPoint Studio").arg(pr.title));
    status_->setText(tr("Opened %1").arg(QDir::toNativeSeparators(pr.dir)));
    openFile(pr.sourcePath());
    updateActions();
}

// ---------------------------------------------------------------- editing

CodeEditor* MainWindow::openFile(const QString& path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* e = qobject_cast<CodeEditor*>(tabs_->widget(i));
        if (e && e->filePath() == abs) { tabs_->setCurrentIndex(i); stack_->setCurrentWidget(tabs_); return e; }
    }
    if (!QFileInfo::exists(abs)) return nullptr;
    auto* editor = new CodeEditor(abs, tabs_);
    const int idx = tabs_->addTab(editor, QFileInfo(abs).fileName());
    tabs_->setTabToolTip(idx, abs);
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this, editor](bool m) {
        const int i = tabs_->indexOf(editor);
        if (i >= 0) tabs_->setTabText(i, QFileInfo(editor->filePath()).fileName() + (m ? QStringLiteral(" •") : QString()));
    });
    tabs_->setCurrentIndex(idx);
    stack_->setCurrentWidget(tabs_);
    return editor;
}

void MainWindow::openFileAt(const QString& file, int line)
{
    const QString abs = QDir::isAbsolutePath(file) || !project_ ? file : QDir(project_->dir).filePath(file);
    if (CodeEditor* e = openFile(abs)) e->goToLine(line);
}

bool MainWindow::saveAll()
{
    bool ok = true;
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* e = qobject_cast<CodeEditor*>(tabs_->widget(i));
        if (!e || !e->document()->isModified()) continue;
        QString err;
        if (!e->save(&err)) {
            QMessageBox::critical(this, tr("Could not save"), err);
            ok = false;
        }
    }
    return ok;
}

bool MainWindow::hasUnsaved() const
{
    for (int i = 0; i < tabs_->count(); ++i)
        if (auto* e = qobject_cast<CodeEditor*>(tabs_->widget(i)); e && e->document()->isModified()) return true;
    return false;
}

// Returns false if the user cancelled.
bool MainWindow::saveAllOrAsk()
{
    if (!hasUnsaved()) return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save your changes before continuing?"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveAll();
    return true;
}

void MainWindow::closeAllTabs()
{
    while (tabs_->count()) {
        QWidget* w = tabs_->widget(0);
        tabs_->removeTab(0);
        w->deleteLater();
    }
    stack_->setCurrentWidget(welcome_);
}

bool MainWindow::closeTab(int index)
{
    auto* e = qobject_cast<CodeEditor*>(tabs_->widget(index));
    if (!e) return true;
    if (e->document()->isModified()) {
        const auto answer = QMessageBox::question(this, tr("Unsaved changes"),
            tr("Save changes to %1?").arg(QFileInfo(e->filePath()).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel) return false;
        QString err;
        if (answer == QMessageBox::Save && !e->save(&err)) {
            QMessageBox::critical(this, tr("Could not save"), err);
            return false;
        }
    }
    tabs_->removeTab(index);
    e->deleteLater();
    if (!tabs_->count()) stack_->setCurrentWidget(welcome_);   // nothing open: back to the start page
    return true;
}

// ---------------------------------------------------------------- build and run

void MainWindow::build(RunMode then)
{
    if (!project_ || runner_.running()) return;
    // Without the emulator built in there is nowhere to play but the external window.
    if (then == RunMode::InWindow && !EmulatorPane::available()) then = RunMode::SeparateWindow;

    const QStringList problems = tc_.buildProblems();
    if (!problems.isEmpty()) {
        showSettings(tr("Studio cannot build yet:\n\n%1").arg(problems.join(QLatin1Char('\n'))));
        return;
    }
    if (then != RunMode::None) {
        // The emulator binary is only needed for the external window; the built-in one needs just the optional ROM files.
        const QStringList runProblems = tc_.runProblems(then == RunMode::SeparateWindow);
        if (!runProblems.isEmpty()) {
            showSettings(tr("Studio cannot run the game yet:\n\n%1").arg(runProblems.join(QLatin1Char('\n'))));
            return;
        }
    }
    if (!saveAll()) return;
    emulator_.stop();
    problems_->clearAll();
    output_->clear();
    bottom_->setCurrentIndex(1);   // watch the build; switches to Problems if it fails
    runAfterBuild_ = then;
    progress_->setVisible(true);
    runner_.start(BuildPlan::make(tc_, *project_), *project_);
    updateActions();
}

void MainWindow::onBuildFinished(const BuildResult& r)
{
    progress_->setVisible(false);
    if (r.cancelled) {
        status_->setText(tr("Build cancelled"));
        log(tr("Build cancelled."), true);
    } else if (r.ok) {
        status_->setText(tr("Built %1 in %2 ms").arg(QFileInfo(r.romPath).fileName()).arg(r.millis));
        log(tr("Built %1 in %2 ms").arg(r.romPath).arg(r.millis));
    } else {
        status_->setText(tr("Build failed at “%1”: %2 error(s)").arg(r.failedStep).arg(r.errors));
        bottom_->setCurrentIndex(0);
        log(tr("Build failed at “%1”.").arg(r.failedStep), true);
    }
    const RunMode mode = r.ok ? runAfterBuild_ : RunMode::None;
    runAfterBuild_ = RunMode::None;
    updateActions();
    if (mode != RunMode::None) runEmulator(r.romPath, mode);
}

void MainWindow::runEmulator(const QString& rom, RunMode mode)
{
    QString err;
    if (mode == RunMode::InWindow) {
        game_->stopGame();                       // a fresh machine for the fresh build
        if (!game_->play(tc_, rom, &err)) {
            QMessageBox::critical(this, tr("Could not start the game"), err);
            return;
        }
        gameDock_->show();
        gameDock_->raise();
        status_->setText(tr("Playing %1 — the keyboard goes to the game while it is focused").arg(QFileInfo(rom).fileName()));
    } else {
        game_->stopGame();
        if (!emulator_.start(tc_, rom, 3, &err)) {
            QMessageBox::critical(this, tr("Could not start the emulator"), err);
            return;
        }
        status_->setText(tr("Running %1 in the emulator window (close it or press Stop)").arg(QFileInfo(rom).fileName()));
    }
    updateActions();
}

void MainWindow::stop()
{
    if (runner_.running()) runner_.cancel();
    if (emulator_.running() || game_->running()) {
        emulator_.stop();
        game_->stopGame();
        status_->setText(tr("Stopped"));
    }
    updateActions();
}

void MainWindow::showSettings(const QString& reason)
{
    if (!reason.isEmpty()) QMessageBox::information(this, tr("Setup needed"), reason);
    SettingsDialog dlg(tc_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    tc_ = dlg.toolchain();
    tc_.save();
    updateWelcome();
    updateActions();
    status_->setText(tc_.buildProblems().isEmpty() ? tr("Tools are ready.") : tr("Some tools are still missing."));
}

void MainWindow::updateActions()
{
    const bool have = project_.has_value();
    const bool building = runner_.running();
    const bool running = emulator_.running() || game_->running();
    actSave_->setEnabled(have);
    actBuild_->setEnabled(have && !building);
    actRun_->setEnabled(have && !building);
    actRunWindow_->setEnabled(have && !building);
    actStop_->setEnabled(building || running);
}

void MainWindow::log(const QString& line, bool isError)
{
    const QString esc = line.toHtmlEscaped();
    output_->appendHtml(isError ? QStringLiteral("<span style=\"color:%1\">%2</span>").arg(QString::fromLatin1(theme::kBad), esc)
                                : QStringLiteral("<span>%1</span>").arg(esc));
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!saveAllOrAsk()) { e->ignore(); return; }
    runner_.cancel();
    emulator_.stop();
    game_->stopGame();             // joins the emulation thread before the widgets go away
    e->accept();
}
