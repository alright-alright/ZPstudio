#include "assetmanifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace zps {

QString AssetEntry::roleLabel(Role r)
{
    switch (r) {
    case Role::Background0: return QStringLiteral("Background 0");
    case Role::Background1: return QStringLiteral("Background 1");
    case Role::Sprite:      return QStringLiteral("Sprite");
    case Role::Font:        return QStringLiteral("Font");
    case Role::Frame:       return QStringLiteral("Box border");
    case Role::TextBox:     return QStringLiteral("Text box");
    }
    return {};
}

QString AssetEntry::label() const
{
    return (role == Role::Sprite || role == Role::TextBox) && !name.isEmpty()
               ? QStringLiteral("%1 \"%2\"").arg(roleLabel(role), name)
               : roleLabel(role);
}

AssetManifest AssetManifest::empty(const QString& path)
{
    AssetManifest m;
    m.path_ = path;
    return m;
}

QString AssetManifest::dir() const { return QFileInfo(path_).absolutePath(); }

bool AssetManifest::load(const QString& path, AssetManifest* out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot read %1").arg(path);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("%1 is not valid JSON (offset %2): %3").arg(path).arg(pe.offset).arg(pe.errorString());
        return false;
    }
    out->path_ = path;
    out->json_ = doc.object();
    return true;
}

bool AssetManifest::save(QString* error) const
{
    QSaveFile f(path_);
    if (!f.open(QIODevice::WriteOnly) ||
        f.write(QJsonDocument(json_).toJson(QJsonDocument::Indented)) < 0 || !f.commit()) {
        if (error) *error = QStringLiteral("Cannot write %1: %2").arg(path_, f.errorString());
        return false;
    }
    return true;
}

QList<AssetEntry> AssetManifest::entries() const
{
    QList<AssetEntry> out;
    auto background = [&](const char* key, AssetEntry::Role role) {
        const QJsonValue v = json_.value(QString::fromLatin1(key));
        if (!v.isObject()) return;
        AssetEntry e;
        e.role = role;
        e.name = AssetEntry::roleLabel(role);
        e.image = v.toObject().value(QStringLiteral("image")).toString();
        e.tiled = v.toObject().value(QStringLiteral("tiled")).toString();
        out << e;
    };
    background("bg0", AssetEntry::Role::Background0);
    background("bg1", AssetEntry::Role::Background1);

    const QJsonArray sprites = json_.value(QStringLiteral("sprites")).toArray();
    for (int i = 0; i < sprites.size(); ++i) {
        const QJsonObject o = sprites[i].toObject();
        AssetEntry e;
        e.role = AssetEntry::Role::Sprite;
        e.index = i;
        e.name = o.value(QStringLiteral("name")).toString();
        e.image = o.value(QStringLiteral("image")).toString();
        e.frameW = o.value(QStringLiteral("frame_w")).toInt(0);
        e.frameH = o.value(QStringLiteral("frame_h")).toInt(0);
        out << e;
    }

    const QJsonValue font = json_.value(QStringLiteral("font"));
    if (font.isObject() || font.isString()) {
        AssetEntry e;
        e.role = AssetEntry::Role::Font;
        e.name = AssetEntry::roleLabel(e.role);
        e.image = font.isObject() ? font.toObject().value(QStringLiteral("image")).toString() : font.toString();
        out << e;
    }
    const QJsonValue frame = json_.value(QStringLiteral("frame"));
    if (frame.isObject() || frame.isString()) {
        AssetEntry e;
        e.role = AssetEntry::Role::Frame;
        e.name = AssetEntry::roleLabel(e.role);
        e.image = frame.isObject() ? frame.toObject().value(QStringLiteral("image")).toString() : frame.toString();
        out << e;
    }
    const QJsonArray boxes = json_.value(QStringLiteral("boxes")).toArray();
    for (int i = 0; i < boxes.size(); ++i) {
        AssetEntry e;
        e.role = AssetEntry::Role::TextBox;
        e.index = i;
        e.name = boxes[i].toObject().value(QStringLiteral("name")).toString();
        out << e;
    }
    return out;
}

void AssetManifest::setBackground(AssetEntry::Role bg, const QString& imageRel)
{
    const QString key = bg == AssetEntry::Role::Background1 ? QStringLiteral("bg1") : QStringLiteral("bg0");
    QJsonObject o = json_.value(key).toObject();   // keeps flips/fill/transparent if the layer existed
    o.remove(QStringLiteral("tiled"));
    o.remove(QStringLiteral("layer"));
    o[QStringLiteral("image")] = imageRel;
    json_[key] = o;
}

void AssetManifest::setTiledBackground(AssetEntry::Role bg, const QString& tiledRel, const QString& layerName)
{
    const QString key = bg == AssetEntry::Role::Background1 ? QStringLiteral("bg1") : QStringLiteral("bg0");
    QJsonObject o = json_.value(key).toObject();
    o.remove(QStringLiteral("image"));
    o[QStringLiteral("tiled")] = tiledRel;
    o[QStringLiteral("layer")] = layerName;
    json_[key] = o;
}

void AssetManifest::addSprite(const QString& name, const QString& imageRel, int frameW, int frameH)
{
    QJsonArray a = json_.value(QStringLiteral("sprites")).toArray();
    QJsonObject o{{QStringLiteral("name"), name}, {QStringLiteral("image"), imageRel}};
    if (frameW > 0) o[QStringLiteral("frame_w")] = frameW;
    if (frameH > 0) o[QStringLiteral("frame_h")] = frameH;
    a.append(o);
    json_[QStringLiteral("sprites")] = a;
}

void AssetManifest::setFont(const QString& imageRel)
{
    QJsonObject o = json_.value(QStringLiteral("font")).toObject();
    o[QStringLiteral("image")] = imageRel;
    if (!o.contains(QStringLiteral("first"))) o[QStringLiteral("first")] = 32;
    json_[QStringLiteral("font")] = o;
}

void AssetManifest::setFrame(const QString& imageRel)
{
    json_[QStringLiteral("frame")] = imageRel;
}

void AssetManifest::setQuantize(int colours)
{
    if (colours <= 0) json_.remove(QStringLiteral("quantize"));
    else json_[QStringLiteral("quantize")] = qMin(colours, 240);
}

bool AssetManifest::remove(const AssetEntry& e)
{
    auto removeAt = [&](const char* key) {
        const QString k = QString::fromLatin1(key);
        QJsonArray a = json_.value(k).toArray();
        if (e.index < 0 || e.index >= a.size()) return false;
        a.removeAt(e.index);
        if (a.isEmpty()) json_.remove(k); else json_[k] = a;
        return true;
    };
    switch (e.role) {
    case AssetEntry::Role::Background0: return json_.remove(QStringLiteral("bg0")), true;
    case AssetEntry::Role::Background1: return json_.remove(QStringLiteral("bg1")), true;
    case AssetEntry::Role::Font:        return json_.remove(QStringLiteral("font")), true;
    case AssetEntry::Role::Frame:       return json_.remove(QStringLiteral("frame")), true;
    case AssetEntry::Role::Sprite:      return removeAt("sprites");
    case AssetEntry::Role::TextBox:     return removeAt("boxes");
    }
    return false;
}

QString AssetManifest::uniqueSpriteName(const QString& base) const
{
    // Names become C identifiers (ASSET_SPR_<NAME>_CELL): letters, digits, underscore.
    QString clean = base;
    clean.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    if (clean.isEmpty() || clean[0].isDigit()) clean.prepend(QLatin1Char('s'));
    QStringList taken;
    for (const AssetEntry& e : entries())
        if (e.role == AssetEntry::Role::Sprite) taken << e.name.toLower();
    QString name = clean;
    for (int n = 2; taken.contains(name.toLower()); ++n) name = QStringLiteral("%1_%2").arg(clean).arg(n);
    return name;
}

} // namespace zps
