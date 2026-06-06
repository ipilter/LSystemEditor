#include "SettingsDialog.h"

#include "AppSettings.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

constexpr int kMinDebounceMs = 0;
constexpr int kMaxDebounceMs = 2000;
constexpr float kMinCreaseAngleDeg = 0.0f;
constexpr float kMaxCreaseAngleDeg = 180.0f;

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
    , m_accelBvhColor(AppSettings::instance().accelBvhColor())
    , m_creaseAngleDeg(AppSettings::instance().creaseAngleDeg())
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

    m_creaseAngleSpinBox = new QDoubleSpinBox(this);
    m_creaseAngleSpinBox->setRange(kMinCreaseAngleDeg, kMaxCreaseAngleDeg);
    m_creaseAngleSpinBox->setDecimals(1);
    m_creaseAngleSpinBox->setSuffix(QStringLiteral(" °"));
    m_creaseAngleSpinBox->setToolTip(
        QStringLiteral(
            "Edges with a larger angle between adjacent faces stay sharp; "
            "smaller angles share averaged vertex normals (smooth shading)."));
    m_creaseAngleSpinBox->setValue(m_creaseAngleDeg);
    formLayout->addRow(QStringLiteral("Crease angle:"), m_creaseAngleSpinBox);

    m_accelBvhColorButton = new QPushButton(this);
    m_accelBvhColorButton->setToolTip(QStringLiteral("Wireframe color for BVH bounds overlay"));
    connect(m_accelBvhColorButton, &QPushButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(
            m_accelBvhColor,
            this,
            QStringLiteral("BVH overlay color"));
        if (chosen.isValid()) {
            m_accelBvhColor = chosen;
            syncColorButtonStyles();
        }
    });
    formLayout->addRow(QStringLiteral("BVH overlay color:"), m_accelBvhColorButton);

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
        AppSettings::instance().setCreaseAngleDeg(static_cast<float>(m_creaseAngleSpinBox->value()));
        AppSettings::instance().setAccelBvhColor(m_accelBvhColor);
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttonBox);
}

void SettingsDialog::syncColorButtonStyles()
{
    m_accelBvhColorButton->setStyleSheet(colorButtonStyleSheet(m_accelBvhColor));
}
