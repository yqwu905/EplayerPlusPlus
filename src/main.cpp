#include <QApplication>
#include <QIcon>
#include "app/MainWindow.h"
#include "services/SettingsManager.h"
#include "styles/FluentStyle.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Application metadata
    QApplication::setApplicationName("ImageCompare");
    QApplication::setApplicationVersion("1.7.17");
    QApplication::setOrganizationName("ImageCompare");

    // Application icon
    app.setWindowIcon(QIcon(":/icons/app_icon.png"));

    FluentStyle::applyGlobalStyle(&app, SettingsManager().darkAppearance());

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
