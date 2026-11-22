#pragma once
// Where the ZeroPoint tools live on this machine, and whether they are usable.
// Pure data + checks: no GUI, no build logic (see buildplan.h for what is run).

#include <QString>
#include <QStringList>

namespace zps {

struct Toolchain {
    QString devtoolsDir;   // a ZPdevtools checkout (executables/, include/, lib/, scripts/, ucode/ ...)
    QString python;        // interpreter command: "python3", "python" or a full path
    QString emulator;      // zeropoint_sdl (optional: only Run needs it)
    QString bootRom;       // retail bootrom.bin (optional but recommended)
    QString apuBios;       // bios_full.bin (optional)
    QString templateDir;   // a game template checkout (ZPgameTemplate); only New Project needs it

    enum class Compiler { Auto, Zpcc, Def88186cc };
    Compiler compilerPref = Compiler::Auto;

    // ---- derived paths -------------------------------------------------
    QString executable(const QString& name) const;    // <devtools>/executables/<name>[.exe]
    QString zpccPath() const;                          // <devtools>/llvm_compiler/zpcc[.exe]
    QString def88186ccPath() const;                    // <devtools>/c_compiler/def88186cc[.exe]
    QString script(const QString& relative) const;     // <devtools>/scripts/<relative>

    // The compiler a build will use: the preference if it exists, else whichever exists
    // (zpcc first). Empty string when none is built.
    QString resolvedCompiler(bool* isDos = nullptr) const;

    // Human-readable problems that stop a *build* (empty = ready to build).
    QStringList buildProblems() const;
    // Extra problems that only stop Run (no emulator).
    QStringList runProblems() const;

    // ---- discovery -----------------------------------------------------
    // Best guesses from a few well-known places next to the Studio checkout / in PATH.
    static Toolchain detect(const QString& devtoolsHint = QString());
    static QString findPython();                       // first of python3/python/py that runs, or empty
    static bool pythonHasPillow(const QString& python);
    static QString exeName(const QString& base);       // adds ".exe" on Windows

    // Settings round-trip (QSettings, group "toolchain"); the CLI and GUI share them.
    static Toolchain load();
    void save() const;
};

} // namespace zps
