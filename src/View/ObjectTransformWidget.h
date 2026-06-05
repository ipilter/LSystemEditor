#pragma once

#include <QWidget>

class ObjectTransformWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ObjectTransformWidget(QWidget* parent = nullptr);

    void setYawPitchRoll(float yawDeg, float pitchDeg, float rollDeg);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float m_yawDeg = 0.0f;
    float m_pitchDeg = 0.0f;
    float m_rollDeg = 0.0f;
};
