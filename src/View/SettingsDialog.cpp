#include "SettingsDialog.h"

#include "AppSettings.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

constexpr int kMinDebounceMs = 0;
constexpr int kMaxDebounceMs = 2000;

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
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

    rootLayout->addLayout(formLayout);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        AppSettings::instance().setDebounceMs(
            DebounceElementIds::kRenderSize,
            m_renderSizeSpinDebounceSpinBox->value());
        AppSettings::instance().setDebounceMs(
            DebounceElementIds::kMaxSamples,
            m_maxSamplesSpinDebounceSpinBox->value());
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttonBox);
}
