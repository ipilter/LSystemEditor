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
constexpr float kMinCameraLinearSpeed = 1.0f;
constexpr float kMaxCameraLinearSpeed = 10000.0f;
constexpr float kMinCameraAngularSpeed = 0.05f;
constexpr float kMaxCameraAngularSpeed = 4.0f;
constexpr float kMinCameraMouseSensitivity = 0.0001f;
constexpr float kMaxCameraMouseSensitivity = 0.05f;
constexpr int kMinCameraTickIntervalMs = 8;
constexpr int kMaxCameraTickIntervalMs = 100;
constexpr int kMinCameraMotionTimingMs = 0;
constexpr int kMaxCameraMotionTimingMs = 2000;
constexpr float kMinCameraDefaultPositionMm = -1000000.0f;
constexpr float kMaxCameraDefaultPositionMm = 1000000.0f;
constexpr float kMinCameraDefaultAngleDeg = -360.0f;
constexpr float kMaxCameraDefaultAngleDeg = 360.0f;
constexpr float kMinCameraDefaultPitchDeg = -89.0f;
constexpr float kMaxCameraDefaultPitchDeg = 89.0f;

QDoubleSpinBox* makeDefaultPositionSpinBox(
    QWidget* parent,
    float value,
    const QString& axisLabel,
    const QString& toolTip)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(kMinCameraDefaultPositionMm, kMaxCameraDefaultPositionMm);
    spinBox->setDecimals(0);
    spinBox->setSingleStep(100.0);
    spinBox->setSuffix(QStringLiteral(" mm"));
    spinBox->setPrefix(axisLabel);
    spinBox->setToolTip(toolTip);
    spinBox->setValue(value);
    return spinBox;
}

QDoubleSpinBox* makeDefaultAngleSpinBox(
    QWidget* parent,
    float value,
    float minDeg,
    float maxDeg,
    const QString& axisLabel,
    const QString& toolTip)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(minDeg, maxDeg);
    spinBox->setDecimals(1);
    spinBox->setSingleStep(5.0);
    spinBox->setSuffix(QStringLiteral(" °"));
    spinBox->setPrefix(axisLabel);
    spinBox->setToolTip(toolTip);
    spinBox->setValue(value);
    return spinBox;
}

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

    m_cameraLinearSpeedSpinBox = new QDoubleSpinBox(cameraGroup);
    m_cameraLinearSpeedSpinBox->setRange(kMinCameraLinearSpeed, kMaxCameraLinearSpeed);
    m_cameraLinearSpeedSpinBox->setDecimals(0);
    m_cameraLinearSpeedSpinBox->setSingleStep(50.0);
    m_cameraLinearSpeedSpinBox->setSuffix(QStringLiteral(" mm/s"));
    m_cameraLinearSpeedSpinBox->setToolTip(
        QStringLiteral("Keyboard only: linear travel speed when WASD/QE keys are held"));
    m_cameraLinearSpeedSpinBox->setValue(cameraSettings.linearSpeedMmPerSec);
    cameraLayout->addRow(QStringLiteral("Linear speed:"), m_cameraLinearSpeedSpinBox);

    m_cameraAngularSpeedSpinBox = new QDoubleSpinBox(cameraGroup);
    m_cameraAngularSpeedSpinBox->setRange(kMinCameraAngularSpeed, kMaxCameraAngularSpeed);
    m_cameraAngularSpeedSpinBox->setDecimals(3);
    m_cameraAngularSpeedSpinBox->setSingleStep(0.05);
    m_cameraAngularSpeedSpinBox->setSuffix(QStringLiteral(" rad/s"));
    m_cameraAngularSpeedSpinBox->setToolTip(
        QStringLiteral("Keyboard only: rotational speed from arrow keys and Z/X roll"));
    m_cameraAngularSpeedSpinBox->setValue(cameraSettings.angularSpeedRadPerSec);
    cameraLayout->addRow(QStringLiteral("Angular speed:"), m_cameraAngularSpeedSpinBox);

    m_cameraMouseSensitivitySpinBox = new QDoubleSpinBox(cameraGroup);
    m_cameraMouseSensitivitySpinBox->setRange(kMinCameraMouseSensitivity, kMaxCameraMouseSensitivity);
    m_cameraMouseSensitivitySpinBox->setDecimals(4);
    m_cameraMouseSensitivitySpinBox->setSingleStep(0.0005);
    m_cameraMouseSensitivitySpinBox->setToolTip(
        QStringLiteral("Mouse drag only: rotation in radians per pixel"));
    m_cameraMouseSensitivitySpinBox->setValue(cameraSettings.mouseSensitivity);
    cameraLayout->addRow(QStringLiteral("Mouse sensitivity:"), m_cameraMouseSensitivitySpinBox);

    m_cameraTickIntervalSpinBox = new QSpinBox(cameraGroup);
    m_cameraTickIntervalSpinBox->setRange(kMinCameraTickIntervalMs, kMaxCameraTickIntervalMs);
    m_cameraTickIntervalSpinBox->setSuffix(QStringLiteral(" ms"));
    m_cameraTickIntervalSpinBox->setToolTip(
        QStringLiteral("Fixed integration step for keyboard camera movement"));
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

    const QString defaultPoseToolTip =
        QStringLiteral("Applied once at app startup; restart required to take effect");

    auto* defaultPositionRow = new QWidget(cameraGroup);
    auto* defaultPositionLayout = new QHBoxLayout(defaultPositionRow);
    defaultPositionLayout->setContentsMargins(0, 0, 0, 0);
    m_cameraDefaultPositionXSpinBox = makeDefaultPositionSpinBox(
        defaultPositionRow,
        cameraSettings.defaultPositionXmm,
        QStringLiteral("X "),
        defaultPoseToolTip);
    m_cameraDefaultPositionYSpinBox = makeDefaultPositionSpinBox(
        defaultPositionRow,
        cameraSettings.defaultPositionYmm,
        QStringLiteral("Y "),
        defaultPoseToolTip);
    m_cameraDefaultPositionZSpinBox = makeDefaultPositionSpinBox(
        defaultPositionRow,
        cameraSettings.defaultPositionZmm,
        QStringLiteral("Z "),
        defaultPoseToolTip);
    defaultPositionLayout->addWidget(m_cameraDefaultPositionXSpinBox);
    defaultPositionLayout->addWidget(m_cameraDefaultPositionYSpinBox);
    defaultPositionLayout->addWidget(m_cameraDefaultPositionZSpinBox);
    cameraLayout->addRow(QStringLiteral("Default position:"), defaultPositionRow);

    auto* defaultOrientationRow = new QWidget(cameraGroup);
    auto* defaultOrientationLayout = new QHBoxLayout(defaultOrientationRow);
    defaultOrientationLayout->setContentsMargins(0, 0, 0, 0);
    m_cameraDefaultYawSpinBox = makeDefaultAngleSpinBox(
        defaultOrientationRow,
        cameraSettings.defaultYawDeg,
        kMinCameraDefaultAngleDeg,
        kMaxCameraDefaultAngleDeg,
        QStringLiteral("Y "),
        defaultPoseToolTip);
    m_cameraDefaultPitchSpinBox = makeDefaultAngleSpinBox(
        defaultOrientationRow,
        cameraSettings.defaultPitchDeg,
        kMinCameraDefaultPitchDeg,
        kMaxCameraDefaultPitchDeg,
        QStringLiteral("P "),
        defaultPoseToolTip);
    m_cameraDefaultRollSpinBox = makeDefaultAngleSpinBox(
        defaultOrientationRow,
        cameraSettings.defaultRollDeg,
        kMinCameraDefaultAngleDeg,
        kMaxCameraDefaultAngleDeg,
        QStringLiteral("R "),
        defaultPoseToolTip);
    defaultOrientationLayout->addWidget(m_cameraDefaultYawSpinBox);
    defaultOrientationLayout->addWidget(m_cameraDefaultPitchSpinBox);
    defaultOrientationLayout->addWidget(m_cameraDefaultRollSpinBox);
    cameraLayout->addRow(QStringLiteral("Default orientation:"), defaultOrientationRow);

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
        cameraDynamicsSettings.linearSpeedMmPerSec =
            static_cast<float>(m_cameraLinearSpeedSpinBox->value());
        cameraDynamicsSettings.angularSpeedRadPerSec =
            static_cast<float>(m_cameraAngularSpeedSpinBox->value());
        cameraDynamicsSettings.mouseSensitivity = static_cast<float>(m_cameraMouseSensitivitySpinBox->value());
        cameraDynamicsSettings.tickIntervalMs = m_cameraTickIntervalSpinBox->value();
        cameraDynamicsSettings.motionResetThrottleMs = m_cameraResetThrottleSpinBox->value();
        cameraDynamicsSettings.motionStopDebounceMs = m_cameraStopDebounceSpinBox->value();
        cameraDynamicsSettings.defaultPositionXmm =
            static_cast<float>(m_cameraDefaultPositionXSpinBox->value());
        cameraDynamicsSettings.defaultPositionYmm =
            static_cast<float>(m_cameraDefaultPositionYSpinBox->value());
        cameraDynamicsSettings.defaultPositionZmm =
            static_cast<float>(m_cameraDefaultPositionZSpinBox->value());
        cameraDynamicsSettings.defaultYawDeg =
            static_cast<float>(m_cameraDefaultYawSpinBox->value());
        cameraDynamicsSettings.defaultPitchDeg =
            static_cast<float>(m_cameraDefaultPitchSpinBox->value());
        cameraDynamicsSettings.defaultRollDeg =
            static_cast<float>(m_cameraDefaultRollSpinBox->value());
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
