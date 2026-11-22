#pragma once
// The tile map editor tab: tools, the canvas, the tileset to pick from, layers, undo. One per open .tmj.

#include "documenttab.h"
#include "tilecanvas.h"

#include <QTimer>
#include <QWidget>

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QToolButton;
class TilesetView;

class MapEditor : public QWidget, public DocumentTab {
    Q_OBJECT
public:
    explicit MapEditor(const zps::TileMap& map, QWidget* parent = nullptr);

    zps::TileMapDocument* document() const { return doc_; }
    TileCanvas* canvas() const { return canvas_; }
    TilesetView* tilesetView() const { return tileset_; }
    void setTool(TileCanvas::Tool tool);
    bool flipH() const;
    bool flipV() const;

    QString documentPath() const override { return doc_->path(); }
    bool documentModified() const override { return doc_->isModified(); }
    bool saveDocument(QString* error) override;

signals:
    void modificationChanged(bool modified);
    void saved();                                   // the file on disk changed: the art budgets may too

private:
    void buildToolbar(class QToolBar* bar, class QToolBar* viewBar);
    QWidget* buildSidePanel();
    void updateBrush();
    void refreshLayers();
    void refreshProblems();
    void showTileset(int index);
    void picked(quint32 gid);
    void showHover(QPoint cell);
    void changeZoom(int delta);

    zps::TileMapDocument* doc_;
    TileCanvas* canvas_ = nullptr;
    TilesetView* tileset_ = nullptr;
    QComboBox *layerBox_ = nullptr, *tilesetBox_ = nullptr;
    QCheckBox* visible_ = nullptr;
    QToolButton *addLayer_ = nullptr, *removeLayer_ = nullptr;
    QLabel *status_ = nullptr, *problems_ = nullptr, *zoomLabel_ = nullptr, *note_ = nullptr;
    QActionGroup* tools_ = nullptr;
    QAction *pencil_ = nullptr, *eraser_ = nullptr, *fill_ = nullptr, *rect_ = nullptr, *picker_ = nullptr;
    QAction *flipH_ = nullptr, *flipV_ = nullptr, *grid_ = nullptr, *frame_ = nullptr;
    QTimer problemTimer_;
    bool refreshingLayers_ = false;
};
