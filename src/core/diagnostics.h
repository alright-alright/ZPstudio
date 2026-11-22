#pragma once
// Turns the text the ZeroPoint tools print into structured problems the UI can list and jump to.
// Formats seen (see tests/tst_diagnostics.cpp):
//   clang (zpcc):      file:12:5: error: message
//   cpp / preprocess:  file:12: error: message
//   def88186cc parser: Parse error at line 3, column 12: syntax error      (no file: the one being compiled)
//   cpuasm:            Error: Undefined label: zz                          (no location)
//   png2zp:            png2zp: error: path: message                        (no location)

#include <QString>
#include <optional>

namespace zps {

struct Diagnostic {
    enum class Severity { Error, Warning, Note };
    Severity severity = Severity::Error;
    QString file;       // as printed (may be relative); empty when the tool gave no location
    int line = 0;       // 1-based, 0 = unknown
    int column = 0;     // 1-based, 0 = unknown
    QString message;
    QString step;       // which build step printed it (filled by the runner)
    bool preprocessedLine = false;   // `line` counts lines of the compiler's preprocessed copy (see linemap.h)

    bool hasLocation() const { return !file.isEmpty() && line > 0; }
};

// `contextFile` is the file the current step is working on; it fills in tools that omit one.
std::optional<Diagnostic> parseDiagnostic(const QString& line, const QString& contextFile = QString());

} // namespace zps
