#pragma once
// Runs a BuildPlan one step at a time and reports progress as signals. Knows nothing about widgets:
// the GUI and the command-line tool both drive it.

#include "buildplan.h"
#include "diagnostics.h"

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QStringList>

namespace zps {

struct BuildResult {
    bool ok = false;
    bool cancelled = false;
    QString failedStep;     // empty when ok
    QString romPath;        // the signed .zpb when ok
    int errors = 0;
    int warnings = 0;
    qint64 millis = 0;
};

class BuildRunner : public QObject {
    Q_OBJECT
public:
    explicit BuildRunner(QObject* parent = nullptr);

    bool running() const { return running_; }
    void start(const BuildPlan& plan, const ProjectConfig& project);
    void cancel();

signals:
    void stepStarted(const QString& name, int index, int total);
    void output(const QString& line, bool isError);
    void diagnostic(const zps::Diagnostic& d);
    void finished(const zps::BuildResult& result);

private:
    void runNext();
    void handleData(bool isStderr);
    void handleLine(const QString& line);
    void placeInSource(Diagnostic* d, const BuildStep& step);
    void stepFinished(int exitCode, QProcess::ExitStatus status);
    void fail(const QString& message);
    void finish(bool ok, bool cancelled = false);

    BuildPlan plan_;
    ProjectConfig project_;
    QProcess* proc_ = nullptr;
    int index_ = -1;
    bool running_ = false;
    bool cancelled_ = false;
    QElapsedTimer timer_;
    QByteArray pendingOut_, pendingErr_;
    QStringList held_;            // output of a quiet step, kept in case it fails
    int errors_ = 0, warnings_ = 0, stepErrors_ = 0;
    QString failedStep_;
};

} // namespace zps
