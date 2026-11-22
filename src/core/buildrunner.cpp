#include "buildrunner.h"

#include "linemap.h"

#include <QFile>
#include <QProcessEnvironment>

namespace zps {

BuildRunner::BuildRunner(QObject* parent) : QObject(parent) {}

void BuildRunner::start(const BuildPlan& plan, const ProjectConfig& project)
{
    if (running_) return;
    plan_ = plan;
    project_ = project;
    index_ = -1;
    running_ = true;
    cancelled_ = false;
    errors_ = warnings_ = 0;
    failedStep_.clear();
    timer_.start();
    if (!plan_.ok()) {
        Diagnostic d;
        d.message = plan_.error;
        ++errors_;
        failedStep_ = QStringLiteral("Toolchain");
        emit output(plan_.error, true);
        emit diagnostic(d);
        finish(false);
        return;
    }
    runNext();
}

void BuildRunner::cancel()
{
    if (!running_) return;
    cancelled_ = true;
    if (proc_) proc_->kill();      // stepFinished() then ends the run
    else finish(false, true);
}

void BuildRunner::runNext()
{
    ++index_;
    if (index_ >= int(plan_.steps.size())) { finish(true); return; }

    const BuildStep& step = plan_.steps[size_t(index_)];
    const int total = int(plan_.steps.size());
    emit stepStarted(step.name, index_ + 1, total);
    held_.clear();
    stepErrors_ = 0;

    if (!step.isProcess()) {
        const QString err = step.action ? step.action() : QString();
        if (!err.isEmpty()) { failedStep_ = step.name; fail(err); return; }
        runNext();
        return;
    }

    const QStringList args = step.argsFn ? step.argsFn() : step.args;
    emit output(QStringLiteral("> %1 %2").arg(step.program, args.join(QLatin1Char(' '))), false);

    proc_ = new QProcess(this);
    proc_->setWorkingDirectory(step.workDir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    proc_->setProcessEnvironment(env);
    pendingOut_.clear();
    pendingErr_.clear();
    connect(proc_, &QProcess::readyReadStandardOutput, this, [this] { handleData(false); });
    connect(proc_, &QProcess::readyReadStandardError,  this, [this] { handleData(true); });
    connect(proc_, &QProcess::finished, this, &BuildRunner::stepFinished);
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;     // crashes arrive through finished()
        const QString prog = proc_->program();
        failedStep_ = plan_.steps[size_t(index_)].name;
        proc_->deleteLater();
        proc_ = nullptr;
        fail(QStringLiteral("Could not start %1: %2").arg(prog, QStringLiteral("is it built and executable?")));
    });
    proc_->start(step.program, args);
}

void BuildRunner::handleData(bool isStderr)
{
    if (!proc_) return;
    QByteArray& pending = isStderr ? pendingErr_ : pendingOut_;
    pending += isStderr ? proc_->readAllStandardError() : proc_->readAllStandardOutput();
    int nl;
    while ((nl = pending.indexOf('\n')) >= 0) {
        handleLine(QString::fromUtf8(pending.left(nl)));
        pending.remove(0, nl + 1);
    }
}

void BuildRunner::handleLine(const QString& rawLine)
{
    const QString line = rawLine.trimmed().isEmpty() ? QString() : rawLine.trimmed();
    if (line.isEmpty()) return;
    const BuildStep& step = plan_.steps[size_t(index_)];

    if (auto d = parseDiagnostic(line, step.contextFile)) {
        d->step = step.name;
        if (d->preprocessedLine) placeInSource(&*d, step);
        if (d->severity == Diagnostic::Severity::Error) { ++errors_; ++stepErrors_; }
        else if (d->severity == Diagnostic::Severity::Warning) ++warnings_;
        emit diagnostic(*d);
    }
    if (step.showOutput) emit output(line, false);
    else held_ << line;
}

// Turns "line N of the preprocessed copy" into a line of the user's file when that can be done reliably,
// and always names the offending text so the message is useful even when it cannot.
void BuildRunner::placeInSource(Diagnostic* d, const BuildStep& step)
{
    d->preprocessedLine = false;
    const int ppLine = d->line;
    d->line = 0;                      // never leave a line number that means something else
    d->column = 0;
    if (!step.preprocessArgsFn || step.contextFile.isEmpty()) return;

    QProcess pp;
    pp.setWorkingDirectory(step.workDir);
    pp.start(step.program, step.preprocessArgsFn());
    if (!pp.waitForFinished(15000)) { pp.kill(); return; }
    const QStringList preprocessed = QString::fromUtf8(pp.readAllStandardOutput()).split(QLatin1Char('\n'));

    QFile src(step.contextFile);
    if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QStringList source = QString::fromUtf8(src.readAll()).split(QLatin1Char('\n'));

    const MappedLine m = mapPreprocessedLine(preprocessed, ppLine, source);
    if (!m.text.isEmpty()) d->message += QStringLiteral(" (at: %1)").arg(m.text);
    d->line = m.line;
}

void BuildRunner::stepFinished(int exitCode, QProcess::ExitStatus status)
{
    // Flush a last line that had no newline.
    for (QByteArray* p : {&pendingOut_, &pendingErr_})
        if (!p->isEmpty()) { handleLine(QString::fromUtf8(*p)); p->clear(); }

    if (proc_) { proc_->deleteLater(); proc_ = nullptr; }
    const BuildStep& step = plan_.steps[size_t(index_)];

    if (cancelled_) { failedStep_ = step.name; finish(false, true); return; }

    if (status != QProcess::NormalExit || exitCode != 0) {
        failedStep_ = step.name;
        for (const QString& l : held_) emit output(l, true);   // a quiet step speaks up when it fails
        if (stepErrors_ == 0) {
            // The tool failed without printing anything we recognise: say so, so Problems is never empty.
            Diagnostic d;
            d.step = step.name;
            d.message = status == QProcess::NormalExit
                            ? QStringLiteral("%1 failed (exit code %2).").arg(step.name).arg(exitCode)
                            : QStringLiteral("%1 crashed.").arg(step.name);
            ++errors_;
            emit diagnostic(d);
        }
        finish(false);
        return;
    }
    runNext();
}

void BuildRunner::fail(const QString& message)
{
    Diagnostic d;
    d.step = failedStep_;
    d.message = message;
    ++errors_;
    emit output(message, true);
    emit diagnostic(d);
    finish(false);
}

void BuildRunner::finish(bool ok, bool cancelled)
{
    running_ = false;
    BuildResult r;
    r.ok = ok;
    r.cancelled = cancelled;
    r.failedStep = failedStep_;
    r.romPath = ok ? project_.romPath() : QString();
    r.errors = errors_;
    r.warnings = warnings_;
    r.millis = timer_.elapsed();
    emit finished(r);
}

} // namespace zps
