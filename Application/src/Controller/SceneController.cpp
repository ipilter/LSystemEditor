#include "SceneController.h"

#include "AppSettings.h"
#include "AppLog.h"
#include "MainView.h"
#include "OpenGLViewportWidget.h"
#include "LSystemTransformDialog.h"
#include "PhysicalCamera.h"
#include "SceneModel.h"
#include "SettingsDialog.h"
#include <QColorDialog>
#include <QDialog>
#include <QFileDialog>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>

#include <cmath>

namespace {

QString colorButtonStyleSheet(const QColor& color)
{
    return QStringLiteral(
               "QPushButton { background-color: %1; border: 1px solid #333; }"
               "QPushButton:hover { border: 1px solid #666; }")
        .arg(color.name(QColor::HexRgb));
}

MeshAccelBoundsOverlayMode boundsOverlayModeFromComboIndex(int index)
{
    switch (index) {
    case 1:
        return MeshAccelBoundsOverlayMode::Bvh;
    case 0:
    default:
        return MeshAccelBoundsOverlayMode::Off;
    }
}

int comboIndexFromBoundsOverlayMode(MeshAccelBoundsOverlayMode mode)
{
    switch (mode) {
    case MeshAccelBoundsOverlayMode::Bvh:
        return 1;
    case MeshAccelBoundsOverlayMode::Off:
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
    m_view->viewport()->setSceneModel(m_model);

    syncRenderSpinBoxes();
    syncMaxSamplesSpinBox();
    syncPreviewStepsSpinBox();
    syncBoundsOverlayComboBox();
    syncEnvironmentHdrPath();
    syncPhysicalCameraUi();
    updateExposureValueLabel();
    applyPhysicalCameraToViewport();

    connect(m_view->colorButton(), &QPushButton::clicked, this, &SceneController::onColorButtonClicked);
    connect(m_model, &SceneModel::clearColorChanged, this, &SceneController::onClearColorChanged);
    connect(m_model, &SceneModel::renderSizeChanged, this, &SceneController::onRenderSizeChanged);
    connect(m_model, &SceneModel::maxSamplesPerPixelChanged, this, [this](int) { syncMaxSamplesSpinBox(); });
    connect(m_model, &SceneModel::previewStepsPerLevelChanged, this, [this](int) { syncPreviewStepsSpinBox(); });
    connect(m_model, &SceneModel::boundsOverlayModeChanged, this, [this](MeshAccelBoundsOverlayMode) {
        syncBoundsOverlayComboBox();
    });
    connect(m_view->renderWidthSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRenderSizeSpinBoxChanged);
    connect(m_view->renderHeightSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRenderSizeSpinBoxChanged);
    connect(m_view->maxSamplesSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onMaxSamplesSpinBoxChanged);
    connect(m_view->previewStepsSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onPreviewStepsSpinBoxChanged);
    connect(
        m_view->boundsOverlayComboBox(),
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &SceneController::onBoundsOverlayComboBoxChanged);
    connect(&m_renderSizeDebounce, &DebounceTimer::triggered, this, &SceneController::applyRenderSizeFromSpinBoxes);
    connect(&m_maxSamplesDebounce, &DebounceTimer::triggered, this, &SceneController::applyMaxSamplesFromSpinBox);
    connect(&m_previewStepsDebounce, &DebounceTimer::triggered, this, &SceneController::applyPreviewStepsFromSpinBox);
    connect(m_view->startButton(), &QPushButton::clicked, this, &SceneController::onStartButtonClicked);
    connect(m_view->stopButton(), &QPushButton::clicked, this, &SceneController::onStopButtonClicked);
    connect(m_view->settingsButton(), &QPushButton::clicked, this, &SceneController::onSettingsButtonClicked);
    connect(m_view->addPrimitiveButton(), &QPushButton::clicked, this, &SceneController::onAddPrimitiveButtonClicked);
    connect(m_view->resetSceneButton(), &QPushButton::clicked, this, &SceneController::onResetSceneButtonClicked);
    connect(m_view->exportSceneButton(), &QPushButton::clicked, this, &SceneController::onExportSceneButtonClicked);
    connect(
        m_view->environmentHdrBrowseButton(),
        &QPushButton::clicked,
        this,
        &SceneController::onEnvironmentHdrBrowseClicked);
    connect(m_model, &SceneModel::environmentHdrPathChanged, this, [this](const QString& path) {
        syncEnvironmentHdrPath();
        m_view->viewport()->setEnvironmentHdrPath(path);
        m_pendingFrameAutoExposure = !path.isEmpty();
        m_pendingAccumulatorExposureRefine = false;
    });
    connect(m_view->fStopSpinBox(), QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        m_model->setFStop(static_cast<float>(value));
        updateExposureValueLabel();
        m_physicalCameraDebounce.schedule();
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

void SceneController::onPreviewStepsSpinBoxChanged()
{
    m_previewStepsDebounce.schedule();
}

void SceneController::applyPreviewStepsFromSpinBox()
{
    m_model->setPreviewStepsPerLevel(m_view->previewStepsSpinBox()->value());
}

void SceneController::onBoundsOverlayComboBoxChanged()
{
    m_model->setBoundsOverlayMode(
        boundsOverlayModeFromComboIndex(m_view->boundsOverlayComboBox()->currentIndex()));
}

void SceneController::onStartButtonClicked()
{
    m_view->setIteration(0);
    m_view->viewport()->restartRender();
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
        QStringLiteral("Wavefront OBJ (*.obj)"));

    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!m_view->viewport()->exportSceneWavefrontObj(path, &error)) {
        AppLog::instance().error(
            error.isEmpty()
                ? QStringLiteral("Scene export failed.")
                : QStringLiteral("Scene export failed: %1").arg(error));
        return;
    }

    AppLog::instance().info(QStringLiteral("Scene exported to %1").arg(path));
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

void SceneController::syncPreviewStepsSpinBox()
{
    m_view->previewStepsSpinBox()->blockSignals(true);
    m_view->previewStepsSpinBox()->setValue(m_model->previewStepsPerLevel());
    m_view->previewStepsSpinBox()->blockSignals(false);
}

void SceneController::syncBoundsOverlayComboBox()
{
    m_view->boundsOverlayComboBox()->blockSignals(true);
    m_view->boundsOverlayComboBox()->setCurrentIndex(
        comboIndexFromBoundsOverlayMode(m_model->boundsOverlayMode()));
    m_view->boundsOverlayComboBox()->blockSignals(false);
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

void SceneController::onIterationChangedForAutoExposure(int sampleCount)
{
    if (m_pendingFrameAutoExposure && sampleCount >= kFrameAutoExposureWarmupSamples) {
        applySuggestedPhysicalCameraFromHdr();
        m_pendingFrameAutoExposure = false;
        m_pendingAccumulatorExposureRefine = true;
    }

    if (!m_pendingAccumulatorExposureRefine || sampleCount < kFrameAutoExposureRefineSamples) {
        return;
    }

    PhysicalCamera suggested{};
    if (m_view->viewport()->computeSuggestedPhysicalCameraFromAccumulator(&suggested)) {
        m_model->setFStop(suggested.fStop);
        m_model->setShutterSpeedSeconds(suggested.shutterSpeedSeconds);
        m_model->setIso(suggested.iso);
        syncPhysicalCameraUi();
        applyPhysicalCameraToViewport();
    }

    m_pendingAccumulatorExposureRefine = false;
}

void SceneController::applyPhysicalCameraToViewport()
{
    m_view->viewport()->setPhysicalCamera(m_model->fStop(), m_model->shutterSpeedSeconds(), m_model->iso());
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
    m_view->shutterSpeedComboBox()->blockSignals(true);
    m_view->isoComboBox()->blockSignals(true);
    m_view->fStopSpinBox()->setValue(static_cast<double>(m_model->fStop()));
    m_view->shutterSpeedComboBox()->setCurrentIndex(
        shutterComboIndexForSeconds(m_view->shutterSpeedComboBox(), m_model->shutterSpeedSeconds()));
    m_view->isoComboBox()->setCurrentIndex(
        isoComboIndexForValue(m_view->isoComboBox(), m_model->iso()));
    m_view->fStopSpinBox()->blockSignals(false);
    m_view->shutterSpeedComboBox()->blockSignals(false);
    m_view->isoComboBox()->blockSignals(false);
    updateExposureValueLabel();
}

void SceneController::updateExposureValueLabel()
{
    const float fStop = static_cast<float>(m_view->fStopSpinBox()->value());
    const float shutterSpeedSeconds = m_view->shutterSpeedComboBox()->currentData().toFloat();
    const float iso = m_view->isoComboBox()->currentData().toFloat();
    const float ev = PhysicalCamera::exposureValue(fStop, shutterSpeedSeconds, iso);
    m_view->setExposureValueText(QString::number(static_cast<double>(ev), 'f', 1));
}
