#pragma once
// A Studio project: a folder with zpstudio.json next to src/ and assets/.
// The file replaces the template's Makefile variables (GAME, TITLE, DEV, ASSETS, BUILD).

#include <QString>

namespace zps {

struct ProjectConfig {
    static constexpr int kFormat = 1;
    static const char* fileName() { return "zpstudio.json"; }

    QString dir;                 // absolute project folder (not stored in the file)
    QString name   = QStringLiteral("game");              // output basename: build/<name>.zpb
    QString title  = QStringLiteral("MY ZEROPOINT GAME"); // embedded ROM title
    QString developer = QStringLiteral("ZP");
    QString source = QStringLiteral("src/main.c");
    QString assets = QStringLiteral("assets");            // folder holding assets.json and the PNGs
    QString build  = QStringLiteral("build");

    QString path(const QString& relative) const;          // dir/relative
    QString sourcePath() const { return path(source); }
    QString assetsDir() const { return path(assets); }
    QString manifestPath() const { return path(assets + QStringLiteral("/assets.json")); }
    QString buildDir() const { return path(build); }
    QString romPath() const { return path(build + QLatin1Char('/') + name + QStringLiteral(".zpb")); }
    QString elfPath() const { return path(build + QLatin1Char('/') + name + QStringLiteral(".elf")); }

    // Returns false and sets *error on a missing/unreadable/odd file.
    static bool load(const QString& projectDir, ProjectConfig* out, QString* error);
    bool save(QString* error) const;

    // A folder that looks like the game template but has no zpstudio.json yet.
    static bool looksLikeTemplate(const QString& projectDir);
};

} // namespace zps
