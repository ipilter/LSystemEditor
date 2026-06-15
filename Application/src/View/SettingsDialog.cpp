#include "SettingsDialog.h"

#include "AppSettings.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
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
constexpr float kMinCameraThrust = 0.1f;
constexpr float kMaxCameraThrust = 20.0f;
constexpr float kMinCameraDrag = 0.1f;
constexpr float kMaxCameraDrag = 30.0f;
constexpr float kMinCameraMouseSensitivity = 0.01f;
constexpr float kMaxCameraMouseSensitivity = 2.0f;
constexpr int kMinCameraTickIntervalMs = 8;
constexpr int kMaxCameraTickIntervalMs = 100;
constexpr int kMinCameraMotionTimingMs = 0;
constexpr int kMaxCameraMotionTimingMs = 2000;

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

    m_physicalCameraDebounceSpinBox = new QSpinBox(this);
    m_physicalCameraDebounceSpinBox->setRange(kMinDebounceMs, kMaxDebounceMs);
    m_physicalCameraDebounceSpinBox->setSuffix(QStringLiteral(" ms"));
    m_physicalCameraDebounceSpinBox->setToolTip(
        QStringLiteral("Delay before applying physical camera control changes to the display"));
    m_physicalCameraDebounceSpinBox->setValue(
        AppSettings::instance().debounceMsFor(DebounceElementIds::kPhysicalCamera));
    formLayout->addRow(QStringLiteral("Physical camera debounce:"), m_physicalCameraDebounceSpinBox);

    const CameraDynamicsSettings cameraSettings = AppSettings::instance().cameraDynamicsSettings();
    auto* cameraGroup = new QGroupBox(QStringLiteral("Camera movement"), this);
    auto* cameraLayout = new QFormLayout(cameraGroup);

    m_cameraThrustLinearSpinBox = new QDoubleSpinBox(cameraGroup);
    m_cameraThrustLinearSpinBox->setRange(kMinCameraThrust, kMaxCameraThrust);
    m_cameraThrustLinearSpinBox->setDecimals(2);
    m_cameraThrustLinearSpinBox->setSingleStep(0.1);
    m_cameraThrustLinearSpinBox->setToolTip(
        QStringLiteral("Linear acceleration when movement keys are held"));
    m_cameraThrustLinearSpinBox->setValue(cameraSettings.thrustLinear);
    cameraLayout->addRow(QStringLiteral("Linear thrust:"), m_cameraThrustLinearSpinBox);

    m_cameraDragLinearSpinBox = new QDoubleSpinBox(cameraGroup);
    m_cameraDragLinearSpinBox->setRange(kMinCameraDrag, kMaxCameraDrag);
    m_cameraDragLinearSpinBox->setDecimals(2);
    m_cameraDragLinearSpinBox->setSingleStep(0.1);
    m_cameraDragLinearSpinBox->setToolTip(
        QStringLiteral("How quickly linear movement slows after keys are released"));
    m_cameraDragLinearSpinBox->setValue(cameraSettings.dragLinear);
    cameraLayout->addRow(QStringLiteral("Linear drag:"), m_cameraDragLinearSpinBox);

    m_cameraThrustAngularSpinBox = new QDoubleSpinBox(cameraGroup);
    m_cameraThrustAngularSpinBox->setRange(kMinCameraThrust, kMaxCameraThrust);
    m_cameraThrustAngularSpinBox->setDecimals(2);
    m_cameraThrustAngularSpinBox->setSingleStep(0.1);
    m_cameraThrustAngularSpinBox->setToolTip(
        QStringLiteral("Rotational acceleration from arrow keys or mouse drag"));
    m_cameraThrustAngularSpinBox->setValue(cameraSettings.thrustAngular);
    cameraLayout->addRow(QStringLiteral("Angular thrust:"), m_cameraThrustAngularSpinBox);

    m_cameraDragAngularSpinBox = new QDoubleSpinBox(cameraGroup);
    m_cameraDragAngularSpinBox->setRange(kMinCameraDrag, kMaxCameraDrag);
    m_cameraDragAngularSpinBox->setDecimals(2);
    m_cameraDragAngularSpinBox->setSingleStep(0.1);
    m_cameraDragAngularSpinBox->setToolTip(
        QStringLiteral("How quickly rotation slows after input stops"));
    m_cameraDragAngularSpinBox->setValue(cameraSettings.dragAngular);
    cameraLayout->addRow(QStringLiteral("Angular drag:"), m_cameraDragAngularSpinBox);

    m_cameraMouseSensitivitySpinBox = new QDoubleSpinBox(cameraGroup);
    m_cameraMouseSensitivitySpinBox->setRange(kMinCameraMouseSensitivity, kMaxCameraMouseSensitivity);
    m_cameraMouseSensitivitySpinBox->setDecimals(3);
    m_cameraMouseSensitivitySpinBox->setSingleStep(0.01);
    m_cameraMouseSensitivitySpinBox->setToolTip(
        QStringLiteral("Mouse drag torque scale for yaw and pitch"));
    m_cameraMouseSensitivitySpinBox->setValue(cameraSettings.mouseSensitivity);
    cameraLayout->addRow(QStringLiteral("Mouse sensitivity:"), m_cameraMouseSensitivitySpinBox);

    m_cameraTickIntervalSpinBox = new QSpinBox(cameraGroup);
    m_cameraTickIntervalSpinBox->setRange(kMinCameraTickIntervalMs, kMaxCameraTickIntervalMs);
    m_cameraTickIntervalSpinBox->setSuffix(QStringLiteral(" ms"));
    m_cameraTickIntervalSpinBox->setToolTip(
        QStringLiteral("How often camera dynamics are integrated"));
    m_cameraTickIntervalSpinBox->setValue(cameraSettings.tickIntervalMs);
    cameraLayout->addRow(QStringLiteral("Update interval:"), m_cameraTickIntervalSpinBox);

    m_cameraResetThrottleSpinBox = new QSpinBox(cameraGroup);
    m_cameraResetThrottleSpinBox->setRange(kMinCameraMotionTimingMs, kMaxCameraMotionTimingMs);
    m_cameraResetThrottleSpinBox->setSuffix(QStringLiteral(" ms"));
    m_cameraResetThrottleSpinBox->setToolTip(
        QStringLiteral(
            "Minimum time between render accumulation resets while the camera is moving. "
            "Use 0 to reset every update."));
    m_cameraResetThrottleSpinBox->setValue(cameraSettings.motionResetThrottleMs);
    cameraLayout->addRow(QStringLiteral("Reset throttle (moving):"), m_cameraResetThrottleSpinBox);

    m_cameraStopDebounceSpinBox = new QSpinBox(cameraGroup);
    m_cameraStopDebounceSpinBox->setRange(kMinCameraMotionTimingMs, kMaxCameraMotionTimingMs);
    m_cameraStopDebounceSpinBox->setSuffix(QStringLiteral(" ms"));
    m_cameraStopDebounceSpinBox->setToolTip(
        QStringLiteral("Delay after camera motion stops before a final clean render restart"));
    m_cameraStopDebounceSpinBox->setValue(cameraSettings.motionStopDebounceMs);
    cameraLayout->addRow(QStringLiteral("Reset debounce (stopped):"), m_cameraStopDebounceSpinBox);

    formLayout->addRow(cameraGroup);

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
        AppSettings::instance().setDebounceMs(
            DebounceElementIds::kPhysicalCamera,
            m_physicalCameraDebounceSpinBox->value());
        CameraDynamicsSettings cameraDynamicsSettings{};
        cameraDynamicsSettings.thrustLinear = static_cast<float>(m_cameraThrustLinearSpinBox->value());
        cameraDynamicsSettings.dragLinear = static_cast<float>(m_cameraDragLinearSpinBox->value());
        cameraDynamicsSettings.thrustAngular = static_cast<float>(m_cameraThrustAngularSpinBox->value());
        cameraDynamicsSettings.dragAngular = static_cast<float>(m_cameraDragAngularSpinBox->value());
        cameraDynamicsSettings.mouseSensitivity = static_cast<float>(m_cameraMouseSensitivitySpinBox->value());
        cameraDynamicsSettings.tickIntervalMs = m_cameraTickIntervalSpinBox->value();
        cameraDynamicsSettings.motionResetThrottleMs = m_cameraResetThrottleSpinBox->value();
        cameraDynamicsSettings.motionStopDebounceMs = m_cameraStopDebounceSpinBox->value();
        AppSettings::instance().setCameraDynamicsSettings(cameraDynamicsSettings);
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
