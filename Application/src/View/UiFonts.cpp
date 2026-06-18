#include "UiFonts.h"

#include <QApplication>
#include <QFont>

QFont monospaceFont(int pointSize)
{
    QFont font;
    font.setFamilies({QStringLiteral("Consolas"), QStringLiteral("Courier New"), QStringLiteral("monospace")});
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(pointSize);
    return font;
}

void applyMonospaceAppFont(QApplication& app)
{
    QFont font = app.font();
    font.setFamilies({QStringLiteral("Consolas"), QStringLiteral("Courier New"), QStringLiteral("monospace")});
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    app.setFont(font);
}
