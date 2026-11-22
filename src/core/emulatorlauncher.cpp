#include "emulatorlauncher.h"

#include <QFileInfo>

namespace zps {

EmulatorLauncher::EmulatorLauncher(QObject* parent) : QObject(parent) {}

EmulatorLauncher::~EmulatorLauncher() { stop(); }

QStringList EmulatorLauncher::arguments(const Toolchain& tc, const QString& romPath, int scale)
{
    QStringList a{romPath};
    // The real Boot ROM is what retail hardware runs; the template README says to test on it, not
    // only on the emulator's built-in HLE boot, since it hands off in a different machine state.
    if (!tc.bootRom.isEmpty())  a << QStringLiteral("--boot") << tc.bootRom;
    if (!tc.apuBios.isEmpty())  a << QStringLiteral("--apu-bios") << tc.apuBios;
    a << QStringLiteral("--scale") << QString::number(qBound(1, scale, 8)) << QStringLiteral("--speed");
    return a;
}

bool EmulatorLauncher::start(const Toolchain& tc, const QString& romPath, int scale, QString* error)
{
    stop();
    const QStringList problems = tc.runProblems();
    if (!problems.isEmpty()) {
        if (error) *error = problems.join(QLatin1Char('\n'));
        return false;
    }
    if (!QFileInfo::exists(romPath)) {
        if (error) *error = QStringLiteral("The ROM %1 has not been built yet.").arg(romPath);
        return false;
    }
    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc_, &QProcess::readyRead, this, [this] {
        pending_ += proc_->readAll();
        int nl;
        while ((nl = pending_.indexOf('\n')) >= 0) {
            emit output(QString::fromUtf8(pending_.left(nl)).trimmed());
            pending_.remove(0, nl + 1);
        }
    });
    connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) { emit stopped(code); });
    proc_->start(tc.emulator, arguments(tc, romPath, scale));
    if (!proc_->waitForStarted(5000)) {
        if (error) *error = QStringLiteral("Could not start %1: %2").arg(tc.emulator, proc_->errorString());
        proc_->deleteLater();
        proc_ = nullptr;
        return false;
    }
    return true;
}

void EmulatorLauncher::stop()
{
    if (!proc_) return;
    QProcess* p = proc_;
    proc_ = nullptr;
    p->disconnect(this);
    if (p->state() != QProcess::NotRunning) {
        p->terminate();
        if (!p->waitForFinished(2000)) p->kill();
    }
    delete p;
}

} // namespace zps
