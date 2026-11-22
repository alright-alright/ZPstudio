#pragma once
// What the window needs from anything it shows in a tab (a source file, a tile map): where it lives, whether it has
// unsaved changes, and how to save it. Keeps MainWindow free of per-editor special cases.

#include <QString>

class DocumentTab {
public:
    virtual ~DocumentTab() = default;
    virtual QString documentPath() const = 0;
    virtual bool documentModified() const = 0;
    virtual bool saveDocument(QString* error) = 0;
};
