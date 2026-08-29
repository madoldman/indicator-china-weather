/*
 * 配置页：城市选择（本地 china-city-list.csv 搜索，不发网络请求）+ 刷新间隔。
 *
 * cfg_cityId / cfg_cityName / cfg_refreshInterval 为 kcfg 注入属性
 * （对应 contents/config/main.xml），修改后点击配置对话框的「确定/应用」保存。
 */

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents3

import org.madoldman.chinaweather

ColumnLayout {
    id: configPage

    property string cfg_cityId
    property string cfg_cityName
    property int cfg_refreshInterval

    // Plasma 6.7 配置框架会额外注入以下属性，显式声明以消除 plasmashell 告警
    property string cfg_cityIdDefault
    property string cfg_cityNameDefault
    property int cfg_refreshIntervalDefault
    property string title

    // 搜索结果（QVariantList，元素为 {id, name, province}）
    property var searchResults: []

    // 仅用于本地城市表搜索的客户端实例（不发网络请求；配置对话框是
    // 独立引擎，无法经 root 访问主界面的客户端）
    WeatherClient {
        id: citySearchClient
    }

    // 当前模式提示：自动定位（cityId 为空）或已固定城市
    PlasmaComponents3.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: cfg_cityId.length > 0
              ? i18n("当前城市：%1（%2）", cfg_cityName, cfg_cityId)
              : i18n("当前模式：自动定位（按公网 IP 解析所在城市，每次启动重新解析）")
        color: cfg_cityId.length > 0 ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
        font.bold: true
    }

    PlasmaComponents3.Button {
        Layout.alignment: Qt.AlignLeft
        visible: cfg_cityId.length > 0
        icon.name: "gps"
        text: i18n("恢复自动定位")

        onClicked: {
            configPage.cfg_cityId = ""
            configPage.cfg_cityName = ""
        }
    }

    PlasmaComponents3.TextField {
        id: searchField
        Layout.fillWidth: true
        placeholderText: i18n("输入城市名 / 拼音 / 缩写搜索，如：北京 / changsha / bj")
        onTextChanged: configPage.updateResults()
    }

    PlasmaComponents3.ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit * 14

        ListView {
            id: resultList
            clip: true
            model: configPage.searchResults

            delegate: PlasmaComponents3.ItemDelegate {
                width: ListView.view.width
                highlighted: modelData.id === configPage.cfg_cityId
                text: modelData.name + "　" + modelData.province + "（" + modelData.id + "）"

                onClicked: {
                    configPage.cfg_cityId = modelData.id
                    configPage.cfg_cityName = modelData.name
                }
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
            text: i18n("刷新间隔（分钟）：")
            color: Kirigami.Theme.textColor
        }

        PlasmaComponents3.SpinBox {
            id: refreshSpin
            from: 5
            to: 360
            // kcfg 旧值可能超出范围，显示时收敛到有效区间
            value: Math.min(Math.max(configPage.cfg_refreshInterval || 30, refreshSpin.from), refreshSpin.to)
            onValueModified: configPage.cfg_refreshInterval = value
        }
    }

    PlasmaComponents3.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Kirigami.Theme.disabledTextColor
        font.pixelSize: Kirigami.Units.gridUnit * 0.7
        text: i18n("间隔在保存后立即生效；和风天气凭据通过环境变量 QWEATHER_API_KEY 提供，配置方式见主仓库 README「和风天气凭据配置」章节。")
    }

    function updateResults() {
        var keyword = searchField.text.trim()
        searchResults = keyword.length > 0 ? citySearchClient.searchCities(keyword, 30) : []
    }
}
