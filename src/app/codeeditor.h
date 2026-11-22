#pragma once
// A small source editor: monospace, line numbers (build errors are reported by line), save, go to line.

#include <QPlainTextEdit>

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(const QString& path, QWidget* parent = nullptr);

    const QString& filePath() const { return path_; }
    bool save(QString* error);           // writes the file and clears the modified flag
    void goToLine(int line);             // 1-based; also scrolls it into view

    int lineNumberAreaWidth() const;
    void paintLineNumbers(QPaintEvent* event);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void updateAreaWidth();
    void updateArea(const QRect& rect, int dy);
    void highlightCurrentLine();

    QString path_;
    QWidget* numbers_;
};
