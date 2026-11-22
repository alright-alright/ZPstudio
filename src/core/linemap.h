#pragma once
// def88186cc reports "Parse error at line N" against its preprocessed copy of the file (headers inlined,
// no #line markers), so N is not a line of the user's source. Studio asks the compiler for that copy
// (-E), takes line N's text, and looks for it in the user's file. A line that occurs exactly once maps
// back reliably; anything else is left without a location rather than pointing at the wrong line.

#include <QString>
#include <QStringList>

namespace zps {

struct MappedLine {
    int line = 0;            // 1-based line in the user's file, 0 = could not be placed
    QString text;            // the offending line as the compiler saw it (trimmed)
};

// `preprocessedLine` is 1-based into `preprocessed`; `source` is the user's file, one entry per line.
MappedLine mapPreprocessedLine(const QStringList& preprocessed, int preprocessedLine, const QStringList& source);

} // namespace zps
