#include "diagnostics.h"

#include <QRegularExpression>

namespace zps {

static Diagnostic::Severity severityOf(const QString& word)
{
    const QString w = word.toLower();
    if (w == QLatin1String("warning")) return Diagnostic::Severity::Warning;
    if (w == QLatin1String("note"))    return Diagnostic::Severity::Note;
    return Diagnostic::Severity::Error;   // "error" and "fatal error"
}

std::optional<Diagnostic> parseDiagnostic(const QString& rawLine, const QString& contextFile)
{
    const QString line = rawLine.trimmed();
    if (line.isEmpty()) return std::nullopt;

    // file:line[:col]: severity: message. The lazy file part copes with "C:\dir\x.c:3: error".
    static const QRegularExpression located(
        QStringLiteral("^(.+?):(\\d+):(?:(\\d+):)?\\s*(fatal error|error|warning|note):\\s*(.*)$"));
    if (auto m = located.match(line); m.hasMatch()) {
        Diagnostic d;
        d.file = m.captured(1);
        d.line = m.captured(2).toInt();
        d.column = m.captured(3).toInt();
        d.severity = severityOf(m.captured(4));
        d.message = m.captured(5);
        return d;
    }

    static const QRegularExpression parseErr(
        QStringLiteral("^Parse error at line (\\d+), column (\\d+):\\s*(.*)$"));
    if (auto m = parseErr.match(line); m.hasMatch()) {
        Diagnostic d;
        d.file = contextFile;
        d.line = m.captured(1).toInt();
        d.column = m.captured(2).toInt();
        d.message = m.captured(3);
        d.preprocessedLine = true;
        return d;
    }

    // No location: "Error: ...", "Warning: ..." or "tool: error: ...".
    static const QRegularExpression bare(
        QStringLiteral("^(?:[A-Za-z0-9_.-]+:\\s+)?(error|warning|fatal error)\\s*:\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = bare.match(line); m.hasMatch()) {
        Diagnostic d;
        d.severity = severityOf(m.captured(1));
        d.message = m.captured(2);
        return d;
    }
    return std::nullopt;
}

} // namespace zps
