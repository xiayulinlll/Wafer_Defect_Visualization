QT += core
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = generate_sample_data
TEMPLATE = app

SOURCES += generate_sample_data.cpp

win32-g++ {
    QMAKE_CXXFLAGS += -Wall -Wextra
    QMAKE_CXXFLAGS_RELEASE += -O2
}
