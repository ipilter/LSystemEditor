#pragma once

#include "DebounceTimer.h"

#include <QSize>
#include <QWidget>

class OpenGLViewportWidget;
class QPushButton;
class QSpinBox;

class MainView : public QWidget
{
    Q_OBJECT

public:
    explicit MainView(QWidget* parent = nullptr);

    OpenGLViewportWidget* viewport() const;
    QPushButton* colorButton() const;
    QSpinBox* renderWidthSpinBox() const;
    QSpinBox* renderHeightSpinBox() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyPendingViewportGeometry();

    QWidget* m_viewportHost = nullptr;
    OpenGLViewportWidget* m_viewport = nullptr;
    QPushButton* m_colorButton = nullptr;
    QSpinBox* m_renderWidthSpinBox = nullptr;
    QSpinBox* m_renderHeightSpinBox = nullptr;
    QSize m_pendingViewportSize;
};
