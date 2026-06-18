#pragma once

#include <QSize>
#include <QString>
#include <QWidget>

class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class OpenGLViewportWidget;
class QSplitter;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class ZoomablePlainTextEdit;
class QPushButton;
class QSpinBox;
class QCheckBox;

enum class RenderAccumulationState;

class MainView : public QWidget
{
    Q_OBJECT

public:
    explicit MainView(QWidget* parent = nullptr);

    OpenGLViewportWidget* viewport() const;
    QPushButton* colorButton() const;
    QLineEdit* environmentHdrPathEdit() const;
    QPushButton* environmentHdrBrowseButton() const;
    QPushButton* environmentHdrClearButton() const;
    QDoubleSpinBox* environmentIntensitySpinBox() const;
    void setEnvironmentHdrPath(const QString& path);
    QDoubleSpinBox* fStopSpinBox() const;
    QComboBox* shutterSpeedComboBox() const;
    QComboBox* isoComboBox() const;
    QLabel* exposureValueLabel() const;
    void setExposureValueText(const QString& text);
    QSpinBox* renderWidthSpinBox() const;
    QSpinBox* renderHeightSpinBox() const;
    QSpinBox* maxSamplesSpinBox() const;
    QSpinBox* minSamplesSpinBox() const;
    QDoubleSpinBox* relativeErrorThresholdSpinBox() const;
    QSpinBox* previewStepsSpinBox() const;
    QSpinBox* russianRouletteMinDepthSpinBox() const;
    QComboBox* boundsOverlayComboBox() const;
    QCheckBox* regionRenderCheckBox() const;
    QSpinBox* regionBottomLeftXSpinBox() const;
    QSpinBox* regionBottomLeftYSpinBox() const;
    QSpinBox* regionTopRightXSpinBox() const;
    QSpinBox* regionTopRightYSpinBox() const;
    QPushButton* defineRegionButton() const;
    QPushButton* startButton() const;
    QPushButton* stopButton() const;
    QPushButton* settingsButton() const;
    QPushButton* addPrimitiveButton() const;
    QPushButton* lsystemLoadButton() const;
    QPushButton* resetSceneButton() const;
    QPushButton* exportSceneButton() const;
    QPlainTextEdit* lsystemEdit() const;
    QSpinBox* lsystemIterationsSpinBox() const;

    void setIteration(int value);
    void setRenderState(RenderAccumulationState state, int sampleCount, int budgetTotal, int activePixelCount);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyPendingViewportGeometry();
    void restoreLayoutFromSettings();
    void saveLayoutToSettings();

    QSplitter* m_horizontalSplitter = nullptr;
    QSplitter* m_verticalSplitter = nullptr;
    QWidget* m_viewportHost = nullptr;
    OpenGLViewportWidget* m_viewport = nullptr;
    QPushButton* m_colorButton = nullptr;
    QLineEdit* m_environmentHdrPathEdit = nullptr;
    QPushButton* m_environmentHdrBrowseButton = nullptr;
    QPushButton* m_environmentHdrClearButton = nullptr;
    QDoubleSpinBox* m_environmentIntensitySpinBox = nullptr;
    QDoubleSpinBox* m_fStopSpinBox = nullptr;
    QComboBox* m_shutterSpeedComboBox = nullptr;
    QComboBox* m_isoComboBox = nullptr;
    QLabel* m_exposureValueLabel = nullptr;
    QSpinBox* m_renderWidthSpinBox = nullptr;
    QSpinBox* m_renderHeightSpinBox = nullptr;
    QSpinBox* m_maxSamplesSpinBox = nullptr;
    QSpinBox* m_minSamplesSpinBox = nullptr;
    QDoubleSpinBox* m_relativeErrorThresholdSpinBox = nullptr;
    QSpinBox* m_previewStepsSpinBox = nullptr;
    QSpinBox* m_russianRouletteMinDepthSpinBox = nullptr;
    QComboBox* m_boundsOverlayComboBox = nullptr;
    QCheckBox* m_regionRenderCheckBox = nullptr;
    QSpinBox* m_regionBottomLeftXSpinBox = nullptr;
    QSpinBox* m_regionBottomLeftYSpinBox = nullptr;
    QSpinBox* m_regionTopRightXSpinBox = nullptr;
    QSpinBox* m_regionTopRightYSpinBox = nullptr;
    QPushButton* m_defineRegionButton = nullptr;
    QLabel* m_iterationLabel = nullptr;
    QLabel* m_renderStateLabel = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_settingsButton = nullptr;
    QPushButton* m_addPrimitiveButton = nullptr;
    QPushButton* m_lsystemLoadButton = nullptr;
    QPushButton* m_resetSceneButton = nullptr;
    QPushButton* m_exportSceneButton = nullptr;
    ZoomablePlainTextEdit* m_lsystemEdit = nullptr;
    QSpinBox* m_lsystemIterationsSpinBox = nullptr;
    ZoomablePlainTextEdit* m_logView = nullptr;
    QSize m_pendingViewportSize;
};
