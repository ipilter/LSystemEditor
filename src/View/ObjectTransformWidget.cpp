#include "ObjectTransformWidget.h"

#include <QPainter>
#include <QtMath>

namespace {

constexpr int kWidgetSize = 120;

} // namespace

ObjectTransformWidget::ObjectTransformWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(kWidgetSize, kWidgetSize);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ObjectTransformWidget::setYawPitchRoll(float yawDeg, float pitchDeg, float rollDeg)
{
    m_yawDeg = yawDeg;
    m_pitchDeg = pitchDeg;
    m_rollDeg = rollDeg;
    update();
}

void ObjectTransformWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(30, 30, 30));

    const QPointF center(width() * 0.5, height() * 0.5);
    const float axisLen = qMin(width(), height()) * 0.32f;

    const float yaw = qDegreesToRadians(m_yawDeg);
    const float pitch = qDegreesToRadians(m_pitchDeg);
    const float roll = qDegreesToRadians(m_rollDeg);

    const auto rotate2D = [&](float x, float y, float z) -> QPointF {
        float x1 = x * qCos(yaw) + z * qSin(yaw);
        float z1 = -x * qSin(yaw) + z * qCos(yaw);
        float y2 = y * qCos(pitch) - z1 * qSin(pitch);
        float z2 = y * qSin(pitch) + z1 * qCos(pitch);
        float x3 = x1 * qCos(roll) - y2 * qSin(roll);
        float y3 = x1 * qSin(roll) + y2 * qCos(roll);
        Q_UNUSED(z2);
        return center + QPointF(x3 * axisLen, -y3 * axisLen);
    };

    const QPointF origin = center;
    const QPointF xEnd = rotate2D(axisLen, 0.0f, 0.0f);
    const QPointF yEnd = rotate2D(0.0f, axisLen, 0.0f);
    const QPointF zEnd = rotate2D(0.0f, 0.0f, axisLen);

    auto drawAxis = [&](const QPointF& end, const QColor& color) {
        QPen pen(color, 2.0);
        painter.setPen(pen);
        painter.drawLine(origin, end);

        const QPointF dir = end - origin;
        const float len = qSqrt(dir.x() * dir.x() + dir.y() * dir.y());
        if (len < 1.0f) {
            return;
        }
        const QPointF unit(dir.x() / len, dir.y() / len);
        const QPointF ortho(-unit.y(), unit.x());
        const QPointF tip = end;
        const QPointF wingA = tip - unit * 8.0 + ortho * 4.0;
        const QPointF wingB = tip - unit * 8.0 - ortho * 4.0;
        painter.drawLine(tip, wingA);
        painter.drawLine(tip, wingB);
    };

    drawAxis(zEnd, QColor(80, 120, 220));
    drawAxis(yEnd, QColor(80, 200, 80));
    drawAxis(xEnd, QColor(220, 80, 80));

    painter.setPen(QPen(QColor(60, 60, 60), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(origin, 3.0, 3.0);
}
