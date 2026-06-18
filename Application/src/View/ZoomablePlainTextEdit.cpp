#include "ZoomablePlainTextEdit.h"

#include "UiFonts.h"

#include <QWheelEvent>

#include <algorithm>

namespace {

constexpr int kMinEditorFontSize = 6;
constexpr int kMaxEditorFontSize = 48;

int clampEditorFontSize(int value)
{
    return std::max(kMinEditorFontSize, std::min(value, kMaxEditorFontSize));
}

} // namespace

ZoomablePlainTextEdit::ZoomablePlainTextEdit(
    FontSizeGetter getFontSize,
    FontSizeSetter setFontSize,
    QWidget* parent)
    : QPlainTextEdit(parent)
    , m_getFontSize(std::move(getFontSize))
    , m_setFontSize(std::move(setFontSize))
{
    setToolTip(QStringLiteral("Ctrl + mouse wheel to change font size"));
    applyFontSize(clampEditorFontSize(m_getFontSize()));
}

void ZoomablePlainTextEdit::applyFontSize(int pointSize)
{
    setFont(monospaceFont(clampEditorFontSize(pointSize)));
}

void ZoomablePlainTextEdit::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            const int currentSize = clampEditorFontSize(m_getFontSize());
            const int nextSize = clampEditorFontSize(currentSize + (delta > 0 ? 1 : -1));
            if (nextSize != currentSize) {
                m_setFontSize(nextSize);
                applyFontSize(nextSize);
            }
        }
        event->accept();
        return;
    }

    QPlainTextEdit::wheelEvent(event);
}
