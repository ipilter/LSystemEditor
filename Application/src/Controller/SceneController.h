#pragma once

#include "DebounceTimer.h"

#include "RenderTypes.h"

#include <QColor>
#include <QObject>
#include <QSize>

class MainView;
class SceneModel;

class SceneController : public QObject
{
    Q_OBJECT

public:
    SceneController(SceneModel* model, MainView* view, QObject* parent = nullptr);

private:
    void onColorButtonClicked();
    void onClearColorChanged(const QColor& color);
    void onRenderSizeChanged(const QSize& size);
    void onRenderSizeSpinBoxChanged();
    void applyRenderSizeFromSpinBoxes();
    void onMaxSamplesSpinBoxChanged();
    void applyMaxSamplesFromSpinBox();
    void onMinSamplesSpinBoxChanged();
    void onRelativeErrorThresholdSpinBoxChanged();
    void onPreviewStepsSpinBoxChanged();
    void applyPreviewStepsFromSpinBox();
    void onRussianRouletteMinDepthSpinBoxChanged();
    void syncRussianRouletteMinDepthSpinBox();
    void onBoundsOverlayComboBoxChanged();
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onSettingsButtonClicked();
    void onAddPrimitiveButtonClicked();
    void onLsystemLoadButtonClicked();
    void onResetSceneButtonClicked();
    void onExportSceneButtonClicked();
    void onEnvironmentHdrBrowseClicked();
    void onEnvironmentHdrClearClicked();
    void onEnvironmentIntensitySpinBoxChanged(double value);
    void syncEnvironmentIntensitySpinBox();
    void syncEnvironmentIntensityEnabled();
    void onIterationChangedForAutoExposure(int sampleCount);
    void applyPhysicalCameraToViewport();
    void applySuggestedPhysicalCameraFromHdr();
    void syncEnvironmentHdrPath();
    void syncPhysicalCameraUi();
    void updateExposureValueLabel();
    void syncColorButtonStyle();
    void syncRenderSpinBoxes();
    void syncMaxSamplesSpinBox();
    void syncMinSamplesSpinBox();
    void syncRelativeErrorThresholdSpinBox();
    void syncPreviewStepsSpinBox();
    void syncBoundsOverlayComboBox();
    bool loadLsystemFromFile(const QString& path);
    void restoreLsystemFromSettings();

    SceneModel* m_model = nullptr;
    MainView* m_view = nullptr;
    DebounceTimer m_renderSizeDebounce;
    DebounceTimer m_maxSamplesDebounce;
    DebounceTimer m_previewStepsDebounce;
    DebounceTimer m_physicalCameraDebounce;
    bool m_pendingFrameAutoExposure = false;
    bool m_pendingAccumulatorExposureRefine = false;
};
