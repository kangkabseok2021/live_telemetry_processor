#include "MainWindow.h"
#include <QApplication>
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Live Telemetry Processor");

    uint16_t port = 57300;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = static_cast<uint16_t>(std::atoi(argv[++i]));

    MainWindow win(port);
    win.show();
    return app.exec();
}
