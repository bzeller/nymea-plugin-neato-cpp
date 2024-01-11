include($$[QT_INSTALL_PREFIX]/include/nymea/plugin.pri)

CONFIG += c++17
QT += network

SOURCES += integrationpluginneato.cpp \
           neato.cpp

HEADERS += integrationpluginneato.h \
           neato.h 
