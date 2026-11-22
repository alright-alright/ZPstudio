#include "tilemap.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace zps {

QRect Tileset::tileRect(int local) const
{
    const int cols = columns > 0 ? columns : 1;
    return QRect(margin + (local % cols) * (tileWidth + spacing), margin + (local / cols) * (tileHeight + spacing),
                 tileWidth, tileHeight);
}

QString TileMap::dir() const { return QFileInfo(path_).absolutePath(); }

namespace {

quint32 toGid(const QJsonValue& v) { return quint32(qint64(v.toDouble())); }

// Reads one tileset object (an embedded one, or the contents of a .tsj) into `t`.
void readTileset(const QJsonObject& o, const QString& baseDir, const QString& mapDir, Tileset* t)
{
    t->name = o.value(QStringLiteral("name")).toString();
    const QString image = o.value(QStringLiteral("image")).toString();
    // Always stored relative to the *map*, whichever file it came from.
    t->image = image.isEmpty() ? QString() : QDir(mapDir).relativeFilePath(QDir(baseDir).filePath(image));
    t->imageWidth = o.value(QStringLiteral("imagewidth")).toInt();
    t->imageHeight = o.value(QStringLiteral("imageheight")).toInt();
    t->tileWidth = o.value(QStringLiteral("tilewidth")).toInt(kCellPixels);
    t->tileHeight = o.value(QStringLiteral("tileheight")).toInt(kCellPixels);
    t->margin = o.value(QStringLiteral("margin")).toInt();
    t->spacing = o.value(QStringLiteral("spacing")).toInt();
    t->columns = o.value(QStringLiteral("columns")).toInt();
    t->tileCount = o.value(QStringLiteral("tilecount")).toInt();

    if (t->imageWidth == 0 && !t->image.isEmpty()) {      // some exporters leave the size out
        const QImage img(QDir(mapDir).filePath(t->image));
        t->imageWidth = img.width();
        t->imageHeight = img.height();
    }
    if (t->columns == 0 && t->tileWidth > 0)
        t->columns = (t->imageWidth - 2 * t->margin + t->spacing) / (t->tileWidth + t->spacing);
    if (t->tileCount == 0 && t->columns > 0 && t->tileHeight > 0)
        t->tileCount = t->columns * ((t->imageHeight - 2 * t->margin + t->spacing) / (t->tileHeight + t->spacing));
}

QJsonArray gidArray(const QVector<quint32>& data)
{
    QJsonArray a;
    for (quint32 g : data) a.append(qint64(g));        // as integers, never 2.1e+09
    return a;
}

QJsonObject tilesetJson(const QString& imageRel, QSize img, const QString& name)
{
    const int cols = img.width() / kCellPixels, rows = img.height() / kCellPixels;
    return QJsonObject{{QStringLiteral("columns"), cols},
                       {QStringLiteral("firstgid"), 1},
                       {QStringLiteral("image"), imageRel},
                       {QStringLiteral("imageheight"), img.height()},
                       {QStringLiteral("imagewidth"), img.width()},
                       {QStringLiteral("margin"), 0},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("spacing"), 0},
                       {QStringLiteral("tilecount"), cols * rows},
                       {QStringLiteral("tileheight"), kCellPixels},
                       {QStringLiteral("tilewidth"), kCellPixels}};
}

QJsonObject mapJson(int w, int h, const QJsonObject& tileset)
{
    return QJsonObject{{QStringLiteral("compressionlevel"), -1},
                       {QStringLiteral("height"), h},
                       {QStringLiteral("infinite"), false},
                       {QStringLiteral("layers"), QJsonArray()},
                       {QStringLiteral("nextlayerid"), 1},
                       {QStringLiteral("nextobjectid"), 1},
                       {QStringLiteral("orientation"), QStringLiteral("orthogonal")},
                       {QStringLiteral("renderorder"), QStringLiteral("right-down")},
                       {QStringLiteral("tiledversion"), QStringLiteral("1.10.2")},
                       {QStringLiteral("tileheight"), kCellPixels},
                       {QStringLiteral("tilesets"), QJsonArray{tileset}},
                       {QStringLiteral("tilewidth"), kCellPixels},
                       {QStringLiteral("type"), QStringLiteral("map")},
                       {QStringLiteral("version"), QStringLiteral("1.10")},
                       {QStringLiteral("width"), w}};
}

} // namespace

bool TileMap::load(const QString& path, TileMap* out, QString* error)
{
    auto fail = [&](const QString& why) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, why);
        return false;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(QStringLiteral("cannot read the file"));
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) return fail(QStringLiteral("not a Tiled JSON map (%1). Export it from Tiled as .tmj/.json.").arg(pe.errorString()));
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("infinite")).toBool())
        return fail(QStringLiteral("infinite maps are not supported (turn off 'Infinite' in the map properties)"));
    if (o.value(QStringLiteral("orientation")).toString(QStringLiteral("orthogonal")) != QLatin1String("orthogonal"))
        return fail(QStringLiteral("only orthogonal maps are supported"));

    TileMap m;
    m.path_ = QFileInfo(path).absoluteFilePath();
    m.json_ = o;
    m.width_ = o.value(QStringLiteral("width")).toInt();
    m.height_ = o.value(QStringLiteral("height")).toInt();
    m.tileWidth_ = o.value(QStringLiteral("tilewidth")).toInt(kCellPixels);
    m.tileHeight_ = o.value(QStringLiteral("tileheight")).toInt(kCellPixels);
    if (m.width_ <= 0 || m.height_ <= 0) return fail(QStringLiteral("the map has no size"));
    const QString mapDir = m.dir();

    for (const QJsonValue& tv : o.value(QStringLiteral("tilesets")).toArray()) {
        const QJsonObject to = tv.toObject();
        Tileset t;
        t.firstGid = to.value(QStringLiteral("firstgid")).toInt(1);
        if (to.contains(QStringLiteral("source"))) {           // an external .tsj
            t.source = to.value(QStringLiteral("source")).toString();
            const QString tsj = QDir(mapDir).filePath(t.source);
            QFile tf(tsj);
            const QJsonDocument td = tf.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(tf.readAll()) : QJsonDocument();
            if (!td.isObject()) return fail(QStringLiteral("cannot read the tileset %1 (external tilesets must be JSON, .tsj)").arg(t.source));
            readTileset(td.object(), QFileInfo(tsj).absolutePath(), mapDir, &t);
        } else {
            readTileset(to, mapDir, mapDir, &t);
        }
        m.tilesets_ << t;
    }

    const QJsonArray layers = o.value(QStringLiteral("layers")).toArray();
    for (int i = 0; i < layers.size(); ++i) {
        const QJsonObject lo = layers[i].toObject();
        if (lo.value(QStringLiteral("type")).toString() != QLatin1String("tilelayer")) continue;
        TileLayer l;
        l.name = lo.value(QStringLiteral("name")).toString();
        l.visible = lo.value(QStringLiteral("visible")).toBool(true);
        l.jsonIndex = i;
        if (lo.value(QStringLiteral("width")).toInt(m.width_) != m.width_ || lo.value(QStringLiteral("height")).toInt(m.height_) != m.height_)
            return fail(QStringLiteral("layer \"%1\" is not the size of the map").arg(l.name));

        const QJsonValue data = lo.value(QStringLiteral("data"));
        if (data.isArray()) {
            for (const QJsonValue& v : data.toArray()) l.data << toGid(v);
        } else if (data.isString() && lo.value(QStringLiteral("encoding")).toString() == QLatin1String("base64") &&
                   lo.value(QStringLiteral("compression")).toString().isEmpty()) {
            const QByteArray raw = QByteArray::fromBase64(data.toString().toLatin1());
            for (int p = 0; p + 4 <= raw.size(); p += 4)
                l.data << (quint32(quint8(raw[p])) | quint32(quint8(raw[p + 1])) << 8 | quint32(quint8(raw[p + 2])) << 16 |
                           quint32(quint8(raw[p + 3])) << 24);
        } else {
            return fail(QStringLiteral("layer \"%1\" stores its tiles compressed or in chunks. In Tiled: Map > Map Properties > "
                                       "Tile Layer Format = CSV, and turn off 'Infinite'.").arg(l.name));
        }
        if (l.data.size() != m.width_ * m.height_)
            return fail(QStringLiteral("layer \"%1\" has %2 cells, expected %3").arg(l.name).arg(l.data.size()).arg(m.width_ * m.height_));
        m.layers_ << l;
    }
    *out = m;
    return true;
}

bool TileMap::save(QString* error)
{
    QJsonObject o = json_;
    o[QStringLiteral("width")] = width_;
    o[QStringLiteral("height")] = height_;

    const QJsonArray src = json_.value(QStringLiteral("layers")).toArray();
    int nextId = o.value(QStringLiteral("nextlayerid")).toInt(1);
    for (const QJsonValue& v : src) nextId = qMax(nextId, v.toObject().value(QStringLiteral("id")).toInt() + 1);

    auto fill = [&](QJsonObject l, const TileLayer& tl) {
        l[QStringLiteral("name")] = tl.name;
        l[QStringLiteral("type")] = QStringLiteral("tilelayer");
        l[QStringLiteral("visible")] = tl.visible;
        l[QStringLiteral("width")] = width_;
        l[QStringLiteral("height")] = height_;
        l[QStringLiteral("x")] = 0;
        l[QStringLiteral("y")] = 0;
        if (!l.contains(QStringLiteral("opacity"))) l[QStringLiteral("opacity")] = 1;
        l[QStringLiteral("data")] = gidArray(tl.data);
        for (const char* k : {"encoding", "compression", "chunks"}) l.remove(QString::fromLatin1(k));
        return l;
    };

    // Rebuild "layers" in the file's order: tile layers we still have are updated, ones we deleted are dropped,
    // everything else (object layers, groups) is copied through unchanged; layers added here go on the end.
    QJsonArray out;
    QList<int> newIndex(layers_.size(), -1);
    for (int i = 0; i < src.size(); ++i) {
        const QJsonObject lo = src[i].toObject();
        if (lo.value(QStringLiteral("type")).toString() != QLatin1String("tilelayer")) { out.append(lo); continue; }
        for (int k = 0; k < layers_.size(); ++k)
            if (layers_[k].jsonIndex == i) {
                newIndex[k] = out.size();
                out.append(fill(lo, layers_[k]));
            }
    }
    for (int k = 0; k < layers_.size(); ++k)
        if (layers_[k].jsonIndex < 0) {
            QJsonObject l;
            l[QStringLiteral("id")] = nextId++;
            newIndex[k] = out.size();
            out.append(fill(l, layers_[k]));
        }
    o[QStringLiteral("layers")] = out;
    o[QStringLiteral("nextlayerid")] = nextId;

    QSaveFile f(path_);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson(QJsonDocument::Indented)) < 0 || !f.commit()) {
        if (error) *error = QStringLiteral("Cannot write %1: %2").arg(path_, f.errorString());
        return false;
    }
    json_ = o;
    for (int k = 0; k < layers_.size(); ++k) layers_[k].jsonIndex = newIndex[k];
    return true;
}

TileMap TileMap::createEmpty(const QString& path, const QString& imageRel, QSize imageSize, int width, int height,
                             const QString& layerName)
{
    TileMap m;
    m.path_ = QFileInfo(path).absoluteFilePath();
    m.width_ = qBound(1, width, kMapCells);
    m.height_ = qBound(1, height, kMapCells);
    m.json_ = mapJson(m.width_, m.height_, tilesetJson(imageRel, imageSize, QFileInfo(imageRel).completeBaseName()));
    Tileset t;
    t.firstGid = 1;
    t.name = QFileInfo(imageRel).completeBaseName();
    t.image = imageRel;
    t.imageWidth = imageSize.width();
    t.imageHeight = imageSize.height();
    t.columns = imageSize.width() / kCellPixels;
    t.tileCount = t.columns * (imageSize.height() / kCellPixels);
    m.tilesets_ << t;
    TileLayer l;
    l.name = layerName;
    l.data = QVector<quint32>(m.width_ * m.height_, 0);
    m.layers_ << l;
    return m;
}

TileMap TileMap::createFromImage(const QString& path, const QString& imageRel, QSize imageSize, const QString& layerName,
                                 QString* error)
{
    if (imageSize.width() % kCellPixels || imageSize.height() % kCellPixels ||
        imageSize.width() > kMapCells * kCellPixels || imageSize.height() > kMapCells * kCellPixels || imageSize.isEmpty()) {
        if (error) *error = QStringLiteral("The image is %1x%2: it has to be a multiple of 8 and at most %3x%3 to become a map.")
                                .arg(imageSize.width()).arg(imageSize.height()).arg(kMapCells * kCellPixels);
        return TileMap();
    }
    TileMap m = createEmpty(path, imageRel, imageSize, imageSize.width() / kCellPixels, imageSize.height() / kCellPixels, layerName);
    for (int i = 0; i < m.layers_[0].data.size(); ++i) m.layers_[0].data[i] = quint32(i + 1);   // cell i shows tile i
    return m;
}

int TileMap::layerIndex(const QString& name) const
{
    for (int i = 0; i < layers_.size(); ++i)
        if (layers_[i].name == name) return i;
    return -1;
}

QString TileMap::uniqueLayerName(const QString& base) const
{
    QString name = base;
    for (int n = 2; layerIndex(name) >= 0; ++n) name = QStringLiteral("%1 %2").arg(base).arg(n);
    return name;
}

quint32 TileMap::cell(int layer, int x, int y) const
{
    if (layer < 0 || layer >= layers_.size() || !inside(x, y)) return 0;
    return layers_[layer].data[y * width_ + x];
}

void TileMap::setCell(int layer, int x, int y, quint32 gid)
{
    if (layer < 0 || layer >= layers_.size() || !inside(x, y)) return;
    layers_[layer].data[y * width_ + x] = gid;
}

const Tileset* TileMap::tilesetFor(quint32 gid) const
{
    const quint32 g = gid & kGidMask;
    for (const Tileset& t : tilesets_)
        if (t.contains(g)) return &t;
    return nullptr;
}

QStringList TileMap::problems() const
{
    QStringList p;
    if (tileWidth_ != kCellPixels || tileHeight_ != kCellPixels)
        p << QStringLiteral("The map's tile size is %1x%2; ZeroPoint needs 8x8.").arg(tileWidth_).arg(tileHeight_);
    if (width_ > kMapCells || height_ > kMapCells)
        p << QStringLiteral("The map is %1x%2 cells; the console's maps are at most %3x%3.").arg(width_).arg(height_).arg(kMapCells);
    for (const Tileset& t : tilesets_) {
        if (t.image.isEmpty()) p << QStringLiteral("Tileset \"%1\" is an image collection; use one tileset image.").arg(t.name);
        if (t.tileWidth != kCellPixels || t.tileHeight != kCellPixels)
            p << QStringLiteral("Tileset \"%1\" has %2x%3 tiles; they have to be 8x8.").arg(t.name).arg(t.tileWidth).arg(t.tileHeight);
    }
    for (const TileLayer& l : layers_) {
        bool rotated = false, orphan = false;
        for (quint32 g : l.data) {
            if (g & (kFlipD | kHexRot)) rotated = true;
            if ((g & kGidMask) && !tilesetFor(g)) orphan = true;
        }
        if (rotated) p << QStringLiteral("Layer \"%1\" has a rotated tile; the console only flips horizontally and vertically.").arg(l.name);
        if (orphan) p << QStringLiteral("Layer \"%1\" uses a tile that belongs to no tileset.").arg(l.name);
    }
    return p;
}

} // namespace zps
