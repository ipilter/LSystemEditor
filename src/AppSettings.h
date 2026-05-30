#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QSize>
#include <QString>

namespace DebounceElementIds {

inline constexpr const char* kRenderSize = "renderSize";

} // namespace DebounceElementIds

class AppSettings : public QObject
{
    Q_OBJECT

public:
    static AppSettings& instance();
    static void setInstance(AppSettings* settings);

    explicit AppSettings(QObject* parent = nullptr);

    static int defaultDebounceMsFor(const QString& elementId);

    int debounceMsFor(const QString& elementId) const;
    void setDebounceMs(const QString& elementId, int ms);

    QSize renderSize() const;
    void setRenderSize(int width, int height);

    QColor clearColor() const;
    void setClearColor(const QColor& color);

    void load();
    void save();

signals:
    void debounceMsChanged(const QString& elementId, int ms);

private:
    static int clampDebounceMs(int value);
    static int clampRenderDimension(int value);
    void seedDefaultDebounceValues();

    static AppSettings* s_instance;

    QHash<QString, int> m_debounceMs;
    QSize m_renderSize = QSize(16, 16);
    QColor m_clearColor = QColor(10, 10, 10);
};
