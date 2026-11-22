#include "codeeditor.h"

#include "theme.h"

#include <QFile>
#include <QFontDatabase>
#include <QPainter>
#include <QSaveFile>
#include <QTextBlock>

namespace {
class NumberArea : public QWidget {
public:
    explicit NumberArea(CodeEditor* e) : QWidget(e), editor_(e) {}
    QSize sizeHint() const override { return {editor_->lineNumberAreaWidth(), 0}; }
protected:
    void paintEvent(QPaintEvent* ev) override { editor_->paintLineNumbers(ev); }
private:
    CodeEditor* editor_;
};
} // namespace

CodeEditor::CodeEditor(const QString& path, QWidget* parent)
    : QPlainTextEdit(parent), path_(path), numbers_(new NumberArea(this))
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(4 * fontMetrics().horizontalAdvance(QLatin1Char(' ')));

    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) setPlainText(QString::fromUtf8(f.readAll()));
    document()->setModified(false);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    updateAreaWidth();
    highlightCurrentLine();
}

bool CodeEditor::save(QString* error)
{
    QSaveFile f(path_);
    const QByteArray bytes = toPlainText().toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        if (error) *error = QStringLiteral("Cannot write %1: %2").arg(path_, f.errorString());
        return false;
    }
    document()->setModified(false);
    return true;
}

void CodeEditor::goToLine(int line)
{
    const QTextBlock block = document()->findBlockByNumber(qMax(0, line - 1));
    if (!block.isValid()) return;
    QTextCursor c(block);
    setTextCursor(c);
    centerCursor();
    setFocus();
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    for (int n = qMax(1, blockCount()); n >= 10; n /= 10) ++digits;
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void CodeEditor::updateAreaWidth() { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); }

void CodeEditor::updateArea(const QRect& rect, int dy)
{
    if (dy) numbers_->scroll(0, dy);
    else numbers_->update(0, rect.y(), numbers_->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    numbers_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine()
{
    QTextEdit::ExtraSelection sel;
    sel.format.setBackground(QColor(0x1a, 0x1b, 0x3a));
    sel.format.setProperty(QTextFormat::FullWidthSelection, true);
    sel.cursor = textCursor();
    sel.cursor.clearSelection();
    setExtraSelections({sel});
}

void CodeEditor::paintLineNumbers(QPaintEvent* event)
{
    QPainter p(numbers_);
    p.fillRect(event->rect(), QColor(0x0b, 0x0c, 0x1c));
    p.setPen(QColor(theme::kMuted));
    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    for (int n = block.blockNumber(); block.isValid() && top <= event->rect().bottom(); ++n) {
        if (block.isVisible() && bottom >= event->rect().top())
            p.drawText(0, top, numbers_->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(n + 1));
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
    }
}
