/*
 * 面板紧凑形态：天气图标 + 当前温度（横向排列）。
 *
 * 图标随当前天气代码（100-999）变化，夜间自动切换带 n 后缀的夜间图标；
 * 单色图标经 Kirigami.Icon 的 isMask 按主题文字色着色，深浅色面板均可用。
 * 点击后切换 Plasma 的展开状态（弹窗位置由 Plasma 决定，本组件不涉及坐标）。
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

    // 由 main.qml 注入：数据客户端与所属 PlasmoidItem（用于切换展开状态）
    property WeatherClient weatherClient
    property PlasmoidItem appletItem

    readonly property bool isNight: IconUtil.isNightHour(new Date().getHours())

    function iconSource(code) {
        return Qt.resolvedUrl("icons/" + IconUtil.iconBase(code, isNight) + ".png")
    }

    implicitWidth: Math.max(row.implicitWidth + Kirigami.Units.smallSpacing * 2,
                            Kirigami.Units.iconSizes.small)
    implicitHeight: Kirigami.Units.iconSizes.small

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            implicitWidth: Kirigami.Units.iconSizes.roundedIconSize(
                compact.height - Kirigami.Units.smallSpacing)
            implicitHeight: implicitWidth

            source: compact.iconSource(compact.weatherClient ? compact.weatherClient.nowIcon : "")
            isMask: true
            color: Kirigami.Theme.textColor
        }

        PlasmaComponents3.Label {
            // 温度只保留整数部分，面板空间有限
            text: {
                var temp = compact.weatherClient ? compact.weatherClient.nowTemp : ""
                return temp.length > 0 ? parseInt(temp, 10) + "°" : "--"
            }
            color: Kirigami.Theme.textColor
            font.pixelSize: Kirigami.Units.gridUnit * 0.8
            Layout.minimumWidth: implicitWidth
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (compact.appletItem) {
                compact.appletItem.expanded = !compact.appletItem.expanded
            }
        }
    }
}
