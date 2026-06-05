#pragma once

#include <QSize>
#include <QWidget>

class QComboBox;
class OpenGLViewportWidget;
class QLabel;
class QPlainTextEdit;
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
    QSpinBox* maxSamplesSpinBox() const;
    QSpinBox* previewStepsSpinBox() const;
    QComboBox* debugVisualModeComboBox() const;
    QComboBox* boundsOverlayComboBox() const;
    QPushButton* startButton() const;
    QPushButton* stopButton() const;
    QPushButton* settingsButton() const;
    QPushButton* addPrimitiveButton() const;
    QPlainTextEdit* lsystemEdit() const;
    QSpinBox* lsystemIterationsSpinBox() const;

    void setIteration(int value);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyPendingViewportGeometry();

    QWidget* m_viewportHost = nullptr;
    OpenGLViewportWidget* m_viewport = nullptr;
    QPushButton* m_colorButton = nullptr;
    QSpinBox* m_renderWidthSpinBox = nullptr;
    QSpinBox* m_renderHeightSpinBox = nullptr;
    QSpinBox* m_maxSamplesSpinBox = nullptr;
    QSpinBox* m_previewStepsSpinBox = nullptr;
    QComboBox* m_debugVisualModeComboBox = nullptr;
    QComboBox* m_boundsOverlayComboBox = nullptr;
    QLabel* m_iterationLabel = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_settingsButton = nullptr;
    QPushButton* m_addPrimitiveButton = nullptr;
    QPlainTextEdit* m_lsystemEdit = nullptr;
    QSpinBox* m_lsystemIterationsSpinBox = nullptr;
    QPlainTextEdit* m_logView = nullptr;
    QSize m_pendingViewportSize;
};
