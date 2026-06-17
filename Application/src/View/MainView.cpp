#include "MainView.h"

#include "AppLog.h"
#include "AppSettings.h"
#include "OpenGLViewportWidget.h"
#include "PhysicalCamera.h"
#include "RenderAccumulationState.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

namespace {

constexpr int kMinRenderDimension = 1;
constexpr int kMaxRenderDimension = 16384;
constexpr int kDefaultRenderDimension = 256;
constexpr int kMinMaxSamplesPerPixel = 0;
constexpr int kMaxMaxSamplesPerPixel = 1'000'000;
constexpr int kDefaultMaxSamplesPerPixel = 1024;
constexpr int kMinPreviewStepsPerLevel = 0;
constexpr int kMaxPreviewStepsPerLevel = 4;
constexpr int kDefaultPreviewStepsPerLevel = 1;
constexpr int kMinRussianRouletteMinDepth = 0;
constexpr int kMaxRussianRouletteMinDepth = 64;
constexpr int kDefaultRussianRouletteMinDepth = 3;
constexpr int kLogMaxBlockCount = 500;
constexpr int kControlPanelMinWidth = 160;
constexpr int kControlPanelInitialWidth = 220;
constexpr int kLogPanelMinHeight = 60;
constexpr int kLogPanelInitialHeight = 120;

} // namespace

MainView::MainView(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("PathTracer 0.0.1"));
    resize(800, 620);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_horizontalSplitter = new QSplitter(Qt::Horizontal, this);
    m_horizontalSplitter->setChildrenCollapsible(false);

    m_viewportHost = new QWidget(m_horizontalSplitter);
    m_viewportHost->setAutoFillBackground(true);
    m_viewportHost->setStyleSheet(QStringLiteral("background-color: #2a2a2a;"));
    m_viewportHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewportHost->installEventFilter(this);

    m_viewport = new OpenGLViewportWidget(m_viewportHost);
    m_viewport->setGeometry(m_viewportHost->rect());
    m_pendingViewportSize = m_viewportHost->size();

    auto* controlPanel = new QWidget(m_horizontalSplitter);
    controlPanel->setMinimumWidth(kControlPanelMinWidth);
    auto* controlLayout = new QVBoxLayout(controlPanel);
    controlLayout->setContentsMargins(8, 8, 8, 8);

    auto* renderGroup = new QGroupBox(QStringLiteral("Render"), controlPanel);
    auto* renderLayout = new QVBoxLayout(renderGroup);

    auto* widthRow = new QHBoxLayout();
    widthRow->addWidget(new QLabel(QStringLiteral("Width:"), renderGroup));
    m_renderWidthSpinBox = new QSpinBox(renderGroup);
    m_renderWidthSpinBox->setRange(kMinRenderDimension, kMaxRenderDimension);
    m_renderWidthSpinBox->setValue(kDefaultRenderDimension);
    widthRow->addWidget(m_renderWidthSpinBox);
    renderLayout->addLayout(widthRow);

    auto* heightRow = new QHBoxLayout();
    heightRow->addWidget(new QLabel(QStringLiteral("Height:"), renderGroup));
    m_renderHeightSpinBox = new QSpinBox(renderGroup);
    m_renderHeightSpinBox->setRange(kMinRenderDimension, kMaxRenderDimension);
    m_renderHeightSpinBox->setValue(kDefaultRenderDimension);
    heightRow->addWidget(m_renderHeightSpinBox);
    renderLayout->addLayout(heightRow);

    auto* samplesRow = new QHBoxLayout();
    samplesRow->addWidget(new QLabel(QStringLiteral("Samples:"), renderGroup));
    m_maxSamplesSpinBox = new QSpinBox(renderGroup);
    m_maxSamplesSpinBox->setRange(kMinMaxSamplesPerPixel, kMaxMaxSamplesPerPixel);
    m_maxSamplesSpinBox->setValue(kDefaultMaxSamplesPerPixel);
    m_maxSamplesSpinBox->setToolTip(QStringLiteral("Max samples per pixel (0 = unlimited)"));
    samplesRow->addWidget(m_maxSamplesSpinBox);
    renderLayout->addLayout(samplesRow);

    auto* previewRow = new QHBoxLayout();
    previewRow->addWidget(new QLabel(QStringLiteral("Preview:"), renderGroup));
    m_previewStepsSpinBox = new QSpinBox(renderGroup);
    m_previewStepsSpinBox->setRange(kMinPreviewStepsPerLevel, kMaxPreviewStepsPerLevel);
    m_previewStepsSpinBox->setValue(kDefaultPreviewStepsPerLevel);
    m_previewStepsSpinBox->setToolTip(
        QStringLiteral(
            "Number of dense low-res preview passes before full-resolution sampling (0 = disabled). "
            "Each level halves resolution (e.g. 2 levels = 1/4 then 1/2), upscaled with nearest-neighbor. "
            "Preview uses primary-ray-only shading for fast feedback."));
    previewRow->addWidget(m_previewStepsSpinBox);
    renderLayout->addLayout(previewRow);

    auto* rrDepthRow = new QHBoxLayout();
    rrDepthRow->addWidget(new QLabel(QStringLiteral("RR depth:"), renderGroup));
    m_russianRouletteMinDepthSpinBox = new QSpinBox(renderGroup);
    m_russianRouletteMinDepthSpinBox->setRange(kMinRussianRouletteMinDepth, kMaxRussianRouletteMinDepth);
    m_russianRouletteMinDepthSpinBox->setValue(kDefaultRussianRouletteMinDepth);
    m_russianRouletteMinDepthSpinBox->setToolTip(
        QStringLiteral(
            "Minimum path depth before Russian roulette may terminate a path. "
            "Higher values keep paths alive longer (0 = from the first bounce)."));
    rrDepthRow->addWidget(m_russianRouletteMinDepthSpinBox);
    renderLayout->addLayout(rrDepthRow);

    auto* boundsOverlayRow = new QHBoxLayout();
    boundsOverlayRow->addWidget(new QLabel(QStringLiteral("Bounds:"), renderGroup));
    m_boundsOverlayComboBox = new QComboBox(renderGroup);
    m_boundsOverlayComboBox->addItem(QStringLiteral("Off"));
    m_boundsOverlayComboBox->addItem(QStringLiteral("BVH"));
    m_boundsOverlayComboBox->setToolTip(
        QStringLiteral("Debug wireframe overlay for BVH node bounds."));
    boundsOverlayRow->addWidget(m_boundsOverlayComboBox, 1);
    renderLayout->addLayout(boundsOverlayRow);

    auto* iterationRow = new QHBoxLayout();
    iterationRow->addWidget(new QLabel(QStringLiteral("Iteration:"), renderGroup));
    m_iterationLabel = new QLabel(QStringLiteral("0"), renderGroup);
    m_iterationLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    iterationRow->addWidget(m_iterationLabel, 1);
    renderLayout->addLayout(iterationRow);

    auto* stateRow = new QHBoxLayout();
    stateRow->addWidget(new QLabel(QStringLiteral("Status:"), renderGroup));
    m_renderStateLabel = new QLabel(renderAccumulationStateLabel(RenderAccumulationState::Stopped), renderGroup);
    m_renderStateLabel->setToolTip(
        QStringLiteral("Accumulating: adding samples. Budget reached: increase Samples or set 0 for unlimited. "
                       "Stopped: press Start to resume (resets accumulation)."));
    stateRow->addWidget(m_renderStateLabel, 1);
    renderLayout->addLayout(stateRow);

    auto* renderControlRow = new QHBoxLayout();
    m_startButton = new QPushButton(QStringLiteral("Start"), renderGroup);
    m_startButton->setToolTip(
        QStringLiteral("Restart rendering and reset accumulated samples to zero."));
    m_stopButton = new QPushButton(QStringLiteral("Stop"), renderGroup);
    m_stopButton->setToolTip(QStringLiteral("Pause the render worker. Increase Samples to continue without pressing Start."));
    renderControlRow->addWidget(m_startButton);
    renderControlRow->addWidget(m_stopButton);
    renderLayout->addLayout(renderControlRow);

    controlLayout->addWidget(renderGroup);

    auto* environmentGroup = new QGroupBox(QStringLiteral("Environment"), controlPanel);
    auto* environmentLayout = new QVBoxLayout(environmentGroup);

    auto* backgroundRow = new QHBoxLayout();
    backgroundRow->addWidget(new QLabel(QStringLiteral("Background:"), environmentGroup));
    m_colorButton = new QPushButton(environmentGroup);
    m_colorButton->setFixedSize(24, 24);
    m_colorButton->setToolTip(
        QStringLiteral("Viewport background color and solid-color environment lighting when no HDRI is loaded"));
    backgroundRow->addWidget(m_colorButton);
    backgroundRow->addStretch();
    environmentLayout->addLayout(backgroundRow);

    auto* intensityRow = new QHBoxLayout();
    intensityRow->addWidget(new QLabel(QStringLiteral("Intensity:"), environmentGroup));
    m_environmentIntensitySpinBox = new QDoubleSpinBox(environmentGroup);
    m_environmentIntensitySpinBox->setRange(0.0, 100.0);
    m_environmentIntensitySpinBox->setDecimals(2);
    m_environmentIntensitySpinBox->setSingleStep(0.1);
    m_environmentIntensitySpinBox->setValue(1.0);
    m_environmentIntensitySpinBox->setToolTip(
        QStringLiteral("Scales radiance of the solid-color environment when no HDRI is loaded"));
    intensityRow->addWidget(m_environmentIntensitySpinBox, 1);
    environmentLayout->addLayout(intensityRow);

    auto* hdriRow = new QHBoxLayout();
    hdriRow->addWidget(new QLabel(QStringLiteral("HDRI:"), environmentGroup));
    m_environmentHdrPathEdit = new QLineEdit(environmentGroup);
    m_environmentHdrPathEdit->setReadOnly(true);
    m_environmentHdrPathEdit->setPlaceholderText(QStringLiteral("No HDR selected"));
    m_environmentHdrPathEdit->setToolTip(QStringLiteral("HDR image used for image-based lighting"));
    hdriRow->addWidget(m_environmentHdrPathEdit, 1);
    m_environmentHdrBrowseButton = new QPushButton(QStringLiteral("Browse…"), environmentGroup);
    m_environmentHdrBrowseButton->setToolTip(QStringLiteral("Select an HDR environment map"));
    hdriRow->addWidget(m_environmentHdrBrowseButton);
    m_environmentHdrClearButton = new QPushButton(QStringLiteral("Clear"), environmentGroup);
    m_environmentHdrClearButton->setToolTip(QStringLiteral("Remove the loaded HDRI and use solid-color environment lighting"));
    hdriRow->addWidget(m_environmentHdrClearButton);
    environmentLayout->addLayout(hdriRow);

    controlLayout->addWidget(environmentGroup);

    auto* physicalCameraGroup = new QGroupBox(QStringLiteral("Physical Camera"), controlPanel);
    auto* physicalCameraLayout = new QVBoxLayout(physicalCameraGroup);

    auto* fStopRow = new QHBoxLayout();
    fStopRow->addWidget(new QLabel(QStringLiteral("F-number:"), physicalCameraGroup));
    m_fStopSpinBox = new QDoubleSpinBox(physicalCameraGroup);
    m_fStopSpinBox->setRange(PhysicalCamera::kMinFStop, PhysicalCamera::kMaxFStop);
    m_fStopSpinBox->setDecimals(1);
    m_fStopSpinBox->setSingleStep(0.1);
    m_fStopSpinBox->setPrefix(QStringLiteral("f/"));
    m_fStopSpinBox->setValue(PhysicalCamera::kDefaultFStop);
    m_fStopSpinBox->setToolTip(QStringLiteral("Aperture f-number (lower = brighter exposure)"));
    fStopRow->addWidget(m_fStopSpinBox, 1);
    physicalCameraLayout->addLayout(fStopRow);

    auto* shutterRow = new QHBoxLayout();
    shutterRow->addWidget(new QLabel(QStringLiteral("Shutter:"), physicalCameraGroup));
    m_shutterSpeedComboBox = new QComboBox(physicalCameraGroup);
    for (std::size_t i = 0; i < PhysicalCamera::shutterSpeedPresetCount(); ++i) {
        const ShutterSpeedPreset& option = PhysicalCamera::shutterSpeedPresets()[i];
        m_shutterSpeedComboBox->addItem(
            QString::fromLatin1(option.label),
            static_cast<double>(option.seconds));
    }
    m_shutterSpeedComboBox->setToolTip(
        QStringLiteral("Shutter speed (longer = brighter exposure). Applied at display time."));
    shutterRow->addWidget(m_shutterSpeedComboBox, 1);
    physicalCameraLayout->addLayout(shutterRow);

    auto* isoRow = new QHBoxLayout();
    isoRow->addWidget(new QLabel(QStringLiteral("ISO:"), physicalCameraGroup));
    m_isoComboBox = new QComboBox(physicalCameraGroup);
    for (std::size_t i = 0; i < PhysicalCamera::isoPresetCount(); ++i) {
        const IsoPreset& option = PhysicalCamera::isoPresets()[i];
        m_isoComboBox->addItem(
            QString::fromLatin1(option.label),
            static_cast<double>(option.iso));
    }
    m_isoComboBox->setToolTip(QStringLiteral("Sensor sensitivity (higher = brighter exposure). Applied at display time."));
    isoRow->addWidget(m_isoComboBox, 1);
    physicalCameraLayout->addLayout(isoRow);

    auto* exposureRow = new QHBoxLayout();
    exposureRow->addWidget(new QLabel(QStringLiteral("Exposure EV:"), physicalCameraGroup));
    m_exposureValueLabel = new QLabel(QStringLiteral("0.0"), physicalCameraGroup);
    m_exposureValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_exposureValueLabel->setToolTip(QStringLiteral("Exposure value from f-number, shutter speed, and ISO"));
    exposureRow->addWidget(m_exposureValueLabel, 1);
    physicalCameraLayout->addLayout(exposureRow);

    controlLayout->addWidget(physicalCameraGroup);

    auto* lsystemGroup = new QGroupBox(QStringLiteral("LSystem"), controlPanel);
    auto* lsystemLayout = new QVBoxLayout(lsystemGroup);

    m_lsystemEdit = new QPlainTextEdit(lsystemGroup);
    m_lsystemEdit->setPlaceholderText(QStringLiteral("L-system definition (axiom and rules)"));
    m_lsystemEdit->setPlainText(QStringLiteral(
        "Mat(Black) = { 0.01, 0.01, 0.01, 1, 0, 0, 0, 1.5, 0, 0 }\n"
        "Mat(MediumGrey) = { 0.5, 0.5, 0.5, 1, 0, 0, 0, 1.5, 0, 0 }\n"
        "Mat(White) = { 1.0, 1.0, 1.0, 1, 0, 0, 0, 1.5, 0, 0 }\n"
        "Mat(GreenGlass) = { 0.8, 0.95, 0.75, 0, 0, 0.95, 0, 1.45, 0, 0 }\n"
        "Mat(Light) = { 0.60, 0.58, 0.10, 0, 0, 0, 0, 0, 0, 1 }\n"
        "d=0.2\n"
        "[Pitch(-90) Mat(White) f(-10000.5) F(0, 10000)]\n"
        "\n"
        "Mat(Black)\nF(0)\nf(d)\n"
        "Mat(MediumGrey)\nF(0)\nf(d)\n"
        "Mat(White)\nF(0)\nf(d)\n"
        "Mat(GreenGlass)\nF(0)\nf(d)\n"
        "Mat(Light)\nF(0, 0.05, 0.05)"));

    m_lsystemEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lsystemLayout->addWidget(m_lsystemEdit, 1);

    auto* lsystemIterationsRow = new QHBoxLayout();
    auto* lsystemIterationsLabel = new QLabel(QStringLiteral("Iterations:"), lsystemGroup);
    m_lsystemIterationsSpinBox = new QSpinBox(lsystemGroup);
    m_lsystemIterationsSpinBox->setRange(0, 32);
    m_lsystemIterationsSpinBox->setValue(0);
    m_lsystemIterationsSpinBox->setToolTip(
        QStringLiteral("0 = axiom only; N = apply rewrite rules N times"));
    lsystemIterationsRow->addWidget(lsystemIterationsLabel);
    lsystemIterationsRow->addWidget(m_lsystemIterationsSpinBox, 1);
    lsystemLayout->addLayout(lsystemIterationsRow);

    auto* lsystemActionsRow = new QHBoxLayout();
    m_lsystemLoadButton = new QPushButton(QStringLiteral("Load"), lsystemGroup);
    m_lsystemLoadButton->setToolTip(QStringLiteral("Load an L-system definition from a .lsystem file"));
    lsystemActionsRow->addWidget(m_lsystemLoadButton);
    m_addPrimitiveButton = new QPushButton(QStringLiteral("Add"), lsystemGroup);
    m_addPrimitiveButton->setToolTip(QStringLiteral("Build procedural mesh from L-system and add to scene"));
    lsystemActionsRow->addWidget(m_addPrimitiveButton);
    lsystemLayout->addLayout(lsystemActionsRow);

    controlLayout->addWidget(lsystemGroup, 1);

    auto* applicationGroup = new QGroupBox(QStringLiteral("Application"), controlPanel);
    auto* applicationLayout = new QVBoxLayout(applicationGroup);

    m_resetSceneButton = new QPushButton(QStringLiteral("Reset Scene"), applicationGroup);
    m_resetSceneButton->setToolTip(
        QStringLiteral("Remove all procedural meshes from the scene. The camera position is unchanged."));
    applicationLayout->addWidget(m_resetSceneButton);

    m_exportSceneButton = new QPushButton(QStringLiteral("Export Scene"), applicationGroup);
    m_exportSceneButton->setToolTip(
        QStringLiteral("Export cached scene geometry and materials as Wavefront OBJ + MTL for Blender."));
    applicationLayout->addWidget(m_exportSceneButton);

    m_settingsButton = new QPushButton(QStringLiteral("Settings"), applicationGroup);
    applicationLayout->addWidget(m_settingsButton);

    auto* closeButton = new QPushButton(QStringLiteral("Close"), applicationGroup);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    applicationLayout->addWidget(closeButton);

    controlLayout->addWidget(applicationGroup);

    m_horizontalSplitter->addWidget(m_viewportHost);
    m_horizontalSplitter->addWidget(controlPanel);
    m_horizontalSplitter->setStretchFactor(0, 1);
    m_horizontalSplitter->setStretchFactor(1, 0);
    m_horizontalSplitter->setSizes({580, kControlPanelInitialWidth});

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMinimumHeight(kLogPanelMinHeight);
    m_logView->setMaximumBlockCount(kLogMaxBlockCount);
    m_logView->setPlaceholderText(QStringLiteral("Log messages appear here..."));

    m_verticalSplitter = new QSplitter(Qt::Vertical, this);
    m_verticalSplitter->setChildrenCollapsible(false);
    m_verticalSplitter->addWidget(m_horizontalSplitter);
    m_verticalSplitter->addWidget(m_logView);
    m_verticalSplitter->setStretchFactor(0, 1);
    m_verticalSplitter->setStretchFactor(1, 0);
    m_verticalSplitter->setSizes({500, kLogPanelInitialHeight});

    rootLayout->addWidget(m_verticalSplitter);

    restoreLayoutFromSettings();

    connect(qApp, &QApplication::aboutToQuit, this, [this]() {
        saveLayoutToSettings();
    });

    connect(&AppLog::instance(), &AppLog::messageLogged, this, [this](const QString& line) {
        m_logView->appendPlainText(line);
        if (QScrollBar* scrollBar = m_logView->verticalScrollBar()) {
            scrollBar->setValue(scrollBar->maximum());
        }
    });
}

OpenGLViewportWidget* MainView::viewport() const
{
    return m_viewport;
}

QPushButton* MainView::colorButton() const
{
    return m_colorButton;
}

QLineEdit* MainView::environmentHdrPathEdit() const
{
    return m_environmentHdrPathEdit;
}

QPushButton* MainView::environmentHdrBrowseButton() const
{
    return m_environmentHdrBrowseButton;
}

QPushButton* MainView::environmentHdrClearButton() const
{
    return m_environmentHdrClearButton;
}

QDoubleSpinBox* MainView::environmentIntensitySpinBox() const
{
    return m_environmentIntensitySpinBox;
}

void MainView::setEnvironmentHdrPath(const QString& path)
{
    if (m_environmentHdrPathEdit != nullptr) {
        m_environmentHdrPathEdit->setText(path);
        m_environmentHdrPathEdit->setToolTip(path.isEmpty() ? m_environmentHdrPathEdit->placeholderText() : path);
    }
}

QDoubleSpinBox* MainView::fStopSpinBox() const
{
    return m_fStopSpinBox;
}

QComboBox* MainView::shutterSpeedComboBox() const
{
    return m_shutterSpeedComboBox;
}

QComboBox* MainView::isoComboBox() const
{
    return m_isoComboBox;
}

QLabel* MainView::exposureValueLabel() const
{
    return m_exposureValueLabel;
}

void MainView::setExposureValueText(const QString& text)
{
    if (m_exposureValueLabel != nullptr) {
        m_exposureValueLabel->setText(text);
    }
}

QSpinBox* MainView::renderWidthSpinBox() const
{
    return m_renderWidthSpinBox;
}

QSpinBox* MainView::renderHeightSpinBox() const
{
    return m_renderHeightSpinBox;
}

QSpinBox* MainView::maxSamplesSpinBox() const
{
    return m_maxSamplesSpinBox;
}

QSpinBox* MainView::previewStepsSpinBox() const
{
    return m_previewStepsSpinBox;
}

QSpinBox* MainView::russianRouletteMinDepthSpinBox() const
{
    return m_russianRouletteMinDepthSpinBox;
}

QComboBox* MainView::boundsOverlayComboBox() const
{
    return m_boundsOverlayComboBox;
}

QPushButton* MainView::startButton() const
{
    return m_startButton;
}

QPushButton* MainView::stopButton() const
{
    return m_stopButton;
}

QPushButton* MainView::settingsButton() const
{
    return m_settingsButton;
}

QPushButton* MainView::addPrimitiveButton() const
{
    return m_addPrimitiveButton;
}

QPushButton* MainView::lsystemLoadButton() const
{
    return m_lsystemLoadButton;
}

QPushButton* MainView::resetSceneButton() const
{
    return m_resetSceneButton;
}

QPushButton* MainView::exportSceneButton() const
{
    return m_exportSceneButton;
}

QPlainTextEdit* MainView::lsystemEdit() const
{
    return m_lsystemEdit;
}

QSpinBox* MainView::lsystemIterationsSpinBox() const
{
    return m_lsystemIterationsSpinBox;
}

void MainView::setIteration(int value)
{
    if (m_iterationLabel != nullptr) {
        m_iterationLabel->setText(QString::number(value));
    }
}

void MainView::setRenderState(RenderAccumulationState state, int sampleCount, int budgetTotal)
{
    Q_UNUSED(sampleCount);

    if (m_renderStateLabel == nullptr) {
        return;
    }

    QString text = renderAccumulationStateLabel(state);
    if (budgetTotal >= 0 && state == RenderAccumulationState::BudgetReached) {
        text += QStringLiteral(" (%1/%2)").arg(sampleCount).arg(budgetTotal);
    } else if (budgetTotal >= 0 && state == RenderAccumulationState::Accumulating) {
        text += QStringLiteral(" (%1/%2)").arg(sampleCount).arg(budgetTotal);
    }

    m_renderStateLabel->setText(text);
}

void MainView::closeEvent(QCloseEvent* event)
{
    saveLayoutToSettings();
    QWidget::closeEvent(event);
}

void MainView::restoreLayoutFromSettings()
{
    AppSettings& settings = AppSettings::instance();

    const QByteArray geometry = settings.windowGeometry();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    if (m_horizontalSplitter != nullptr) {
        const QByteArray horizontalState = settings.horizontalSplitterState();
        if (!horizontalState.isEmpty()) {
            m_horizontalSplitter->restoreState(horizontalState);
        }
    }

    if (m_verticalSplitter != nullptr) {
        const QByteArray verticalState = settings.verticalSplitterState();
        if (!verticalState.isEmpty()) {
            m_verticalSplitter->restoreState(verticalState);
        }
    }
}

void MainView::saveLayoutToSettings()
{
    AppSettings& settings = AppSettings::instance();
    settings.setWindowGeometry(saveGeometry());

    if (m_horizontalSplitter != nullptr) {
        settings.setHorizontalSplitterState(m_horizontalSplitter->saveState());
    }

    if (m_verticalSplitter != nullptr) {
        settings.setVerticalSplitterState(m_verticalSplitter->saveState());
    }
}

bool MainView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_viewportHost && event->type() == QEvent::Resize) {
        const auto* resizeEvent = static_cast<QResizeEvent*>(event);
        m_pendingViewportSize = resizeEvent->size();
        applyPendingViewportGeometry();
    }
    return QWidget::eventFilter(watched, event);
}

void MainView::applyPendingViewportGeometry()
{
    m_viewport->setGeometry(QRect(QPoint(0, 0), m_pendingViewportSize));
    m_viewport->update();
}

