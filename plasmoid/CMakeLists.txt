# Plasma 6 天气小部件：
#   1) Qt6 QML 扩展模块 org.madoldman.chinaweather（后端 WeatherClient）
#   2) Plasmoid 包（package/ 原样安装到系统小部件目录）

find_package(Qt6 REQUIRED COMPONENTS Qml Network)

# QML 模块产物（后端库 + 自动生成的插件 + qmldir）集中输出到本目录下，
# 便于统一安装与本地（未安装时）通过 QML2_IMPORT_PATH 加载测试
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml")

qt_add_library(chinaweather SHARED)
qt_add_qml_module(chinaweather
    URI org.madoldman.chinaweather
    VERSION 1.0
    SOURCES
        src/weatherclient.h
        src/weatherclient.cpp
)
target_include_directories(chinaweather PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(chinaweather PRIVATE Qt6::Qml Qt6::Network)
# 后端库与插件同目录输出（/…/qml/org/madoldman/chinaweather），运行时才能互相找到
set_target_properties(chinaweather PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${QT_QML_OUTPUT_DIRECTORY}/org/madoldman/chinaweather)

# QML 模块安装到 Qt6 的 QML 导入目录（Arch: /usr/lib/qt6/qml/）
set(CHINAWEATHER_QML_DIR lib/qt6/qml/org/madoldman/chinaweather)

# 注意：不能把两个 .so 都装进 QML 目录再依赖 INSTALL_RPATH=$ORIGIN——
# qt_add_qml_module 收尾会用自身逻辑覆盖插件目标的 INSTALL_RPATH（实测得到指向
# /usr/lib/qt6/lib 的错误 RUNPATH）。因此后端库安装到标准链接路径 /usr/lib，
# 插件经默认搜索路径即可解析依赖，无需任何 RPATH
install(TARGETS chinaweather
    LIBRARY DESTINATION lib)
install(TARGETS chinaweatherplugin
    LIBRARY DESTINATION ${CHINAWEATHER_QML_DIR})
install(FILES ${QT_QML_OUTPUT_DIRECTORY}/org/madoldman/chinaweather/qmldir
    DESTINATION ${CHINAWEATHER_QML_DIR})
if(EXISTS ${QT_QML_OUTPUT_DIRECTORY}/org/madoldman/chinaweather/chinaweather.qmltypes)
    install(FILES ${QT_QML_OUTPUT_DIRECTORY}/org/madoldman/chinaweather/chinaweather.qmltypes
        DESTINATION ${CHINAWEATHER_QML_DIR})
endif()

# Plasmoid 包（metadata.json + contents/）整体安装到 Plasma 小部件目录
install(DIRECTORY package/
    DESTINATION share/plasma/plasmoids/org.madoldman.chinaweather)
