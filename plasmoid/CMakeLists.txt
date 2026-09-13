# Plasma 6 天气小部件：
#   1) Qt6 QML 扩展模块 org.madoldman.chinaweather（后端 WeatherClient）
#   2) Plasmoid 包（package/ 原样安装到系统小部件目录）

find_package(Qt6 REQUIRED COMPONENTS Qml Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSETTINGS_QT6 REQUIRED IMPORTED_TARGET gsettings-qt6)

include(GNUInstallDirs)

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
target_link_libraries(chinaweather PRIVATE Qt6::Qml Qt6::Network PkgConfig::GSETTINGS_QT6)
# 后端库与插件同目录输出（/…/qml/org/madoldman/chinaweather），运行时才能互相找到
set_target_properties(chinaweather PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${QT_QML_OUTPUT_DIRECTORY}/org/madoldman/chinaweather)

# 城市表 CSV 的安装路径按安装前缀经编译定义注入后端库（替换原先硬编码的
# /usr 绝对路径，非 /usr 前缀打包也能找到；C++ 侧保留同值兜底）
set(CHINAWEATHER_PLASMOID_INSTALL_DIR
    ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/plasma/plasmoids/org.madoldman.chinaweather)
target_compile_definitions(chinaweather PRIVATE
    CHINAWEATHER_PACKAGED_CITY_CSV="${CHINAWEATHER_PLASMOID_INSTALL_DIR}/contents/data/china-city-list.csv")

# QML 模块安装到 Qt6 的 QML 导入目录（QT6_INSTALL_QML 为相对 Qt 安装前缀的
# 布局路径，Arch: lib/qt6/qml/；作为相对 DESTINATION 随本工程安装前缀解析）
set(CHINAWEATHER_QML_DIR ${QT6_INSTALL_QML}/org/madoldman/chinaweather)

# 注意：不能把两个 .so 都装进 QML 目录再依赖 INSTALL_RPATH=$ORIGIN——后端库
# 必须装在标准链接路径（GNUInstallDirs 的 libdir，Arch: /usr/lib）才能被默认
# 搜索路径解析。此外 qt_add_qml_module 会为插件自动写入 INSTALL_RPATH，其值按
# 「qml/<URI 路径> 直接位于安装前缀下」的假定布局推算（$ORIGIN/../../../../lib），
# 与本工程实际安装布局 <prefix>/lib/qt6/qml/... 差两层，实际解析到
# <prefix>/lib/qt6/lib；标准 /usr 打包靠系统默认搜索路径侥幸解析成功，
# 自定义前缀则失败（见 Qt6QmlMacros.cmake：该值在插件 INSTALL_RPATH 未设置时
# 才写入，同步执行、无延迟覆盖点，可在 qt_add_qml_module 之后按实际布局覆写）
install(TARGETS chinaweather
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(NOT IS_ABSOLUTE "${QT6_INSTALL_QML}" AND NOT IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    # 由插件实际安装目录到 libdir 的相对路径（借用假根目录保持相对形式），
    # 对任意安装前缀与 GNUInstallDirs libdir 取值均成立；/usr 标准布局下
    # 解析结果与现状一致（/usr/lib），仅自定义前缀由不可解析变为可解析
    file(RELATIVE_PATH _cw_plugin_to_libdir
        "/__cw_root__/${QT6_INSTALL_QML}/org/madoldman/chinaweather"
        "/__cw_root__/${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(chinaweatherplugin PROPERTIES
        INSTALL_RPATH "$ORIGIN/${_cw_plugin_to_libdir}")
endif()
install(TARGETS chinaweatherplugin
    LIBRARY DESTINATION ${CHINAWEATHER_QML_DIR})
install(FILES ${QT_QML_OUTPUT_DIRECTORY}/org/madoldman/chinaweather/qmldir
    DESTINATION ${CHINAWEATHER_QML_DIR})
# qmltypes 与 qmldir 同为 qt_add_qml_module 在构建期的生成产物（本模块未设
# NO_GENERATE_QMLTYPES），configure 阶段文件尚不存在，禁止用 if(EXISTS) 条件
# 安装——全新构建目录下该条件恒为假，安装规则被永久跳过
install(FILES ${QT_QML_OUTPUT_DIRECTORY}/org/madoldman/chinaweather/chinaweather.qmltypes
    DESTINATION ${CHINAWEATHER_QML_DIR})

# Plasmoid 包（metadata.json + contents/）整体安装到 Plasma 小部件目录。
# 城市表除外：以仓库 data/china-city-list.csv 为单一来源（App 内嵌资源同源，
# App 与小部件读到同一份数据），避免 package/contents/data/ 下的拷贝与 data/
# 静默分叉；package/ 内的拷贝保留，仅供 kpackagetool6 直装 package/ 流程使用
install(DIRECTORY package/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/plasma/plasmoids/org.madoldman.chinaweather
    PATTERN "contents/data/china-city-list.csv" EXCLUDE)
install(FILES ${CMAKE_SOURCE_DIR}/data/china-city-list.csv
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/plasma/plasmoids/org.madoldman.chinaweather/contents/data)
