/*
 * 弹出面板完整形态：城市名、实况天气（大温度/图标/描述/风/湿度/体感）、
 * 7 天预报、空气质量（AQI + 类别）与 6 项生活指数。
 *
 * 面板的弹出与锚定完全由 Plasma 原生完成（PlasmoidItem 的
 * fullRepresentation 自动在部件位置弹出），本组件不含任何坐标/定位代码。
 */

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras

import org.madoldman.chinaweather
import "WeatherIconUtil.js" as IconUtil

PlasmaExtras.Representation {
    id: full

    readonly property bool isNight: IconUtil.isNightHour(new Date().getHours())

    // 弹出窗口的建议尺寸（内容较长时由内部 ScrollView 滚动）
    Layout.preferredWidth: Kirigami.Units.gridUnit * 27
    Layout.preferredHeight: Kirigami.Units.gridUnit * 33

    function iconSource(code) {
        return Qt.resolvedUrl(IconUtil.iconPath(code, isNight))
    }

    // 生活指数彩色圆形背景：半透明柔和色（55% 不透明度），与面板背景融合，
    // 避免纯色块在深色面板上过亮刺眼
    function indexBgColor(type, name) {
        if (type === 1 || /运动/.test(name || "")) return "#8CB2DFDB"  // 青
        if (type === 2 || /洗车/.test(name || "")) return "#8CBBDEFB"  // 蓝
        if (type === 3 || /穿衣/.test(name || "")) return "#8CF8BBD0"  // 粉
        if (type === 5 || /紫外线/.test(name || "")) return "#8CFFF59D" // 黄
        if (type === 9 || /感冒/.test(name || "")) return "#8CFFCCBC"  // 橙
        if (type === 10 || /空气/.test(name || "")) return "#8CC8E6C9" // 绿
        return "#40E0E0E0"
    }

    // 生活指数彩色图标：自官网精灵图（city-icon.png）裁剪的官方彩色图标
    // （穿衣/感冒/紫外线/洗车/空气/运动），type 映射与和风 indices 一致
    function indexIconSource(type, name) {
        var key = ""
        if (type === 1 || /运动/.test(name || "")) key = "index_sport"
        else if (type === 2 || /洗车/.test(name || "")) key = "index_cw"
        else if (type === 3 || /穿衣/.test(name || "")) key = "index_drsg"
        else if (type === 5 || /紫外线/.test(name || "")) key = "index_uv"
        else if (type === 9 || /感冒/.test(name || "")) key = "index_flu"
        else if (type === 10 || /空气/.test(name || "")) key = "index_air"
        return key.length > 0 ? Qt.resolvedUrl("icons/index/" + key + ".png") : ""
    }

    // 今天/明天直接标注，其余按日期算出星期
    function weekLabel(dateStr, index) {
        if (index === 0) {
            return i18n("今天")
        }
        if (index === 1) {
            return i18n("明天")
        }
        var day = new Date(dateStr + "T00:00:00")
        if (isNaN(day.getTime())) {
            return dateStr
        }
        var names = [i18n("周日"), i18n("周一"), i18n("周二"), i18n("周三"),
                     i18n("周四"), i18n("周五"), i18n("周六")]
        return names[day.getDay()]
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        // 1) 顶栏：城市名 + 空气质量徽标
        RowLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.Label {
                text: {
                    var name = root.weatherClient.activeCityName ?? ""
                    if (name.length === 0) {
                        name = (root.weatherClient.locating ?? false) ? i18n("正在定位…") : i18n("天气")
                    }
                    if (root.weatherClient.autoMode ?? false) {
                        name += i18n("·自动")
                    }
                    return name
                }
                font.pixelSize: Kirigami.Units.gridUnit * 1.1
                font.bold: true
                color: Kirigami.Theme.textColor
            }

            Item {
                Layout.fillWidth: true
            }

            Rectangle {
                visible: (root.weatherClient.airAqi ?? "").length > 0
                implicitWidth: aqiLabel.implicitWidth + Kirigami.Units.largeSpacing
                implicitHeight: aqiLabel.implicitHeight + Kirigami.Units.smallSpacing / 2
                radius: Kirigami.Units.smallSpacing / 2
                color: IconUtil.aqiColor(root.weatherClient.airCategory ?? "")
                       || Kirigami.Theme.disabledTextColor

                PlasmaComponents3.Label {
                    id: aqiLabel
                    anchors.centerIn: parent
                    text: "AQI " + (root.weatherClient.airAqi ?? "") + " "
                          + (root.weatherClient.airCategory ?? "")
                    color: "#ffffff"
                    font.pixelSize: Kirigami.Units.gridUnit * 0.7
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

        // 2) 实况天气：大温度 + 图标 + 描述 + 风向/湿度/体感
        RowLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            Image {
                Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                Layout.preferredHeight: Layout.preferredWidth
                fillMode: Image.PreserveAspectFit
                smooth: true
                source: full.iconSource(root.weatherClient.nowIcon)
            }

            ColumnLayout {
                spacing: 0

                PlasmaComponents3.Label {
                    text: {
                        var temp = root.weatherClient.nowTemp ?? ""
                        return temp.length > 0 ? temp + "°C" : "--"
                    }
                    font.pixelSize: Kirigami.Units.gridUnit * 2
                    color: Kirigami.Theme.textColor
                }

                PlasmaComponents3.Label {
                    text: root.weatherClient.nowText ?? ""
                    font.pixelSize: Kirigami.Units.gridUnit * 0.9
                    color: Kirigami.Theme.textColor
                }
            }

            Item {
                Layout.fillWidth: true
            }

            GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.smallSpacing / 2

                PlasmaComponents3.Label {
                    text: i18n("风向")
                    color: Kirigami.Theme.disabledTextColor
                    font.pixelSize: Kirigami.Units.gridUnit * 0.75
                }
                PlasmaComponents3.Label {
                    text: (root.weatherClient.windDir ?? "") + " "
                          + (root.weatherClient.windScale ?? "") + i18n("级")
                    color: Kirigami.Theme.textColor
                    font.pixelSize: Kirigami.Units.gridUnit * 0.75
                }
                PlasmaComponents3.Label {
                    text: i18n("湿度")
                    color: Kirigami.Theme.disabledTextColor
                    font.pixelSize: Kirigami.Units.gridUnit * 0.75
                }
                PlasmaComponents3.Label {
                    text: (root.weatherClient.humidity ?? "") + "%"
                    color: Kirigami.Theme.textColor
                    font.pixelSize: Kirigami.Units.gridUnit * 0.75
                }
                PlasmaComponents3.Label {
                    text: i18n("体感")
                    color: Kirigami.Theme.disabledTextColor
                    font.pixelSize: Kirigami.Units.gridUnit * 0.75
                }
                PlasmaComponents3.Label {
                    text: (root.weatherClient.feelsLike ?? "") + "°C"
                    color: Kirigami.Theme.textColor
                    font.pixelSize: Kirigami.Units.gridUnit * 0.75
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

        // 3) 7 天预报列表
        PlasmaComponents3.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing

            ListView {
                id: dailyView
                clip: true
                spacing: Kirigami.Units.smallSpacing
                model: root.weatherClient.daily ?? []

                delegate: Item {
                    width: ListView.view.width
                    implicitHeight: Kirigami.Units.gridUnit * 2.6

                    // 今日高亮、其余行浅色底，行与行之间由间距自然分隔
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: Kirigami.Units.smallSpacing
                        anchors.rightMargin: Kirigami.Units.smallSpacing
                        radius: Kirigami.Units.smallSpacing
                        color: index === 0
                               ? Kirigami.Theme.highlightColor
                               : Qt.rgba(Kirigami.Theme.textColor.r,
                                         Kirigami.Theme.textColor.g,
                                         Kirigami.Theme.textColor.b, 0.07)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Kirigami.Units.largeSpacing
                        anchors.rightMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.largeSpacing

                        ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing / 2
                            PlasmaComponents3.Label {
                                text: full.weekLabel(modelData.date ?? "", index)
                                color: index === 0 ? Kirigami.Theme.highlightedTextColor
                                                  : Kirigami.Theme.textColor
                                font.pixelSize: Kirigami.Units.gridUnit * 0.8
                                font.bold: index === 0
                            }
                            PlasmaComponents3.Label {
                                text: modelData.date ?? ""
                                color: index === 0 ? Kirigami.Theme.highlightedTextColor
                                                  : Kirigami.Theme.disabledTextColor
                                font.pixelSize: Kirigami.Units.gridUnit * 0.65
                            }
                        }

                        Image {
                            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                            Layout.preferredHeight: Layout.preferredWidth
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            source: full.iconSource(modelData.iconDay)
                        }

                        PlasmaComponents3.Label {
                            text: modelData.textDay ?? ""
                            color: index === 0 ? Kirigami.Theme.highlightedTextColor
                                              : Kirigami.Theme.textColor
                            font.pixelSize: Kirigami.Units.gridUnit * 0.8
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        PlasmaComponents3.Label {
                            text: (modelData.tempMin ?? "") + " ~ "
                                  + (modelData.tempMax ?? "") + "°C"
                            color: index === 0 ? Kirigami.Theme.highlightedTextColor
                                              : Kirigami.Theme.textColor
                            font.pixelSize: Kirigami.Units.gridUnit * 0.8
                            Layout.minimumWidth: Kirigami.Units.gridUnit * 7
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                PlasmaComponents3.Label {
                    anchors.centerIn: parent
                    visible: dailyView.count === 0
                    text: root.weatherClient.loading ?? false ? i18n("正在获取天气数据…") : i18n("暂无预报数据")
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

        // 4) 生活指数（6 项：穿衣/洗车/感冒/紫外线/空气污染扩散/运动）：
        //    2 列 3 行，每项「图标 + 名称：等级」，不显示长描述，保持简洁
        GridLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.largeSpacing
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing * 2
            rowSpacing: Kirigami.Units.smallSpacing * 2

            Repeater {
                model: root.weatherClient.indices ?? []

                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Rectangle {
                        Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                             + Kirigami.Units.smallSpacing
                        Layout.preferredHeight: Layout.preferredWidth
                        radius: width / 2
                        color: full.indexBgColor(modelData.type, modelData.name)

                        Image {
                            anchors.centerIn: parent
                            width: parent.width * 0.68
                            height: width
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            source: full.indexIconSource(modelData.type, modelData.name)
                        }
                    }

                    PlasmaComponents3.Label {
                        text: (modelData.name ?? "") + "："
                        color: Kirigami.Theme.disabledTextColor
                        font.pixelSize: Kirigami.Units.gridUnit * 0.75
                    }
                    PlasmaComponents3.Label {
                        text: modelData.category ?? ""
                        color: Kirigami.Theme.textColor
                        font.pixelSize: Kirigami.Units.gridUnit * 0.75
                        font.bold: true
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // 5) 状态栏：错误信息 / 更新时间 / 加载指示
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            visible: root.weatherClient.error ?? false
            text: root.weatherClient.errorString ?? ""
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.negativeTextColor
            font.pixelSize: Kirigami.Units.gridUnit * 0.7
        }

        RowLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            PlasmaComponents3.BusyIndicator {
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: implicitWidth
                running: visible
                visible: (root.weatherClient.loading ?? false)
                         || (root.weatherClient.locating ?? false)
            }

            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: (root.weatherClient.updateTime ?? "").length > 0
                      ? i18n("更新时间 %1", root.weatherClient.updateTime)
                      : ""
                color: Kirigami.Theme.disabledTextColor
                font.pixelSize: Kirigami.Units.gridUnit * 0.65
                elide: Text.ElideRight
            }
        }
    }
}
