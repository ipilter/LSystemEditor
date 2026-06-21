#pragma once

#include <QColor>
#include <QDialog>

class QGroupBox;
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
    QDoubleSpinBox* m_cameraLinearSpeedSpinBox = nullptr;
    QDoubleSpinBox* m_cameraAngularSpeedSpinBox = nullptr;
    QDoubleSpinBox* m_cameraMouseSensitivitySpinBox = nullptr;
    QSpinBox* m_cameraTickIntervalSpinBox = nullptr;
    QSpinBox* m_cameraResetThrottleSpinBox = nullptr;
    QSpinBox* m_cameraStopDebounceSpinBox = nullptr;
    QDoubleSpinBox* m_cameraDefaultPositionXSpinBox = nullptr;
    QDoubleSpinBox* m_cameraDefaultPositionYSpinBox = nullptr;
    QDoubleSpinBox* m_cameraDefaultPositionZSpinBox = nullptr;
    QDoubleSpinBox* m_cameraDefaultYawSpinBox = nullptr;
    QDoubleSpinBox* m_cameraDefaultPitchSpinBox = nullptr;
    QDoubleSpinBox* m_cameraDefaultRollSpinBox = nullptr;
    QDoubleSpinBox* m_creaseAngleSpinBox = nullptr;
    QPushButton* m_accelBvhColorButton = nullptr;
    QColor m_accelBvhColor;
    float m_creaseAngleDeg = 50.0f;
};
