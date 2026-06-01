#include "SettingsDialog.h"

#include "AppSettings.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

constexpr int kMinDebounceMs = 0;
constexpr int kMaxDebounceMs = 2000;

QString colorButtonStyleSheet(const QColor& color)
{
    return QStringLiteral(
               "QPushButton { background-color: %1; border: 1px solid #333; min-width: 24px; min-height: 24px; }"
               "QPushButton:hover { border: 1px solid #666; }")
        .arg(color.name(QColor::HexRgb));
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , m_accelAabbColor(AppSettings::instance().accelAabbColor())
    , m_accelOctreeColor(AppSettings::instance().accelOctreeColor())
{
    setWindowTitle(QStringLiteral("Settings"));
    setModal(true);

    auto* rootLayout = new QVBoxLayout(this);

    auto* formLayout = new QFormLayout();

    m_renderSizeSpinDebounceSpinBox = new QSpinBox(this);
    m_renderSizeSpinDebounceSpinBox->setRange(kMinDebounceMs, kMaxDebounceMs);
    m_renderSizeSpinDebounceSpinBox->setSuffix(QStringLiteral(" ms"));
    m_renderSizeSpinDebounceSpinBox->setToolTip(
        QStringLiteral("Delay before applying width/height spin box changes to the render buffer"));
    m_renderSizeSpinDebounceSpinBox->setValue(
        AppSettings::instance().debounceMsFor(DebounceElementIds::kRenderSize));
    formLayout->addRow(QStringLiteral("Width/height spin debounce:"), m_renderSizeSpinDebounceSpinBox);

    m_maxSamplesSpinDebounceSpinBox = new QSpinBox(this);
    m_maxSamplesSpinDebounceSpinBox->setRange(kMinDebounceMs, kMaxDebounceMs);
    m_maxSamplesSpinDebounceSpinBox->setSuffix(QStringLiteral(" ms"));
    m_maxSamplesSpinDebounceSpinBox->setToolTip(
        QStringLiteral("Delay before applying the samples spin box change to the renderer"));
    m_maxSamplesSpinDebounceSpinBox->setValue(
        AppSettings::instance().debounceMsFor(DebounceElementIds::kMaxSamples));
    formLayout->addRow(QStringLiteral("Samples spin debounce:"), m_maxSamplesSpinDebounceSpinBox);

    m_accelAabbColorButton = new QPushButton(this);
    m_accelAabbColorButton->setToolTip(QStringLiteral("Wireframe color for object AABB overlay"));
    connect(m_accelAabbColorButton, &QPushButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(
            m_accelAabbColor,
            this,
            QStringLiteral("AABB overlay color"));
        if (chosen.isValid()) {
            m_accelAabbColor = chosen;
            syncColorButtonStyles();
        }
    });
    formLayout->addRow(QStringLiteral("AABB overlay color:"), m_accelAabbColorButton);

    m_accelOctreeColorButton = new QPushButton(this);
    m_accelOctreeColorButton->setToolTip(QStringLiteral("Wireframe color for octree node overlay"));
    connect(m_accelOctreeColorButton, &QPushButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(
            m_accelOctreeColor,
            this,
            QStringLiteral("Octree overlay color"));
        if (chosen.isValid()) {
            m_accelOctreeColor = chosen;
            syncColorButtonStyles();
        }
    });
    formLayout->addRow(QStringLiteral("Octree overlay color:"), m_accelOctreeColorButton);

    syncColorButtonStyles();
    rootLayout->addLayout(formLayout);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        AppSettings::instance().setDebounceMs(
            DebounceElementIds::kRenderSize,
            m_renderSizeSpinDebounceSpinBox->value());
        AppSettings::instance().setDebounceMs(
            DebounceElementIds::kMaxSamples,
            m_maxSamplesSpinDebounceSpinBox->value());
        AppSettings::instance().setAccelAabbColor(m_accelAabbColor);
        AppSettings::instance().setAccelOctreeColor(m_accelOctreeColor);
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttonBox);
}

void SettingsDialog::syncColorButtonStyles()
{
    m_accelAabbColorButton->setStyleSheet(colorButtonStyleSheet(m_accelAabbColor));
    m_accelOctreeColorButton->setStyleSheet(colorButtonStyleSheet(m_accelOctreeColor));
}
