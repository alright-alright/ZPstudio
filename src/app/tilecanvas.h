#pragma once
// The map itself: draws every visible layer, and turns mouse input into edits on the TileMapDocument.

#include "tileatlas.h"
#include "tilemapdoc.h"

#include <QWidget>

class TileCanvas : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Pencil, Eraser, Fill, Rect, Picker };

    explicit TileCanvas(zps::TileMapDocument* doc, QWidget* parent = nullptr);

    void setTool(Tool t);
    Tool tool() const { return tool_; }
    void setBrush(const zps::Brush& b);
    void setActiveLayer(int layer);
    int activeLayer() const { return layer_; }
    void setZoom(int zoom);                         // whole pixels per map pixel, 1..16
    int zoom() const { return zoom_; }
    void setGridVisible(bool on);
    void setScreenFrameVisible(bool on);
    bool gridVisible() const { return grid_; }

    const zps::TileAtlas& atlas() const { return atlas_; }
    QPoint cellAt(QPoint widgetPos) const;          // may be outside the map
    QSize sizeHint() const override;

signals:
    void hovered(QPoint cell);                      // (-1, -1) when the pointer leaves the map
    void picked(quint32 gid);                       // the Picker tool took the tile under the pointer

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    int cellPx() const { return zps::kCellPixels * zoom_; }
    QRect cellRect(const QRect& cells) const;       // map cells -> widget pixels
    void paintAt(QPoint cell, bool erase);          // pencil / eraser: one stamp, part of the open stroke
    void endStroke();
    zps::Brush erasingBrush() const;                // zeros, as big as the selection

    zps::TileMapDocument* doc_;
    zps::TileAtlas atlas_;
    Tool tool_ = Tool::Pencil;
    zps::Brush brush_ = zps::Brush::single(1);
    int layer_ = 0;
    int zoom_ = 3;
    bool grid_ = true, frame_ = true;

    QPoint hover_{-1, -1};
    bool strokeOpen_ = false;                       // a macro is open on the undo stack
    bool erasing_ = false;                          // the stroke in progress erases (eraser tool, or right button)
    QPoint last_{-1, -1};
    bool rectActive_ = false;
    QPoint rectFrom_, rectTo_;
};
