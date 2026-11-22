#pragma once

#include "buildrunner.h"
#include "emulatorlauncher.h"
#include "projectconfig.h"
#include "toolchain.h"

#include <QMainWindow>
#include <optional>

class AssetPanel;
class CodeEditor;
class ProblemsPanel;
class QAction;
class QFileSystemModel;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QStackedWidget;
class QTabWidget;
class QTreeView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Opens a project folder (used by File > Open, the recent list and the command line).
    bool openProject(const QString& dir);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    // ---- setup ----
    void buildMenusAndToolbar();
    void buildCentral();
    void buildDocks();

    // ---- project ----
    void newProject();
    void chooseProject();
    void adoptOrWarn(const QString& dir, const QString& loadError);
    void showProject(const zps::ProjectConfig& project);
    void updateWelcome();
    void rememberProject(const QString& dir);
    QStringList recentProjects() const;

    // ---- editing ----
    CodeEditor* openFile(const QString& path);
    void openFileAt(const QString& file, int line);
    bool saveAll();
    bool saveAllOrAsk();
    bool closeTab(int index);
    void closeAllTabs();
    bool hasUnsaved() const;

    // ---- build and run ----
    void build(bool thenRun);
    void onBuildFinished(const zps::BuildResult& r);
    void runEmulator(const QString& rom);
    void stop();
    void showSettings(const QString& reason = QString());
    void updateActions();

    // ---- output ----
    void log(const QString& line, bool isError = false);

    zps::Toolchain tc_;
    std::optional<zps::ProjectConfig> project_;
    zps::BuildRunner runner_;
    zps::EmulatorLauncher emulator_;
    bool runAfterBuild_ = false;

    QStackedWidget* stack_ = nullptr;
    QWidget* welcome_ = nullptr;
    QLabel* welcomeStatus_ = nullptr;
    QListWidget* recent_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTreeView* files_ = nullptr;
    QFileSystemModel* fileModel_ = nullptr;
    AssetPanel* assets_ = nullptr;
    ProblemsPanel* problems_ = nullptr;
    QPlainTextEdit* output_ = nullptr;
    QTabWidget* bottom_ = nullptr;
    QLabel* status_ = nullptr;
    QProgressBar* progress_ = nullptr;

    QAction *actNew_, *actOpen_, *actSave_, *actBuild_, *actRun_, *actStop_, *actSettings_;
};
