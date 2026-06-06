#pragma once

#include <QSize>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class OpenGLViewportWidget;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

enum class RenderAccumulationState;

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
    QDoubleSpinBox* sunAzimuthSpinBox() const;
    QDoubleSpinBox* sunElevationSpinBox() const;
    QPushButton* sunColorButton() const;
    QDoubleSpinBox* sunDiskSizeSpinBox() const;
    QSpinBox* secondaryBounceSpinBox() const;
    QPushButton* loadEnvironmentButton() const;
    QPushButton* startButton() const;
    QPushButton* stopButton() const;
    QPushButton* settingsButton() const;
    QPushButton* addPrimitiveButton() const;
    QPlainTextEdit* lsystemEdit() const;
    QSpinBox* lsystemIterationsSpinBox() const;

    void setIteration(int value);
    void setRenderState(RenderAccumulationState state, int sampleCount, int budgetTotal);

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
    QDoubleSpinBox* m_sunAzimuthSpinBox = nullptr;
    QDoubleSpinBox* m_sunElevationSpinBox = nullptr;
    QPushButton* m_sunColorButton = nullptr;
    QDoubleSpinBox* m_sunDiskSizeSpinBox = nullptr;
    QSpinBox* m_secondaryBounceSpinBox = nullptr;
    QPushButton* m_loadEnvironmentButton = nullptr;
    QLabel* m_iterationLabel = nullptr;
    QLabel* m_renderStateLabel = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_settingsButton = nullptr;
    QPushButton* m_addPrimitiveButton = nullptr;
    QPlainTextEdit* m_lsystemEdit = nullptr;
    QSpinBox* m_lsystemIterationsSpinBox = nullptr;
    QPlainTextEdit* m_logView = nullptr;
    QSize m_pendingViewportSize;
};
