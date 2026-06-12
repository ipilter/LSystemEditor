#include "LSystemTransformDialog.h"

#include "ObjectTransformWidget.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>

namespace {

QDoubleSpinBox* makeSpin(QWidget* parent, double min, double max, double value)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(min, max);
    spin->setDecimals(3);
    spin->setSingleStep(0.1);
    spin->setValue(value);
    return spin;
}

} // namespace

LSystemTransformDialog::LSystemTransformDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Object Transform"));
    setModal(true);

    auto* rootLayout = new QVBoxLayout(this);

    m_previewWidget = new ObjectTransformWidget(this);
    rootLayout->addWidget(m_previewWidget);

    auto* formLayout = new QFormLayout();
    m_rootXSpinBox = makeSpin(this, -1000.0, 1000.0, 0.0);
    m_rootYSpinBox = makeSpin(this, -1000.0, 1000.0, 0.0);
    m_rootZSpinBox = makeSpin(this, -1000.0, 1000.0, 0.0);
    m_rootYawSpinBox = makeSpin(this, -360.0, 360.0, 0.0);
    m_rootPitchSpinBox = makeSpin(this, -360.0, 360.0, 0.0);
    m_rootRollSpinBox = makeSpin(this, -360.0, 360.0, 0.0);
    m_rootYawSpinBox->setToolTip(QStringLiteral("Root yaw in degrees (Y axis, applied first)"));
    m_rootPitchSpinBox->setToolTip(QStringLiteral("Root pitch in degrees (X axis)"));
    m_rootRollSpinBox->setToolTip(QStringLiteral("Root roll in degrees (Z axis)"));
    formLayout->addRow(QStringLiteral("X"), m_rootXSpinBox);
    formLayout->addRow(QStringLiteral("Y"), m_rootYSpinBox);
    formLayout->addRow(QStringLiteral("Z"), m_rootZSpinBox);
    formLayout->addRow(QStringLiteral("Yaw"), m_rootYawSpinBox);
    formLayout->addRow(QStringLiteral("Pitch"), m_rootPitchSpinBox);
    formLayout->addRow(QStringLiteral("Roll"), m_rootRollSpinBox);
    rootLayout->addLayout(formLayout);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttonBox);

    const auto connectSpin = [this](QDoubleSpinBox* spin) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
            syncPreview();
        });
    };
    connectSpin(m_rootXSpinBox);
    connectSpin(m_rootYSpinBox);
    connectSpin(m_rootZSpinBox);
    connectSpin(m_rootYawSpinBox);
    connectSpin(m_rootPitchSpinBox);
    connectSpin(m_rootRollSpinBox);

    syncPreview();
}

Vec3 LSystemTransformDialog::translation() const
{
    return Vec3{
        static_cast<float>(m_rootXSpinBox->value()),
        static_cast<float>(m_rootYSpinBox->value()),
        static_cast<float>(m_rootZSpinBox->value())};
}

Vec3 LSystemTransformDialog::rotationDeg() const
{
    return Vec3{
        static_cast<float>(m_rootYawSpinBox->value()),
        static_cast<float>(m_rootPitchSpinBox->value()),
        static_cast<float>(m_rootRollSpinBox->value())};
}

void LSystemTransformDialog::syncPreview()
{
    m_previewWidget->setYawPitchRoll(
        static_cast<float>(m_rootYawSpinBox->value()),
        static_cast<float>(m_rootPitchSpinBox->value()),
        static_cast<float>(m_rootRollSpinBox->value()));
}
