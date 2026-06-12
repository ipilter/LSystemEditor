#include "MainView.h"

#include "AppLog.h"
#include "AppSettings.h"
#include "OpenGLViewportWidget.h"
#include "RenderAccumulationState.h"

#include <QComboBox>
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
        QStringLiteral(
            "Sparse preview kernel launches before full-res sampling (0 = disabled, all pixels every iteration). "
            "Each preview step uses a coarse pixel lattice (e.g. every 32nd pixel), not extra samples per pixel."));
    previewRow->addWidget(m_previewStepsSpinBox);
    renderLayout->addLayout(previewRow);

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

    auto* backgroundRow = new QHBoxLayout();
    backgroundRow->addWidget(new QLabel(QStringLiteral("Background:"), renderGroup));
    m_colorButton = new QPushButton(renderGroup);
    m_colorButton->setFixedSize(24, 24);
    m_colorButton->setToolTip(QStringLiteral("Viewport background color for unaccumulated pixels"));
    backgroundRow->addWidget(m_colorButton);
    backgroundRow->addStretch();
    renderLayout->addLayout(backgroundRow);

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

