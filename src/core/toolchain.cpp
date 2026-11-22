#include "toolchain.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace zps {

QString Toolchain::exeName(const QString& base)
{
#ifdef Q_OS_WIN
    return base + QStringLiteral(".exe");
#else
    return base;
#endif
}

QString Toolchain::executable(const QString& name) const
{
    return QDir(devtoolsDir).filePath(QStringLiteral("executables/") + exeName(name));
}

QString Toolchain::zpccPath() const
{
    return QDir(devtoolsDir).filePath(QStringLiteral("llvm_compiler/") + exeName(QStringLiteral("zpcc")));
}

QString Toolchain::def88186ccPath() const
{
    return QDir(devtoolsDir).filePath(QStringLiteral("c_compiler/") + exeName(QStringLiteral("def88186cc")));
}

QString Toolchain::script(const QString& relative) const
{
    return QDir(devtoolsDir).filePath(QStringLiteral("scripts/") + relative);
}

QString Toolchain::resolvedCompiler(bool* isDos) const
{
    const bool haveZpcc = QFileInfo::exists(zpccPath());
    const bool haveDos = QFileInfo::exists(def88186ccPath());
    bool useDos;
    switch (compilerPref) {
    case Compiler::Zpcc:       useDos = false; if (!haveZpcc) return {}; break;
    case Compiler::Def88186cc: useDos = true;  if (!haveDos) return {};  break;
    default:
        if (haveZpcc)      useDos = false;
        else if (haveDos)  useDos = true;
        else               return {};
    }
    if (isDos) *isDos = useDos;
    return useDos ? def88186ccPath() : zpccPath();
}

QStringList Toolchain::buildProblems() const
{
    QStringList p;
    if (devtoolsDir.isEmpty() || !QFileInfo::exists(script(QStringLiteral("png2zp.py")))) {
        p << QStringLiteral("ZPdevtools folder not set (or it has no scripts/png2zp.py).");
        return p;   // everything else is relative to it
    }
    for (const char* t : {"cpuasm", "zplink", "zpbuild"})
        if (!QFileInfo::exists(executable(QString::fromLatin1(t))))
            p << QStringLiteral("%1 is not built: run `make` in the ZPdevtools folder.").arg(QString::fromLatin1(t));
    if (!QFileInfo::exists(QDir(devtoolsDir).filePath(QStringLiteral("ucode/layers/gen_layers.py"))))
        p << QStringLiteral("ucode/layers/gen_layers.py is missing from ZPdevtools.");
    if (resolvedCompiler().isEmpty())
        p << QStringLiteral("No C compiler is built: run `make` in ZPdevtools/c_compiler (def88186cc) "
                            "or ZPdevtools/llvm_compiler (zpcc).");
    if (python.isEmpty())
        p << QStringLiteral("Python 3 was not found. Install it and Pillow (`pip install pillow`).");
    else if (!pythonHasPillow(python))
        p << QStringLiteral("Python is missing Pillow, which converts the art: `%1 -m pip install pillow`.").arg(python);
    return p;
}

QStringList Toolchain::runProblems() const
{
    QStringList p;
    if (emulator.isEmpty() || !QFileInfo::exists(emulator))
        p << QStringLiteral("The emulator (zeropoint_sdl) was not found. Set it in Settings.");
    if (!bootRom.isEmpty() && !QFileInfo::exists(bootRom))
        p << QStringLiteral("The Boot ROM file does not exist: %1").arg(bootRom);
    if (!apuBios.isEmpty() && !QFileInfo::exists(apuBios))
        p << QStringLiteral("The APU BIOS file does not exist: %1").arg(apuBios);
    return p;
}

static bool runsOk(const QString& program, const QStringList& args)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(3000)) return false;
    if (!p.waitForFinished(15000)) { p.kill(); return false; }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

QString Toolchain::findPython()
{
    for (const char* c : {"python3", "python"})
        if (runsOk(QString::fromLatin1(c), {QStringLiteral("-c"), QStringLiteral("import sys; sys.exit(sys.version_info < (3,))")}))
            return QString::fromLatin1(c);
    return {};
}

bool Toolchain::pythonHasPillow(const QString& python)
{
    return runsOk(python, {QStringLiteral("-c"), QStringLiteral("import PIL")});
}

Toolchain Toolchain::detect(const QString& devtoolsHint)
{
    Toolchain t;
    QStringList roots;
    if (!devtoolsHint.isEmpty()) roots << devtoolsHint;
    const QByteArray env = qgetenv("ZPSTUDIO_DEVTOOLS");
    if (!env.isEmpty()) roots << QString::fromLocal8Bit(env);
    // Siblings of the working directory (the usual ~/Code/{ZPstudio,ZPdevtools,ZeroPoint} layout).
    const QDir cwd = QDir::current();
    roots << cwd.filePath(QStringLiteral("../ZPdevtools"));
    // ...and of the folders above the program itself, so a double-clicked app (cwd = /) still finds them.
    if (QCoreApplication::instance()) {
        QDir up(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 7; ++i) {
            roots << up.filePath(QStringLiteral("ZPdevtools"));
            if (!up.cdUp()) break;
        }
    }
    for (const QString& r : roots) {
        const QString abs = QDir::cleanPath(QDir(r).absolutePath());
        if (QFileInfo::exists(QDir(abs).filePath(QStringLiteral("scripts/png2zp.py")))) { t.devtoolsDir = abs; break; }
    }

    const QString parent = t.devtoolsDir.isEmpty() ? QDir::cleanPath(cwd.filePath(QStringLiteral(".."))) : QDir::cleanPath(QDir(t.devtoolsDir).filePath(QStringLiteral("..")));
    const QString tpl = QDir(parent).filePath(QStringLiteral("ZPgameTemplate"));
    if (QFileInfo::exists(QDir(tpl).filePath(QStringLiteral("assets/assets.json")))) t.templateDir = tpl;

    const QString emu = QDir(parent).filePath(QStringLiteral("ZeroPoint/build/bin/") + exeName(QStringLiteral("zeropoint_sdl")));
    if (QFileInfo::exists(emu)) t.emulator = emu;
    else t.emulator = QStandardPaths::findExecutable(QStringLiteral("zeropoint_sdl"));

    t.python = findPython();
    return t;
}

Toolchain Toolchain::load()
{
    QSettings s;
    s.beginGroup(QStringLiteral("toolchain"));
    Toolchain t = detect(s.value(QStringLiteral("devtools")).toString());
    // A saved value wins over a guess, but only if the user actually saved one.
    auto pick = [&](const char* key, QString& field) {
        const QString v = s.value(QString::fromLatin1(key)).toString();
        if (!v.isEmpty()) field = v;
    };
    pick("devtools", t.devtoolsDir);
    pick("emulator", t.emulator);
    pick("bootRom", t.bootRom);
    pick("apuBios", t.apuBios);
    pick("template", t.templateDir);
    pick("python", t.python);
    t.compilerPref = static_cast<Compiler>(s.value(QStringLiteral("compiler"), 0).toInt());
    return t;
}

void Toolchain::save() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("toolchain"));
    s.setValue(QStringLiteral("devtools"), devtoolsDir);
    s.setValue(QStringLiteral("emulator"), emulator);
    s.setValue(QStringLiteral("bootRom"), bootRom);
    s.setValue(QStringLiteral("apuBios"), apuBios);
    s.setValue(QStringLiteral("template"), templateDir);
    s.setValue(QStringLiteral("python"), python);
    s.setValue(QStringLiteral("compiler"), static_cast<int>(compilerPref));
}

} // namespace zps
