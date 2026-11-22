#include "projectcreator.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace zps {

static bool copyTree(const QString& from, const QString& to, QString* error)
{
    if (!QDir().mkpath(to)) {
        if (error) *error = QStringLiteral("Cannot create %1").arg(to);
        return false;
    }
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir src(from);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString dest = QDir(to).filePath(src.relativeFilePath(file));
        if (!QDir().mkpath(QFileInfo(dest).absolutePath()) || !QFile::copy(file, dest)) {
            if (error) *error = QStringLiteral("Cannot copy %1 to %2").arg(file, dest);
            return false;
        }
    }
    return true;
}

static QString nameFromFolder(const QString& dir)
{
    QString n = QFileInfo(dir).fileName();
    n.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return n.isEmpty() ? QStringLiteral("game") : n;
}

bool createProject(const CreateOptions& o, ProjectConfig* out, QString* error)
{
    const QDir tpl(o.templateDir);
    if (o.templateDir.isEmpty() || !QFileInfo::exists(tpl.filePath(QStringLiteral("src/main.c"))) ||
        !QFileInfo::exists(tpl.filePath(QStringLiteral("assets/assets.json")))) {
        if (error) *error = QStringLiteral("The game template folder is not set (it needs src/main.c and assets/assets.json): %1").arg(o.templateDir);
        return false;
    }
    if (o.targetDir.isEmpty()) {
        if (error) *error = QStringLiteral("Choose a folder for the new project.");
        return false;
    }
    const QFileInfo target(o.targetDir);
    if (target.exists() && !QDir(o.targetDir).isEmpty()) {
        if (error) *error = QStringLiteral("%1 already has files in it. Pick an empty or new folder.").arg(o.targetDir);
        return false;
    }

    ProjectConfig c;
    c.dir = QDir(o.targetDir).absolutePath();
    c.name = o.name.isEmpty() ? nameFromFolder(o.targetDir) : o.name;
    c.title = o.title.isEmpty() ? c.name.toUpper().replace(QLatin1Char('_'), QLatin1Char(' ')) : o.title;
    c.developer = o.developer.isEmpty() ? QStringLiteral("ZP") : o.developer;

    if (!copyTree(tpl.filePath(QStringLiteral("src")), QDir(c.dir).filePath(QStringLiteral("src")), error) ||
        !copyTree(tpl.filePath(QStringLiteral("assets")), QDir(c.dir).filePath(QStringLiteral("assets")), error))
        return false;
    if (!c.save(error)) return false;
    // Re-read so the caller gets exactly what a later "Open" would (and so name rules are enforced once).
    return ProjectConfig::load(c.dir, out, error);
}

bool adoptProject(const QString& dir, ProjectConfig* out, QString* error)
{
    if (!ProjectConfig::looksLikeTemplate(dir)) {
        if (error) *error = QStringLiteral("%1 has no zpstudio.json and does not look like a game template (src/main.c, assets/assets.json).").arg(dir);
        return false;
    }
    ProjectConfig c;
    c.dir = QDir(dir).absolutePath();
    c.name = QStringLiteral("game");
    if (!c.save(error)) return false;
    return ProjectConfig::load(c.dir, out, error);
}

} // namespace zps
