#include <QApplication>
#include <QIcon>
#include "app/MainWindow.h"
#include "styles/FluentStyle.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Application metadata
    QApplication::setApplicationName("ImageCompare");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("ImageCompare");

    // Application icon
    app.setWindowIcon(QIcon(":/icons/app_icon.png"));

    // Apply Fluent 2 design system
    FluentStyle::applyGlobalStyle(&app);

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
