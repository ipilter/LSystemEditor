#pragma once

#include <QColor>
#include <QDialog>

class QPushButton;
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
    QPushButton* m_accelAabbColorButton = nullptr;
    QPushButton* m_accelOctreeColorButton = nullptr;
    QColor m_accelAabbColor;
    QColor m_accelOctreeColor;
};
