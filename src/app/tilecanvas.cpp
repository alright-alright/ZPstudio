#include "tilecanvas.h"

#include "theme.h"

#include <QMouseEvent>
#include <QPainter>

using namespace zps;

TileCanvas::TileCanvas(TileMapDocument* doc, QWidget* parent) : QWidget(parent), doc_(doc), atlas_(doc->map())
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    connect(doc_, &TileMapDocument::cellsChanged, this, [this](int, const QRect& cells) { update(cellRect(cells)); });
    connect(doc_, &TileMapDocument::layersChanged, this, [this] { update(); });
}

QSize TileCanvas::sizeHint() const
{
    return {doc_->map().width() * cellPx(), doc_->map().height() * cellPx()};
}

void TileCanvas::setTool(Tool t) { tool_ = t; rectActive_ = false; update(); }
void TileCanvas::setBrush(const Brush& b) { brush_ = b; update(); }
void TileCanvas::setActiveLayer(int layer) { layer_ = layer; }
void TileCanvas::setGridVisible(bool on) { grid_ = on; update(); }
void TileCanvas::setScreenFrameVisible(bool on) { frame_ = on; update(); }

void TileCanvas::setZoom(int zoom)
{
    zoom_ = qBound(1, zoom, 16);
    updateGeometry();
    resize(sizeHint());
    update();
}

QPoint TileCanvas::cellAt(QPoint pos) const
{
    // floor division, so a pointer just left of or above the map is cell -1, not 0
    auto div = [this](int v) { return v >= 0 ? v / cellPx() : -((-v + cellPx() - 1) / cellPx()); };
    return {div(pos.x()), div(pos.y())};
}

QRect TileCanvas::cellRect(const QRect& cells) const
{
    return QRect(cells.x() * cellPx(), cells.y() * cellPx(), cells.width() * cellPx(), cells.height() * cellPx());
}

Brush TileCanvas::erasingBrush() const
{
    Brush b;
    b.w = brush_.w;
    b.h = brush_.h;
    b.gids = QVector<quint32>(b.w * b.h, 0);
    return b;
}

void TileCanvas::paintAt(QPoint cell, bool erase)
{
    const QVector<CellChange> changes = stampCells(doc_->map(), erase ? erasingBrush() : brush_, cell.x(), cell.y());
    // Open the undo macro on the first cell that actually changes, so a click that changes nothing leaves no step.
    bool changedAny = false;
    for (const CellChange& c : changes)
        if (doc_->map().cell(layer_, c.x, c.y) != c.gid) { changedAny = true; break; }
    if (!changedAny) return;
    if (!strokeOpen_) {
        doc_->beginStroke(erase ? tr("Erase") : tr("Paint"));
        strokeOpen_ = true;
    }
    doc_->edit(layer_, changes);
}

void TileCanvas::endStroke()
{
    if (strokeOpen_) doc_->endStroke();
    strokeOpen_ = false;
    last_ = QPoint(-1, -1);
}

void TileCanvas::mousePressEvent(QMouseEvent* e)
{
    const QPoint cell = cellAt(e->position().toPoint());
    if (!doc_->map().inside(cell.x(), cell.y())) return;
    setFocus();

    if (e->button() == Qt::RightButton && tool_ == Tool::Pencil) {      // quick undo of a slip
        erasing_ = true;
        last_ = cell;
        paintAt(cell, true);
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    switch (tool_) {
    case Tool::Pencil:
    case Tool::Eraser:
        erasing_ = tool_ == Tool::Eraser;
        last_ = cell;
        paintAt(cell, erasing_);
        break;
    case Tool::Fill:
        doc_->edit(layer_, fillCells(doc_->map(), layer_, cell, brush_), tr("Fill"));
        break;
    case Tool::Rect:
        rectActive_ = true;
        rectFrom_ = rectTo_ = cell;
        update();
        break;
    case Tool::Picker:
        emit picked(doc_->map().cell(layer_, cell.x(), cell.y()));
        break;
    }
}

void TileCanvas::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint cell = cellAt(e->position().toPoint());
    const QPoint shown = doc_->map().inside(cell.x(), cell.y()) ? cell : QPoint(-1, -1);
    if (shown != hover_) {
        hover_ = shown;
        emit hovered(hover_);
        update();
    }
    // `last_` is set by a press and cleared by the release: it is the stroke state, so a drag continues exactly
    // while this canvas owns a press (not on whatever button state the move event happens to report).
    if (last_.x() >= 0 && (tool_ == Tool::Pencil || tool_ == Tool::Eraser)) {
        // A fast drag skips cells: join the dots so the stroke is unbroken.
        const QVector<QPoint> pts = lineCells(last_, QPoint(qBound(0, cell.x(), doc_->map().width() - 1),
                                                            qBound(0, cell.y(), doc_->map().height() - 1)));
        for (int i = 1; i < pts.size(); ++i) paintAt(pts[i], erasing_);
        last_ = pts.last();
    }
    if (rectActive_) {
        rectTo_ = QPoint(qBound(0, cell.x(), doc_->map().width() - 1), qBound(0, cell.y(), doc_->map().height() - 1));
        update();
    }
}

void TileCanvas::mouseReleaseEvent(QMouseEvent*)
{
    if (rectActive_) {
        rectActive_ = false;
        doc_->edit(layer_, rectCells(doc_->map(), rectFrom_, rectTo_, brush_), tr("Rectangle"));
        update();
    }
    endStroke();
    erasing_ = false;
}

void TileCanvas::leaveEvent(QEvent*)
{
    hover_ = QPoint(-1, -1);
    emit hovered(hover_);
    update();
}

void TileCanvas::paintEvent(QPaintEvent* ev)
{
    QPainter p(this);
    const TileMap& map = doc_->map();
    const int cp = cellPx();
    const QRect dirty = ev->rect();
    const int x0 = qMax(0, dirty.left() / cp), x1 = qMin(map.width() - 1, dirty.right() / cp);
    const int y0 = qMax(0, dirty.top() / cp), y1 = qMin(map.height() - 1, dirty.bottom() / cp);

    // Checkerboard under everything, so an empty (see-through) cell reads as empty.
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            p.fillRect(x * cp, y * cp, cp, cp, ((x + y) & 1) ? QColor(0x2a, 0x2b, 0x4a) : QColor(0x20, 0x21, 0x3c));

    for (const TileLayer& layer : map.layers()) {
        if (!layer.visible) continue;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const quint32 gid = layer.data[y * map.width() + x];
                if (!(gid & kGidMask)) continue;
                const QImage t = atlas_.tile(gid);
                if (!t.isNull()) p.drawImage(QRect(x * cp, y * cp, cp, cp), t);
            }
    }

    if (grid_ && cp >= 12) {
        p.setPen(QColor(255, 255, 255, 28));
        for (int x = x0; x <= x1 + 1; ++x) p.drawLine(x * cp, y0 * cp, x * cp, (y1 + 1) * cp);
        for (int y = y0; y <= y1 + 1; ++y) p.drawLine(x0 * cp, y * cp, (x1 + 1) * cp, y * cp);
    }

    // The 32x32 cells (256x256 pixels) the console shows when the layer is not scrolled.
    if (frame_) {
        p.setPen(QPen(QColor(theme::kAccent), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(0, 0, qMin(32, map.width()) * cp - 1, qMin(32, map.height()) * cp - 1);
    }

    // Brush preview under the pointer, or the rectangle being dragged.
    auto preview = [&](const QVector<CellChange>& cells, bool erase) {
        p.setOpacity(0.55);
        for (const CellChange& c : cells) {
            const QRect r(c.x * cp, c.y * cp, cp, cp);
            if (erase) { p.fillRect(r, QColor(theme::kBad)); continue; }
            const QImage t = atlas_.tile(c.gid);
            if (!t.isNull()) p.drawImage(r, t);
        }
        p.setOpacity(1.0);
    };
    if (rectActive_) {
        const QVector<CellChange> cells = rectCells(map, rectFrom_, rectTo_, brush_);
        preview(cells, false);
        const QRect box = QRect(rectFrom_, rectTo_).normalized();
        p.setPen(QPen(QColor(theme::kAccent), 2));
        p.drawRect(QRect(box.x() * cp, box.y() * cp, box.width() * cp + cp, box.height() * cp + cp).adjusted(1, 1, -1, -1));
    } else if (map.inside(hover_.x(), hover_.y())) {
        if (tool_ == Tool::Pencil) preview(stampCells(map, brush_, hover_.x(), hover_.y()), false);
        else if (tool_ == Tool::Eraser) preview(stampCells(map, erasingBrush(), hover_.x(), hover_.y()), true);
        p.setPen(QPen(tool_ == Tool::Eraser ? QColor(theme::kBad) : QColor(255, 255, 255, 160), 1));
        const int bw = (tool_ == Tool::Pencil || tool_ == Tool::Eraser) ? brush_.w : 1;
        const int bh = (tool_ == Tool::Pencil || tool_ == Tool::Eraser) ? brush_.h : 1;
        p.setBrush(Qt::NoBrush);
        p.drawRect(hover_.x() * cp, hover_.y() * cp, bw * cp - 1, bh * cp - 1);
    }
}
