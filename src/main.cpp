#include <QApplication>
#include <QSurfaceFormat>
#include "SqueegeeWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QSurfaceFormat format;
    format.setMajorVersion(4);
    format.setMinorVersion(3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    SqueegeeWindow window;
    window.resize(1280, 720);
    window.setTitle("Digital Squeegee Art");
    window.show();

    return app.exec();
}
