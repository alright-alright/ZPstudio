#pragma once
// "New Tilemap": which background it is for, the tile sheet to draw with, and the size.

#include "assetmanifest.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class NewMapDialog : public QDialog {
    Q_OBJECT
public:
    struct Result {
        zps::AssetEntry::Role slot = zps::AssetEntry::Role::Background1;
        QString tilesetFile;         // absolute path of the sheet PNG
        int width = 64, height = 64;
        QString name;                // file base name: <name>.tmj
        QString layer;
    };

    explicit NewMapDialog(const QString& assetsDir, QWidget* parent = nullptr);
    Result result() const;

private:
    void browse();
    void validate();
    QString sheetPath() const;

    QString assetsDir_;
    QComboBox *slot_, *sheet_;
    QSpinBox *width_, *height_;
    QLineEdit *name_, *layer_;
    QLabel* message_;
    QPushButton* ok_;
};
