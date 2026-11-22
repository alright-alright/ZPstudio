#include "linemap.h"

#include <QtTest>

using namespace zps;

class TstLineMap : public QObject {
    Q_OBJECT
private slots:
    void uniqueLineIsFound()
    {
        const QStringList pp{"int hdr(void);", "int main()", "{", "  int broken( {", "}"};
        const QStringList src{"#include <x.h>", "int main()", "{", "    int broken( {", "}"};
        const MappedLine m = mapPreprocessedLine(pp, 4, src);
        QCOMPARE(m.line, 4);
        QCOMPARE(m.text, QStringLiteral("int broken( {"));       // indentation differences do not matter
    }

    void aLineThatAppearsTwiceIsNotGuessed()
    {
        const QStringList pp{"x = 1;", "}", "}"};
        const QStringList src{"x = 1;", "}", "}"};
        const MappedLine m = mapPreprocessedLine(pp, 2, src);
        QCOMPARE(m.line, 0);
        QCOMPARE(m.text, QStringLiteral("}"));                   // the text is still reported
    }

    void macroExpandedOrHeaderLinesHaveNoMatch()
    {
        const QStringList pp{"int a = 5 + 3;"};
        const QStringList src{"int a = FIVE + 3;"};
        QCOMPARE(mapPreprocessedLine(pp, 1, src).line, 0);
    }

    void badLineNumbersAreHarmless()
    {
        const QStringList pp{"a"};
        const QStringList src{"a"};
        QCOMPARE(mapPreprocessedLine(pp, 0, src).line, 0);
        QCOMPARE(mapPreprocessedLine(pp, 2, src).line, 0);
        QCOMPARE(mapPreprocessedLine({}, 1, src).line, 0);
        QCOMPARE(mapPreprocessedLine({"   "}, 1, src).line, 0);   // blank never matches
    }
};

QTEST_APPLESS_MAIN(TstLineMap)
#include "tst_linemap.moc"
