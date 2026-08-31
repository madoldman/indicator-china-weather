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

    // 天气数据客户端：城市列表/自动定位状态直接读写共享 gsettings
    // （org.china-weather-data.settings 的 citylist/autolocate，与应用共用），
    // kcfg 不再作为城市数据源。cityId 绑定仅用于把旧版 kcfg 的单城市配置
    // 一次性迁移进 gsettings（见 WeatherClient::setCityId 的迁移逻辑）。
    // 各视图通过 root.weatherClient 访问（与官方 systemmonitor 一致；注意
    // 不要写 `weatherClient: weatherClient`，会被视图自身同名属性遮蔽而解析为 null）
    property WeatherClient weatherClient: WeatherClient {
        cityId: Plasmoid.configuration.cityId
    }

    // 唤起小部件配置对话框：弹出面板城市页签的「+」使用，
    // 走 Plasma 内建的 configure 动作
    function openConfig() {
        var action = Plasmoid.internalAction("configure")
        if (action) {
            action.trigger()
        }
    }

    // 标题/悬停提示随活动页签（activeCityName 已反映当前浏览的城市）；
    // 「（自动定位）」后缀仅在处于自动定位页（页 0）时附加，浏览手动城市时不误标
    Plasmoid.title: {
        const name = root.weatherClient.activeCityName
        if (name.length === 0) {
            return i18n("天气")
        }
        return (root.weatherClient.autoMode && root.weatherClient.activeCityIndex === 0)
            ? i18n("%1（自动定位）", name) : name
    }

    // 面板图标的悬停提示
    toolTipMainText: {
        const name = root.weatherClient.activeCityName
        if (name.length === 0) {
            return root.weatherClient.locating ? i18n("正在定位…") : i18n("天气")
        }
        return (root.weatherClient.autoMode && root.weatherClient.activeCityIndex === 0)
            ? i18n("%1（自动定位）", name) : name
    }
    toolTipSubText: root.weatherClient.nowText && root.weatherClient.nowTemp.length > 0
        ? i18n("%1 %2°C", root.weatherClient.nowText, root.weatherClient.nowTemp)
        : i18n("点击查看天气详情")

    // 自定义右键菜单：
    //   打开应用 -- 启动本仓库的 Qt 应用 indicator-china-weather（已下线托盘图标，
    //               可作为完整天气窗口使用；已在运行时唤起其主窗口）
    // 不再自定义「设置」动作：Plasma 会因存在配置页而在右键菜单自动提供
    // 「配置天气…」，与自定义项重复；「+」按钮与配置页仍经 openConfig() 打开
    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18n("打开应用")
            icon.name: "indicator-china-weather"
            onTriggered: root.weatherClient.launchApp()
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
