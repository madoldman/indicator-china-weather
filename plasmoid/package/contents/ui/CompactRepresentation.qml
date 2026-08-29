/*
 * 面板紧凑形态：天气图标 + 当前温度（横向排列）。
 *
 * 数据经 QML 单例 WeatherClient 直接访问。图标随当前天气代码（100-999）
 * 变化，夜间自动切换带 n 后缀的夜间图标；按主题深浅选择 white/darkgrey
 * 图标集，用普通 Image 渲染完整 artwork（不做单色遮罩）。
 * 点击切换 Plasma 的展开状态（root 为 main.qml 的 PlasmoidItem，
 * 与 Plasma 官方 systemmonitor 小部件的写法一致），弹窗位置由 Plasma 决定。
 */

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents3
import org.kde.plasma.plasmoid 2.0

import org.madoldman.chinaweather
import "WeatherIconUtil.js" as IconUtil

Item {
    id: compact

    readonly property bool isNight: IconUtil.isNightHour(new Date().getHours())
    readonly property bool isDarkTheme: IconUtil.isDarkTheme(Kirigami.Theme.textColor)

    function iconSource(code) {
        return Qt.resolvedUrl(IconUtil.iconPath(code, isNight, isDarkTheme))
    }

    implicitWidth: Math.max(row.implicitWidth + Kirigami.Units.smallSpacing * 2,
                            Kirigami.Units.iconSizes.small)
    implicitHeight: Kirigami.Units.iconSizes.small

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing

        Image {
            Layout.preferredWidth: Kirigami.Units.iconSizes.roundedIconSize(
                compact.height - Kirigami.Units.smallSpacing)
            Layout.preferredHeight: Layout.preferredWidth
            fillMode: Image.PreserveAspectFit
            smooth: true
            source: compact.iconSource(root.weatherClient.nowIcon)
        }

        PlasmaComponents3.Label {
            // 温度只保留整数部分，面板空间有限
            text: root.weatherClient.nowTemp.length > 0 ? parseInt(root.weatherClient.nowTemp, 10) + "°" : "--"
            color: Kirigami.Theme.textColor
            font.pixelSize: Kirigami.Units.gridUnit * 0.8
            Layout.minimumWidth: implicitWidth
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.expanded = !root.expanded
    }
}
