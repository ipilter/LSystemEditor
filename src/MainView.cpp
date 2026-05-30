#include "MainView.h"

#include "AppSettings.h"
#include "OpenGLViewportWidget.h"

#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

constexpr int kMinRenderDimension = 1;
constexpr int kMaxRenderDimension = 8192;
constexpr int kDefaultRenderDimension = 256;

} // namespace

MainView::MainView(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("PathTracer 0.0.1"));
    resize(800, 500);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_viewportHost = new QWidget(this);
    m_viewportHost->setAutoFillBackground(true);
    m_viewportHost->setStyleSheet(QStringLiteral("background-color: #2a2a2a;"));
    m_viewportHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewportHost->installEventFilter(this);
    mainLayout->addWidget(m_viewportHost, 1);

    m_viewport = new OpenGLViewportWidget(m_viewportHost);
    m_viewport->setGeometry(m_viewportHost->rect());
    m_pendingViewportSize = m_viewportHost->size();

    auto* controlBar = new QWidget(this);
    controlBar->setFixedWidth(160);
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

    controlLayout->addWidget(renderGroup);

    auto* backgroundRow = new QHBoxLayout();
    auto* backgroundLabel = new QLabel(QStringLiteral("Background: "), controlBar);
    m_colorButton = new QPushButton(controlBar);
    m_colorButton->setFixedSize(24, 24);
    m_colorButton->setToolTip(QStringLiteral("Background color"));
    backgroundRow->addWidget(backgroundLabel);
    backgroundRow->addWidget(m_colorButton);
    backgroundRow->addStretch();
    controlLayout->addLayout(backgroundRow);

    controlLayout->addStretch();

    auto* closeButton = new QPushButton(QStringLiteral("Close"), controlBar);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    controlLayout->addWidget(closeButton);

    mainLayout->addWidget(controlBar);
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
