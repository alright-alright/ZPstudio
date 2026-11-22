#include "tilemapdoc.h"

#include <QUndoCommand>

namespace zps {

// A set of cells changed on one layer. Holds the old and new values so it can go both ways.
class CellsCommand : public QUndoCommand {
public:
    CellsCommand(TileMapDocument* doc, int layer, QVector<CellChange> before, QVector<CellChange> after, const QString& text)
        : QUndoCommand(text), doc_(doc), layer_(layer), before_(std::move(before)), after_(std::move(after)) {}
    void undo() override { doc_->applyCells(layer_, before_); }
    void redo() override { doc_->applyCells(layer_, after_); }
private:
    TileMapDocument* doc_;
    int layer_;
    QVector<CellChange> before_, after_;
};

class AddLayerCommand : public QUndoCommand {
public:
    AddLayerCommand(TileMapDocument* doc, const TileLayer& layer)
        : QUndoCommand(QObject::tr("Add layer")), doc_(doc), layer_(layer) {}
    void redo() override { doc_->insertLayer(doc_->map_.layers().size(), layer_); }
    void undo() override { doc_->takeLayer(doc_->map_.layers().size() - 1); }
private:
    TileMapDocument* doc_;
    TileLayer layer_;
};

class RemoveLayerCommand : public QUndoCommand {
public:
    RemoveLayerCommand(TileMapDocument* doc, int index)
        : QUndoCommand(QObject::tr("Remove layer")), doc_(doc), index_(index) {}
    void redo() override { saved_ = doc_->takeLayer(index_); }
    void undo() override { doc_->insertLayer(index_, saved_); }
private:
    TileMapDocument* doc_;
    int index_;
    TileLayer saved_;
};

TileMapDocument::TileMapDocument(const TileMap& map, QObject* parent) : QObject(parent), map_(map)
{
    connect(&stack_, &QUndoStack::cleanChanged, this, [this] { emitModified(); });
    connect(&stack_, &QUndoStack::indexChanged, this, [this] { emitModified(); });
}

TileMapDocument::~TileMapDocument()
{
    // The undo stack is a member, so it is destroyed after this body, and clearing itself it announces a change in
    // the modified state. By then whoever listens (a window whose tabs are being torn down) may be half-gone:
    // nothing may be signalled from here on.
    stack_.disconnect(this);
    disconnect(this, nullptr, nullptr, nullptr);
}

void TileMapDocument::emitModified()
{
    const bool m = isModified();
    if (m != lastModified_) {
        lastModified_ = m;
        emit modifiedChanged(m);
    }
}

bool TileMapDocument::save(QString* error)
{
    if (!map_.save(error)) return false;
    visibilityDirty_ = false;
    stack_.setClean();
    emitModified();
    return true;
}

bool TileMapDocument::edit(int layer, const QVector<CellChange>& changes, const QString& text)
{
    if (layer < 0 || layer >= map_.layers().size()) return false;
    QVector<CellChange> before, after;
    for (const CellChange& c : changes) {
        if (!map_.inside(c.x, c.y)) continue;
        const quint32 old = map_.cell(layer, c.x, c.y);
        if (old == c.gid) continue;
        before.push_back({c.x, c.y, old});
        after.push_back(c);
    }
    if (after.isEmpty()) return false;
    stack_.push(new CellsCommand(this, layer, before, after, text.isEmpty() ? tr("Paint") : text));
    return true;
}

void TileMapDocument::beginStroke(const QString& text) { stack_.beginMacro(text); }
void TileMapDocument::endStroke() { stack_.endMacro(); }

void TileMapDocument::applyCells(int layer, const QVector<CellChange>& changes)
{
    if (changes.isEmpty()) return;
    QRect box(changes[0].x, changes[0].y, 1, 1);
    for (const CellChange& c : changes) {
        map_.setCell(layer, c.x, c.y, c.gid);
        box = box.united(QRect(c.x, c.y, 1, 1));
    }
    emit cellsChanged(layer, box);
}

int TileMapDocument::addLayer(const QString& name)
{
    TileLayer l;
    l.name = map_.uniqueLayerName(name.isEmpty() ? tr("Layer") : name);
    l.data = QVector<quint32>(map_.cellCount(), 0);
    stack_.push(new AddLayerCommand(this, l));
    return map_.layers().size() - 1;
}

bool TileMapDocument::removeLayer(int index)
{
    if (map_.layers().size() <= 1 || index < 0 || index >= map_.layers().size()) return false;
    stack_.push(new RemoveLayerCommand(this, index));
    return true;
}

void TileMapDocument::insertLayer(int index, const TileLayer& layer)
{
    map_.layers().insert(index, layer);
    emit layersChanged();
}

TileLayer TileMapDocument::takeLayer(int index)
{
    const TileLayer l = map_.layers().takeAt(index);
    emit layersChanged();
    return l;
}

void TileMapDocument::setLayerVisible(int index, bool visible)
{
    if (index < 0 || index >= map_.layers().size() || map_.layers()[index].visible == visible) return;
    map_.layers()[index].visible = visible;
    visibilityDirty_ = true;       // saved in the file, but not an undo step
    emit layersChanged();
    emitModified();
}

} // namespace zps
