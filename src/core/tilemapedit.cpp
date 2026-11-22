#include "tilemapedit.h"

#include <QQueue>
#include <QSet>
#include <cstdlib>

namespace zps {

static quint32 flip(quint32 gid, quint32 bit)
{
    if (!(gid & kGidMask)) return gid;       // an empty cell has nothing to flip
    return gid ^ bit;
}

Brush Brush::flippedH() const
{
    Brush b = *this;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) b.gids[y * w + x] = flip(gids[y * w + (w - 1 - x)], kFlipH);
    return b;
}

Brush Brush::flippedV() const
{
    Brush b = *this;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) b.gids[y * w + x] = flip(gids[(h - 1 - y) * w + x], kFlipV);
    return b;
}

QVector<CellChange> stampCells(const TileMap& map, const Brush& brush, int x, int y)
{
    QVector<CellChange> out;
    for (int by = 0; by < brush.h; ++by)
        for (int bx = 0; bx < brush.w; ++bx)
            if (map.inside(x + bx, y + by)) out.push_back({x + bx, y + by, brush.gids[by * brush.w + bx]});
    return out;
}

QVector<QPoint> lineCells(QPoint a, QPoint b)
{
    QVector<QPoint> pts;
    int x = a.x(), y = a.y();
    const int dx = std::abs(b.x() - x), dy = -std::abs(b.y() - y);
    const int sx = x < b.x() ? 1 : -1, sy = y < b.y() ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pts.push_back({x, y});
        if (x == b.x() && y == b.y()) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
    return pts;
}

QVector<CellChange> rectCells(const TileMap& map, QPoint a, QPoint b, const Brush& brush)
{
    const int x0 = qMin(a.x(), b.x()), x1 = qMax(a.x(), b.x());
    const int y0 = qMin(a.y(), b.y()), y1 = qMax(a.y(), b.y());
    QVector<CellChange> out;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (map.inside(x, y)) out.push_back({x, y, brush.at(x - x0, y - y0)});
    return out;
}

QVector<CellChange> fillCells(const TileMap& map, int layer, QPoint at, const Brush& brush)
{
    QVector<CellChange> out;
    if (!map.inside(at.x(), at.y())) return out;
    const quint32 target = map.cell(layer, at.x(), at.y());

    QSet<int> seen;
    QQueue<QPoint> todo;
    todo.enqueue(at);
    seen.insert(at.y() * map.width() + at.x());
    while (!todo.isEmpty()) {
        const QPoint p = todo.dequeue();
        out.push_back({p.x(), p.y(), brush.at(p.x() - at.x(), p.y() - at.y())});
        for (const QPoint d : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
            const QPoint n = p + d;
            if (!map.inside(n.x(), n.y())) continue;
            const int key = n.y() * map.width() + n.x();
            if (seen.contains(key) || map.cell(layer, n.x(), n.y()) != target) continue;
            seen.insert(key);
            todo.enqueue(n);
        }
    }
    return out;
}

} // namespace zps
