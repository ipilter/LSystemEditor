#pragma once

#include "ScenePrimitive.h"

#include <QDialog>
#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QWidget;

class AddPrimitiveDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddPrimitiveDialog(QWidget* parent = nullptr);

    std::unique_ptr<ScenePrimitive> result();

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

    std::unique_ptr<ScenePrimitive> m_result;
};
