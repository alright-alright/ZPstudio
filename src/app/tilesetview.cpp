#include "tilesetview.h"

#include "theme.h"

#include <QMouseEvent>
#include <QPainter>

using namespace zps;

TilesetView::TilesetView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(false);
}

void TilesetView::setTileset(const Tileset& tileset, const QImage& sheet)
{
    ts_ = tileset;
    sheet_ = sheet;
    anchor_ = cursor_ = QPoint(0, 0);
    updateGeometry();
    update();
    emit brushChanged();
}

void TilesetView::setZoom(int zoom)
{
    zoom_ = qBound(1, zoom, 8);
    updateGeometry();
    update();
}

QSize TilesetView::sizeHint() const { return sheet_.isNull() ? QSize(160, 64) : sheet_.size() * zoom_; }

QRect TilesetView::tilePixels(int column, int row) const
{
    return QRect(ts_.margin + column * (ts_.tileWidth + ts_.spacing), ts_.margin + row * (ts_.tileHeight + ts_.spacing),
                 ts_.tileWidth, ts_.tileHeight);
}

QPoint TilesetView::tileAt(QPoint pos) const
{
    const int cols = qMax(1, ts_.columns);
    const int rows = qMax(1, (ts_.tileCount + cols - 1) / cols);
    const int x = (pos.x() / zoom_ - ts_.margin) / (ts_.tileWidth + ts_.spacing);
    const int y = (pos.y() / zoom_ - ts_.margin) / (ts_.tileHeight + ts_.spacing);
    return {qBound(0, x, cols - 1), qBound(0, y, rows - 1)};
}

Brush TilesetView::brush() const
{
    const int cols = qMax(1, ts_.columns);
    const int c0 = qMin(anchor_.x(), cursor_.x()), c1 = qMax(anchor_.x(), cursor_.x());
    const int r0 = qMin(anchor_.y(), cursor_.y()), r1 = qMax(anchor_.y(), cursor_.y());
    Brush b;
    b.w = c1 - c0 + 1;
    b.h = r1 - r0 + 1;
    b.gids.resize(b.w * b.h);
    for (int r = r0; r <= r1; ++r)
        for (int c = c0; c <= c1; ++c) {
            const int local = r * cols + c;
            b.gids[(r - r0) * b.w + (c - c0)] = local < ts_.tileCount ? quint32(ts_.firstGid + local) : 0u;
        }
    return b;
}

bool TilesetView::selectGid(quint32 gid)
{
    const quint32 g = gid & kGidMask;
    if (!ts_.contains(g)) return false;
    const int local = int(g) - ts_.firstGid;
    const int cols = qMax(1, ts_.columns);
    anchor_ = cursor_ = QPoint(local % cols, local / cols);
    update();
    emit brushChanged();
    return true;
}

void TilesetView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || sheet_.isNull()) return;
    anchor_ = cursor_ = tileAt(e->position().toPoint());
    dragging_ = true;
    update();
    emit brushChanged();
}

void TilesetView::mouseMoveEvent(QMouseEvent* e)
{
    if (!dragging_) return;
    const QPoint t = tileAt(e->position().toPoint());
    if (t == cursor_) return;
    cursor_ = t;
    update();
    emit brushChanged();
}

void TilesetView::mouseReleaseEvent(QMouseEvent*) { dragging_ = false; }

void TilesetView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0b, 0x0c, 0x1c));
    if (sheet_.isNull()) {
        p.setPen(QColor(theme::kMuted));
        p.drawText(rect(), Qt::AlignCenter, tr("The tile sheet could not be read."));
        return;
    }
    p.drawImage(QRect(QPoint(0, 0), sheet_.size() * zoom_), sheet_);

    // Tile grid.
    p.setPen(QColor(255, 255, 255, 40));
    const int cols = qMax(1, ts_.columns);
    const int rows = qMax(1, (ts_.tileCount + cols - 1) / cols);
    for (int c = 0; c <= cols; ++c) p.drawLine(tilePixels(c, 0).left() * zoom_, 0, tilePixels(c, 0).left() * zoom_, height());
    for (int r = 0; r <= rows; ++r) p.drawLine(0, tilePixels(0, r).top() * zoom_, width(), tilePixels(0, r).top() * zoom_);

    // Selection.
    const QRect a = tilePixels(qMin(anchor_.x(), cursor_.x()), qMin(anchor_.y(), cursor_.y()));
    const QRect b = tilePixels(qMax(anchor_.x(), cursor_.x()), qMax(anchor_.y(), cursor_.y()));
    const QRect sel = a.united(b);
    const QRect px(sel.left() * zoom_, sel.top() * zoom_, sel.width() * zoom_, sel.height() * zoom_);
    p.fillRect(px, QColor(139, 92, 246, 60));
    p.setPen(QPen(QColor(theme::kAccent), 2));
    p.drawRect(px.adjusted(1, 1, -1, -1));
}
