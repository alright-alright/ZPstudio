#include "buildplan.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace zps;

namespace {

void touch(const QString& path, const QByteArray& content = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

// A ZPdevtools look-alike: just the files BuildPlan checks for.
void fakeDevtools(const QString& root, bool zpcc, bool def)
{
    touch(root + "/scripts/png2zp.py");
    touch(root + "/ucode/layers/gen_layers.py");
    for (const char* t : {"cpuasm", "zplink", "zpbuild"}) touch(root + "/executables/" + Toolchain::exeName(t));
    if (zpcc) touch(root + "/llvm_compiler/" + Toolchain::exeName("zpcc"));
    if (def) touch(root + "/c_compiler/" + Toolchain::exeName("def88186cc"));
}

ProjectConfig fakeProject(const QString& root)
{
    ProjectConfig p;
    p.dir = root;
    p.name = "mygame";
    p.title = "MY GAME";
    touch(p.sourcePath());
    touch(p.manifestPath(), "{}");
    return p;
}

} // namespace

class TstBuildPlan : public QObject {
    Q_OBJECT
    Toolchain tc(const QString& dev, Toolchain::Compiler pref = Toolchain::Compiler::Auto)
    {
        Toolchain t;
        t.devtoolsDir = dev;
        t.python = Toolchain::findPython();
        t.compilerPref = pref;
        return t;
    }
    bool havePython() { return !Toolchain::findPython().isEmpty() && Toolchain::pythonHasPillow(Toolchain::findPython()); }

private slots:
    void stepsInOrder()
    {
        if (!havePython()) QSKIP("needs python3 with Pillow");
        QTemporaryDir dev, proj;
        fakeDevtools(dev.path(), false, true);
        const ProjectConfig pr = fakeProject(proj.path());
        const BuildPlan plan = BuildPlan::make(tc(dev.path()), pr);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QStringList names;
        for (const BuildStep& s : plan.steps) names << s.name;
        QCOMPARE(names, (QStringList{"Prepare build folder", "Generate PPU microcode", "Pad microcode to pages",
                                     "Convert art", "Compile game", "Assemble program layout", "Assemble",
                                     "Link ROM image", "Sign ROM (dev key)"}));
    }

    void lazyArgumentsUseEarlierSteps()
    {
        if (!havePython()) QSKIP("needs python3 with Pillow");
        QTemporaryDir dev, proj;
        fakeDevtools(dev.path(), false, true);
        const ProjectConfig pr = fakeProject(proj.path());
        BuildPlan plan = BuildPlan::make(tc(dev.path()), pr);
        QVERIFY(plan.ok());

        // Stand in for the external steps' output: 300 bytes of microcode (-> 2 pages), a 1000-byte program.
        touch(pr.buildDir() + "/layers.raw", QByteArray(300, 'u'));
        touch(pr.buildDir() + "/mygame.bin", QByteArray(1000, 'p'));

        const BuildStep *compile = nullptr, *sign = nullptr;
        for (const BuildStep& s : plan.steps) {
            if (s.action) QVERIFY2(s.action().isEmpty(), qPrintable(s.name));   // prepare, pad, layout
            if (s.name == "Compile game") compile = &s;
            if (s.name.startsWith("Sign")) sign = &s;
        }
        QVERIFY(compile && sign);

        const QStringList c = compile->argsFn();
        QVERIFY(c.contains("-DZP_CC_DOS"));               // only def88186cc exists here
        QVERIFY(c.contains("-DLAYERS_PAGES=2"));
        QVERIFY(c.contains(pr.sourcePath()));
        QCOMPARE(c.at(c.indexOf("-o") + 1), QDir::cleanPath(pr.buildDir() + "/main.asm"));

        // (0x8000 + 1000) rounded up to a whole 256-byte page.
        const QStringList s = sign->argsFn();
        QCOMPARE(s.at(s.indexOf("--codesize") + 1), QStringLiteral("33792"));
        QCOMPARE(s.at(s.indexOf("--trailer") + 1), QStringLiteral("2"));
        QCOMPARE(s.at(s.indexOf("--title") + 1), QStringLiteral("MY GAME"));
        QCOMPARE(s.at(s.indexOf("-o") + 1), pr.romPath());
        QCOMPARE(QFileInfo(pr.buildDir() + "/layers.bin").size(), qint64(512));
    }

    void layoutFileIncludesCrt0ProgramAndRuntime()
    {
        if (!havePython()) QSKIP("needs python3 with Pillow");
        QTemporaryDir dev, proj;
        fakeDevtools(dev.path(), false, true);
        const ProjectConfig pr = fakeProject(proj.path());
        const BuildPlan plan = BuildPlan::make(tc(dev.path()), pr);
        touch(pr.buildDir() + "/layers.raw", QByteArray(10, 'u'));      // the pad step reads the microcode
        for (const BuildStep& s : plan.steps)
            if (s.action) QVERIFY2(s.action().isEmpty(), qPrintable(s.name));
        QFile f(pr.buildDir() + "/mygame.asm");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.startsWith(".org 0x8000\n"));
        QVERIFY(text.contains("lib/crt0_entry.asm"));
        QVERIFY(text.indexOf("main.asm") < text.indexOf("zp_mmio.asm"));   // program before its runtime
        QVERIFY(text.contains("zp_cop.asm") && text.contains("zp_long.asm"));
    }

    void compilerChoice()
    {
        if (!havePython()) QSKIP("needs python3 with Pillow");
        QTemporaryDir dev, proj;
        fakeDevtools(dev.path(), true, true);
        const ProjectConfig pr = fakeProject(proj.path());

        auto compileOf = [&](Toolchain::Compiler pref) {
            const BuildPlan plan = BuildPlan::make(tc(dev.path(), pref), pr);
            for (const BuildStep& s : plan.steps) if (s.name == "Compile game") return s;
            return BuildStep();
        };
        QVERIFY(compileOf(Toolchain::Compiler::Auto).program.contains("zpcc"));          // preferred when built
        QVERIFY(compileOf(Toolchain::Compiler::Def88186cc).program.contains("def88186cc"));
    }

    void planExplainsAMissingToolchain()
    {
        QTemporaryDir proj;
        const ProjectConfig pr = fakeProject(proj.path());
        Toolchain empty;
        const BuildPlan plan = BuildPlan::make(empty, pr);
        QVERIFY(!plan.ok());
        QVERIFY(plan.error.contains("ZPdevtools"));
    }

    void planExplainsMissingProjectFiles()
    {
        if (!havePython()) QSKIP("needs python3 with Pillow");
        QTemporaryDir dev, proj;
        fakeDevtools(dev.path(), false, true);
        ProjectConfig pr;
        pr.dir = proj.path();     // no source, no manifest
        const BuildPlan plan = BuildPlan::make(tc(dev.path()), pr);
        QVERIFY(!plan.ok());
        QVERIFY(plan.error.contains("main.c"));
    }
};

QTEST_APPLESS_MAIN(TstBuildPlan)
#include "tst_buildplan.moc"
