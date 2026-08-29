/*
 * 配置对话框结构：单个「城市与刷新」页，实际页面为 ui/ConfigCity.qml
 */
import QtQuick

import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18n("城市与刷新")
        icon: "weather-cloud"
        source: "ConfigCity.qml"
    }
}
