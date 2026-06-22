#include "MainView.h"

#include "AppLog.h"
#include "AppSettings.h"
#include "OpenGLViewportWidget.h"
#include "PhysicalCamera.h"
#include "SceneUnits.h"
#include "RenderAccumulationState.h"
#include "ZoomablePlainTextEdit.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCheckBox>
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
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

constexpr int kMinRenderDimension = 1;
constexpr int kMaxRenderDimension = 16384;
constexpr int kDefaultRenderDimension = 256;
constexpr int kMinMaxSamplesPerPixel = 0;
constexpr int kMaxMaxSamplesPerPixel = 1'000'000;
constexpr int kDefaultMaxSamplesPerPixel = 1024;
constexpr int kMinMinSamples = 1;
constexpr int kMaxMinSamples = 10000;
constexpr int kDefaultMinSamples = 16;
constexpr double kMinRelativeErrorThreshold = 0.001;
constexpr double kMaxRelativeErrorThreshold = 1.0;
constexpr double kDefaultRelativeErrorThreshold = 0.02;
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

    m_controlTabWidget = new QTabWidget(controlPanel);
    auto* lsystemTab = new QWidget(m_controlTabWidget);
    auto* lsystemTabLayout = new QVBoxLayout(lsystemTab);
    lsystemTabLayout->setContentsMargins(0, 0, 0, 0);

    auto* controlsTab = new QWidget(m_controlTabWidget);
    auto* controlsTabLayout = new QVBoxLayout(controlsTab);
    controlsTabLayout->setContentsMargins(0, 0, 0, 0);

    auto* renderGroup = new QGroupBox(QStringLiteral("Render"), controlsTab);
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

    auto* maxSamplesRow = new QHBoxLayout();
    maxSamplesRow->addWidget(new QLabel(QStringLiteral("Max samples:"), renderGroup));
    m_maxSamplesSpinBox = new QSpinBox(renderGroup);
    m_maxSamplesSpinBox->setRange(kMinMaxSamplesPerPixel, kMaxMaxSamplesPerPixel);
    m_maxSamplesSpinBox->setValue(kDefaultMaxSamplesPerPixel);
    m_maxSamplesSpinBox->setToolTip(
        QStringLiteral("Per-pixel sample cap for adaptive sampling (0 = unlimited)."));
    maxSamplesRow->addWidget(m_maxSamplesSpinBox);
    renderLayout->addLayout(maxSamplesRow);

    auto* minSamplesRow = new QHBoxLayout();
    minSamplesRow->addWidget(new QLabel(QStringLiteral("Min samples:"), renderGroup));
    m_minSamplesSpinBox = new QSpinBox(renderGroup);
    m_minSamplesSpinBox->setRange(kMinMinSamples, kMaxMinSamples);
    m_minSamplesSpinBox->setValue(kDefaultMinSamples);
    m_minSamplesSpinBox->setToolTip(
        QStringLiteral("Minimum samples per pixel before convergence can be declared."));
    minSamplesRow->addWidget(m_minSamplesSpinBox);
    renderLayout->addLayout(minSamplesRow);

    auto* errorRow = new QHBoxLayout();
    errorRow->addWidget(new QLabel(QStringLiteral("Error:"), renderGroup));
    m_relativeErrorThresholdSpinBox = new QDoubleSpinBox(renderGroup);
    m_relativeErrorThresholdSpinBox->setRange(kMinRelativeErrorThreshold, kMaxRelativeErrorThreshold);
    m_relativeErrorThresholdSpinBox->setDecimals(3);
    m_relativeErrorThresholdSpinBox->setSingleStep(0.005);
    m_relativeErrorThresholdSpinBox->setValue(kDefaultRelativeErrorThreshold);
    m_relativeErrorThresholdSpinBox->setToolTip(
        QStringLiteral("Relative luminance error threshold (lower = cleaner, slower)."));
    errorRow->addWidget(m_relativeErrorThresholdSpinBox);
    renderLayout->addLayout(errorRow);

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
    boundsOverlayRow->addWidget(new QLabel(QStringLiteral("View Mode:"), renderGroup));
    m_renderViewOverlayComboBox = new QComboBox(renderGroup);
    m_renderViewOverlayComboBox->addItem(QStringLiteral("Render"));
    m_renderViewOverlayComboBox->addItem(QStringLiteral("BVH"));
    m_renderViewOverlayComboBox->addItem(QStringLiteral("Adaptive"));
    m_renderViewOverlayComboBox->addItem(QStringLiteral("UV"));
    m_renderViewOverlayComboBox->addItem(QStringLiteral("Normals"));
    m_renderViewOverlayComboBox->setToolTip(
        QStringLiteral(
            "View modes: Render (path-traced image), BVH wireframe overlay, "
            "Adaptive sampling (red = active, dark green = converged), "
            "UV (U to red, V to green), "
            "Normals (R=±X, G=±Y, B=±Z; uses interpolated split normals)."));
    boundsOverlayRow->addWidget(m_renderViewOverlayComboBox, 1);
    renderLayout->addLayout(boundsOverlayRow);

    auto* brdfDebugRow = new QHBoxLayout();
    brdfDebugRow->addWidget(new QLabel(QStringLiteral("Glass debug:"), renderGroup));
    m_brdfDebugComboBox = new QComboBox(renderGroup);
    m_brdfDebugComboBox->addItem(QStringLiteral("Normal"));
    m_brdfDebugComboBox->addItem(QStringLiteral("Transmit lobe only"));
    m_brdfDebugComboBox->addItem(QStringLiteral("Reflection only"));
    m_brdfDebugComboBox->addItem(QStringLiteral("Refraction only"));
    m_brdfDebugComboBox->addItem(QStringLiteral("Glass path tint"));
    m_brdfDebugComboBox->addItem(QStringLiteral("No TIR fallback"));
    m_brdfDebugComboBox->setToolTip(
        QStringLiteral(
            "Debug glass/transmission paths in the path tracer. "
            "Normal uses standard BRDF sampling. Other modes isolate refracted rays, "
            "reflected rays, or tint transmitted paths green and reflected paths red."));
    brdfDebugRow->addWidget(m_brdfDebugComboBox, 1);
    renderLayout->addLayout(brdfDebugRow);

    m_sceneOverlayCheckBox = new QCheckBox(QStringLiteral("Show overlay"), renderGroup);
    m_sceneOverlayCheckBox->setChecked(true);
    m_sceneOverlayCheckBox->setToolTip(
        QStringLiteral("Show the origin gizmo and BVH wireframe overlay on the rendered image."));
    renderLayout->addWidget(m_sceneOverlayCheckBox);

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

    controlsTabLayout->addWidget(renderGroup);

    auto* regionRenderGroup = new QGroupBox(QStringLiteral("Region Render"), controlsTab);
    auto* regionRenderLayout = new QVBoxLayout(regionRenderGroup);

    m_regionRenderCheckBox = new QCheckBox(QStringLiteral("Region Render"), regionRenderGroup);
    m_regionRenderCheckBox->setToolTip(
        QStringLiteral("When enabled, CUDA samples only the selected region. Pixels outside keep their last state."));
    regionRenderLayout->addWidget(m_regionRenderCheckBox);

    auto* bottomLeftRow = new QHBoxLayout();
    bottomLeftRow->addWidget(new QLabel(QStringLiteral("Bottom-left:"), regionRenderGroup));
    m_regionBottomLeftXSpinBox = new QSpinBox(regionRenderGroup);
    m_regionBottomLeftXSpinBox->setRange(kMinRenderDimension - 1, kMaxRenderDimension - 1);
    m_regionBottomLeftXSpinBox->setToolTip(QStringLiteral("Bottom-left corner X in image pixels."));
    bottomLeftRow->addWidget(m_regionBottomLeftXSpinBox);
    m_regionBottomLeftYSpinBox = new QSpinBox(regionRenderGroup);
    m_regionBottomLeftYSpinBox->setRange(kMinRenderDimension - 1, kMaxRenderDimension - 1);
    m_regionBottomLeftYSpinBox->setToolTip(QStringLiteral("Bottom-left corner Y in image pixels."));
    bottomLeftRow->addWidget(m_regionBottomLeftYSpinBox);
    regionRenderLayout->addLayout(bottomLeftRow);

    auto* topRightRow = new QHBoxLayout();
    topRightRow->addWidget(new QLabel(QStringLiteral("Top-right:"), regionRenderGroup));
    m_regionTopRightXSpinBox = new QSpinBox(regionRenderGroup);
    m_regionTopRightXSpinBox->setRange(kMinRenderDimension - 1, kMaxRenderDimension - 1);
    m_regionTopRightXSpinBox->setToolTip(QStringLiteral("Top-right corner X in image pixels."));
    topRightRow->addWidget(m_regionTopRightXSpinBox);
    m_regionTopRightYSpinBox = new QSpinBox(regionRenderGroup);
    m_regionTopRightYSpinBox->setRange(kMinRenderDimension - 1, kMaxRenderDimension - 1);
    m_regionTopRightYSpinBox->setToolTip(QStringLiteral("Top-right corner Y in image pixels."));
    topRightRow->addWidget(m_regionTopRightYSpinBox);
    regionRenderLayout->addLayout(topRightRow);

    m_defineRegionButton = new QPushButton(QStringLiteral("Define Region"), regionRenderGroup);
    m_defineRegionButton->setCheckable(true);
    m_defineRegionButton->setToolTip(
        QStringLiteral("Click two corners on the image to define the region (bottom-left then top-right)."));
    regionRenderLayout->addWidget(m_defineRegionButton);

    controlsTabLayout->addWidget(regionRenderGroup);

    auto* environmentGroup = new QGroupBox(QStringLiteral("Environment"), controlsTab);
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

    controlsTabLayout->addWidget(environmentGroup);

    auto* physicalCameraGroup = new QGroupBox(QStringLiteral("Physical Camera"), controlsTab);
    physicalCameraGroup->setToolTip(
        QStringLiteral("Shift+click the render view to set depth-of-field focus on a surface."));
    auto* physicalCameraLayout = new QVBoxLayout(physicalCameraGroup);

    auto* focalLengthRow = new QHBoxLayout();
    focalLengthRow->addWidget(new QLabel(QStringLiteral("Focal length:"), physicalCameraGroup));
    m_focalLengthSpinBox = new QDoubleSpinBox(physicalCameraGroup);
    m_focalLengthSpinBox->setRange(PhysicalCamera::kMinFocalLengthMm, PhysicalCamera::kMaxFocalLengthMm);
    m_focalLengthSpinBox->setDecimals(0);
    m_focalLengthSpinBox->setSingleStep(1.0);
    m_focalLengthSpinBox->setSuffix(QStringLiteral(" mm"));
    m_focalLengthSpinBox->setValue(PhysicalCamera::kDefaultFocalLengthMm);
    m_focalLengthSpinBox->setToolTip(
        QStringLiteral("Lens focal length for a Nikon Z7 full-frame sensor (14–1000 mm). "
                       "Controls field of view and depth-of-field aperture."));
    focalLengthRow->addWidget(m_focalLengthSpinBox, 1);
    physicalCameraLayout->addLayout(focalLengthRow);

    auto* focusDistanceRow = new QHBoxLayout();
    focusDistanceRow->addWidget(new QLabel(QStringLiteral("Focus distance:"), physicalCameraGroup));
    m_focusDistanceSpinBox = new QDoubleSpinBox(physicalCameraGroup);
    m_focusDistanceSpinBox->setRange(PhysicalCamera::kMinFocalLengthMm, SceneUnits::kDefaultRayTMaxMm);
    m_focusDistanceSpinBox->setDecimals(0);
    m_focusDistanceSpinBox->setSingleStep(10.0);
    m_focusDistanceSpinBox->setSuffix(QStringLiteral(" mm"));
    m_focusDistanceSpinBox->setValue(PhysicalCamera::kDefaultFocusDistance);
    m_focusDistanceSpinBox->setToolTip(
        QStringLiteral("Distance from the camera to the depth-of-field focus plane along the view axis. "
                       "Shift+click the render view to pin focus to a surface; editing this value returns to manual focus."));
    focusDistanceRow->addWidget(m_focusDistanceSpinBox, 1);
    physicalCameraLayout->addLayout(focusDistanceRow);

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

    controlsTabLayout->addWidget(physicalCameraGroup);

    m_lsystemEdit = new ZoomablePlainTextEdit(
        []() { return AppSettings::instance().lsystemEditorFontSize(); },
        [](int size) { AppSettings::instance().setLsystemEditorFontSize(size); },
        lsystemTab);
    m_lsystemEdit->setPlaceholderText(
        QStringLiteral("L-system definition (axiom and rules)\n# Distances and radii are in millimeters (mm)."));
    m_lsystemEdit->setPlainText(QStringLiteral(
      "# Distances and radii are in millimeters (mm).\n"
      "# Mat(id) = { property: value, ... }  — omit any property to use its default\n"
      "# albedo / alb: {r,g,b} | grayScalar | {Grid,...} | {Stripe,...} | {Noise,...}  default 0.8 gray\n"
      "# roughness / rou: scalar | texture  default 0.5\n"
      "# metallic / met: scalar | texture  default 0\n"
      "# transmission / trans: scalar | texture  default 0\n"
      "# thin: scalar | texture  default 0\n"
      "# ior: scalar | texture  default 1.5\n"
      "# subsurface / subs: scalar | texture  default 0\n"
      "# emission / emiss / em: scalar | texture  default 0  (1.0 ~ moderate area light)\n"
      "# diffuseRoughness / diffuse / diffRou: scalar | texture  default -1 (uses roughness)\n"
      "# scatterRadiusR / scatterR / sR: scalar | texture  default 0\n"
      "# scatterRadiusG / scatterG / sG: scalar | texture  default 0\n"
      "# scatterRadiusB / scatterB / sB: scalar | texture  default 0\n"
      "# specular / spec: scalar | texture  default 1\n"
      "# scalar textures multiply inline when inline > 0; inline 0 uses texture values directly\n"
      "# Procedural textures share on/off RGB (or gray), intensityOn/intensityOff, plus type-specific keys:\n"
      "# Grid: {Grid, on:{r,g,b}|gray, off:{r,g,b}|gray, intensityOn:1, intensityOff:1, freq:8, freqU:8, freqV:8, thickness:0.05}\n"
      "# Stripe: {Stripe, on:{r,g,b}|gray, off:{r,g,b}|gray, intensityOn:1, intensityOff:1, freq, thickness:0.05}\n"
      "# Noise: {Noise, on:{r,g,b}|gray, off:{r,g,b}|gray, intensityOn:1, intensityOff:1, scale, octaves:1, seed:0}\n"
      "# Legacy positional syntax still supported: {r,g,b, [roughness], [metallic], ...}\n\n"
      "Mat(Wax) = {albedo: {0.9, 0.95, 0.2}, roughness: 0.8, subsurface: 0.8, diffuseRoughness: 0.7, scatterRadiusR: 0.05, scatterRadiusG: 0.04, scatterRadiusB: 0.02, specular: 0.2}\n"
      "Mat(Leaf) = {albedo: {0.2, 0.8, 0.1}, roughness: 0.5, transmission: 1, thin: 1, ior: 1.5, subsurface: 0.2, diffuseRoughness: 0.5, scatterRadiusR: 0.01, scatterRadiusG: 0.02, scatterRadiusB: 0.005, specular: 1.0}\n"
      "Mat(Grid) = {albedo: {Grid, on: {0.95, 0.95, 0.95}, off: {0.1, 0.1, 0.1}, freq: 400, thickness: 0.05}, roughness: 0.5}\n"
      "Mat(Light) = {albedo: {0.9, 0.8, 0.7}, roughness: 0.65, metallic: 1, emission: {Stripe, freq: 20, thickness: 0.25, on: 1, off: 0, intensityOn: 50}}\n"
      "Mat(Metal) = {albedo: {0.9, 0.5, 0.2}, roughness: 0.15, metallic: 1, ior: 1.45}\n"
      "Mat(Glass) = {albedo: {0.810, 0.929, 0.78}, roughness: 0.15, transmission: 0.95, ior: 1.42}\n"
      "Mat(Diffuse) = {albedo: {0.9, 0.9, 0.9}, roughness: 0.90}\n"
      "Mat(Plastic) = {albedo: {0.99, 0.7, 0.94}, roughness: 0.1, ior: 1.5, subsurface: 1}\n\n"
      "Mat(Pearl) = {albedo: {0.9, 0.8, 0.7}, roughness: 0.35, subsurface: 0.85}\n\n"
      "Mat(Grid)\n"
      "Pitch(-90) f(-100) F(100, 5000, 5000)\n\n"
      "f(100)\n\n"
      "Mat(Diffuse)\n"
      "F(0, 100)\n\n"
      "f(500)\n"
      "Mat(Light)\n"
      "F(0, 100)\n\n"
      "f(-500)\n"
      "Pitch(-90) f(250) Pitch(90)\n"
      "Mat(Glass)\n"
      "F(0, 100)\n\n"
      "Pitch(-90) f(250) Pitch(90)\n"
      "Mat(Metal)\n"
      "F(0, 100)\n\n"
      "Pitch(-90) f(250) Pitch(90)\n"
      "Mat(Plastic)\n"
      "F(0, 100)\n"
      "Pitch(-90) f(250) Pitch(90)\n"
      "Mat(Wax)\n"
      "F(0, 100)\n"
      "Pitch(-90) f(250) Pitch(90)\n"
      "Mat(Pearl)\n"
      "F(0, 100)\n"
    ));

    m_lsystemEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lsystemTabLayout->addWidget(m_lsystemEdit, 1);

    auto* lsystemIterationsRow = new QHBoxLayout();
    auto* lsystemIterationsLabel = new QLabel(QStringLiteral("Iterations:"), lsystemTab);
    m_lsystemIterationsSpinBox = new QSpinBox(lsystemTab);
    m_lsystemIterationsSpinBox->setRange(0, 32);
    m_lsystemIterationsSpinBox->setValue(0);
    m_lsystemIterationsSpinBox->setToolTip(
        QStringLiteral("0 = axiom only; N = apply rewrite rules N times"));
    lsystemIterationsRow->addWidget(lsystemIterationsLabel);
    lsystemIterationsRow->addWidget(m_lsystemIterationsSpinBox, 1);
    lsystemTabLayout->addLayout(lsystemIterationsRow);

    auto* lsystemActionsRow = new QHBoxLayout();
    m_lsystemLoadButton = new QPushButton(QStringLiteral("Load"), lsystemTab);
    m_lsystemLoadButton->setToolTip(QStringLiteral("Load an L-system definition from a .lsystem file"));
    lsystemActionsRow->addWidget(m_lsystemLoadButton);
    m_addPrimitiveButton = new QPushButton(QStringLiteral("Add"), lsystemTab);
    m_addPrimitiveButton->setToolTip(QStringLiteral("Build procedural mesh from L-system and add to scene"));
    lsystemActionsRow->addWidget(m_addPrimitiveButton);
    m_resetSceneButton = new QPushButton(QStringLiteral("Reset Scene"), lsystemTab);
    m_resetSceneButton->setToolTip(
        QStringLiteral("Remove all procedural meshes from the scene. The camera position is unchanged."));
    lsystemActionsRow->addWidget(m_resetSceneButton);
    lsystemTabLayout->addLayout(lsystemActionsRow);

    auto* applicationGroup = new QGroupBox(QStringLiteral("Application"), controlsTab);
    auto* applicationLayout = new QVBoxLayout(applicationGroup);

    m_exportSceneButton = new QPushButton(QStringLiteral("Export Scene"), applicationGroup);
    m_exportSceneButton->setToolTip(
        QStringLiteral("Export cached scene geometry and materials as Wavefront OBJ + MTL for Blender."));
    applicationLayout->addWidget(m_exportSceneButton);

    m_settingsButton = new QPushButton(QStringLiteral("Settings"), applicationGroup);
    applicationLayout->addWidget(m_settingsButton);

    auto* closeButton = new QPushButton(QStringLiteral("Close"), applicationGroup);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    applicationLayout->addWidget(closeButton);

    controlsTabLayout->addWidget(applicationGroup);

    m_controlTabWidget->addTab(lsystemTab, QStringLiteral("L-System"));
    m_controlTabWidget->addTab(controlsTab, QStringLiteral("Controls"));
    m_controlTabWidget->setCurrentIndex(0);
    controlLayout->addWidget(m_controlTabWidget, 1);

    m_horizontalSplitter->addWidget(m_viewportHost);
    m_horizontalSplitter->addWidget(controlPanel);
    m_horizontalSplitter->setStretchFactor(0, 1);
    m_horizontalSplitter->setStretchFactor(1, 0);
    m_horizontalSplitter->setSizes({580, kControlPanelInitialWidth});

    m_logView = new ZoomablePlainTextEdit(
        []() { return AppSettings::instance().logFontSize(); },
        [](int size) { AppSettings::instance().setLogFontSize(size); },
        this);
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

QDoubleSpinBox* MainView::focalLengthSpinBox() const
{
    return m_focalLengthSpinBox;
}

QDoubleSpinBox* MainView::focusDistanceSpinBox() const
{
    return m_focusDistanceSpinBox;
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

QSpinBox* MainView::minSamplesSpinBox() const
{
    return m_minSamplesSpinBox;
}

QDoubleSpinBox* MainView::relativeErrorThresholdSpinBox() const
{
    return m_relativeErrorThresholdSpinBox;
}

QSpinBox* MainView::previewStepsSpinBox() const
{
    return m_previewStepsSpinBox;
}

QSpinBox* MainView::russianRouletteMinDepthSpinBox() const
{
    return m_russianRouletteMinDepthSpinBox;
}

QComboBox* MainView::renderViewOverlayComboBox() const
{
    return m_renderViewOverlayComboBox;
}

QComboBox* MainView::brdfDebugComboBox() const
{
    return m_brdfDebugComboBox;
}

QCheckBox* MainView::sceneOverlayCheckBox() const
{
    return m_sceneOverlayCheckBox;
}

QCheckBox* MainView::regionRenderCheckBox() const
{
    return m_regionRenderCheckBox;
}

QSpinBox* MainView::regionBottomLeftXSpinBox() const
{
    return m_regionBottomLeftXSpinBox;
}

QSpinBox* MainView::regionBottomLeftYSpinBox() const
{
    return m_regionBottomLeftYSpinBox;
}

QSpinBox* MainView::regionTopRightXSpinBox() const
{
    return m_regionTopRightXSpinBox;
}

QSpinBox* MainView::regionTopRightYSpinBox() const
{
    return m_regionTopRightYSpinBox;
}

QPushButton* MainView::defineRegionButton() const
{
    return m_defineRegionButton;
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

void MainView::setRenderState(
    RenderAccumulationState state,
    int sampleCount,
    int budgetTotal,
    int activePixelCount)
{
    if (m_renderStateLabel == nullptr) {
        return;
    }

    QString text = renderAccumulationStateLabel(state);
    if (state == RenderAccumulationState::BudgetReached) {
        if (activePixelCount >= 0) {
            text += QStringLiteral(" (%1, converged)").arg(sampleCount);
        } else if (budgetTotal >= 0) {
            text += QStringLiteral(" (%1/%2)").arg(sampleCount).arg(budgetTotal);
        }
    } else if (state == RenderAccumulationState::Accumulating) {
        if (activePixelCount >= 0) {
            text += QStringLiteral(" (%1, %2 active)").arg(sampleCount).arg(activePixelCount);
        } else if (budgetTotal >= 0) {
            text += QStringLiteral(" (%1/%2)").arg(sampleCount).arg(budgetTotal);
        }
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

