#include "projectconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace zps {

QString ProjectConfig::path(const QString& relative) const
{
    return QDir::cleanPath(QDir(dir).filePath(relative));
}

// A path that would leave the project folder (or is absolute) is never valid in the file.
static bool insideProject(const QString& rel)
{
    if (rel.isEmpty() || QDir::isAbsolutePath(rel)) return false;
    return !QDir::cleanPath(rel).startsWith(QStringLiteral(".."));
}

bool ProjectConfig::load(const QString& projectDir, ProjectConfig* out, QString* error)
{
    const QString file = QDir(projectDir).filePath(QString::fromLatin1(fileName()));
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("No %1 in %2.").arg(QString::fromLatin1(fileName()), projectDir);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("%1 is not valid JSON: %2").arg(file, pe.errorString());
        return false;
    }
    const QJsonObject o = doc.object();
    const int fmt = o.value(QStringLiteral("format")).toInt(kFormat);
    if (fmt > kFormat) {
        if (error) *error = QStringLiteral("%1 was made by a newer ZPstudio (format %2).").arg(file).arg(fmt);
        return false;
    }
    ProjectConfig c;
    c.dir = QDir(projectDir).absolutePath();
    auto str = [&](const char* k, QString& dst) {
        const QJsonValue v = o.value(QString::fromLatin1(k));
        if (v.isString() && !v.toString().isEmpty()) dst = v.toString();
    };
    str("name", c.name);
    str("title", c.title);
    str("developer", c.developer);
    str("source", c.source);
    str("assets", c.assets);
    str("build", c.build);
    for (const QString* rel : {&c.source, &c.assets, &c.build}) {
        if (!insideProject(*rel)) {
            if (error) *error = QStringLiteral("%1: \"%2\" must be a path inside the project folder.").arg(file, *rel);
            return false;
        }
    }
    // The name becomes part of file names (and of a generated .include line): keep it plain.
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (!plain.match(c.name).hasMatch()) {
        if (error) *error = QStringLiteral("%1: \"name\" may only contain letters, digits, - and _.").arg(file);
        return false;
    }
    *out = c;
    return true;
}

bool ProjectConfig::save(QString* error) const
{
    QJsonObject o;
    o[QStringLiteral("format")] = kFormat;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("title")] = title;
    o[QStringLiteral("developer")] = developer;
    o[QStringLiteral("source")] = source;
    o[QStringLiteral("assets")] = assets;
    o[QStringLiteral("build")] = build;
    QSaveFile f(QDir(dir).filePath(QString::fromLatin1(fileName())));
    if (!f.open(QIODevice::WriteOnly) ||
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented)) < 0 || !f.commit()) {
        if (error) *error = QStringLiteral("Cannot write %1: %2").arg(f.fileName(), f.errorString());
        return false;
    }
    return true;
}

bool ProjectConfig::looksLikeTemplate(const QString& projectDir)
{
    const QDir d(projectDir);
    return !QFileInfo::exists(d.filePath(QString::fromLatin1(fileName()))) &&
           QFileInfo::exists(d.filePath(QStringLiteral("src/main.c"))) &&
           QFileInfo::exists(d.filePath(QStringLiteral("assets/assets.json")));
}

} // namespace zps
