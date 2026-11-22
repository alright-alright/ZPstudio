#pragma once
// An open map: the TileMap plus undo/redo and a modified flag. Every change goes through here so the editor, the
// tests and anything else see the same history.

#include "tilemapedit.h"

#include <QObject>
#include <QUndoStack>

namespace zps {

class TileMapDocument : public QObject {
    Q_OBJECT
public:
    explicit TileMapDocument(const TileMap& map, QObject* parent = nullptr);
    ~TileMapDocument() override;

    const TileMap& map() const { return map_; }
    QString path() const { return map_.path(); }
    QUndoStack* undoStack() { return &stack_; }
    bool isModified() const { return !stack_.isClean() || visibilityDirty_; }
    bool save(QString* error);                  // writes the .tmj and marks the document clean

    // Paints `changes` on a layer as one undo step (or as part of the current stroke). False if nothing changed.
    bool edit(int layer, const QVector<CellChange>& changes, const QString& text = QString());
    // A mouse drag is many edits but one undo step.
    void beginStroke(const QString& text);
    void endStroke();

    int addLayer(const QString& name);          // appended on top; returns its index
    bool removeLayer(int index);                // false for the last layer: a map keeps at least one
    void setLayerVisible(int index, bool visible);

signals:
    void cellsChanged(int layer, const QRect& cells);   // cell coordinates
    void layersChanged();
    void modifiedChanged(bool modified);

private:
    friend class CellsCommand;
    friend class AddLayerCommand;
    friend class RemoveLayerCommand;

    void applyCells(int layer, const QVector<CellChange>& changes);
    void insertLayer(int index, const TileLayer& layer);
    TileLayer takeLayer(int index);
    void emitModified();

    TileMap map_;
    QUndoStack stack_;
    bool visibilityDirty_ = false;
    bool lastModified_ = false;
};

} // namespace zps
