/*
 * 配置页：多城市管理 + 城市搜索添加 + 刷新间隔。
 *
 * 城市列表与自动定位状态的单一数据源为共享 gsettings
 * （org.china-weather-data.settings 的 citylist/autolocate，与应用侧同一份），
 * 经 managerClient 提供的 Q_INVOKABLE（setAutoLocate/addCity/removeCity）读写，
 * 修改即时生效并同步到应用与小部件。配置对话框是独立引擎，无法经 root 访问
 * 主界面的客户端实例，故各建一个 WeatherClient。
 *
 * cfg_cityId/cfg_cityName 为旧版 kcfg 单城市配置，已废弃（C++ 侧一次性迁移
 * 到共享 gsettings）；此处仅保留声明以消除 plasmashell 属性注入告警。
 */

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents3

import org.madoldman.chinaweather

ColumnLayout {
    id: configPage

    // 已废弃的旧 kcfg 单城市配置（不再读写）
    property string cfg_cityId
    property string cfg_cityName

    // Plasma 6.7 配置框架会额外注入以下属性，显式声明以消除 plasmashell 告警
    property string cfg_cityIdDefault
    property string cfg_cityNameDefault
    property string title

    // 搜索结果（QVariantList，元素为 {id, name, province}）
    property var searchResults: []

    // 仅用于本地城市表搜索的客户端实例（不发网络请求；配置对话框是
    // 独立引擎，无法经 root 访问主界面的客户端）
    WeatherClient {
        id: citySearchClient
    }

    // 多城市管理客户端：读 cityTabs/activeCityIndex 展示当前状态，
    // 经 setAutoLocate/addCity/removeCity 写共享 gsettings
    WeatherClient {
        id: managerClient
    }

    // 手动城市列表（cityTabs 去掉页 0 的自动定位项；
    // tabIndex 为该城市在完整页签中的下标，删除时定位用）
    readonly property var manualCityTabs: {
        var tabs = []
        var all = managerClient.cityTabs
        for (var i = 0; i < all.length; ++i) {
            if (!all[i].isAuto) {
                tabs.push({ name: all[i].name, id: all[i].id, tabIndex: i })
            }
        }
        return tabs
    }

    // ---- 自动定位 ----
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: {
                var tabs = managerClient.cityTabs
                var idx = managerClient.activeCityIndex
                var tab = (idx >= 0 && idx < tabs.length) ? tabs[idx] : null
                if (!tab || tab.isAuto) {
                    return i18n("当前模式：自动定位（按公网 IP 解析所在城市，每次启动重新解析）")
                }
                return i18n("当前城市：%1（%2）", tab.name, tab.id)
            }
            color: managerClient.autoMode ? Kirigami.Theme.disabledTextColor
                                          : Kirigami.Theme.textColor
            font.bold: true
        }

        PlasmaComponents3.Button {
            icon.name: "gps"
            text: i18n("自动定位")
            onClicked: managerClient.setAutoLocate(true)
        }
    }

    // ---- 已存城市 ----
    PlasmaComponents3.Label {
        text: i18n("已存城市：")
        color: Kirigami.Theme.textColor
    }

    PlasmaComponents3.ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 8

        ListView {
            id: savedCityView
            clip: true
            model: configPage.manualCityTabs

            delegate: RowLayout {
                width: ListView.view.width
                spacing: Kirigami.Units.smallSpacing

                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    text: (modelData.name ?? "") + "（" + (modelData.id ?? "") + "）"
                    color: Kirigami.Theme.textColor
                    elide: Text.ElideRight
                }

                PlasmaComponents3.Button {
                    icon.name: "edit-delete"
                    text: i18n("删除")
                    onClicked: managerClient.removeCity(modelData.tabIndex)
                }
            }

            PlasmaComponents3.Label {
                anchors.centerIn: parent
                visible: savedCityView.count === 0
                text: i18n("暂无已存城市，可在下方搜索添加")
                color: Kirigami.Theme.disabledTextColor
            }
        }
    }

    // ---- 添加城市 ----
    PlasmaComponents3.TextField {
        id: searchField
        Layout.fillWidth: true
        placeholderText: i18n("输入城市名 / 拼音 / 缩写搜索，如：北京 / changsha / bj")
        onTextChanged: configPage.updateResults()
    }

    PlasmaComponents3.ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 12

        ListView {
            id: resultList
            clip: true
            model: configPage.searchResults

            // 点击搜索结果即添加到共享城市列表（后端保证去重）
            delegate: PlasmaComponents3.ItemDelegate {
                width: ListView.view.width
                text: modelData.name + "　" + modelData.province + "（" + modelData.id + "）"

                onClicked: managerClient.addCity(modelData.id, modelData.name)
            }

            PlasmaComponents3.Label {
                anchors.centerIn: parent
                visible: resultList.count === 0
                text: searchField.text.length > 0 ? i18n("未找到匹配的城市") : i18n("输入关键词搜索城市")
                color: Kirigami.Theme.disabledTextColor
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents3.Label {
            text: i18n("刷新间隔：")
            color: Kirigami.Theme.textColor
        }

        // 预设档位与应用菜单一致，保证两侧状态永远一致；选择即生效
        PlasmaComponents3.ComboBox {
            id: intervalBox
            Layout.preferredWidth: Kirigami.Units.gridUnit * 8
            model: [5, 10, 20, 30, 60]
            currentIndex: {
                var idx = model.indexOf(citySearchClient.refreshInterval)
                return idx >= 0 ? idx : 2 // 历史遗留的非预设值回退显示 20 分钟
            }
            onActivated: citySearchClient.refreshInterval = model[currentIndex]
        }
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Kirigami.Theme.disabledTextColor
        font.pixelSize: Kirigami.Units.gridUnit * 0.7
        text: i18n("城市列表与自动定位状态即时保存，并与天气应用实时同步（共享 gsettings）；间隔选择后立即生效；和风天气凭据通过环境变量 QWEATHER_API_KEY 提供，配置方式见主仓库 README「和风天气凭据配置」章节。")
    }

    function updateResults() {
        var keyword = searchField.text.trim()
        searchResults = keyword.length > 0 ? citySearchClient.searchCities(keyword, 30) : []
    }
}
