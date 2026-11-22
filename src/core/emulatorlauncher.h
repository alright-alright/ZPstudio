#pragma once
// Starts the emulator (zeropoint_sdl) on a built ROM and relays what it prints.
// Milestone 1 runs it as its own window in a separate process; embedding it in Studio comes later.

#include "toolchain.h"

#include <QObject>
#include <QProcess>

namespace zps {

class EmulatorLauncher : public QObject {
    Q_OBJECT
public:
    explicit EmulatorLauncher(QObject* parent = nullptr);
    ~EmulatorLauncher() override;

    // The argument list for a ROM (exposed for tests).
    static QStringList arguments(const Toolchain& tc, const QString& romPath, int scale);

    bool running() const { return proc_ && proc_->state() != QProcess::NotRunning; }
    // Returns false and sets *error if it cannot start.
    bool start(const Toolchain& tc, const QString& romPath, int scale, QString* error);
    void stop();

signals:
    void output(const QString& line);
    void stopped(int exitCode);

private:
    QProcess* proc_ = nullptr;
    QByteArray pending_;
};

} // namespace zps
