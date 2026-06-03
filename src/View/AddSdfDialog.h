#pragma once

#include "Sdf/Shapes/SdfShape.h"

#include <QDialog>
#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QWidget;

class AddSdfDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddSdfDialog(QWidget* parent = nullptr);

    std::unique_ptr<SdfShape> result();

private:
    void onTypeChanged(int index);
    void updateParameterVisibility();

    QComboBox* m_typeComboBox = nullptr;
    QFormLayout* m_formLayout = nullptr;
    QDoubleSpinBox* m_centerXSpinBox = nullptr;
    QDoubleSpinBox* m_centerYSpinBox = nullptr;
    QDoubleSpinBox* m_centerZSpinBox = nullptr;
    QDoubleSpinBox* m_radiusSpinBox = nullptr;
    QDoubleSpinBox* m_halfHeightSpinBox = nullptr;
    QDoubleSpinBox* m_radiusBottomSpinBox = nullptr;
    QDoubleSpinBox* m_radiusTopSpinBox = nullptr;

    std::unique_ptr<SdfShape> m_result;
};
