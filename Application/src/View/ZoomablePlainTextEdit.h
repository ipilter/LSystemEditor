#pragma once

#include <QPlainTextEdit>

#include <functional>

class ZoomablePlainTextEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    using FontSizeGetter = std::function<int()>;
    using FontSizeSetter = std::function<void(int)>;

    explicit ZoomablePlainTextEdit(
        FontSizeGetter getFontSize,
        FontSizeSetter setFontSize,
        QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* event) override;

private:
    void applyFontSize(int pointSize);

    FontSizeGetter m_getFontSize;
    FontSizeSetter m_setFontSize;
};
