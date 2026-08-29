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

    // 由 main.qml 注入的数据客户端
    property WeatherClient weatherClient

    readonly property bool isNight: IconUtil.isNightHour(new Date().getHours())

    // 弹出窗口的建议尺寸（内容较长时由内部 ScrollView 滚动）
    Layout.preferredWidth: Kirigami.Units.gridUnit * 25
    Layout.preferredHeight: Kirigami.Units.gridUnit * 30

    function iconSource(code) {
        return Qt.resolvedUrl("icons/" + IconUtil.iconBase(code, isNight) + ".png")
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
                    var name = full.weatherClient?.activeCityName ?? ""
                    if (name.length === 0) {
                        name = (full.weatherClient?.locating ?? false) ? i18n("正在定位…") : i18n("天气")
                    }
                    if (full.weatherClient?.autoMode ?? false) {
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
                visible: (full.weatherClient?.airAqi ?? "").length > 0
                implicitWidth: aqiLabel.implicitWidth + Kirigami.Units.largeSpacing
                implicitHeight: aqiLabel.implicitHeight + Kirigami.Units.smallSpacing / 2
                radius: Kirigami.Units.smallSpacing / 2
                color: IconUtil.aqiColor(full.weatherClient?.airCategory ?? "")
                       || Kirigami.Theme.disabledTextColor

                PlasmaComponents3.Label {
                    id: aqiLabel
                    anchors.centerIn: parent
                    text: "AQI " + (full.weatherClient?.airAqi ?? "") + " "
                          + (full.weatherClient?.airCategory ?? "")
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

            Kirigami.Icon {
                implicitWidth: Kirigami.Units.iconSizes.huge
                implicitHeight: implicitWidth
                source: full.iconSource(full.weatherClient?.nowIcon ?? "")
                isMask: true
                color: Kirigami.Theme.textColor
            }

            ColumnLayout {
                spacing: 0

                PlasmaComponents3.Label {
                    text: {
                        var temp = full.weatherClient?.nowTemp ?? ""
                        return temp.length > 0 ? temp + "°C" : "--"
                    }
                    font.pixelSize: Kirigami.Units.gridUnit * 2
                    color: Kirigami.Theme.textColor
                }

                PlasmaComponents3.Label {
                    text: full.weatherClient?.nowText ?? ""
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
                    text: (full.weatherClient?.windDir ?? "") + " "
                          + (full.weatherClient?.windScale ?? "") + i18n("级")
                    color: Kirigami.Theme.textColor
                    font.pixelSize: Kirigami.Units.gridUnit * 0.75
                }
                PlasmaComponents3.Label {
                    text: i18n("湿度")
                    color: Kirigami.Theme.disabledTextColor
                    font.pixelSize: Kirigami.Units.gridUnit * 0.75
                }
                PlasmaComponents3.Label {
                    text: (full.weatherClient?.humidity ?? "") + "%"
                    color: Kirigami.Theme.textColor
                    font.pixelSize: Kirigami.Units.gridUnit * 0.75
                }
                PlasmaComponents3.Label {
                    text: i18n("体感")
                    color: Kirigami.Theme.disabledTextColor
                    font.pixelSize: Kirigami.Units.gridUnit * 0.75
                }
                PlasmaComponents3.Label {
                    text: (full.weatherClient?.feelsLike ?? "") + "°C"
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
                model: full.weatherClient?.daily ?? []

                delegate: Item {
                    width: ListView.view.width
                    implicitHeight: Kirigami.Units.gridUnit * 1.7

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Kirigami.Units.smallSpacing
                        anchors.rightMargin: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing

                        ColumnLayout {
                            spacing: 0
                            PlasmaComponents3.Label {
                                text: full.weekLabel(modelData.date ?? "", index)
                                color: Kirigami.Theme.textColor
                                font.pixelSize: Kirigami.Units.gridUnit * 0.8
                            }
                            PlasmaComponents3.Label {
                                text: modelData.date ?? ""
                                color: Kirigami.Theme.disabledTextColor
                                font.pixelSize: Kirigami.Units.gridUnit * 0.65
                            }
                        }

                        Kirigami.Icon {
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: implicitWidth
                            source: full.iconSource(modelData.iconDay ?? "")
                            isMask: true
                            color: Kirigami.Theme.textColor
                        }

                        PlasmaComponents3.Label {
                            text: modelData.textDay ?? ""
                            color: Kirigami.Theme.textColor
                            font.pixelSize: Kirigami.Units.gridUnit * 0.8
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        PlasmaComponents3.Label {
                            text: (modelData.tempMin ?? "") + " ~ "
                                  + (modelData.tempMax ?? "") + "°C"
                            color: Kirigami.Theme.textColor
                            font.pixelSize: Kirigami.Units.gridUnit * 0.8
                        }
                    }
                }

                PlasmaComponents3.Label {
                    anchors.centerIn: parent
                    visible: dailyView.count === 0
                    text: full.weatherClient?.loading ?? false ? i18n("正在获取天气数据…") : i18n("暂无预报数据")
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

        // 4) 生活指数（6 项：穿衣/洗车/感冒/紫外线/空气污染扩散/运动）
        GridLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            Repeater {
                model: full.weatherClient?.indices ?? []

                delegate: ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true

                    PlasmaComponents3.Label {
                        text: (modelData.name ?? "") + "：" + (modelData.category ?? "")
                        color: Kirigami.Theme.textColor
                        font.pixelSize: Kirigami.Units.gridUnit * 0.75
                        font.bold: true
                    }
                    PlasmaComponents3.Label {
                        text: modelData.text ?? ""
                        color: Kirigami.Theme.disabledTextColor
                        font.pixelSize: Kirigami.Units.gridUnit * 0.7
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // 5) 状态栏：错误信息 / 更新时间 / 加载指示
        PlasmaComponents3.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            visible: full.weatherClient?.error ?? false
            text: full.weatherClient?.errorString ?? ""
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
                visible: (full.weatherClient?.loading ?? false)
                         || (full.weatherClient?.locating ?? false)
            }

            PlasmaComponents3.Label {
                Layout.fillWidth: true
                text: (full.weatherClient?.updateTime ?? "").length > 0
                      ? i18n("更新时间 %1", full.weatherClient.updateTime)
                      : ""
                color: Kirigami.Theme.disabledTextColor
                font.pixelSize: Kirigami.Units.gridUnit * 0.65
                elide: Text.ElideRight
            }
        }
    }
}
