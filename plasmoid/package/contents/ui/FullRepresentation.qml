/*
 * 弹出面板完整形态：城市页签（自动定位 + 手动城市切换）、实况天气（大温度/
 * 图标/描述/风/湿度/体感）、7 天预报、空气质量（AQI + 类别）与 16 项生活指数。
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

    // 生活指数 4 列等宽：按面板宽度均分（减去左右边距与 3 个列间距），
    // 避免「空气指数」这类长名称撑宽单列、挤压其它列
    readonly property real indexCellWidth: (Kirigami.Units.gridUnit * 27
        - 2 * Kirigami.Units.largeSpacing
        - 3 * Kirigami.Units.smallSpacing * 2) / 4

    // 弹出窗口的建议尺寸（生活指数扩至 16 项后加高；超出屏幕可用高度时由
    // Plasma 钳制面板高度，7 天预报列表在其 ScrollView 内部滚动）
    Layout.preferredWidth: Kirigami.Units.gridUnit * 27
    Layout.preferredHeight: Kirigami.Units.gridUnit * 42

    function iconSource(code) {
        return Qt.resolvedUrl(IconUtil.iconPath(code, isNight))
    }

    // 生活指数彩色圆形背景：半透明柔和色（55% 不透明度），与面板背景融合，
    // 避免纯色块在深色面板上过亮刺眼
    function indexBgColor(type, name) {
        if (type === 1 || /运动/.test(name || "")) return "#8CB2DFDB"  // 青
        if (type === 2 || /洗车/.test(name || "")) return "#8CBBDEFB"  // 蓝
        if (type === 3 || /穿衣/.test(name || "")) return "#8CF8BBD0"  // 粉
        if (type === 4 || /钓鱼/.test(name || "")) return "#8C80DEEA"  // 淡青
        if (type === 5 || /紫外线/.test(name || "")) return "#8CFFF59D" // 黄
        if (type === 6 || /旅游/.test(name || "")) return "#8CB39DDB"  // 蓝紫
        if (type === 7 || /花粉/.test(name || "")) return "#8CFFCDD2"  // 淡粉
        if (type === 8 || /舒适/.test(name || "")) return "#8CFFF9C4"  // 淡米
        if (type === 9 || /感冒/.test(name || "")) return "#8CFFCCBC"  // 橙
        if (type === 10 || /空气/.test(name || "")) return "#8CC8E6C9" // 绿
        if (type === 11 || /空调/.test(name || "")) return "#8CB3E5FC" // 淡蓝
        if (type === 12 || /太阳镜/.test(name || "")) return "#8CE6EE9C" // 淡黄
        if (type === 13 || /化妆/.test(name || "")) return "#8CFCE4EC" // 浅粉
        if (type === 14 || /晾晒/.test(name || "")) return "#8CA5D6A7" // 淡绿
        if (type === 15 || /交通/.test(name || "")) return "#8CFFE0B2" // 浅橙
        if (type === 16 || /防晒/.test(name || "")) return "#8CFFF176" // 浅黄
        return "#40E0E0E0"
    }

    // 生活指数彩色图标：自官网精灵图（city-icon.png）裁剪的官方彩色图标
    // （16 类全覆盖），type 映射与和风 indices 一致
    function indexIconSource(type, name) {
        var key = ""
        if (type === 1 || /运动/.test(name || "")) key = "index_sport"
        else if (type === 2 || /洗车/.test(name || "")) key = "index_cw"
        else if (type === 3 || /穿衣/.test(name || "")) key = "index_drsg"
        else if (type === 4 || /钓鱼/.test(name || "")) key = "index_fishing"
        else if (type === 5 || /紫外线/.test(name || "")) key = "index_uv"
        else if (type === 6 || /旅游/.test(name || "")) key = "index_trav"
        else if (type === 7 || /花粉/.test(name || "")) key = "index_allergy"
        else if (type === 8 || /舒适/.test(name || "")) key = "index_comf"
        else if (type === 9 || /感冒/.test(name || "")) key = "index_flu"
        else if (type === 10 || /空气/.test(name || "")) key = "index_air"
        else if (type === 11 || /空调/.test(name || "")) key = "index_ac"
        else if (type === 12 || /太阳镜/.test(name || "")) key = "index_gl"
        else if (type === 13 || /化妆/.test(name || "")) key = "index_mu"
        else if (type === 14 || /晾晒/.test(name || "")) key = "index_dc"
        else if (type === 15 || /交通/.test(name || "")) key = "index_ptfc"
        else if (type === 16 || /防晒/.test(name || "")) key = "index_spi"
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

        // 1) 顶栏：城市页签（可横向滚动）+「+」添加城市 + 空气质量徽标
        RowLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            // 城市页签：页 0 = 自动定位（IP 解析出的城市名），其余 = 手动城市
            // （共享 gsettings citylist，最多约 9 个，超出宽度时横向滚动）；
            // 活动页高亮加粗，点击即切换当前城市
            PlasmaComponents3.ScrollView {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                implicitHeight: Kirigami.Units.gridUnit * 1.9

                ListView {
                    id: cityTabsView
                    orientation: ListView.Horizontal
                    clip: true
                    spacing: Kirigami.Units.smallSpacing
                    model: root.weatherClient.cityTabs ?? []

                    delegate: Item {
                        id: cityTab

                        // 页 0 定位中显示「正在定位…」，成功后由后端替换为城市名
                        readonly property bool isActive: index === root.weatherClient.activeCityIndex
                        readonly property bool locatingNow: modelData.isAuto
                                                             && (root.weatherClient.locating ?? false)

                        implicitWidth: tabLabel.implicitWidth + Kirigami.Units.largeSpacing
                        implicitHeight: Kirigami.Units.gridUnit * 1.7

                        Rectangle {
                            anchors.fill: parent
                            radius: Kirigami.Units.smallSpacing
                            color: cityTab.isActive
                                   ? Kirigami.Theme.highlightColor
                                   : (tabMouse.containsMouse
                                          ? Qt.rgba(Kirigami.Theme.textColor.r,
                                                    Kirigami.Theme.textColor.g,
                                                    Kirigami.Theme.textColor.b, 0.1)
                                          : "transparent")
                        }

                        PlasmaComponents3.Label {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: cityTab.locatingNow ? i18n("正在定位…")
                                                      : (modelData.name ?? "")
                            font.bold: cityTab.isActive
                            font.pixelSize: Kirigami.Units.gridUnit * 0.8
                            color: cityTab.isActive ? Kirigami.Theme.highlightedTextColor
                                                    : Kirigami.Theme.textColor
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.weatherClient.setActiveCityIndex(index)
                        }
                    }
                }
            }

            // 打开小部件配置对话框管理城市（main.qml 提供的统一入口）
            PlasmaComponents3.ToolButton {
                icon.name: "list-add"
                text: i18n("添加城市")
                display: PlasmaComponents3.ToolButton.IconOnly
                Layout.alignment: Qt.AlignVCenter
                onClicked: root.openConfig()
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

        // 4) 生活指数（16 项全覆盖：穿衣/洗车/感冒/紫外线/空气指数/运动/钓鱼/旅游/
        //    花粉过敏/舒适度/空调/太阳镜/化妆/晾晒/交通/防晒）：4 列 4 行紧凑排布，
        //    每项横向排列——圆形彩色图标居左，名称/等级两行文字在图标右侧（名称小字
        //    一行，等级稍大一行）；图标（medium）与两行文字垂直居中、同高，不额外占
        //    行高，每格高度仅由两行文字决定；4 列等宽（indexCellWidth 均分面板宽度），
        //    长名称（如「空气指数」）在均分宽度内 elide 截断，不撑宽单列
        GridLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.largeSpacing / 2
            columns: 4
            columnSpacing: Kirigami.Units.smallSpacing * 2
            rowSpacing: Kirigami.Units.smallSpacing * 2

            Repeater {
                model: root.weatherClient.indices ?? []

                delegate: RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: full.indexCellWidth
                    spacing: Kirigami.Units.largeSpacing / 2

                    // 圆形彩色底图标（medium 约 32px），与右侧两行文字垂直居中
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: Kirigami.Units.iconSizes.medium
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

                    // 名称（小字）+ 等级（大字加粗）两行，位于图标右侧
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        PlasmaComponents3.Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: modelData.name ?? ""
                            color: Kirigami.Theme.disabledTextColor
                            font.pixelSize: Kirigami.Units.gridUnit * 0.6
                            elide: Text.ElideRight
                        }
                        PlasmaComponents3.Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: modelData.category ?? ""
                            color: Kirigami.Theme.textColor
                            font.pixelSize: Kirigami.Units.gridUnit * 0.7
                            elide: Text.ElideRight
                        }
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
