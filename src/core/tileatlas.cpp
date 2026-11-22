#include "tileatlas.h"

#include <QDir>

namespace zps {

const QImage TileAtlas::kNull;

void TileAtlas::load(const TileMap& map)
{
    tilesets_ = map.tilesets();
    sheets_.clear();
    cache_.clear();
    const QDir dir(map.dir());
    for (const Tileset& t : tilesets_) {
        QImage img = t.image.isEmpty() ? QImage() : QImage(dir.filePath(t.image));
        sheets_ << (img.isNull() ? img : img.convertToFormat(QImage::Format_ARGB32));
    }
}

const QImage& TileAtlas::sheet(int i) const { return i >= 0 && i < sheets_.size() ? sheets_[i] : kNull; }

QImage TileAtlas::tile(quint32 gid) const
{
    const quint32 key = gid & (kGidMask | kFlipH | kFlipV);
    if (!(key & kGidMask)) return {};
    if (auto it = cache_.constFind(key); it != cache_.constEnd()) return it.value();

    QImage out;
    for (int i = 0; i < tilesets_.size(); ++i) {
        const Tileset& t = tilesets_[i];
        if (!t.contains(key & kGidMask)) continue;
        const QRect r = t.tileRect(int((key & kGidMask) - quint32(t.firstGid)));
        if (!sheets_[i].isNull() && sheets_[i].rect().contains(r)) {
            out = sheets_[i].copy(r);
            if (key & (kFlipH | kFlipV)) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
                Qt::Orientations o;                    // flipped() is new; mirrored() is deprecated from 6.13
                if (key & kFlipH) o |= Qt::Horizontal;
                if (key & kFlipV) o |= Qt::Vertical;
                out = out.flipped(o);
#else
                out = out.mirrored(key & kFlipH, key & kFlipV);
#endif
            }
        }
        break;
    }
    cache_.insert(key, out);
    return out;
}

} // namespace zps
