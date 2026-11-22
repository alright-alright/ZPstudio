#include "projectcreator.h"

#include <QDir>
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

void fakeTemplate(const QString& root)
{
    touch(root + "/src/main.c", "int main(){return 0;}");
    touch(root + "/assets/assets.json", "{}");
    touch(root + "/assets/ground.png");
    touch(root + "/Makefile");                 // must not be copied
    touch(root + "/build/game.zpb");           // build output must not be copied
    touch(root + "/tools/make_art.py");
}

} // namespace

class TstProject : public QObject {
    Q_OBJECT
private slots:
    void configRoundTrip()
    {
        QTemporaryDir d;
        ProjectConfig c;
        c.dir = d.path();
        c.name = "demo_1";
        c.title = "DEMO ONE";
        c.developer = "AW";
        QString err;
        QVERIFY(c.save(&err));
        ProjectConfig back;
        QVERIFY2(ProjectConfig::load(d.path(), &back, &err), qPrintable(err));
        QCOMPARE(back.name, QStringLiteral("demo_1"));
        QCOMPARE(back.title, QStringLiteral("DEMO ONE"));
        QCOMPARE(back.developer, QStringLiteral("AW"));
        QCOMPARE(back.romPath(), QDir::cleanPath(d.path() + "/build/demo_1.zpb"));
        QCOMPARE(back.manifestPath(), QDir::cleanPath(d.path() + "/assets/assets.json"));
    }

    void refusesPathsThatEscapeTheProject()
    {
        QTemporaryDir d;
        touch(d.path() + "/zpstudio.json", R"({"source": "../../etc/main.c"})");
        ProjectConfig c;
        QString err;
        QVERIFY(!ProjectConfig::load(d.path(), &c, &err));
        QVERIFY(err.contains("inside the project"));

        touch(d.path() + "/zpstudio.json", R"({"build": "/tmp/out"})");
        QVERIFY(!ProjectConfig::load(d.path(), &c, &err));
    }

    void refusesNamesThatWouldBreakFilenamesOrAsm()
    {
        QTemporaryDir d;
        // A quote in the name would end the quoted .include line in the generated asm (not a raw string: moc trips on it).
        touch(d.path() + "/zpstudio.json", "{\"name\": \"bad name\\\"; evil\"}");
        ProjectConfig c;
        QString err;
        QVERIFY(!ProjectConfig::load(d.path(), &c, &err));
        QVERIFY(err.contains("name"));
    }

    void newerFormatIsRefused()
    {
        QTemporaryDir d;
        touch(d.path() + "/zpstudio.json", R"({"format": 99})");
        ProjectConfig c;
        QString err;
        QVERIFY(!ProjectConfig::load(d.path(), &c, &err));
        QVERIFY(err.contains("newer"));
    }

    void createCopiesOnlySourceAndAssets()
    {
        QTemporaryDir tpl, base;
        fakeTemplate(tpl.path());
        CreateOptions o;
        o.templateDir = tpl.path();
        o.targetDir = base.path() + "/my-game";
        ProjectConfig c;
        QString err;
        QVERIFY2(createProject(o, &c, &err), qPrintable(err));
        QCOMPARE(c.name, QStringLiteral("my-game"));
        QCOMPARE(c.title, QStringLiteral("MY-GAME"));
        QVERIFY(QFile::exists(o.targetDir + "/src/main.c"));
        QVERIFY(QFile::exists(o.targetDir + "/assets/ground.png"));
        QVERIFY(QFile::exists(o.targetDir + "/zpstudio.json"));
        QVERIFY(!QFile::exists(o.targetDir + "/Makefile"));
        QVERIFY(!QFile::exists(o.targetDir + "/build"));
        QVERIFY(!QFile::exists(o.targetDir + "/tools"));
    }

    void createRefusesANonEmptyFolderAndABrokenTemplate()
    {
        QTemporaryDir tpl, base;
        fakeTemplate(tpl.path());
        touch(base.path() + "/precious.txt");
        CreateOptions o;
        o.templateDir = tpl.path();
        o.targetDir = base.path();
        ProjectConfig c;
        QString err;
        QVERIFY(!createProject(o, &c, &err));
        QVERIFY(err.contains("already has files"));
        QVERIFY(QFile::exists(base.path() + "/precious.txt"));    // untouched

        o.targetDir = base.path() + "/fresh";
        o.templateDir = base.path() + "/nope";
        QVERIFY(!createProject(o, &c, &err));
    }

    void adoptATemplateClone()
    {
        QTemporaryDir tpl;
        fakeTemplate(tpl.path());
        QVERIFY(ProjectConfig::looksLikeTemplate(tpl.path()));
        ProjectConfig c;
        QString err;
        QVERIFY2(adoptProject(tpl.path(), &c, &err), qPrintable(err));
        QVERIFY(!ProjectConfig::looksLikeTemplate(tpl.path()));   // now it has a zpstudio.json
        QCOMPARE(c.name, QStringLiteral("game"));
        QVERIFY(QFile::exists(tpl.path() + "/Makefile"));         // adopting never moves or deletes anything
    }

    void adoptRefusesRandomFolders()
    {
        QTemporaryDir d;
        ProjectConfig c;
        QString err;
        QVERIFY(!adoptProject(d.path(), &c, &err));
    }
};

QTEST_APPLESS_MAIN(TstProject)
#include "tst_project.moc"
