#include "SceneController.h"

#include "AppSettings.h"
#include "MainView.h"
#include "OpenGLViewportWidget.h"
#include "LSystemTransformDialog.h"
#include "SceneModel.h"
#include "SettingsDialog.h"
#include <QColorDialog>
#include <QDialog>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>

namespace {

QString colorButtonStyleSheet(const QColor& color)
{
    return QStringLiteral(
               "QPushButton { background-color: %1; border: 1px solid #333; }"
               "QPushButton:hover { border: 1px solid #666; }")
        .arg(color.name(QColor::HexRgb));
}

RenderDebugVisualMode visualModeFromComboIndex(int index)
{
    switch (index) {
    case 1:
        return RenderDebugVisualMode::Off;
    case 0:
    default:
        return RenderDebugVisualMode::Normals;
    }
}

int comboIndexFromVisualMode(RenderDebugVisualMode mode)
{
    switch (mode) {
    case RenderDebugVisualMode::Off:
        return 1;
    case RenderDebugVisualMode::Normals:
    default:
        return 0;
    }
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

} // namespace

SceneController::SceneController(SceneModel* model, MainView* view, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_view(view)
    , m_renderSizeDebounce(AppSettings::instance().debounceMsFor(DebounceElementIds::kRenderSize), this)
    , m_maxSamplesDebounce(AppSettings::instance().debounceMsFor(DebounceElementIds::kMaxSamples), this)
    , m_previewStepsDebounce(AppSettings::instance().debounceMsFor(DebounceElementIds::kPreviewSteps), this)
{
    syncColorButtonStyle();
    m_view->viewport()->setClearColor(m_model->clearColor());
    m_view->viewport()->setSceneModel(m_model);

    syncRenderSpinBoxes();
    syncMaxSamplesSpinBox();
    syncPreviewStepsSpinBox();
    syncDebugVisualModeComboBox();
    syncSunControls();
    syncBoundsOverlayComboBox();

    connect(m_view->colorButton(), &QPushButton::clicked, this, &SceneController::onColorButtonClicked);
    connect(m_model, &SceneModel::clearColorChanged, this, &SceneController::onClearColorChanged);
    connect(m_model, &SceneModel::renderSizeChanged, this, &SceneController::onRenderSizeChanged);
    connect(m_model, &SceneModel::maxSamplesPerPixelChanged, this, [this](int) { syncMaxSamplesSpinBox(); });
    connect(m_model, &SceneModel::previewStepsPerLevelChanged, this, [this](int) { syncPreviewStepsSpinBox(); });
    connect(m_model, &SceneModel::debugVisualModeChanged, this, [this](RenderDebugVisualMode) {
        syncDebugVisualModeComboBox();
    });
    connect(m_model, &SceneModel::sunSettingsChanged, this, [this]() {
        syncSunControls();
    });
    connect(m_model, &SceneModel::secondaryBounceCountChanged, this, [this](int) {
        syncSunControls();
    });
    connect(m_model, &SceneModel::boundsOverlayModeChanged, this, [this](MeshAccelBoundsOverlayMode) {
        syncBoundsOverlayComboBox();
    });
    connect(m_view->renderWidthSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRenderSizeSpinBoxChanged);
    connect(m_view->renderHeightSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRenderSizeSpinBoxChanged);
    connect(m_view->maxSamplesSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onMaxSamplesSpinBoxChanged);
    connect(m_view->previewStepsSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onPreviewStepsSpinBoxChanged);
    connect(
        m_view->debugVisualModeComboBox(),
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &SceneController::onDebugVisualModeComboBoxChanged);
    connect(
        m_view->boundsOverlayComboBox(),
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &SceneController::onBoundsOverlayComboBoxChanged);
    connect(m_view->sunAzimuthSpinBox(), &QDoubleSpinBox::valueChanged, this, &SceneController::onSunAzimuthSpinBoxChanged);
    connect(
        m_view->sunElevationSpinBox(),
        &QDoubleSpinBox::valueChanged,
        this,
        &SceneController::onSunElevationSpinBoxChanged);
    connect(m_view->sunColorButton(), &QPushButton::clicked, this, &SceneController::onSunColorButtonClicked);
    connect(
        m_view->sunDiskSizeSpinBox(),
        &QDoubleSpinBox::valueChanged,
        this,
        &SceneController::onSunDiskSizeSpinBoxChanged);
    connect(
        m_view->secondaryBounceSpinBox(),
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        &SceneController::onSecondaryBounceSpinBoxChanged);
    connect(&m_renderSizeDebounce, &DebounceTimer::triggered, this, &SceneController::applyRenderSizeFromSpinBoxes);
    connect(&m_maxSamplesDebounce, &DebounceTimer::triggered, this, &SceneController::applyMaxSamplesFromSpinBox);
    connect(&m_previewStepsDebounce, &DebounceTimer::triggered, this, &SceneController::applyPreviewStepsFromSpinBox);
    connect(m_view->startButton(), &QPushButton::clicked, this, &SceneController::onStartButtonClicked);
    connect(m_view->stopButton(), &QPushButton::clicked, this, &SceneController::onStopButtonClicked);
    connect(m_view->settingsButton(), &QPushButton::clicked, this, &SceneController::onSettingsButtonClicked);
    connect(m_view->addPrimitiveButton(), &QPushButton::clicked, this, &SceneController::onAddPrimitiveButtonClicked);
    connect(m_view->viewport(), &OpenGLViewportWidget::iterationChanged, m_view, &MainView::setIteration);
    connect(&AppSettings::instance(), &AppSettings::debounceMsChanged, this,
            [this](const QString& elementId, int ms) {
                if (elementId == DebounceElementIds::kRenderSize) {
                    m_renderSizeDebounce.setIntervalMs(ms);
                } else if (elementId == DebounceElementIds::kMaxSamples) {
                    m_maxSamplesDebounce.setIntervalMs(ms);
                } else if (elementId == DebounceElementIds::kPreviewSteps) {
                    m_previewStepsDebounce.setIntervalMs(ms);
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

void SceneController::onDebugVisualModeComboBoxChanged()
{
    m_model->setDebugVisualMode(visualModeFromComboIndex(m_view->debugVisualModeComboBox()->currentIndex()));
}

void SceneController::onSunAzimuthSpinBoxChanged()
{
    m_model->setSunAzimuthDeg(static_cast<float>(m_view->sunAzimuthSpinBox()->value()));
}

void SceneController::onSunElevationSpinBoxChanged()
{
    m_model->setSunElevationDeg(static_cast<float>(m_view->sunElevationSpinBox()->value()));
}

void SceneController::onSunColorButtonClicked()
{
    const QColor chosen = QColorDialog::getColor(
        m_model->sunColor(),
        m_view,
        QStringLiteral("Sun color"));

    if (chosen.isValid()) {
        m_model->setSunColor(chosen);
    }
}

void SceneController::onSunDiskSizeSpinBoxChanged()
{
    m_model->setSunDiskSizeDeg(static_cast<float>(m_view->sunDiskSizeSpinBox()->value()));
}

void SceneController::onSecondaryBounceSpinBoxChanged()
{
    m_model->setSecondaryBounceCount(m_view->secondaryBounceSpinBox()->value());
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

void SceneController::syncDebugVisualModeComboBox()
{
    m_view->debugVisualModeComboBox()->blockSignals(true);
    m_view->debugVisualModeComboBox()->setCurrentIndex(comboIndexFromVisualMode(m_model->debugVisualMode()));
    m_view->debugVisualModeComboBox()->blockSignals(false);
}

void SceneController::syncSunControls()
{
    m_view->sunAzimuthSpinBox()->blockSignals(true);
    m_view->sunElevationSpinBox()->blockSignals(true);
    m_view->sunDiskSizeSpinBox()->blockSignals(true);
    m_view->secondaryBounceSpinBox()->blockSignals(true);

    m_view->sunAzimuthSpinBox()->setValue(m_model->sunAzimuthDeg());
    m_view->sunElevationSpinBox()->setValue(m_model->sunElevationDeg());
    m_view->sunDiskSizeSpinBox()->setValue(m_model->sunDiskSizeDeg());
    m_view->secondaryBounceSpinBox()->setValue(m_model->secondaryBounceCount());
    m_view->sunColorButton()->setStyleSheet(colorButtonStyleSheet(m_model->sunColor()));

    m_view->sunAzimuthSpinBox()->blockSignals(false);
    m_view->sunElevationSpinBox()->blockSignals(false);
    m_view->sunDiskSizeSpinBox()->blockSignals(false);
    m_view->secondaryBounceSpinBox()->blockSignals(false);
}

void SceneController::syncBoundsOverlayComboBox()
{
    m_view->boundsOverlayComboBox()->blockSignals(true);
    m_view->boundsOverlayComboBox()->setCurrentIndex(
        comboIndexFromBoundsOverlayMode(m_model->boundsOverlayMode()));
    m_view->boundsOverlayComboBox()->blockSignals(false);
}
