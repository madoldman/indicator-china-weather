cmake_minimum_required(VERSION 3.24)

project(indicator-china-weather VERSION 3.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network DBus LinguistTools)
# 仅为 NETWM 头文件引入 KF6::WindowSystem
find_package(KF6WindowSystem REQUIRED)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSETTINGS_QT6 REQUIRED IMPORTED_TARGET gsettings-qt6)

find_path(GEOIP_INCLUDE_DIR GeoIP.h REQUIRED)
find_library(GEOIP_LIBRARY NAMES GeoIP REQUIRED)

option(ENABLE_UKUI_LOG4QT "Enable UKUI log4qt logging library (not available on Arch)" OFF)
if(ENABLE_UKUI_LOG4QT)
    find_library(UKUI_LOG4QT_LIBRARY ukui-log4qt REQUIRED)
endif()

set(APP_SOURCES
    src/daemondbus.cpp
    src/main.cpp
    src/addcityaction.cpp
    src/cityaddhotview.cpp
    src/cityaddsearchbox.cpp
    src/cityaddsearchdelegate.cpp
    src/cityaddsearchview.cpp
    src/cityaddwidget.cpp
    src/citycollectionitem.cpp
    src/citycollectionwidget.cpp
    src/darkcityaddsearchdelegate.cpp
    src/dbusadaptor.cpp
    src/geoipworker.cpp
    src/hotcity.cpp
    src/informationwidget.cpp
    src/leftupcitybtn.cpp
    src/leftupsearchbox.cpp
    src/leftupsearchdelegate.cpp
    src/leftupsearchview.cpp
    src/locationworker.cpp
    src/mainwindow.cpp
    src/menumodule.cpp
    src/promptwidget.cpp
    src/weathermanager.cpp
    src/weatherworker.cpp
    src/xatom-helper.cpp
    # vendored QtSingleApplication；qtlockedfile*.cpp 经 qtlocalpeer.cpp
    # 以 #include 方式并入同一命名空间，不单独编译，否则符号重复
    src/qtsingleapplication/qtsingleapplication.cpp
    src/qtsingleapplication/qtlocalpeer.cpp
)

set(APP_HEADERS
    src/mainwindow.h
    src/addcityaction.h
    src/cityaddhotview.h
    src/cityaddsearchbox.h
    src/cityaddsearchdelegate.h
    src/cityaddsearchview.h
    src/cityaddwidget.h
    src/citycollectionitem.h
    src/citycollectionwidget.h
    src/daemondbus.h
    src/darkcityaddsearchdelegate.h
    src/data.h
    src/dbusadaptor.h
    src/geoipworker.h
    src/hotcity.h
    src/informationwidget.h
    src/leftupcitybtn.h
    src/leftupsearchbox.h
    src/leftupsearchdelegate.h
    src/leftupsearchview.h
    src/locationworker.h
    src/menumodule.h
    src/promptwidget.h
    src/weathermanager.h
    src/weatherworker.h
    src/xatom-helper.h
    src/qtsingleapplication/qtsingleapplication.h
    src/qtsingleapplication/qtlocalpeer.h
    src/qtsingleapplication/qtlockedfile.h
)

set(APP_UI
    src/cityaddwidget.ui
    src/citycollectionitem.ui
    src/citycollectionwidget.ui
    src/informationwidget.ui
    src/mainwindow.ui
)

add_executable(indicator-china-weather
    ${APP_SOURCES}
    ${APP_HEADERS}
    ${APP_UI}
    res.qrc
)

target_include_directories(indicator-china-weather PRIVATE
    src
    src/qtsingleapplication
    ${GEOIP_INCLUDE_DIR}
)

target_link_libraries(indicator-china-weather PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
    KF6::WindowSystem
    X11::X11
    ${GEOIP_LIBRARY}
    PkgConfig::GSETTINGS_QT6
)

if(ENABLE_UKUI_LOG4QT)
    target_compile_definitions(indicator-china-weather PRIVATE ENABLE_UKUI_LOG4QT)
    target_link_libraries(indicator-china-weather PRIVATE ${UKUI_LOG4QT_LIBRARY})
endif()

set(TS_FILES
    translations/indicator-china-weather_zh_CN.ts
    translations/indicator-china-weather_bo.ts
)
qt_add_lrelease(
    TS_FILES ${TS_FILES}
    QM_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/translations
    QM_FILES_OUTPUT_VARIABLE QM_FILES
)

# 安装规则与 qmake 工程(.pro)的 INSTALLS 保持一致
install(TARGETS indicator-china-weather RUNTIME DESTINATION bin)
# .pro 中此文件写作 res/indicator-china-weather.png，实际路径在 res/control_icons/ 下
# （qmake 对缺失文件静默跳过，CMake 会硬失败，这里指向真实文件）
install(FILES res/control_icons/indicator-china-weather.png
    DESTINATION share/pixmaps)
install(FILES ${QM_FILES} DESTINATION share/indicator-china-weather/translations)
install(FILES indicator-china-weather.desktop
    DESTINATION share/applications)
# /etc/xdg/autostart 为绝对路径（同 .pro 行为），不随 CMAKE_INSTALL_PREFIX 变化
install(FILES indicator-china-weather.desktop
    DESTINATION /etc/xdg/autostart)
install(FILES data/org.china-weather-data.gschema.xml
    DESTINATION share/glib-2.0/schemas)

# Plasma 6 天气小部件（Plasmoid 包 + QML 后端模块 org.madoldman.chinaweather）
add_subdirectory(plasmoid)
