#include "AppLog.h"
#include "AppSettings.h"
#include "MainView.h"
#include "SceneController.h"
#include "SceneModel.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    AppSettings settings;
    AppSettings::setInstance(&settings);

    QApplication app(argc, argv);

    AppLog::instance().info(QStringLiteral("PathTracer started"));
    QObject::connect(&app, &QApplication::aboutToQuit, &settings, &AppSettings::save);

    SceneModel model;
    MainView view;
    SceneController controller(&model, &view);

    view.show();
    return app.exec();
}
