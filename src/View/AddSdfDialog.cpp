#include "AddSdfDialog.h"

#include "Sdf/Shapes/CappedConeSdf.h"
#include "Sdf/Shapes/CylinderSdf.h"
#include "Sdf/Shapes/SphereSdf.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>

namespace {

constexpr double kMinParameter = 0.01;
constexpr double kMaxParameter = 100.0;
constexpr double kMinCenter = -100.0;
constexpr double kMaxCenter = 100.0;

QDoubleSpinBox* makeCenterSpinBox(QWidget* parent, double value)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(kMinCenter, kMaxCenter);
    spinBox->setDecimals(3);
    spinBox->setSingleStep(0.1);
    spinBox->setValue(value);
    return spinBox;
}

QDoubleSpinBox* makeParameterSpinBox(QWidget* parent, double value)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(kMinParameter, kMaxParameter);
    spinBox->setDecimals(3);
    spinBox->setSingleStep(0.1);
    spinBox->setValue(value);
    return spinBox;
}

} // namespace

AddSdfDialog::AddSdfDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Add SDF"));
    setModal(true);

    auto* rootLayout = new QVBoxLayout(this);

    m_formLayout = new QFormLayout();

    m_typeComboBox = new QComboBox(this);
    m_typeComboBox->addItem(QStringLiteral("Sphere"));
    m_typeComboBox->addItem(QStringLiteral("Cylinder"));
    m_typeComboBox->addItem(QStringLiteral("Capped Cone"));
    m_formLayout->addRow(QStringLiteral("Type:"), m_typeComboBox);

    m_centerXSpinBox = makeCenterSpinBox(this, 1.0);
    m_centerYSpinBox = makeCenterSpinBox(this, 0.0);
    m_centerZSpinBox = makeCenterSpinBox(this, 0.0);
    m_formLayout->addRow(QStringLiteral("Center X:"), m_centerXSpinBox);
    m_formLayout->addRow(QStringLiteral("Center Y:"), m_centerYSpinBox);
    m_formLayout->addRow(QStringLiteral("Center Z:"), m_centerZSpinBox);

    m_radiusSpinBox = makeParameterSpinBox(this, 0.5);
    m_formLayout->addRow(QStringLiteral("Radius:"), m_radiusSpinBox);

    m_halfHeightSpinBox = makeParameterSpinBox(this, 1.0);
    m_formLayout->addRow(QStringLiteral("Half height:"), m_halfHeightSpinBox);

    m_radiusBottomSpinBox = makeParameterSpinBox(this, 0.5);
    m_formLayout->addRow(QStringLiteral("Bottom radius:"), m_radiusBottomSpinBox);

    m_radiusTopSpinBox = makeParameterSpinBox(this, 0.2);
    m_formLayout->addRow(QStringLiteral("Top radius:"), m_radiusTopSpinBox);

    rootLayout->addLayout(m_formLayout);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        const SdfFloat3 center = sdfMakeFloat3(
            static_cast<float>(m_centerXSpinBox->value()),
            static_cast<float>(m_centerYSpinBox->value()),
            static_cast<float>(m_centerZSpinBox->value()));

        switch (m_typeComboBox->currentIndex()) {
        case 0:
            m_result = std::make_unique<SphereSdf>(center, static_cast<float>(m_radiusSpinBox->value()));
            break;
        case 1:
            m_result = std::make_unique<CylinderSdf>(
                center,
                sdfMakeFloat2(
                    static_cast<float>(m_radiusSpinBox->value()),
                    static_cast<float>(m_halfHeightSpinBox->value())));
            break;
        case 2:
            m_result = std::make_unique<CappedConeSdf>(
                center,
                static_cast<float>(m_halfHeightSpinBox->value()),
                static_cast<float>(m_radiusBottomSpinBox->value()),
                static_cast<float>(m_radiusTopSpinBox->value()));
            break;
        default:
            break;
        }

        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttonBox);

    connect(
        m_typeComboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &AddSdfDialog::onTypeChanged);

    onTypeChanged(m_typeComboBox->currentIndex());
}

std::unique_ptr<SdfShape> AddSdfDialog::result()
{
    return std::move(m_result);
}

void AddSdfDialog::onTypeChanged(int index)
{
    Q_UNUSED(index);
    updateParameterVisibility();
}

void AddSdfDialog::updateParameterVisibility()
{
    const int typeIndex = m_typeComboBox->currentIndex();

    auto setRowVisible = [this](QDoubleSpinBox* spinBox, bool visible) {
        if (spinBox == nullptr) {
            return;
        }
        spinBox->setVisible(visible);
        if (QWidget* labelWidget = m_formLayout->labelForField(spinBox)) {
            labelWidget->setVisible(visible);
        }
    };

    setRowVisible(m_radiusSpinBox, typeIndex == 0 || typeIndex == 1);
    setRowVisible(m_halfHeightSpinBox, typeIndex == 1 || typeIndex == 2);
    setRowVisible(m_radiusBottomSpinBox, typeIndex == 2);
    setRowVisible(m_radiusTopSpinBox, typeIndex == 2);
}
