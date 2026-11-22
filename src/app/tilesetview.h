#pragma once
// The tile sheet to pick from: click a tile, or drag a rectangle of them, to choose what to paint with.

#include "tilemapedit.h"

#include <QImage>
#include <QWidget>

class TilesetView : public QWidget {
    Q_OBJECT
public:
    explicit TilesetView(QWidget* parent = nullptr);

    void setTileset(const zps::Tileset& tileset, const QImage& sheet);
    void setZoom(int zoom);                         // whole pixels per sheet pixel
    int zoom() const { return zoom_; }

    // The selection as a brush (flip bits not applied: the editor's flip toggles do that).
    zps::Brush brush() const;
    // Selects the single tile with this gid (a picked cell); flip bits are ignored. False if it is not in this sheet.
    bool selectGid(quint32 gid);

    QSize sizeHint() const override;

signals:
    void brushChanged();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QPoint tileAt(QPoint pos) const;                // (column, row), clamped onto the sheet
    QRect tilePixels(int column, int row) const;    // in sheet pixels

    zps::Tileset ts_;
    QImage sheet_;
    int zoom_ = 3;
    QPoint anchor_{0, 0}, cursor_{0, 0};            // selection corners, in tiles
    bool dragging_ = false;
};
