#include "MainView.h"

#include "AppLog.h"
#include "AppSettings.h"
#include "OpenGLViewportWidget.h"

#include <QComboBox>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

constexpr int kMinRenderDimension = 1;
constexpr int kMaxRenderDimension = 8192;
constexpr int kDefaultRenderDimension = 256;
constexpr int kMinMaxSamplesPerPixel = 0;
constexpr int kMaxMaxSamplesPerPixel = 1'000'000;
constexpr int kDefaultMaxSamplesPerPixel = 1024;
constexpr int kMinPreviewStepsPerLevel = 0;
constexpr int kMaxPreviewStepsPerLevel = 128;
constexpr int kDefaultPreviewStepsPerLevel = 0;
constexpr int kLogPanelHeight = 120;
constexpr int kLogMaxBlockCount = 500;

} // namespace

MainView::MainView(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("PathTracer 0.0.1"));
    resize(800, 620);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* contentRow = new QWidget(this);
    auto* contentLayout = new QHBoxLayout(contentRow);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_viewportHost = new QWidget(contentRow);
    m_viewportHost->setAutoFillBackground(true);
    m_viewportHost->setStyleSheet(QStringLiteral("background-color: #2a2a2a;"));
    m_viewportHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewportHost->installEventFilter(this);
    contentLayout->addWidget(m_viewportHost, 1);

    m_viewport = new OpenGLViewportWidget(m_viewportHost);
    m_viewport->setGeometry(m_viewportHost->rect());
    m_pendingViewportSize = m_viewportHost->size();

    auto* controlBar = new QWidget(contentRow);
    controlBar->setFixedWidth(180);
    auto* controlLayout = new QVBoxLayout(controlBar);
    controlLayout->setContentsMargins(8, 8, 8, 8);

    auto* renderGroup = new QGroupBox(QStringLiteral("Render"), controlBar);
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
    m_sdfVisualModeComboBox = new QComboBox(renderGroup);
    m_sdfVisualModeComboBox->addItem(QStringLiteral("Step Count"));
    m_sdfVisualModeComboBox->addItem(QStringLiteral("Distance"));
    m_sdfVisualModeComboBox->addItem(QStringLiteral("Off"));
    m_sdfVisualModeComboBox->setToolTip(
        QStringLiteral("Step Count: march iteration heatmap. Distance: hit distance heatmap. Off: no debug visualization."));
    visualModeRow->addWidget(m_sdfVisualModeComboBox, 1);
    renderLayout->addLayout(visualModeRow);

    auto* traversalModeRow = new QHBoxLayout();
    traversalModeRow->addWidget(new QLabel(QStringLiteral("Traversal:"), renderGroup));
    m_sdfTraversalModeComboBox = new QComboBox(renderGroup);
    m_sdfTraversalModeComboBox->addItem(QStringLiteral("Brute Force"));
    m_sdfTraversalModeComboBox->addItem(QStringLiteral("BVH"));
    m_sdfTraversalModeComboBox->setToolTip(
        QStringLiteral(
            "Brute Force: evaluate every object each march step. "
            "BVH: object-level BVH culling with analytical conservative bounds; exact SDF near surfaces."));
    traversalModeRow->addWidget(m_sdfTraversalModeComboBox, 1);
    renderLayout->addLayout(traversalModeRow);

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

    auto* renderControlRow = new QHBoxLayout();
    m_startButton = new QPushButton(QStringLiteral("Start"), renderGroup);
    m_stopButton = new QPushButton(QStringLiteral("Stop"), renderGroup);
    renderControlRow->addWidget(m_startButton);
    renderControlRow->addWidget(m_stopButton);
    renderLayout->addLayout(renderControlRow);

    renderLayout->addStretch(1);

    m_settingsButton = new QPushButton(QStringLiteral("Settings"), renderGroup);
    renderLayout->addWidget(m_settingsButton);

    auto* closeButton = new QPushButton(QStringLiteral("Close"), renderGroup);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    renderLayout->addWidget(closeButton);

    controlLayout->addWidget(renderGroup, 1);

    auto* userGroup = new QGroupBox(QStringLiteral("User"), controlBar);
    auto* userLayout = new QVBoxLayout(userGroup);
    m_addSdfButton = new QPushButton(QStringLiteral("Add"), userGroup);
    m_addSdfButton->setToolTip(QStringLiteral("Add a new SDF primitive to the scene"));
    userLayout->addWidget(m_addSdfButton);
    controlLayout->addWidget(userGroup);

    auto* backgroundRow = new QHBoxLayout();
    auto* backgroundLabel = new QLabel(QStringLiteral("Background: "), controlBar);
    m_colorButton = new QPushButton(controlBar);
    m_colorButton->setFixedSize(24, 24);
    m_colorButton->setToolTip(QStringLiteral("Background color"));
    backgroundRow->addWidget(backgroundLabel);
    backgroundRow->addWidget(m_colorButton);
    backgroundRow->addStretch();
    controlLayout->addLayout(backgroundRow);

    contentLayout->addWidget(controlBar);
    rootLayout->addWidget(contentRow, 1);

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setFixedHeight(kLogPanelHeight);
    m_logView->setMaximumBlockCount(kLogMaxBlockCount);
    m_logView->setPlaceholderText(QStringLiteral("Log messages appear here..."));
    rootLayout->addWidget(m_logView);

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

QComboBox* MainView::sdfVisualModeComboBox() const
{
    return m_sdfVisualModeComboBox;
}

QComboBox* MainView::sdfTraversalModeComboBox() const
{
    return m_sdfTraversalModeComboBox;
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

QPushButton* MainView::addSdfButton() const
{
    return m_addSdfButton;
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
