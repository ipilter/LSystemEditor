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
    QDoubleSpinBox* m_cameraThrustLinearSpinBox = nullptr;
    QDoubleSpinBox* m_cameraDragLinearSpinBox = nullptr;
    QDoubleSpinBox* m_cameraThrustAngularSpinBox = nullptr;
    QDoubleSpinBox* m_cameraDragAngularSpinBox = nullptr;
    QDoubleSpinBox* m_cameraMouseSensitivitySpinBox = nullptr;
    QSpinBox* m_cameraTickIntervalSpinBox = nullptr;
    QSpinBox* m_cameraResetThrottleSpinBox = nullptr;
    QSpinBox* m_cameraStopDebounceSpinBox = nullptr;
    QDoubleSpinBox* m_creaseAngleSpinBox = nullptr;
    QPushButton* m_accelBvhColorButton = nullptr;
    QColor m_accelBvhColor;
    float m_creaseAngleDeg = 50.0f;
};
