#pragma once
// The art side of a project: every image in assets.json with its size and cost, the budget meters
// (colours, tiles per bank, sprite cells), and drag-and-drop to add PNGs. Edits write assets.json.

#include "assetmanifest.h"
#include "projectconfig.h"

#include <QWidget>
#include <optional>

class QCheckBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QProgressBar;

class AssetPanel : public QWidget {
    Q_OBJECT
public:
    explicit AssetPanel(QWidget* parent = nullptr);

    void setProject(const std::optional<zps::ProjectConfig>& project);
    void refresh();                          // re-read assets.json and the images

    // Adds images (copying them into the project's assets folder) after asking what each one is.
    void addImages(const QStringList& files);

signals:
    void manifestChanged();                  // assets.json was rewritten

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void chooseAndAdd();
    void removeSelected();
    void setQuantize(bool on);
    bool saveManifest();
    QString copyIntoAssets(const QString& file);   // returns the path relative to the manifest, or empty
    bool confirmReplace(zps::AssetEntry::Role role);

    std::optional<zps::ProjectConfig> project_;
    zps::AssetManifest manifest_;
    bool manifestOk_ = false;

    QLabel* message_;
    QListWidget* list_;
    QGridLayout* budgetGrid_;
    QCheckBox* quantize_;
    QWidget* buttons_;
    QList<zps::AssetEntry> shown_;           // parallel to list_ rows
};
