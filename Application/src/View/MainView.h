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
    QLineEdit* environmentHdrPathEdit() const;
    QPushButton* environmentHdrBrowseButton() const;
    void setEnvironmentHdrPath(const QString& path);
    QDoubleSpinBox* fStopSpinBox() const;
    QComboBox* shutterSpeedComboBox() const;
    QComboBox* isoComboBox() const;
    QLabel* exposureValueLabel() const;
    void setExposureValueText(const QString& text);
    QSpinBox* renderWidthSpinBox() const;
    QSpinBox* renderHeightSpinBox() const;
    QSpinBox* maxSamplesSpinBox() const;
    QSpinBox* previewStepsSpinBox() const;
    QComboBox* boundsOverlayComboBox() const;
    QPushButton* startButton() const;
    QPushButton* stopButton() const;
    QPushButton* settingsButton() const;
    QPushButton* addPrimitiveButton() const;
    QPushButton* exportSceneButton() const;
    QPlainTextEdit* lsystemEdit() const;
    QSpinBox* lsystemIterationsSpinBox() const;

    void setIteration(int value);
    void setRenderState(RenderAccumulationState state, int sampleCount, int budgetTotal);

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
    QDoubleSpinBox* m_fStopSpinBox = nullptr;
    QComboBox* m_shutterSpeedComboBox = nullptr;
    QComboBox* m_isoComboBox = nullptr;
    QLabel* m_exposureValueLabel = nullptr;
    QSpinBox* m_renderWidthSpinBox = nullptr;
    QSpinBox* m_renderHeightSpinBox = nullptr;
    QSpinBox* m_maxSamplesSpinBox = nullptr;
    QSpinBox* m_previewStepsSpinBox = nullptr;
    QComboBox* m_boundsOverlayComboBox = nullptr;
    QLabel* m_iterationLabel = nullptr;
    QLabel* m_renderStateLabel = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_settingsButton = nullptr;
    QPushButton* m_addPrimitiveButton = nullptr;
    QPushButton* m_exportSceneButton = nullptr;
    QPlainTextEdit* m_lsystemEdit = nullptr;
    QSpinBox* m_lsystemIterationsSpinBox = nullptr;
    QPlainTextEdit* m_logView = nullptr;
    QSize m_pendingViewportSize;
};
