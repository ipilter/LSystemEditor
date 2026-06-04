#pragma once

#include "DebounceTimer.h"

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
    void onPreviewStepsSpinBoxChanged();
    void applyPreviewStepsFromSpinBox();
    void onDebugVisualModeComboBoxChanged();
    void onBoundsOverlayComboBoxChanged();
    void onStartButtonClicked();
    void onStopButtonClicked();
    void onSettingsButtonClicked();
    void onAddPrimitiveButtonClicked();
    void syncColorButtonStyle();
    void syncRenderSpinBoxes();
    void syncMaxSamplesSpinBox();
    void syncPreviewStepsSpinBox();
    void syncDebugVisualModeComboBox();
    void syncBoundsOverlayComboBox();

    SceneModel* m_model = nullptr;
    MainView* m_view = nullptr;
    DebounceTimer m_renderSizeDebounce;
    DebounceTimer m_maxSamplesDebounce;
    DebounceTimer m_previewStepsDebounce;
};
