#include "mapeditor.h"

#include "theme.h"
#include "tilesetview.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

using namespace zps;

MapEditor::MapEditor(const TileMap& map, QWidget* parent) : QWidget(parent), doc_(new TileMapDocument(map, this))
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Two rows: the centre of the window is narrow once the docks are open, and one row would hide its right-hand end.
    auto* bar = new QToolBar(this);            // what to paint with
    auto* viewBar = new QToolBar(this);        // how to look at it, and undo
    for (QToolBar* b : {bar, viewBar}) {
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        root->addWidget(b);
    }

    problems_ = new QLabel(this);
    problems_->setWordWrap(true);
    problems_->setTextFormat(Qt::PlainText);
    problems_->setContentsMargins(8, 4, 8, 4);
    problems_->setStyleSheet(QStringLiteral("background: #3a2f12; color: %1;").arg(QString::fromLatin1(theme::kWarn)));
    problems_->hide();
    root->addWidget(problems_);

    canvas_ = new TileCanvas(doc_, this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(canvas_);                      // not resizable: the canvas is exactly map-size x zoom
    scroll->setBackgroundRole(QPalette::Dark);
    scroll->setAlignment(Qt::AlignCenter);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(scroll);
    split->addWidget(buildSidePanel());
    split->setStretchFactor(0, 1);
    split->setSizes({700, 320});
    root->addWidget(split, 1);

    auto* foot = new QHBoxLayout;
    foot->setContentsMargins(8, 3, 8, 3);
    status_ = new QLabel(this);
    note_ = new QLabel(this);
    note_->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromLatin1(theme::kMuted)));
    foot->addWidget(status_, 1);
    foot->addWidget(note_);
    root->addLayout(foot);

    buildToolbar(bar, viewBar);

    // Pick a zoom that shows the whole map at a comfortable size.
    const int biggest = qMax(doc_->map().width(), doc_->map().height());
    canvas_->setZoom(qBound(1, 1024 / (biggest * kCellPixels), 8));
    zoomLabel_->setText(QStringLiteral("%1×").arg(canvas_->zoom()));
    canvas_->resize(canvas_->sizeHint());

    if (doc_->map().width() < kMapCells || doc_->map().height() < kMapCells)
        note_->setText(tr("A map smaller than 64×64 repeats across the console's map."));

    connect(canvas_, &TileCanvas::hovered, this, &MapEditor::showHover);
    connect(canvas_, &TileCanvas::picked, this, &MapEditor::picked);
    connect(doc_, &TileMapDocument::layersChanged, this, &MapEditor::refreshLayers);
    connect(doc_, &TileMapDocument::modifiedChanged, this, &MapEditor::modificationChanged);
    connect(doc_, &TileMapDocument::cellsChanged, this, [this] { problemTimer_.start(); });
    problemTimer_.setSingleShot(true);
    problemTimer_.setInterval(200);                  // after a drag settles, not on every cell
    connect(&problemTimer_, &QTimer::timeout, this, &MapEditor::refreshProblems);

    refreshLayers();
    showTileset(0);
    refreshProblems();
}

void MapEditor::buildToolbar(QToolBar* bar, QToolBar* viewBar)
{
    auto tool = [&](const QString& text, const QString& tip, const QKeySequence& key) {
        QAction* a = bar->addAction(text);
        a->setCheckable(true);
        a->setToolTip(QStringLiteral("%1 (%2)").arg(tip, key.toString()));
        a->setShortcut(key);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        tools_->addAction(a);
        return a;
    };
    tools_ = new QActionGroup(this);
    pencil_ = tool(tr("Pencil"), tr("Paint with the selected tiles; right-click erases"), QKeySequence(Qt::Key_B));
    eraser_ = tool(tr("Eraser"), tr("Clear cells"), QKeySequence(Qt::Key_E));
    fill_ = tool(tr("Fill"), tr("Flood-fill a connected area"), QKeySequence(Qt::Key_G));
    rect_ = tool(tr("Rectangle"), tr("Fill a rectangle"), QKeySequence(Qt::Key_R));
    picker_ = tool(tr("Picker"), tr("Pick the tile under the pointer"), QKeySequence(Qt::Key_I));
    pencil_->setChecked(true);
    connect(tools_, &QActionGroup::triggered, this, [this](QAction* a) {
        canvas_->setTool(a == pencil_ ? TileCanvas::Tool::Pencil : a == eraser_ ? TileCanvas::Tool::Eraser
                         : a == fill_ ? TileCanvas::Tool::Fill : a == rect_ ? TileCanvas::Tool::Rect : TileCanvas::Tool::Picker);
    });

    bar->addSeparator();
    auto toggle = [&](QToolBar* where, const QString& text, const QString& tip, const QKeySequence& key, bool on) {
        QAction* a = where->addAction(text);
        a->setCheckable(true);
        a->setChecked(on);
        a->setToolTip(key.isEmpty() ? tip : QStringLiteral("%1 (%2)").arg(tip, key.toString()));
        if (!key.isEmpty()) {
            a->setShortcut(key);
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        }
        return a;
    };
    flipH_ = toggle(bar, tr("Flip H"), tr("Paint mirrored left-to-right"), QKeySequence(Qt::Key_X), false);
    flipV_ = toggle(bar, tr("Flip V"), tr("Paint mirrored top-to-bottom"), QKeySequence(Qt::Key_Y), false);
    connect(flipH_, &QAction::toggled, this, &MapEditor::updateBrush);
    connect(flipV_, &QAction::toggled, this, &MapEditor::updateBrush);

    grid_ = toggle(viewBar, tr("Grid"), tr("Show the cell grid (when zoomed in)"), QKeySequence(), true);
    frame_ = toggle(viewBar, tr("Screen"), tr("Outline the 32×32 cells the console shows when this layer is not scrolled"), QKeySequence(), true);
    connect(grid_, &QAction::toggled, canvas_, &TileCanvas::setGridVisible);
    connect(frame_, &QAction::toggled, canvas_, &TileCanvas::setScreenFrameVisible);

    viewBar->addSeparator();
    QAction* out = viewBar->addAction(tr("−"), this, [this] { changeZoom(-1); });
    out->setToolTip(tr("Zoom out"));
    zoomLabel_ = new QLabel(this);
    zoomLabel_->setMinimumWidth(32);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    viewBar->addWidget(zoomLabel_);
    QAction* in = viewBar->addAction(tr("+"), this, [this] { changeZoom(+1); });
    in->setToolTip(tr("Zoom in"));

    viewBar->addSeparator();
    QAction* undo = doc_->undoStack()->createUndoAction(this, tr("Undo"));
    QAction* redo = doc_->undoStack()->createRedoAction(this, tr("Redo"));
    undo->setShortcut(QKeySequence::Undo);
    redo->setShortcut(QKeySequence::Redo);
    undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    redo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    viewBar->addAction(undo);
    viewBar->addAction(redo);
}

QWidget* MapEditor::buildSidePanel()
{
    auto* panel = new QWidget(this);
    auto* col = new QVBoxLayout(panel);
    col->setContentsMargins(8, 8, 8, 8);

    col->addWidget(new QLabel(tr("Layer"), panel));
    auto* row = new QHBoxLayout;
    layerBox_ = new QComboBox(panel);
    addLayer_ = new QToolButton(panel);
    addLayer_->setText(QStringLiteral("＋"));
    addLayer_->setToolTip(tr("Add a layer"));
    removeLayer_ = new QToolButton(panel);
    removeLayer_->setText(QStringLiteral("−"));
    removeLayer_->setToolTip(tr("Remove this layer"));
    row->addWidget(layerBox_, 1);
    row->addWidget(addLayer_);
    row->addWidget(removeLayer_);
    col->addLayout(row);
    visible_ = new QCheckBox(tr("Show this layer"), panel);
    col->addWidget(visible_);
    connect(layerBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (refreshingLayers_ || i < 0) return;
        canvas_->setActiveLayer(i);
        visible_->blockSignals(true);
        visible_->setChecked(doc_->map().layers().value(i).visible);
        visible_->blockSignals(false);
    });
    connect(visible_, &QCheckBox::toggled, this, [this](bool on) { doc_->setLayerVisible(layerBox_->currentIndex(), on); });
    connect(addLayer_, &QToolButton::clicked, this, [this] {
        const int idx = doc_->addLayer(tr("Layer"));
        layerBox_->setCurrentIndex(idx);
    });
    connect(removeLayer_, &QToolButton::clicked, this, [this] { doc_->removeLayer(layerBox_->currentIndex()); });

    col->addWidget(new QLabel(tr("Tiles"), panel));
    tilesetBox_ = new QComboBox(panel);
    for (const Tileset& t : doc_->map().tilesets()) tilesetBox_->addItem(t.name.isEmpty() ? t.image : t.name);
    tilesetBox_->setVisible(doc_->map().tilesets().size() > 1);
    col->addWidget(tilesetBox_);
    connect(tilesetBox_, &QComboBox::currentIndexChanged, this, &MapEditor::showTileset);

    tileset_ = new TilesetView(panel);
    auto* scroll = new QScrollArea(panel);
    scroll->setWidget(tileset_);
    scroll->setWidgetResizable(false);
    scroll->setMinimumHeight(220);
    col->addWidget(scroll, 1);
    connect(tileset_, &TilesetView::brushChanged, this, &MapEditor::updateBrush);

    auto* help = new QLabel(tr("Click a tile, or drag to pick a block of tiles, then paint on the map."), panel);
    help->setWordWrap(true);
    help->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromLatin1(theme::kMuted)));
    col->addWidget(help);
    return panel;
}

void MapEditor::showTileset(int index)
{
    const auto& sets = doc_->map().tilesets();
    if (index < 0 || index >= sets.size()) return;
    const QImage& sheet = canvas_->atlas().sheet(index);
    tileset_->setTileset(sets[index], sheet);
    // Small sheets deserve a big zoom, big ones a small one, so the picker is about as wide as its panel.
    tileset_->setZoom(sheet.isNull() ? 3 : qBound(2, 320 / qMax(1, sheet.width()), 6));
    tileset_->resize(tileset_->sizeHint());
}

void MapEditor::refreshLayers()
{
    refreshingLayers_ = true;
    const int keep = layerBox_->currentIndex();
    layerBox_->clear();
    for (const TileLayer& l : doc_->map().layers()) layerBox_->addItem(l.visible ? l.name : l.name + tr(" (hidden)"));
    const int idx = qBound(0, keep < 0 ? 0 : keep, qMax(0, layerBox_->count() - 1));
    layerBox_->setCurrentIndex(idx);
    refreshingLayers_ = false;
    canvas_->setActiveLayer(idx);
    visible_->blockSignals(true);
    visible_->setChecked(doc_->map().layers().value(idx).visible);
    visible_->blockSignals(false);
    removeLayer_->setEnabled(doc_->map().layers().size() > 1);
}

void MapEditor::refreshProblems()
{
    const QStringList p = doc_->map().problems();
    problems_->setVisible(!p.isEmpty());
    problems_->setText(tr("The build will refuse this map: %1").arg(p.join(QStringLiteral("  ·  "))));
}

void MapEditor::updateBrush()
{
    Brush b = tileset_->brush();
    if (flipH_ && flipH_->isChecked()) b = b.flippedH();
    if (flipV_ && flipV_->isChecked()) b = b.flippedV();
    canvas_->setBrush(b);
}

bool MapEditor::flipH() const { return flipH_->isChecked(); }
bool MapEditor::flipV() const { return flipV_->isChecked(); }

void MapEditor::setTool(TileCanvas::Tool tool)
{
    QAction* a = tool == TileCanvas::Tool::Pencil ? pencil_ : tool == TileCanvas::Tool::Eraser ? eraser_
                 : tool == TileCanvas::Tool::Fill ? fill_ : tool == TileCanvas::Tool::Rect ? rect_ : picker_;
    a->setChecked(true);
    canvas_->setTool(tool);
}

void MapEditor::picked(quint32 gid)
{
    if (!(gid & kGidMask)) { setTool(TileCanvas::Tool::Eraser); return; }       // picked an empty cell
    // Select the tile (flips cleared, then restored from the cell), so the brush is exactly what was on the map.
    flipH_->blockSignals(true);
    flipV_->blockSignals(true);
    flipH_->setChecked(false);
    flipV_->setChecked(false);
    flipH_->blockSignals(false);
    flipV_->blockSignals(false);
    if (!tileset_->selectGid(gid)) return;
    flipH_->setChecked(gid & kFlipH);
    flipV_->setChecked(gid & kFlipV);
    setTool(TileCanvas::Tool::Pencil);
}

void MapEditor::showHover(QPoint cell)
{
    if (cell.x() < 0) { status_->clear(); return; }
    const quint32 gid = doc_->map().cell(canvas_->activeLayer(), cell.x(), cell.y());
    QString text = tr("Cell %1, %2").arg(cell.x()).arg(cell.y());
    if (gid & kGidMask) {
        const Tileset* ts = doc_->map().tilesetFor(gid);
        text += tr("  ·  tile %1").arg(ts ? int((gid & kGidMask) - quint32(ts->firstGid)) : int(gid & kGidMask));
        if (gid & kFlipH) text += tr("  ·  mirrored H");
        if (gid & kFlipV) text += tr("  ·  mirrored V");
    } else {
        text += tr("  ·  empty");
    }
    status_->setText(text);
}

void MapEditor::changeZoom(int delta)
{
    canvas_->setZoom(canvas_->zoom() + delta);
    zoomLabel_->setText(QStringLiteral("%1×").arg(canvas_->zoom()));
}

bool MapEditor::saveDocument(QString* error)
{
    if (!doc_->save(error)) return false;
    emit saved();
    return true;
}
