/*
 * Plasma 6 天气小部件（org.madoldman.chinaweather）入口
 *
 * 数据后端为本仓库的 Qt6 QML 模块 org.madoldman.chinaweather（WeatherClient），
 * 直连和风天气 API v7（凭据从环境变量读取，见 README「和风天气凭据配置」）。
 *
 * 弹出面板的锚定/定位完全由 Plasma 原生处理：fullRepresentation 会自动
 * 在面板上部件所在的位置向上/向下弹出，本文件不包含任何坐标/定位代码。
 */

import QtQuick

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core as PlasmaCore

import org.madoldman.chinaweather

PlasmoidItem {
    id: root

    // 天气数据客户端：kcfg 配置直接绑定。各视图通过 root.weatherClient 访问
    // （与官方 systemmonitor 一致；注意不要写 `weatherClient: weatherClient`，
    //  会被视图自身同名属性遮蔽而解析为 null）
    property WeatherClient weatherClient: WeatherClient {
        cityId: Plasmoid.configuration.cityId
        cityName: Plasmoid.configuration.cityName
    }

    Plasmoid.title: {
        const name = root.weatherClient.activeCityName
        if (name.length === 0) {
            return i18n("天气")
        }
        return root.weatherClient.autoMode ? i18n("%1（自动定位）", name) : name
    }

    // 面板图标的悬停提示
    toolTipMainText: {
        const name = root.weatherClient.activeCityName
        if (name.length === 0) {
            return root.weatherClient.locating ? i18n("正在定位…") : i18n("天气")
        }
        return root.weatherClient.autoMode ? i18n("%1（自动定位）", name) : name
    }
    toolTipSubText: root.weatherClient.nowText && root.weatherClient.nowTemp.length > 0
        ? i18n("%1 %2°C", root.weatherClient.nowText, root.weatherClient.nowTemp)
        : i18n("点击查看天气详情")

    // 自定义右键菜单：
    //   打开应用 -- 启动本仓库的 Qt 应用 indicator-china-weather（已下线托盘图标，
    //               可作为完整天气窗口使用；已在运行时唤起其主窗口）
    //   设置     -- 触发 Plasma 内建的 configure 动作，打开小部件配置对话框
    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18n("打开应用")
            icon.name: "indicator-china-weather"
            onTriggered: root.weatherClient.launchApp()
        },
        PlasmaCore.Action {
            text: i18n("设置")
            icon.name: "settings-configure"
            onTriggered: {
                var action = Plasmoid.internalAction("configure")
                if (action) {
                    action.trigger()
                }
            }
        }
    ]

    // 面板上的紧凑形态：天气图标 + 当前温度
    compactRepresentation: CompactRepresentation {}

    // 点击紧凑形态后由 Plasma 弹出的完整面板
    fullRepresentation: FullRepresentation {}

    // 小部件启动时拉取一次数据
    Component.onCompleted: root.weatherClient.refresh()

    // 面板展开（弹出面板可见）时刷新
    onExpandedChanged: {
        if (expanded) {
            root.weatherClient.refresh()
        }
    }
}
