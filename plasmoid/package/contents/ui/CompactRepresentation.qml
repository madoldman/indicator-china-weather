/*
 * 面板紧凑形态：天气图标 + 当前温度（横向排列）。
 *
 * 数据经 root.weatherClient 访问。图标为包内官方彩色图标集（icons/color），
 * 天气代码直接映射文件名；和风 now 接口夜间本返回 150-155 夜间码（自带昼夜
 * 形态），isNightHour 映射仅兜底「跨午夜未刷新仍显示白天码」的场景，随每次
 * 数据刷新重新求值。用普通 Image 渲染完整 artwork（不做单色遮罩）。
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

    // 实况图标：now 接口夜间返回 150-155 夜间码（自带昼夜形态），isNightHour
    // 映射仅作跨午夜未刷新的兜底；绑定随每次数据刷新（nowIcon 变更）重新求值，
    // 不随组件创建时刻冻结
    function iconSource(code) {
        return Qt.resolvedUrl(IconUtil.iconPath(code, IconUtil.isNightHour(new Date().getHours())))
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
