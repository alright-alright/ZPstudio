#pragma once
// The build, as data. This is the template's Makefile turned into an ordered list of steps that
// does not need make, sh or cat (so it runs the same on Windows, Linux and macOS):
//
//   microcode -> art -> compile -> program asm -> assemble -> link -> sign
//
// A step is either an external program or a small in-process action. Arguments that depend on an
// earlier step's output (the microcode page count, the code size) are computed lazily.

#include "projectconfig.h"
#include "toolchain.h"

#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

namespace zps {

// Where things sit in ROM (the template's CPU_BASE / LAYERS_BASE / ASSETS_BASE).
constexpr unsigned kCpuBase    = 0x8000;
constexpr unsigned kLayersBase = 0x100000;
constexpr unsigned kAssetsBase = 0x110000;

struct BuildStep {
    QString name;                              // shown to the user: "Convert art"
    QString program;                           // empty for an in-process step
    QStringList args;
    std::function<QStringList()> argsFn;       // if set, replaces args (evaluated when the step starts)
    std::function<QString()> action;           // in-process step: returns an error message, empty = ok
    // Compiler step only: arguments that make the same program print its preprocessed text (-E). Used to
    // place a "Parse error at line N" (a line of that text) back in the user's file; see linemap.h.
    std::function<QStringList()> preprocessArgsFn;
    QString workDir;
    QString contextFile;                       // the file this step reads, for diagnostics without one
    bool showOutput = true;                    // false: only surface the output if the step fails

    bool isProcess() const { return !program.isEmpty(); }
};

struct BuildPlan {
    std::vector<BuildStep> steps;
    QString error;                             // non-empty: the plan could not be made (toolchain problem)
    bool ok() const { return error.isEmpty(); }

    static BuildPlan make(const Toolchain& tc, const ProjectConfig& project);
};

} // namespace zps
