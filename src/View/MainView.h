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
    QComboBox* sdfVisualModeComboBox() const;
    QComboBox* sdfTraversalModeComboBox() const;
    QComboBox* boundsOverlayComboBox() const;
    QPushButton* startButton() const;
    QPushButton* stopButton() const;
    QPushButton* settingsButton() const;
    QPushButton* addSdfButton() const;

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
    QComboBox* m_sdfVisualModeComboBox = nullptr;
    QComboBox* m_sdfTraversalModeComboBox = nullptr;
    QComboBox* m_boundsOverlayComboBox = nullptr;
    QLabel* m_iterationLabel = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_settingsButton = nullptr;
    QPushButton* m_addSdfButton = nullptr;
    QPlainTextEdit* m_logView = nullptr;
    QSize m_pendingViewportSize;
};
