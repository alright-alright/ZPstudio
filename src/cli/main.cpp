// zpstudio-cli: the Studio's build pipeline without a window. Same code the GUI uses (src/core),
// so it is also what CI and headless Linux boxes run.
//
//   zpstudio-cli check
//   zpstudio-cli new   <dir> [--name N] [--title T]
//   zpstudio-cli build <projectDir>
//   zpstudio-cli assets <projectDir>
//
// Options for any command: --devtools DIR  --template DIR  --compiler zpcc|def88186cc

#include "assetanalysis.h"
#include "buildrunner.h"
#include "projectcreator.h"

#include <QCoreApplication>
#include <QDir>
#include <QTextStream>

using namespace zps;

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

int usage()
{
    err << "usage: zpstudio-cli <check | new <dir> | build <dir> | assets <dir>>\n"
           "       [--devtools DIR] [--template DIR] [--compiler zpcc|def88186cc] [--name N] [--title T]\n";
    return 2;
}

QString option(const QStringList& args, const QString& name)
{
    const int i = args.indexOf(name);
    return i >= 0 && i + 1 < args.size() ? args[i + 1] : QString();
}

// First non-option argument after the command.
QString positional(const QStringList& args)
{
    for (int i = 1; i < args.size(); ++i) {
        if (args[i].startsWith(QLatin1String("--"))) { ++i; continue; }
        return args[i];
    }
    return {};
}

bool openProject(const QString& dir, ProjectConfig* pr)
{
    QString e;
    if (ProjectConfig::load(dir, pr, &e)) return true;
    if (ProjectConfig::looksLikeTemplate(dir)) {
        out << "No zpstudio.json: adopting " << dir << " as a project.\n";
        if (adoptProject(dir, pr, &e)) return true;
    }
    err << "error: " << e << "\n";
    return false;
}

int cmdCheck(const Toolchain& tc)
{
    auto line = [](const QString& what, const QString& value) { out << QString("  %1 %2\n").arg(what, -14).arg(value.isEmpty() ? "(not set)" : value); };
    out << "Toolchain\n";
    line("devtools", tc.devtoolsDir);
    line("python", tc.python);
    line("compiler", tc.resolvedCompiler());
    line("emulator", tc.emulator);
    line("boot ROM", tc.bootRom);
    line("APU BIOS", tc.apuBios);
    line("template", tc.templateDir);
    const QStringList build = tc.buildProblems();
    const QStringList run = tc.runProblems();
    for (const QString& p : build) out << "  cannot build: " << p << "\n";
    for (const QString& p : run) out << "  cannot run:   " << p << "\n";
    out << (build.isEmpty() ? "Ready to build.\n" : "Not ready to build.\n");
    return build.isEmpty() ? 0 : 1;
}

int cmdAssets(const ProjectConfig& pr)
{
    AssetManifest m;
    QString e;
    if (!AssetManifest::load(pr.manifestPath(), &m, &e)) { err << "error: " << e << "\n"; return 1; }
    const AssetReport r = analyseAssets(m);
    for (const EntryReport& en : r.entries) {
        out << QString("  %1 %2").arg(en.entry.label(), -24).arg(en.entry.image.isEmpty() ? en.entry.tiled : en.entry.image);
        if (en.analysed) out << QString("  %1x%2  %3 colours  %4 tiles").arg(en.width).arg(en.height).arg(en.colours).arg(en.tiles);
        out << "\n";
        for (const QString& x : en.errors) out << "      error: " << x << "\n";
        for (const QString& x : en.notes) out << "      " << x << "\n";
    }
    for (const Budget& b : r.budgets)
        out << QString("  %1 %2 / %3%4%5\n").arg(b.label, -22).arg(b.used).arg(b.limit)
                   .arg(b.estimate ? " (estimate)" : "").arg(b.over() ? "  OVER" : "");
    return r.ok() ? 0 : 1;
}

int cmdBuild(const Toolchain& tc, const ProjectConfig& pr)
{
    BuildRunner runner;
    QObject::connect(&runner, &BuildRunner::stepStarted, [](const QString& n, int i, int t) {
        out << QString("[%1/%2] %3\n").arg(i).arg(t).arg(n);
    });
    QObject::connect(&runner, &BuildRunner::output, [](const QString& l, bool isErr) {
        (isErr ? err : out) << "    " << l << "\n";
        (isErr ? err : out).flush();
    });
    QList<Diagnostic> problems;
    QObject::connect(&runner, &BuildRunner::diagnostic, [&](const Diagnostic& d) { problems << d; });
    int exitCode = 1;
    QObject::connect(&runner, &BuildRunner::finished, [&](const BuildResult& r) {
        if (!problems.isEmpty()) {
            out << "\nProblems:\n";
            for (const Diagnostic& d : problems) {
                const char* sev = d.severity == Diagnostic::Severity::Error ? "error"
                                : d.severity == Diagnostic::Severity::Warning ? "warning" : "note";
                out << "  ";
                if (d.hasLocation()) out << d.file << ":" << d.line << (d.column ? ":" + QString::number(d.column) : QString()) << ": ";
                else if (!d.file.isEmpty()) out << d.file << ": ";
                out << sev << ": " << d.message << "\n";
            }
        }
        if (r.ok) out << "\nBuilt " << r.romPath << " in " << r.millis << " ms (" << r.warnings << " warnings)\n";
        else      out << "\nBuild FAILED at \"" << r.failedStep << "\": " << r.errors << " error(s)\n";
        exitCode = r.ok ? 0 : 1;
        QCoreApplication::quit();
    });
    runner.start(BuildPlan::make(tc, pr), pr);
    if (runner.running()) QCoreApplication::exec();
    return exitCode;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ZeroPoint"));
    QCoreApplication::setApplicationName(QStringLiteral("Studio"));

    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) return usage();
    const QString cmd = args[0];

    Toolchain tc = Toolchain::load();
    if (const QString d = option(args, QStringLiteral("--devtools")); !d.isEmpty()) tc = Toolchain::detect(d), tc.devtoolsDir = QDir(d).absolutePath();
    if (const QString t = option(args, QStringLiteral("--template")); !t.isEmpty()) tc.templateDir = QDir(t).absolutePath();
    if (const QString c = option(args, QStringLiteral("--compiler")); !c.isEmpty())
        tc.compilerPref = c == QLatin1String("zpcc") ? Toolchain::Compiler::Zpcc
                        : c == QLatin1String("def88186cc") ? Toolchain::Compiler::Def88186cc : Toolchain::Compiler::Auto;

    if (cmd == QLatin1String("check")) return cmdCheck(tc);

    const QString dir = positional(args);
    if (dir.isEmpty()) return usage();

    if (cmd == QLatin1String("new")) {
        CreateOptions o;
        o.templateDir = tc.templateDir;
        o.targetDir = QDir(dir).absolutePath();
        o.name = option(args, QStringLiteral("--name"));
        o.title = option(args, QStringLiteral("--title"));
        ProjectConfig pr;
        QString e;
        if (!createProject(o, &pr, &e)) { err << "error: " << e << "\n"; return 1; }
        out << "Created " << pr.dir << " (" << pr.name << ")\n";
        return 0;
    }

    ProjectConfig pr;
    if (!openProject(QDir(dir).absolutePath(), &pr)) return 1;
    if (cmd == QLatin1String("build")) return cmdBuild(tc, pr);
    if (cmd == QLatin1String("assets")) return cmdAssets(pr);
    return usage();
}
