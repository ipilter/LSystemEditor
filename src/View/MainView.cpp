#include "MainView.h"

#include "AppLog.h"
#include "AppSettings.h"
#include "OpenGLViewportWidget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
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
constexpr double kDefaultSunAzimuth = 20.0;
constexpr double kDefaultSunElevation = 45.0;
constexpr double kDefaultSunDiskSize = 0.53;
constexpr int kDefaultSecondaryBounceCount = 1;
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

    auto* horizontalSplitter = new QSplitter(Qt::Horizontal, this);
    horizontalSplitter->setChildrenCollapsible(false);

    m_viewportHost = new QWidget(horizontalSplitter);
    m_viewportHost->setAutoFillBackground(true);
    m_viewportHost->setStyleSheet(QStringLiteral("background-color: #2a2a2a;"));
    m_viewportHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewportHost->installEventFilter(this);

    m_viewport = new OpenGLViewportWidget(m_viewportHost);
    m_viewport->setGeometry(m_viewportHost->rect());
    m_pendingViewportSize = m_viewportHost->size();

    auto* controlPanel = new QWidget(horizontalSplitter);
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
        QStringLiteral("Coarse preview iterations before full-resolution samples (0 = disabled)"));
    previewRow->addWidget(m_previewStepsSpinBox);
    renderLayout->addLayout(previewRow);

    auto* visualModeRow = new QHBoxLayout();
    visualModeRow->addWidget(new QLabel(QStringLiteral("Debug:"), renderGroup));
    m_debugVisualModeComboBox = new QComboBox(renderGroup);
    m_debugVisualModeComboBox->addItem(QStringLiteral("Off"));
    m_debugVisualModeComboBox->addItem(QStringLiteral("Normals"));
    m_debugVisualModeComboBox->setToolTip(
        QStringLiteral("Normals: surface normal debug view. Off: sun shading with shadows."));
    visualModeRow->addWidget(m_debugVisualModeComboBox, 1);
    renderLayout->addLayout(visualModeRow);

    auto* sunAzimuthRow = new QHBoxLayout();
    sunAzimuthRow->addWidget(new QLabel(QStringLiteral("Sun azimuth:"), renderGroup));
    m_sunAzimuthSpinBox = new QDoubleSpinBox(renderGroup);
    m_sunAzimuthSpinBox->setRange(0.0, 360.0);
    m_sunAzimuthSpinBox->setDecimals(1);
    m_sunAzimuthSpinBox->setValue(kDefaultSunAzimuth);
    m_sunAzimuthSpinBox->setToolTip(QStringLiteral("Sun direction azimuth in degrees"));
    sunAzimuthRow->addWidget(m_sunAzimuthSpinBox);
    renderLayout->addLayout(sunAzimuthRow);

    auto* sunElevationRow = new QHBoxLayout();
    sunElevationRow->addWidget(new QLabel(QStringLiteral("Sun elevation:"), renderGroup));
    m_sunElevationSpinBox = new QDoubleSpinBox(renderGroup);
    m_sunElevationSpinBox->setRange(-90.0, 90.0);
    m_sunElevationSpinBox->setDecimals(1);
    m_sunElevationSpinBox->setValue(kDefaultSunElevation);
    m_sunElevationSpinBox->setToolTip(QStringLiteral("Sun direction elevation in degrees"));
    sunElevationRow->addWidget(m_sunElevationSpinBox);
    renderLayout->addLayout(sunElevationRow);

    auto* sunColorRow = new QHBoxLayout();
    sunColorRow->addWidget(new QLabel(QStringLiteral("Sun color:"), renderGroup));
    m_sunColorButton = new QPushButton(renderGroup);
    m_sunColorButton->setFixedSize(24, 24);
    m_sunColorButton->setToolTip(QStringLiteral("Sun light color"));
    sunColorRow->addWidget(m_sunColorButton);
    sunColorRow->addStretch();
    renderLayout->addLayout(sunColorRow);

    auto* sunDiskRow = new QHBoxLayout();
    sunDiskRow->addWidget(new QLabel(QStringLiteral("Sun disk:"), renderGroup));
    m_sunDiskSizeSpinBox = new QDoubleSpinBox(renderGroup);
    m_sunDiskSizeSpinBox->setRange(0.1, 10.0);
    m_sunDiskSizeSpinBox->setDecimals(2);
    m_sunDiskSizeSpinBox->setSuffix(QStringLiteral(" deg"));
    m_sunDiskSizeSpinBox->setValue(kDefaultSunDiskSize);
    m_sunDiskSizeSpinBox->setToolTip(QStringLiteral("Angular diameter of the sun disk for soft shadows"));
    sunDiskRow->addWidget(m_sunDiskSizeSpinBox);
    renderLayout->addLayout(sunDiskRow);

    auto* secondaryBounceRow = new QHBoxLayout();
    secondaryBounceRow->addWidget(new QLabel(QStringLiteral("Sec. bounces:"), renderGroup));
    m_secondaryBounceSpinBox = new QSpinBox(renderGroup);
    m_secondaryBounceSpinBox->setRange(0, 8);
    m_secondaryBounceSpinBox->setValue(kDefaultSecondaryBounceCount);
    m_secondaryBounceSpinBox->setToolTip(QStringLiteral("Number of secondary light bounces"));
    secondaryBounceRow->addWidget(m_secondaryBounceSpinBox);
    renderLayout->addLayout(secondaryBounceRow);

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

    auto* backgroundRow = new QHBoxLayout();
    backgroundRow->addWidget(new QLabel(QStringLiteral("Background:"), renderGroup));
    m_colorButton = new QPushButton(renderGroup);
    m_colorButton->setFixedSize(24, 24);
    m_colorButton->setToolTip(QStringLiteral("Background color"));
    backgroundRow->addWidget(m_colorButton);
    backgroundRow->addStretch();
    renderLayout->addLayout(backgroundRow);

    auto* renderControlRow = new QHBoxLayout();
    m_startButton = new QPushButton(QStringLiteral("Start"), renderGroup);
    m_stopButton = new QPushButton(QStringLiteral("Stop"), renderGroup);
    renderControlRow->addWidget(m_startButton);
    renderControlRow->addWidget(m_stopButton);
    renderLayout->addLayout(renderControlRow);

    controlLayout->addWidget(renderGroup);

    auto* lsystemGroup = new QGroupBox(QStringLiteral("LSystem"), controlPanel);
    auto* lsystemLayout = new QVBoxLayout(lsystemGroup);

    m_lsystemEdit = new QPlainTextEdit(lsystemGroup);
    m_lsystemEdit->setPlaceholderText(QStringLiteral("L-system definition (axiom and rules)"));
    m_lsystemEdit->setPlainText(QStringLiteral("Mat(0) = {0.9, 0.2, 0.1, 0.9, 0.2}\nMat(0)\nF(0, 0.5)"));
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

    m_addPrimitiveButton = new QPushButton(QStringLiteral("Add"), lsystemGroup);
    m_addPrimitiveButton->setToolTip(QStringLiteral("Build procedural mesh from L-system and add to scene"));
    lsystemLayout->addWidget(m_addPrimitiveButton);

    controlLayout->addWidget(lsystemGroup, 1);

    auto* applicationGroup = new QGroupBox(QStringLiteral("Application"), controlPanel);
    auto* applicationLayout = new QVBoxLayout(applicationGroup);

    m_settingsButton = new QPushButton(QStringLiteral("Settings"), applicationGroup);
    applicationLayout->addWidget(m_settingsButton);

    auto* closeButton = new QPushButton(QStringLiteral("Close"), applicationGroup);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    applicationLayout->addWidget(closeButton);

    controlLayout->addWidget(applicationGroup);

    horizontalSplitter->addWidget(m_viewportHost);
    horizontalSplitter->addWidget(controlPanel);
    horizontalSplitter->setStretchFactor(0, 1);
    horizontalSplitter->setStretchFactor(1, 0);
    horizontalSplitter->setSizes({580, kControlPanelInitialWidth});

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMinimumHeight(kLogPanelMinHeight);
    m_logView->setMaximumBlockCount(kLogMaxBlockCount);
    m_logView->setPlaceholderText(QStringLiteral("Log messages appear here..."));

    auto* verticalSplitter = new QSplitter(Qt::Vertical, this);
    verticalSplitter->setChildrenCollapsible(false);
    verticalSplitter->addWidget(horizontalSplitter);
    verticalSplitter->addWidget(m_logView);
    verticalSplitter->setStretchFactor(0, 1);
    verticalSplitter->setStretchFactor(1, 0);
    verticalSplitter->setSizes({500, kLogPanelInitialHeight});

    rootLayout->addWidget(verticalSplitter);

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

QComboBox* MainView::debugVisualModeComboBox() const
{
    return m_debugVisualModeComboBox;
}

QComboBox* MainView::boundsOverlayComboBox() const
{
    return m_boundsOverlayComboBox;
}

QDoubleSpinBox* MainView::sunAzimuthSpinBox() const
{
    return m_sunAzimuthSpinBox;
}

QDoubleSpinBox* MainView::sunElevationSpinBox() const
{
    return m_sunElevationSpinBox;
}

QPushButton* MainView::sunColorButton() const
{
    return m_sunColorButton;
}

QDoubleSpinBox* MainView::sunDiskSizeSpinBox() const
{
    return m_sunDiskSizeSpinBox;
}

QSpinBox* MainView::secondaryBounceSpinBox() const
{
    return m_secondaryBounceSpinBox;
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

