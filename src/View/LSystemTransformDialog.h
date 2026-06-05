#pragma once

#include "GeometryTypes.h"

#include <QDialog>

class QDoubleSpinBox;
class ObjectTransformWidget;

class LSystemTransformDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LSystemTransformDialog(QWidget* parent = nullptr);

    Vec3 translation() const;
    Vec3 rotationDeg() const;

private:
    void syncPreview();

    ObjectTransformWidget* m_previewWidget = nullptr;
    QDoubleSpinBox* m_rootXSpinBox = nullptr;
    QDoubleSpinBox* m_rootYSpinBox = nullptr;
    QDoubleSpinBox* m_rootZSpinBox = nullptr;
    QDoubleSpinBox* m_rootYawSpinBox = nullptr;
    QDoubleSpinBox* m_rootPitchSpinBox = nullptr;
    QDoubleSpinBox* m_rootRollSpinBox = nullptr;
};
