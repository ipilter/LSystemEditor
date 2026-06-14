#pragma once

#include <QColor>
#include <QDialog>

class QPushButton;
class QDoubleSpinBox;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    void syncColorButtonStyles();

    QSpinBox* m_renderSizeSpinDebounceSpinBox = nullptr;
    QSpinBox* m_maxSamplesSpinDebounceSpinBox = nullptr;
    QSpinBox* m_physicalCameraDebounceSpinBox = nullptr;
    QDoubleSpinBox* m_creaseAngleSpinBox = nullptr;
    QPushButton* m_accelBvhColorButton = nullptr;
    QColor m_accelBvhColor;
    float m_creaseAngleDeg = 50.0f;
};
