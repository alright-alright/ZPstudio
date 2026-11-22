#include "buildplan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace zps {

namespace {

// Values one step produces for a later one (the Makefile passes these through files).
struct State {
    int layersPages = 0;
};

QString hex(unsigned v) { return QStringLiteral("0x") + QString::number(v, 16); }

QString inDir(const QString& dir, const QString& rel) { return QDir::cleanPath(QDir(dir).filePath(rel)); }

} // namespace

BuildPlan BuildPlan::make(const Toolchain& tc, const ProjectConfig& pr)
{
    BuildPlan plan;
    const QStringList problems = tc.buildProblems();
    if (!problems.isEmpty()) {
        plan.error = problems.join(QLatin1Char('\n'));
        return plan;
    }
    if (!QFileInfo::exists(pr.sourcePath())) {
        plan.error = QStringLiteral("The game source %1 does not exist.").arg(pr.sourcePath());
        return plan;
    }
    if (!QFileInfo::exists(pr.manifestPath())) {
        plan.error = QStringLiteral("The art manifest %1 does not exist.").arg(pr.manifestPath());
        return plan;
    }

    bool dos = false;
    const QString compiler = tc.resolvedCompiler(&dos);

    const QString build   = pr.buildDir();
    const QString dev     = tc.devtoolsDir;
    const QString layersRaw = inDir(build, QStringLiteral("layers.raw"));
    const QString layersBin = inDir(build, QStringLiteral("layers.bin"));
    const QString assetsBin = inDir(build, QStringLiteral("assets.bin"));
    const QString mainAsm   = inDir(build, QStringLiteral("main.asm"));
    const QString gameAsm   = inDir(build, pr.name + QStringLiteral(".asm"));
    const QString gameBin   = inDir(build, pr.name + QStringLiteral(".bin"));
    const QString elf       = pr.elfPath();
    const QString rom       = pr.romPath();

    auto state = std::make_shared<State>();

    auto process = [&](const QString& name, const QString& program, const QStringList& args,
                       const QString& context = QString()) {
        BuildStep s;
        s.name = name;
        s.program = program;
        s.args = args;
        s.workDir = pr.dir;
        s.contextFile = context;
        plan.steps.push_back(std::move(s));
        return &plan.steps.back();
    };
    auto inProcess = [&](const QString& name, std::function<QString()> fn) {
        BuildStep s;
        s.name = name;
        s.action = std::move(fn);
        plan.steps.push_back(std::move(s));
    };

    inProcess(QStringLiteral("Prepare build folder"), [build] {
        return QDir().mkpath(build) ? QString() : QStringLiteral("Cannot create %1").arg(build);
    });

    // The layers microcode (scrolling BGs, sprites, window) the console's PPU runs.
    process(QStringLiteral("Generate PPU microcode"), tc.python,
            {QDir(dev).filePath(QStringLiteral("ucode/layers/gen_layers.py")), QStringLiteral("-o"), layersRaw})
        ->showOutput = false;

    // Whole 256-byte pages: the Boot ROM loader copies pages, and the game needs the count.
    inProcess(QStringLiteral("Pad microcode to pages"), [layersRaw, layersBin, state] {
        QFile in(layersRaw);
        if (!in.open(QIODevice::ReadOnly)) return QStringLiteral("Cannot read %1").arg(layersRaw);
        QByteArray d = in.readAll();
        if (d.isEmpty()) return QStringLiteral("%1 is empty").arg(layersRaw);
        d.append(QByteArray((256 - d.size() % 256) % 256, '\0'));
        QFile out(layersBin);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(d) != d.size())
            return QStringLiteral("Cannot write %1").arg(layersBin);
        state->layersPages = int(d.size() / 256);
        return QString();
    });

    // assets.json + PNGs -> assets.bin and assets.h
    process(QStringLiteral("Convert art"), tc.python,
            {tc.script(QStringLiteral("png2zp.py")), pr.manifestPath(), QStringLiteral("-o"), build,
             QStringLiteral("--rom-base"), hex(kAssetsBase)},
            pr.manifestPath());

    {
        BuildStep* s = process(QStringLiteral("Compile game"), compiler, {}, pr.sourcePath());
        s->argsFn = [dos, state, dev, build, pr] {
            QStringList a;
            if (dos) a << QStringLiteral("-DZP_CC_DOS");
            a << QStringLiteral("-DLAYERS_PAGES=%1").arg(state->layersPages)
              << QStringLiteral("-I") << QDir(dev).filePath(QStringLiteral("include"))
              << QStringLiteral("-I") << build
              << pr.sourcePath() << QStringLiteral("-o") << inDir(build, QStringLiteral("main.asm"));
            return a;
        };
        // Only def88186cc numbers its parse errors by the preprocessed text; clang already reports real lines.
        if (dos)
            s->preprocessArgsFn = [state, dev, build, pr] {
                return QStringList{QStringLiteral("-E"), QStringLiteral("-DZP_CC_DOS"),
                                   QStringLiteral("-DLAYERS_PAGES=%1").arg(state->layersPages),
                                   QStringLiteral("-I"), QDir(dev).filePath(QStringLiteral("include")),
                                   QStringLiteral("-I"), build, pr.sourcePath()};
            };
    }

    // crt0 (the entry stub), the compiled program, then the runtime it calls (peek/poke, COP, 32-bit math).
    inProcess(QStringLiteral("Assemble program layout"), [gameAsm, mainAsm, dev] {
        auto inc = [&](const QString& p) { return QStringLiteral(".include \"%1\"\n").arg(p); };
        QByteArray text = QStringLiteral(".org %1\n").arg(hex(kCpuBase)).toUtf8();
        text += inc(QDir(dev).filePath(QStringLiteral("lib/crt0_entry.asm"))).toUtf8();
        text += inc(mainAsm).toUtf8();
        for (const char* lib : {"zp_mmio", "zp_cop", "zp_long"})
            text += inc(QDir(dev).filePath(QStringLiteral("lib/%1.asm").arg(QString::fromLatin1(lib)))).toUtf8();
        QFile f(gameAsm);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(text) != text.size())
            return QStringLiteral("Cannot write %1").arg(gameAsm);
        return QString();
    });

    process(QStringLiteral("Assemble"), tc.executable(QStringLiteral("cpuasm")), {gameAsm, gameBin}, gameAsm)
        ->showOutput = false;

    process(QStringLiteral("Link ROM image"), tc.executable(QStringLiteral("zplink")),
            {QStringLiteral("-o"), elf,
             QStringLiteral("--cpu"),  gameBin   + QLatin1Char('@') + hex(kCpuBase),
             QStringLiteral("--ppu"),  layersBin + QLatin1Char('@') + hex(kLayersBase),
             QStringLiteral("--data"), assetsBin + QLatin1Char('@') + hex(kAssetsBase),
             QStringLiteral("--entry"), hex(kCpuBase)})
        ->showOutput = false;

    {
        // --codesize N --trailer 2: the console hashes the whole ROM with its BLAKE2s engine at boot.
        // Without them the real Boot ROM hashes in software, which for a ROM this size never finishes.
        BuildStep* s = process(QStringLiteral("Sign ROM (dev key)"), tc.executable(QStringLiteral("zpbuild")), {});
        s->showOutput = false;
        s->argsFn = [elf, rom, gameBin, pr] {
            const qint64 programEnd = qint64(kCpuBase) + QFileInfo(gameBin).size();
            const qint64 codeSize = (programEnd + 255) / 256 * 256;
            return QStringList{elf, QStringLiteral("-o"), rom,
                               QStringLiteral("--title"), pr.title, QStringLiteral("--dev"), pr.developer,
                               QStringLiteral("--codesize"), QString::number(codeSize),
                               QStringLiteral("--trailer"), QStringLiteral("2")};
        };
    }
    return plan;
}

} // namespace zps
