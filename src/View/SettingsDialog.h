#pragma once

#include <QDialog>

class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    QSpinBox* m_renderSizeSpinDebounceSpinBox = nullptr;
    QSpinBox* m_maxSamplesSpinDebounceSpinBox = nullptr;
};
