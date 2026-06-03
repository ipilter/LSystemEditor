#include "SceneController.h"

#include "AppSettings.h"
#include "MainView.h"
#include "OpenGLViewportWidget.h"
#include "SceneModel.h"
#include "SettingsDialog.h"
#include "AddSdfDialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QSpinBox>

namespace {

QString colorButtonStyleSheet(const QColor& color)
{
    return QStringLiteral(
               "QPushButton { background-color: %1; border: 1px solid #333; }"
               "QPushButton:hover { border: 1px solid #666; }")
        .arg(color.name(QColor::HexRgb));
}

SdfDebugVisualMode visualModeFromComboIndex(int index)
{
    switch (index) {
    case 1:
        return SdfDebugVisualMode::HitDistance;
    case 2:
        return SdfDebugVisualMode::Off;
    case 0:
    default:
        return SdfDebugVisualMode::StepCount;
    }
}

int comboIndexFromVisualMode(SdfDebugVisualMode mode)
{
    switch (mode) {
    case SdfDebugVisualMode::HitDistance:
        return 1;
    case SdfDebugVisualMode::Off:
        return 2;
    case SdfDebugVisualMode::StepCount:
    default:
        return 0;
    }
}

SdfAccelBoundsOverlayMode boundsOverlayModeFromComboIndex(int index)
{
    switch (index) {
    case 1:
        return SdfAccelBoundsOverlayMode::Aabb;
    case 2:
        return SdfAccelBoundsOverlayMode::Octree;
    case 3:
        return SdfAccelBoundsOverlayMode::OctreeExterior;
    case 4:
        return SdfAccelBoundsOverlayMode::OctreeLeaves;
    case 5:
        return SdfAccelBoundsOverlayMode::Both;
    case 0:
    default:
        return SdfAccelBoundsOverlayMode::Off;
    }
}

int comboIndexFromBoundsOverlayMode(SdfAccelBoundsOverlayMode mode)
{
    switch (mode) {
    case SdfAccelBoundsOverlayMode::Aabb:
        return 1;
    case SdfAccelBoundsOverlayMode::Octree:
        return 2;
    case SdfAccelBoundsOverlayMode::OctreeExterior:
        return 3;
    case SdfAccelBoundsOverlayMode::OctreeLeaves:
        return 4;
    case SdfAccelBoundsOverlayMode::Both:
        return 5;
    case SdfAccelBoundsOverlayMode::Off:
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
    syncSdfVisualModeComboBox();
    syncBoundsOverlayComboBox();
    syncOctreeMaxDepthSpinBox();

    connect(m_view->colorButton(), &QPushButton::clicked, this, &SceneController::onColorButtonClicked);
    connect(m_model, &SceneModel::clearColorChanged, this, &SceneController::onClearColorChanged);
    connect(m_model, &SceneModel::renderSizeChanged, this, &SceneController::onRenderSizeChanged);
    connect(m_model, &SceneModel::maxSamplesPerPixelChanged, this, [this](int) { syncMaxSamplesSpinBox(); });
    connect(m_model, &SceneModel::previewStepsPerLevelChanged, this, [this](int) { syncPreviewStepsSpinBox(); });
    connect(m_model, &SceneModel::sdfVisualModeChanged, this, [this](SdfDebugVisualMode) { syncSdfVisualModeComboBox(); });
    connect(m_model, &SceneModel::boundsOverlayModeChanged, this, [this](SdfAccelBoundsOverlayMode) {
        syncBoundsOverlayComboBox();
    });
    connect(m_model, &SceneModel::octreeMaxDepthChanged, this, [this](int) { syncOctreeMaxDepthSpinBox(); });

    connect(m_view->renderWidthSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRenderSizeSpinBoxChanged);
    connect(m_view->renderHeightSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onRenderSizeSpinBoxChanged);
    connect(m_view->maxSamplesSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onMaxSamplesSpinBoxChanged);
    connect(m_view->previewStepsSpinBox(), &QSpinBox::valueChanged, this, &SceneController::onPreviewStepsSpinBoxChanged);
    connect(
        m_view->sdfVisualModeComboBox(),
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &SceneController::onSdfVisualModeComboBoxChanged);
    connect(
        m_view->boundsOverlayComboBox(),
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &SceneController::onBoundsOverlayComboBoxChanged);
    connect(
        m_view->octreeMaxDepthSpinBox(),
        QOverload<int>::of(&QSpinBox::valueChanged),
        this,
        &SceneController::onOctreeMaxDepthSpinBoxChanged);
    connect(&m_renderSizeDebounce, &DebounceTimer::triggered, this, &SceneController::applyRenderSizeFromSpinBoxes);
    connect(&m_maxSamplesDebounce, &DebounceTimer::triggered, this, &SceneController::applyMaxSamplesFromSpinBox);
    connect(&m_previewStepsDebounce, &DebounceTimer::triggered, this, &SceneController::applyPreviewStepsFromSpinBox);
    connect(m_view->startButton(), &QPushButton::clicked, this, &SceneController::onStartButtonClicked);
    connect(m_view->stopButton(), &QPushButton::clicked, this, &SceneController::onStopButtonClicked);
    connect(m_view->settingsButton(), &QPushButton::clicked, this, &SceneController::onSettingsButtonClicked);
    connect(m_view->addSdfButton(), &QPushButton::clicked, this, &SceneController::onAddSdfButtonClicked);
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

void SceneController::onSdfVisualModeComboBoxChanged()
{
    m_model->setSdfVisualMode(visualModeFromComboIndex(m_view->sdfVisualModeComboBox()->currentIndex()));
}

void SceneController::onBoundsOverlayComboBoxChanged()
{
    m_model->setBoundsOverlayMode(
        boundsOverlayModeFromComboIndex(m_view->boundsOverlayComboBox()->currentIndex()));
}

void SceneController::onOctreeMaxDepthSpinBoxChanged()
{
    m_model->setOctreeMaxDepth(m_view->octreeMaxDepthSpinBox()->value());
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
        m_model->setAccelAabbColor(AppSettings::instance().accelAabbColor());
        m_model->setAccelOctreeColor(AppSettings::instance().accelOctreeColor());
    }
}

void SceneController::onAddSdfButtonClicked()
{
    AddSdfDialog dialog(m_view);
    if (dialog.exec() == QDialog::Accepted) {
        m_model->addSdfShape(dialog.result());
    }
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

void SceneController::syncSdfVisualModeComboBox()
{
    m_view->sdfVisualModeComboBox()->blockSignals(true);
    m_view->sdfVisualModeComboBox()->setCurrentIndex(comboIndexFromVisualMode(m_model->sdfVisualMode()));
    m_view->sdfVisualModeComboBox()->blockSignals(false);
}

void SceneController::syncBoundsOverlayComboBox()
{
    m_view->boundsOverlayComboBox()->blockSignals(true);
    m_view->boundsOverlayComboBox()->setCurrentIndex(
        comboIndexFromBoundsOverlayMode(m_model->boundsOverlayMode()));
    m_view->boundsOverlayComboBox()->blockSignals(false);
}

void SceneController::syncOctreeMaxDepthSpinBox()
{
    m_view->octreeMaxDepthSpinBox()->blockSignals(true);
    m_view->octreeMaxDepthSpinBox()->setValue(m_model->octreeMaxDepth());
    m_view->octreeMaxDepthSpinBox()->blockSignals(false);
}
