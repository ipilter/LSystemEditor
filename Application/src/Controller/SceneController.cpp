#include "SceneController.h"

#include "AppSettings.h"
#include "AppLog.h"
#include "Brdf/BrdfDebug.h"
#include "MainView.h"
#include "OpenGLViewportWidget.h"
#include "LSystemTransformDialog.h"
#include "ProceduralMeshBuilder.h"
#include "PhysicalCamera.h"
#include "SceneModel.h"
#include "SettingsDialog.h"
#include "MeshAccel/Mesh.h"
#include <QColorDialog>
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>

#include <QtConcurrent>

#include <cmath>

namespace {

QString colorButtonStyleSheet(const QColor& color)
{
    return QStringLiteral(
               "QPushButton { background-color: %1; border: 1px solid #333; }"
               "QPushButton:hover { border: 1px solid #666; }")
        .arg(color.name(QColor::HexRgb));
}

RenderViewOverlayMode renderViewOverlayModeFromComboIndex(int index)
{
    switch (index) {
    case 1:
        return RenderViewOverlayMode::Bvh;
    case 2:
        return RenderViewOverlayMode::AdaptiveSampling;
    case 3:
        return RenderViewOverlayMode::Uv;
    case 4:
        return RenderViewOverlayMode::Normals;
    case 5:
        return RenderViewOverlayMode::EmissiveLights;
    case 0:
    default:
        return RenderViewOverlayMode::Render;
    }
}

int comboIndexFromBoundsOverlayMode(RenderViewOverlayMode mode)
{
    switch (mode) {
    case RenderViewOverlayMode::Bvh:
        return 1;
    case RenderViewOverlayMode::AdaptiveSampling:
        return 2;
    case RenderViewOverlayMode::Uv:
        return 3;
    case RenderViewOverlayMode::Normals:
        return 4;
    case RenderViewOverlayMode::EmissiveLights:
        return 5;
    case RenderViewOverlayMode::Render:
    default:
        return 0;
    }
}

int brdfDebugFlagsFromComboIndex(int index)
{
    switch (index) {
    case 1:
        return BrdfDebugFlags::kForceTransmitLobeOnly;
    case 2:
        return BrdfDebugFlags::kDisableRefract;
    case 3:
        return BrdfDebugFlags::kDisableReflect;
    case 4:
        return BrdfDebugFlags::kTintGlassPaths;
    case 5:
        return BrdfDebugFlags::kDisableTirFallback;
    case 0:
    default:
        return BrdfDebugFlags::kNone;
    }
}

int comboIndexFromBrdfDebugFlags(int flags)
{
    switch (flags) {
    case BrdfDebugFlags::kForceTransmitLobeOnly:
        return 1;
    case BrdfDebugFlags::kDisableRefract:
        return 2;
    case BrdfDebugFlags::kDisableReflect:
        return 3;
    case BrdfDebugFlags::kTintGlassPaths:
        return 4;
    case BrdfDebugFlags::kDisableTirFallback:
        return 5;
    case BrdfDebugFlags::kNone:
    default:
        return 0;
    }
}

int shutterComboIndexForSeconds(QComboBox* comboBox, float shutterSpeedSeconds)
{
    if (comboBox == nullptr || comboBox->count() == 0) {
        return 0;
    }

    int bestIndex = 0;
    float bestDelta = std::abs(comboBox->itemData(0).toFloat() - shutterSpeedSeconds);
    for (int i = 1; i < comboBox->count(); ++i) {
        const float delta = std::abs(comboBox->itemData(i).toFloat() - shutterSpeedSeconds);
        if (delta < bestDelta) {
            bestDelta = delta;
            bestIndex = i;
        }
    }
    return bestIndex;
}

int isoComboIndexForValue(QComboBox* comboBox, float iso)
{
    if (comboBox == nullptr || comboBox->count() == 0) {
        return 0;
    }

    int bestIndex = 0;
    float bestDelta = std::abs(comboBox->itemData(0).toFloat() - iso);
    for (int i = 1; i < comboBox->count(); ++i) {
        const float delta = std::abs(comboBox->itemData(i).toFloat() - iso);
        if (delta < bestDelta) {
            bestDelta = delta;
            bestIndex = i;
        }
    }
    return bestIndex;
}

constexpr int kFrameAutoExposureWarmupSamples = 64;
constexpr int kFrameAutoExposureRefineSamples = 512;

} // namespace

SceneController::SceneController(SceneModel* model, MainView* view, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_view(view)
    , m_renderSizeDebounce(AppSettings::instance().debounceMsFor(DebounceElementIds::kRenderSize), this)
    , m_maxSamplesDebounce(AppSettings::instance().debounceMsFor(DebounceElementIds::kMaxSamples), this)
    , m_previewStepsDebounce(AppSettings::instance().debounceMsFor(DebounceElementIds::kPreviewSteps), this)
    , m_physicalCameraDebounce(AppSettings::instance().debounceMsFor(DebounceElementIds::kPhysicalCamera), this)
{
    syncColorButtonStyle();
    m_view->viewport()->setClearColor(m_model->clearColor());
    m_view->viewport()->setEnvironmentIntensity(m_model->environmentIntensity());
    m_view->viewport()->setEnvironmentRotationY(m_model->environmentRotationY());
    m_view->viewport()->setSceneModel(m_model);

    syncRenderSpinBoxes();
    syncMaxSamplesSpinBox();
    syncMinSamplesSpinBox();
    syncRelativeErrorThresholdSpinBox();
    syncPreviewStepsSpinBox();
    syncRussianRouletteMinDepthSpinBox();
    syncMaxSubsurfaceScattersSpinBox();
    syncEmissiveNeeSamplesSpinBox();
    syncBoundsOverlayComboBox();
    syncBrdfDebugComboBox();
    syncSceneOverlayCheckBox();
    syncRegionRenderUi();
    updateRegionSpinBoxRanges();
    syncEnvironmentHdrPath();
    syncEnvironmentIntensitySpinBox();
    syncEnvironmentIntensityEnabled();
    syncEnvironmentRotationYSpinBox();
    syncPhysicalCameraUi();
    syncFocusDistanceSpinBox();
    updateExposureValueLabel();
    applyPhysicalCameraToViewport();
    restoreLsystemFromSettings();

    connect(m_view->colorButton(), &QPushButton::clicked, this, &SceneController::onColorButtonClicked);
    connect(m_model, &SceneModel::clearColorChanged, this, &SceneController::onClearColorChanged);
    connect(m_model, &SceneModel::renderSizeChanged, this, &SceneController::onRenderSizeChanged);
    connect(m_model, &SceneModel::maxSamplesPerPixelChanged, this, [this](int) { syncMaxSamplesSpinBox(); });
    connect(m_model, &SceneModel::minSamplesChanged, this, [this](int) { syncMinSamplesSpinBox(); });
    connect(m_model, &SceneModel::relativeErrorThresholdChanged, this, [this](float) {
        syncRelativeErrorThresholdSpinBox();
    });
    connect(m_model, &SceneModel::previewStepsPerLevelChanged, this, [this](int) { syncPreviewStepsSpinBox(); });
    connect(m_model, &SceneModel::russianRouletteMinDepthChanged, this, [this](int) {
        syncRussianRouletteMinDepthSpinBox();
    });
    connect(m_model, &SceneModel::maxSubsurfaceScattersChanged, this, [this](int) {
        syncMaxSubsurfaceScattersSpinBox();
    });
    connect(m_model, &SceneModel::emissiveNeeSamplesChanged, this, [this](int) {
        syncEmissiveNeeSamplesSpinBox();
    });
    connect(m_model, &SceneModel::boundsOverlayModeChanged, this, [this](RenderViewOverlayMode) {
        syncBoundsOverlayComboBox();
    });
    connect(m_model, &SceneModel::brdfDebugFlagsChanged, this, [this](int) {
        syncBrdfDebugComboBox();
    });
    connect(m_model, &SceneModel::sceneOverlayVisibleChanged, this, [this](bool) {
        syncSceneOverlayCheckBox();
    });
    connect(m_view->renderWidthSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRenderSizeSpinBoxChanged);
    connect(m_view->renderHeightSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRenderSizeSpinBoxChanged);
    connect(m_view->maxSamplesSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onMaxSamplesSpinBoxChanged);
    connect(m_view->minSamplesSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onMinSamplesSpinBoxChanged);
    connect(
        m_view->relativeErrorThresholdSpinBox(),
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        &SceneController::onRelativeErrorThresholdSpinBoxChanged);
    connect(m_view->previewStepsSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onPreviewStepsSpinBoxChanged);
    connect(
        m_view->russianRouletteMinDepthSpinBox(),
        &QSpinBox::valueChanged,
        this,
        &SceneController::onRussianRouletteMinDepthSpinBoxChanged);
    connect(
        m_view->maxSubsurfaceScattersSpinBox(),
        &QSpinBox::valueChanged,
        this,
        &SceneController::onMaxSubsurfaceScattersSpinBoxChanged);
    connect(
        m_view->emissiveNeeSamplesSpinBox(),
        &QSpinBox::valueChanged,
        this,
        &SceneController::onEmissiveNeeSamplesSpinBoxChanged);
    connect(
        m_view->renderViewOverlayComboBox(),
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &SceneController::onBoundsOverlayComboBoxChanged);
    connect(
        m_view->brdfDebugComboBox(),
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &SceneController::onBrdfDebugComboBoxChanged);
    connect(
        m_view->sceneOverlayCheckBox(),
        &QCheckBox::toggled,
        this,
        &SceneController::onSceneOverlayCheckBoxChanged);
    connect(m_view->regionRenderCheckBox(), &QCheckBox::toggled, this, &SceneController::onRegionRenderCheckBoxChanged);
    connect(m_view->regionBottomLeftXSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRegionRectSpinBoxesChanged);
    connect(m_view->regionBottomLeftYSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRegionRectSpinBoxesChanged);
    connect(m_view->regionTopRightXSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRegionRectSpinBoxesChanged);
    connect(m_view->regionTopRightYSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRegionRectSpinBoxesChanged);
    connect(m_view->defineRegionButton(), &QPushButton::toggled, this, &SceneController::onDefineRegionButtonToggled);
    connect(m_model, &SceneModel::regionRenderEnabledChanged, this, [this](bool) {
        syncRegionRenderUi();
        m_view->viewport()->applyRegionRenderSettings(true);
    });
    connect(m_model, &SceneModel::regionRectChanged, this, [this](const QRect&) {
        syncRegionRenderUi();
        m_view->viewport()->applyRegionRenderSettings(true);
    });
    connect(m_view->viewport(), &OpenGLViewportWidget::regionDefineModeChanged, this, [this](bool active) {
        m_view->defineRegionButton()->blockSignals(true);
        m_view->defineRegionButton()->setChecked(active);
        m_view->defineRegionButton()->blockSignals(false);
    });
    connect(&m_renderSizeDebounce, &DebounceTimer::triggered, this, &SceneController::applyRenderSizeFromSpinBoxes);
    connect(&m_maxSamplesDebounce, &DebounceTimer::triggered, this, &SceneController::applyMaxSamplesFromSpinBox);
    connect(&m_previewStepsDebounce, &DebounceTimer::triggered, this, &SceneController::applyPreviewStepsFromSpinBox);
    connect(m_view->startButton(), &QPushButton::clicked, this, &SceneController::onStartButtonClicked);
    connect(m_view->stopButton(), &QPushButton::clicked, this, &SceneController::onStopButtonClicked);
    connect(m_view->settingsButton(), &QPushButton::clicked, this, &SceneController::onSettingsButtonClicked);
    connect(m_view->addPrimitiveButton(), &QPushButton::clicked, this, &SceneController::onAddPrimitiveButtonClicked);
    connect(m_view->lsystemLoadButton(), &QPushButton::clicked, this, &SceneController::onLsystemLoadButtonClicked);
    connect(m_view->resetSceneButton(), &QPushButton::clicked, this, &SceneController::onResetSceneButtonClicked);
    connect(m_view->exportSceneButton(), &QPushButton::clicked, this, &SceneController::onExportSceneButtonClicked);
    connect(m_view->importSceneButton(), &QPushButton::clicked, this, &SceneController::onImportSceneButtonClicked);
    connect(
        m_view->environmentHdrBrowseButton(),
        &QPushButton::clicked,
        this,
        &SceneController::onEnvironmentHdrBrowseClicked);
    connect(
        m_view->environmentHdrClearButton(),
        &QPushButton::clicked,
        this,
        &SceneController::onEnvironmentHdrClearClicked);
    connect(
        m_view->environmentIntensitySpinBox(),
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        &SceneController::onEnvironmentIntensitySpinBoxChanged);
    connect(
        m_view->environmentRotationYSpinBox(),
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        &SceneController::onEnvironmentRotationYSpinBoxChanged);
    connect(m_model, &SceneModel::environmentHdrPathChanged, this, [this](const QString& path) {
        syncEnvironmentHdrPath();
        syncEnvironmentIntensityEnabled();
        m_view->viewport()->setEnvironmentHdrPath(path);
        m_pendingFrameAutoExposure = !path.isEmpty();
        m_pendingAccumulatorExposureRefine = false;
    });
    connect(m_model, &SceneModel::environmentIntensityChanged, this, [this](float value) {
        syncEnvironmentIntensitySpinBox();
        m_view->viewport()->setEnvironmentIntensity(value);
    });
    connect(m_model, &SceneModel::environmentRotationYChanged, this, [this](int degrees) {
        syncEnvironmentRotationYSpinBox();
        m_view->viewport()->setEnvironmentRotationY(degrees);
    });
    connect(m_view->fStopSpinBox(), QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        m_model->setFStop(static_cast<float>(value));
        updateExposureValueLabel();
        m_physicalCameraDebounce.schedule();
    });
    connect(m_view->focalLengthSpinBox(), QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        m_model->setFocalLengthMm(static_cast<float>(value));
        m_physicalCameraDebounce.schedule();
    });
    connect(
        m_view->focusDistanceSpinBox(),
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        [this](double value) {
            m_model->setFocusDistanceMm(static_cast<float>(value));
            m_view->viewport()->setFocusDistanceMm(m_model->focusDistanceMm());
        });
    connect(m_model, &SceneModel::focusDistanceMmChanged, this, [this](float) {
        syncFocusDistanceSpinBox();
    });
    connect(
        m_view->shutterSpeedComboBox(),
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int) {
            const float shutterSpeedSeconds = m_view->shutterSpeedComboBox()->currentData().toFloat();
            m_model->setShutterSpeedSeconds(shutterSpeedSeconds);
            updateExposureValueLabel();
            m_physicalCameraDebounce.schedule();
        });
    connect(
        m_view->isoComboBox(),
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int) {
            const float iso = m_view->isoComboBox()->currentData().toFloat();
            m_model->setIso(iso);
            updateExposureValueLabel();
            m_physicalCameraDebounce.schedule();
        });
    connect(&m_physicalCameraDebounce, &DebounceTimer::triggered, this, &SceneController::applyPhysicalCameraToViewport);
    connect(m_view->viewport(), &OpenGLViewportWidget::iterationChanged, this, &SceneController::onIterationChangedForAutoExposure);
    connect(m_view->viewport(), &OpenGLViewportWidget::iterationChanged, m_view, &MainView::setIteration);
    connect(m_view, &MainView::applicationShuttingDown, this, &SceneController::onApplicationShuttingDown);
    connect(
        m_view->viewport(),
        &OpenGLViewportWidget::renderStateChanged,
        m_view,
        &MainView::setRenderState);
    connect(&AppSettings::instance(), &AppSettings::debounceMsChanged, this,
            [this](const QString& elementId, int ms) {
                if (elementId == DebounceElementIds::kRenderSize) {
                    m_renderSizeDebounce.setIntervalMs(ms);
                } else if (elementId == DebounceElementIds::kMaxSamples) {
                    m_maxSamplesDebounce.setIntervalMs(ms);
                } else if (elementId == DebounceElementIds::kPreviewSteps) {
                    m_previewStepsDebounce.setIntervalMs(ms);
                } else if (elementId == DebounceElementIds::kPhysicalCamera) {
                    m_physicalCameraDebounce.setIntervalMs(ms);
                }
            });
}

void SceneController::onColorButtonClicked()
{
    const QColor chosen = QColorDialog::getColor(
        m_model->clearColor(),
        m_view,
        QStringLiteral("Background color"));

    if (chosen.isValid()) {
        m_model->setClearColor(chosen);
    }
}

void SceneController::onClearColorChanged(const QColor& color)
{
    m_view->viewport()->setClearColor(color);
    syncColorButtonStyle();
}

void SceneController::onRenderSizeChanged(const QSize& size)
{
    Q_UNUSED(size);
    syncRenderSpinBoxes();
    updateRegionSpinBoxRanges();
    syncRegionRenderUi();
}

void SceneController::onRenderSizeSpinBoxChanged()
{
    m_renderSizeDebounce.schedule();
}

void SceneController::applyRenderSizeFromSpinBoxes()
{
    m_model->setRenderSize(m_view->renderWidthSpinBox()->value(), m_view->renderHeightSpinBox()->value());
}

void SceneController::onMaxSamplesSpinBoxChanged()
{
    m_maxSamplesDebounce.schedule();
}

void SceneController::applyMaxSamplesFromSpinBox()
{
    m_model->setMaxSamplesPerPixel(m_view->maxSamplesSpinBox()->value());
}

void SceneController::onMinSamplesSpinBoxChanged()
{
    m_model->setMinSamples(m_view->minSamplesSpinBox()->value());
}

void SceneController::onRelativeErrorThresholdSpinBoxChanged()
{
    m_model->setRelativeErrorThreshold(
        static_cast<float>(m_view->relativeErrorThresholdSpinBox()->value()));
}

void SceneController::onPreviewStepsSpinBoxChanged()
{
    m_previewStepsDebounce.schedule();
}

void SceneController::applyPreviewStepsFromSpinBox()
{
    m_model->setPreviewStepsPerLevel(m_view->previewStepsSpinBox()->value());
}

void SceneController::onRussianRouletteMinDepthSpinBoxChanged()
{
    m_model->setRussianRouletteMinDepth(m_view->russianRouletteMinDepthSpinBox()->value());
}

void SceneController::onMaxSubsurfaceScattersSpinBoxChanged()
{
    m_model->setMaxSubsurfaceScatters(m_view->maxSubsurfaceScattersSpinBox()->value());
}

void SceneController::onEmissiveNeeSamplesSpinBoxChanged()
{
    m_model->setEmissiveNeeSamples(m_view->emissiveNeeSamplesSpinBox()->value());
}

void SceneController::onBoundsOverlayComboBoxChanged()
{
    m_model->setBoundsOverlayMode(
        renderViewOverlayModeFromComboIndex(m_view->renderViewOverlayComboBox()->currentIndex()));
}

void SceneController::onBrdfDebugComboBoxChanged()
{
    m_model->setBrdfDebugFlags(brdfDebugFlagsFromComboIndex(m_view->brdfDebugComboBox()->currentIndex()));
}

void SceneController::onSceneOverlayCheckBoxChanged()
{
    m_model->setSceneOverlayVisible(m_view->sceneOverlayCheckBox()->isChecked());
}

void SceneController::onRegionRenderCheckBoxChanged()
{
    m_model->setRegionRenderEnabled(m_view->regionRenderCheckBox()->isChecked());
    m_view->viewport()->applyRegionRenderSettings(true);
}

void SceneController::onRegionRectSpinBoxesChanged()
{
    applyRegionRectFromSpinBoxes();
}

void SceneController::onDefineRegionButtonToggled(bool checked)
{
    m_view->viewport()->setRegionDefineMode(checked);
}

void SceneController::syncRegionRenderUi()
{
    const QRect rect = m_model->regionRect();

    m_view->regionRenderCheckBox()->blockSignals(true);
    m_view->regionRenderCheckBox()->setChecked(m_model->regionRenderEnabled());
    m_view->regionRenderCheckBox()->blockSignals(false);

    m_view->regionBottomLeftXSpinBox()->blockSignals(true);
    m_view->regionBottomLeftYSpinBox()->blockSignals(true);
    m_view->regionTopRightXSpinBox()->blockSignals(true);
    m_view->regionTopRightYSpinBox()->blockSignals(true);
    m_view->regionBottomLeftXSpinBox()->setValue(rect.left());
    m_view->regionBottomLeftYSpinBox()->setValue(rect.bottom());
    m_view->regionTopRightXSpinBox()->setValue(rect.right());
    m_view->regionTopRightYSpinBox()->setValue(rect.top());
    m_view->regionBottomLeftXSpinBox()->blockSignals(false);
    m_view->regionBottomLeftYSpinBox()->blockSignals(false);
    m_view->regionTopRightXSpinBox()->blockSignals(false);
    m_view->regionTopRightYSpinBox()->blockSignals(false);
}

void SceneController::updateRegionSpinBoxRanges()
{
    const int maxX = std::max(0, m_model->renderSize().width() - 1);
    const int maxY = std::max(0, m_model->renderSize().height() - 1);

    m_view->regionBottomLeftXSpinBox()->setRange(0, maxX);
    m_view->regionBottomLeftYSpinBox()->setRange(0, maxY);
    m_view->regionTopRightXSpinBox()->setRange(0, maxX);
    m_view->regionTopRightYSpinBox()->setRange(0, maxY);
}

void SceneController::applyRegionRectFromSpinBoxes()
{
    m_model->setRegionRect(
        m_view->regionBottomLeftXSpinBox()->value(),
        m_view->regionTopRightYSpinBox()->value(),
        m_view->regionTopRightXSpinBox()->value(),
        m_view->regionBottomLeftYSpinBox()->value());
}

void SceneController::onStartButtonClicked()
{
    m_view->setIteration(0);
    m_view->viewport()->restartRender(m_model->regionRenderEnabled());
}

void SceneController::onStopButtonClicked()
{
    m_view->viewport()->pauseRender();
}

void SceneController::onSettingsButtonClicked()
{
    SettingsDialog dialog(m_view);
    if (dialog.exec() == QDialog::Accepted) {
        m_model->setCreaseAngleDeg(AppSettings::instance().creaseAngleDeg());
        m_model->setAccelBvhColor(AppSettings::instance().accelBvhColor());
    }
}

void SceneController::onAddPrimitiveButtonClicked()
{
    const QString command = m_view->lsystemEdit()->toPlainText().trimmed();
    if (command.isEmpty()) {
        return;
    }

    LSystemTransformDialog dialog(m_view);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    ProceduralInstance instance{};
    instance.commandString = command.toStdString();
    instance.iterations = static_cast<std::size_t>(m_view->lsystemIterationsSpinBox()->value());
    instance.translation = dialog.translation();
    instance.rotationDeg = dialog.rotationDeg();
    m_model->addProceduralInstance(std::move(instance));
}

bool SceneController::loadLsystemFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    m_view->lsystemEdit()->setPlainText(QString::fromUtf8(file.readAll()));
    AppSettings::instance().setLsystemFilePath(path);
    AppLog::instance().info(QStringLiteral("L-system loaded from %1").arg(path));
    return true;
}

void SceneController::restoreLsystemFromSettings()
{
    const QString path = AppSettings::instance().lsystemFilePath();
    if (path.isEmpty() || !QFile::exists(path)) {
        return;
    }

    if (!loadLsystemFromFile(path)) {
        AppLog::instance().warning(
            QStringLiteral("Failed to restore L-system from saved path: %1").arg(path));
    }
}

void SceneController::onLsystemLoadButtonClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        m_view,
        QStringLiteral("Load L-System"),
        AppSettings::instance().lsystemFilePath(),
        QStringLiteral("L-System Files (*.lsystem);;All Files (*)"));

    if (path.isEmpty()) {
        return;
    }

    if (!loadLsystemFromFile(path)) {
        AppLog::instance().error(QStringLiteral("Failed to load L-system from %1").arg(path));
    }
}

void SceneController::onResetSceneButtonClicked()
{
    m_model->resetScene();
}

void SceneController::onExportSceneButtonClicked()
{
    const QString path = QFileDialog::getSaveFileName(
        m_view,
        QStringLiteral("Export Scene"),
        QString(),
        QStringLiteral("glTF Binary (*.glb);;Wavefront OBJ (*.obj)"));

    if (path.isEmpty()) {
        return;
    }

    QString error;
    const bool isGltf = path.endsWith(QStringLiteral(".glb"), Qt::CaseInsensitive);
    const bool ok = isGltf
        ? m_view->viewport()->exportSceneGltf(path, &error)
        : m_view->viewport()->exportSceneWavefrontObj(path, &error);

    if (!ok) {
        AppLog::instance().error(
            error.isEmpty()
                ? QStringLiteral("Scene export failed.")
                : QStringLiteral("Scene export failed: %1").arg(error));
        return;
    }

    AppLog::instance().info(QStringLiteral("Scene exported to %1").arg(path));
}

void SceneController::onImportSceneButtonClicked()
{
    LSystemTransformDialog dialog(m_view);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        m_view,
        QStringLiteral("Import glb"),
        QString(),
        QStringLiteral("glTF (*.glb *.gltf)"));

    if (path.isEmpty()) {
        return;
    }

    Mesh importedMesh{};
    QString error;
    if (!m_view->viewport()->importSceneGltf(path, &importedMesh, &error)) {
        AppLog::instance().error(
            error.isEmpty()
                ? QStringLiteral("Scene import failed.")
                : QStringLiteral("Scene import failed: %1").arg(error));
        return;
    }

    RootTransform root{};
    root.translation = dialog.translation();
    root.rotationDeg = dialog.rotationDeg();
    ProceduralMeshBuilder::applyRootTransform(importedMesh, root);

    m_model->setImportedMesh(std::move(importedMesh));
    AppLog::instance().info(QStringLiteral("Scene imported from %1").arg(path));
}

void SceneController::syncColorButtonStyle()
{
    m_view->colorButton()->setStyleSheet(colorButtonStyleSheet(m_model->clearColor()));
}

void SceneController::syncRenderSpinBoxes()
{
    const QSize size = m_model->renderSize();
    m_view->renderWidthSpinBox()->blockSignals(true);
    m_view->renderHeightSpinBox()->blockSignals(true);
    m_view->renderWidthSpinBox()->setValue(size.width());
    m_view->renderHeightSpinBox()->setValue(size.height());
    m_view->renderWidthSpinBox()->blockSignals(false);
    m_view->renderHeightSpinBox()->blockSignals(false);
}

void SceneController::syncMaxSamplesSpinBox()
{
    m_view->maxSamplesSpinBox()->blockSignals(true);
    m_view->maxSamplesSpinBox()->setValue(m_model->maxSamplesPerPixel());
    m_view->maxSamplesSpinBox()->blockSignals(false);
}

void SceneController::syncMinSamplesSpinBox()
{
    m_view->minSamplesSpinBox()->blockSignals(true);
    m_view->minSamplesSpinBox()->setValue(m_model->minSamples());
    m_view->minSamplesSpinBox()->blockSignals(false);
}

void SceneController::syncRelativeErrorThresholdSpinBox()
{
    m_view->relativeErrorThresholdSpinBox()->blockSignals(true);
    m_view->relativeErrorThresholdSpinBox()->setValue(m_model->relativeErrorThreshold());
    m_view->relativeErrorThresholdSpinBox()->blockSignals(false);
}

void SceneController::syncPreviewStepsSpinBox()
{
    m_view->previewStepsSpinBox()->blockSignals(true);
    m_view->previewStepsSpinBox()->setValue(m_model->previewStepsPerLevel());
    m_view->previewStepsSpinBox()->blockSignals(false);
}

void SceneController::syncRussianRouletteMinDepthSpinBox()
{
    m_view->russianRouletteMinDepthSpinBox()->blockSignals(true);
    m_view->russianRouletteMinDepthSpinBox()->setValue(m_model->russianRouletteMinDepth());
    m_view->russianRouletteMinDepthSpinBox()->blockSignals(false);
}

void SceneController::syncMaxSubsurfaceScattersSpinBox()
{
    m_view->maxSubsurfaceScattersSpinBox()->blockSignals(true);
    m_view->maxSubsurfaceScattersSpinBox()->setValue(m_model->maxSubsurfaceScatters());
    m_view->maxSubsurfaceScattersSpinBox()->blockSignals(false);
}

void SceneController::syncEmissiveNeeSamplesSpinBox()
{
    m_view->emissiveNeeSamplesSpinBox()->blockSignals(true);
    m_view->emissiveNeeSamplesSpinBox()->setValue(m_model->emissiveNeeSamples());
    m_view->emissiveNeeSamplesSpinBox()->blockSignals(false);
}

void SceneController::syncBoundsOverlayComboBox()
{
    m_view->renderViewOverlayComboBox()->blockSignals(true);
    m_view->renderViewOverlayComboBox()->setCurrentIndex(
        comboIndexFromBoundsOverlayMode(m_model->boundsOverlayMode()));
    m_view->renderViewOverlayComboBox()->blockSignals(false);
}

void SceneController::syncBrdfDebugComboBox()
{
    m_view->brdfDebugComboBox()->blockSignals(true);
    m_view->brdfDebugComboBox()->setCurrentIndex(comboIndexFromBrdfDebugFlags(m_model->brdfDebugFlags()));
    m_view->brdfDebugComboBox()->blockSignals(false);
}

void SceneController::syncSceneOverlayCheckBox()
{
    m_view->sceneOverlayCheckBox()->blockSignals(true);
    m_view->sceneOverlayCheckBox()->setChecked(m_model->sceneOverlayVisible());
    m_view->sceneOverlayCheckBox()->blockSignals(false);
}

void SceneController::syncEnvironmentHdrPath()
{
    m_view->setEnvironmentHdrPath(m_model->environmentHdrPath());
}

void SceneController::onEnvironmentHdrBrowseClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        m_view,
        QStringLiteral("Select HDR Environment Map"),
        m_model->environmentHdrPath(),
        QStringLiteral("HDR Images (*.hdr *.HDR)"));

    if (path.isEmpty()) {
        return;
    }

    m_model->setEnvironmentHdrPath(path);
}

void SceneController::onEnvironmentHdrClearClicked()
{
    m_model->setEnvironmentHdrPath({});
}

void SceneController::onEnvironmentIntensitySpinBoxChanged(double value)
{
    m_model->setEnvironmentIntensity(static_cast<float>(value));
}

void SceneController::syncEnvironmentIntensitySpinBox()
{
    m_view->environmentIntensitySpinBox()->blockSignals(true);
    m_view->environmentIntensitySpinBox()->setValue(static_cast<double>(m_model->environmentIntensity()));
    m_view->environmentIntensitySpinBox()->blockSignals(false);
}

void SceneController::syncEnvironmentIntensityEnabled()
{
    const bool hasHdr = !m_model->environmentHdrPath().isEmpty();
    m_view->environmentIntensitySpinBox()->setEnabled(!hasHdr);
}

void SceneController::onEnvironmentRotationYSpinBoxChanged(int value)
{
    m_model->setEnvironmentRotationY(value);
}

void SceneController::syncEnvironmentRotationYSpinBox()
{
    m_view->environmentRotationYSpinBox()->blockSignals(true);
    m_view->environmentRotationYSpinBox()->setValue(m_model->environmentRotationY());
    m_view->environmentRotationYSpinBox()->blockSignals(false);
}

void SceneController::onApplicationShuttingDown()
{
    m_applicationActive.store(false);
    m_pendingFrameAutoExposure = false;
    m_pendingAccumulatorExposureRefine = false;
}

void SceneController::onIterationChangedForAutoExposure(int sampleCount)
{
    if (!m_applicationActive.load()) {
        return;
    }

    if (m_pendingFrameAutoExposure && sampleCount >= kFrameAutoExposureWarmupSamples) {
        applySuggestedPhysicalCameraFromHdr();
        m_pendingFrameAutoExposure = false;
        m_pendingAccumulatorExposureRefine = true;
    }

    if (!m_pendingAccumulatorExposureRefine || sampleCount < kFrameAutoExposureRefineSamples ||
        m_autoExposureComputeRunning.load()) {
        return;
    }

    m_pendingAccumulatorExposureRefine = false;
    m_autoExposureComputeRunning.store(true);
    OpenGLViewportWidget* viewport = m_view->viewport();
    (void)QtConcurrent::run([this, viewport]() {
        if (!m_applicationActive.load()) {
            m_autoExposureComputeRunning.store(false);
            return;
        }

        PhysicalCamera suggested{};
        const bool ok =
            viewport != nullptr && m_applicationActive.load()
            && viewport->computeSuggestedPhysicalCameraFromAccumulator(&suggested);
        if (!m_applicationActive.load()) {
            m_autoExposureComputeRunning.store(false);
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, ok, suggested]() { applyAccumulatorExposureRefine(ok, suggested); },
            Qt::QueuedConnection);
    });
}

void SceneController::applyAccumulatorExposureRefine(bool ok, PhysicalCamera suggested)
{
    m_autoExposureComputeRunning.store(false);

    if (!m_applicationActive.load()) {
        return;
    }

    if (ok) {
        m_model->setFStop(suggested.fStop);
        m_model->setShutterSpeedSeconds(suggested.shutterSpeedSeconds);
        m_model->setIso(suggested.iso);
        syncPhysicalCameraUi();
        applyPhysicalCameraToViewport();
    }
}

void SceneController::applyPhysicalCameraToViewport()
{
    m_view->viewport()->setPhysicalCamera(m_model->fStop(), m_model->shutterSpeedSeconds(), m_model->iso());
    m_view->viewport()->setFocalLengthMm(m_model->focalLengthMm());
    if (!m_model->focusPointPinned()) {
        m_view->viewport()->setFocusDistanceMm(m_model->focusDistanceMm());
    }
}

void SceneController::applySuggestedPhysicalCameraFromHdr()
{
    const PhysicalCamera suggested = m_view->viewport()->suggestedPhysicalCamera();
    m_model->setFStop(suggested.fStop);
    m_model->setShutterSpeedSeconds(suggested.shutterSpeedSeconds);
    m_model->setIso(suggested.iso);
    syncPhysicalCameraUi();
    applyPhysicalCameraToViewport();
}

void SceneController::syncPhysicalCameraUi()
{
    m_view->fStopSpinBox()->blockSignals(true);
    m_view->focalLengthSpinBox()->blockSignals(true);
    m_view->focusDistanceSpinBox()->blockSignals(true);
    m_view->shutterSpeedComboBox()->blockSignals(true);
    m_view->isoComboBox()->blockSignals(true);
    m_view->fStopSpinBox()->setValue(static_cast<double>(m_model->fStop()));
    m_view->focalLengthSpinBox()->setValue(static_cast<double>(m_model->focalLengthMm()));
    m_view->focusDistanceSpinBox()->setValue(static_cast<double>(m_model->focusDistanceMm()));
    m_view->shutterSpeedComboBox()->setCurrentIndex(
        shutterComboIndexForSeconds(m_view->shutterSpeedComboBox(), m_model->shutterSpeedSeconds()));
    m_view->isoComboBox()->setCurrentIndex(
        isoComboIndexForValue(m_view->isoComboBox(), m_model->iso()));
    m_view->fStopSpinBox()->blockSignals(false);
    m_view->focalLengthSpinBox()->blockSignals(false);
    m_view->focusDistanceSpinBox()->blockSignals(false);
    m_view->shutterSpeedComboBox()->blockSignals(false);
    m_view->isoComboBox()->blockSignals(false);
    updateExposureValueLabel();
}

void SceneController::syncFocusDistanceSpinBox()
{
    m_view->focusDistanceSpinBox()->blockSignals(true);
    m_view->focusDistanceSpinBox()->setValue(static_cast<double>(m_model->focusDistanceMm()));
    m_view->focusDistanceSpinBox()->blockSignals(false);
}

void SceneController::updateExposureValueLabel()
{
    const float fStop = static_cast<float>(m_view->fStopSpinBox()->value());
    const float shutterSpeedSeconds = m_view->shutterSpeedComboBox()->currentData().toFloat();
    const float iso = m_view->isoComboBox()->currentData().toFloat();
    const float ev = PhysicalCamera::exposureValue(fStop, shutterSpeedSeconds, iso);
    m_view->setExposureValueText(QString::number(static_cast<double>(ev), 'f', 1));
}
