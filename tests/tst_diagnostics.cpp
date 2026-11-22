#include "diagnostics.h"

#include <QtTest>

using namespace zps;

class TstDiagnostics : public QObject {
    Q_OBJECT
private slots:
    void clangStyle()
    {
        auto d = parseDiagnostic(QStringLiteral("/p/src/main.c:12:5: error: use of undeclared identifier 'y'"));
        QVERIFY(d);
        QCOMPARE(d->file, QStringLiteral("/p/src/main.c"));
        QCOMPARE(d->line, 12);
        QCOMPARE(d->column, 5);
        QCOMPARE(d->severity, Diagnostic::Severity::Error);
        QCOMPARE(d->message, QStringLiteral("use of undeclared identifier 'y'"));
        QVERIFY(d->hasLocation());
    }

    void warningAndNote()
    {
        auto w = parseDiagnostic(QStringLiteral("a.c:3:1: warning: unused variable"));
        QVERIFY(w);
        QCOMPARE(w->severity, Diagnostic::Severity::Warning);
        auto n = parseDiagnostic(QStringLiteral("a.c:3:1: note: declared here"));
        QVERIFY(n);
        QCOMPARE(n->severity, Diagnostic::Severity::Note);
    }

    void preprocessorHasNoColumn()
    {
        auto d = parseDiagnostic(QStringLiteral("/t/bad2.c:1: error: cannot open include file 'nothere.h'"));
        QVERIFY(d);
        QCOMPARE(d->line, 1);
        QCOMPARE(d->column, 0);
        QCOMPARE(d->message, QStringLiteral("cannot open include file 'nothere.h'"));
    }

    void windowsDrivePath()
    {
        auto d = parseDiagnostic(QStringLiteral("C:\\proj\\src\\main.c:7: error: bad"));
        QVERIFY(d);
        QCOMPARE(d->file, QStringLiteral("C:\\proj\\src\\main.c"));
        QCOMPARE(d->line, 7);
    }

    void def88186ccParseErrorUsesContextFile()
    {
        auto d = parseDiagnostic(QStringLiteral("Parse error at line 3, column 12: syntax error"), QStringLiteral("/p/src/main.c"));
        QVERIFY(d);
        QCOMPARE(d->file, QStringLiteral("/p/src/main.c"));
        QCOMPARE(d->line, 3);
        QCOMPARE(d->column, 12);
        QCOMPARE(d->message, QStringLiteral("syntax error"));
    }

    void toolsWithoutLocation()
    {
        auto a = parseDiagnostic(QStringLiteral("Error: Undefined label: zz"));
        QVERIFY(a);
        QVERIFY(!a->hasLocation());
        QCOMPARE(a->message, QStringLiteral("Undefined label: zz"));

        auto p = parseDiagnostic(QStringLiteral("png2zp: error: /x/odd.png: is RGB, not an indexed (palette) PNG."));
        QVERIFY(p);
        QCOMPARE(p->severity, Diagnostic::Severity::Error);
        QVERIFY(p->message.startsWith(QStringLiteral("/x/odd.png")));
    }

    void ordinaryOutputIsNotADiagnostic()
    {
        QVERIFY(!parseDiagnostic(QStringLiteral("Pass 1: Assembled 3 bytes, 0 labels, 0 fixups")));
        QVERIFY(!parseDiagnostic(QStringLiteral("3 errors found, not writing output")));
        QVERIFY(!parseDiagnostic(QStringLiteral("")));
        QVERIFY(!parseDiagnostic(QStringLiteral("   ")));
        QVERIFY(!parseDiagnostic(QStringLiteral("Preprocessing failed for /x/bad2.c")));
    }
};

QTEST_APPLESS_MAIN(TstDiagnostics)
#include "tst_diagnostics.moc"
