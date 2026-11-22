#include "linemap.h"

namespace zps {

MappedLine mapPreprocessedLine(const QStringList& preprocessed, int preprocessedLine, const QStringList& source)
{
    MappedLine r;
    if (preprocessedLine < 1 || preprocessedLine > preprocessed.size()) return r;
    r.text = preprocessed[preprocessedLine - 1].trimmed();
    if (r.text.isEmpty()) return r;

    int found = 0;
    for (int i = 0; i < source.size(); ++i) {
        if (source[i].trimmed() == r.text) {
            if (++found > 1) { r.line = 0; return r; }     // ambiguous: better no location than a wrong one
            r.line = i + 1;
        }
    }
    return r;
}

} // namespace zps
