/*
 * Copyright (C) 2020, KylinSoft Co., Ltd.
 *
 * Authors:
 *  刘传玉 <madoldman@users.noreply.github.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CHINAWEATHER_PLASMOID_WEATHERCLIENT_H
#define CHINAWEATHER_PLASMOID_WEATHERCLIENT_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <QtQml/qqmlregistration.h>
#include <QGSettings>

class QQmlEngine;
class QJSEngine;
class QNetworkAccessManager;
class QTimer;

/*
 * Plasma 小部件（org.madoldman.chinaweather）的数据后端。
 *
 * 直连和风天气 API v7 的四个请求（now / 7d / air / indices），与托盘应用
 * src/weatherworker.cpp 的端点、认证与字段映射保持一致，但为独立精简实现，
 * 不与托盘应用的 worker/信号体系耦合。
 *
 * 多城市支持：城市列表与自动定位状态的单一数据源为共享 gsettings
 * （org.china-weather-data.settings 的 citylist / autolocate，与 Qt 应用
 * 读写同一份键值）；本类据其维护城市页签模型（cityTabs，页 0 = 自动定位）
 * 与活动页（activeCityIndex），只请求活动城市的完整天气数据。
 *
 * QML 端在 main.qml（PlasmoidItem，id: root）实例化，各视图经
 * root.weatherClient 访问--与官方 systemmonitor 小部件的写法一致
 * （representation 组件通过声明上下文解析 main.qml 的 id）。
 */
class WeatherClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // 城市配置：多城市，数据源 = 共享 gsettings 的 citylist / autolocate。
    // cityId / cityName 为旧 kcfg 单城市配置的兼容入口，仅用于一次性迁移
    // （见 migrateLegacyCity），不再作为展示与请求的数据源。
    Q_PROPERTY(QString cityId READ cityId WRITE setCityId NOTIFY cityIdChanged)
    Q_PROPERTY(QString cityName READ cityName WRITE setCityName NOTIFY cityNameChanged)
    Q_PROPERTY(int refreshInterval READ refreshInterval WRITE setRefreshInterval NOTIFY refreshIntervalChanged)

    // 城市页签模型（元素 {id, name, isAuto}）：页 0 = 自动定位，其余 = 手动城市
    Q_PROPERTY(QVariantList cityTabs READ cityTabs NOTIFY cityTabsChanged)
    // 当前活动页：自动定位（或无手动城市）= 0；手动模式 = citylist[0] 对应页
    Q_PROPERTY(int activeCityIndex READ activeCityIndex NOTIFY activeCityIndexChanged)

    // 自动定位相关（readonly，供 UI 展示）
    Q_PROPERTY(bool autoMode READ autoMode NOTIFY autoModeChanged)
    Q_PROPERTY(bool locating READ locating NOTIFY locatingChanged)
    Q_PROPERTY(QString activeCityName READ activeCityName NOTIFY activeCityNameChanged)

    // 状态
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

    // 实况天气（/v7/weather/now）
    Q_PROPERTY(QString nowTemp READ nowTemp NOTIFY nowChanged)
    Q_PROPERTY(QString nowIcon READ nowIcon NOTIFY nowChanged)
    Q_PROPERTY(QString nowText READ nowText NOTIFY nowChanged)
    Q_PROPERTY(QString windDir READ windDir NOTIFY nowChanged)
    Q_PROPERTY(QString windScale READ windScale NOTIFY nowChanged)
    Q_PROPERTY(QString humidity READ humidity NOTIFY nowChanged)
    Q_PROPERTY(QString feelsLike READ feelsLike NOTIFY nowChanged)
    Q_PROPERTY(QString updateTime READ updateTime NOTIFY nowChanged)

    // 7 天预报（/v7/weather/7d），元素为 QVariantMap
    Q_PROPERTY(QVariantList daily READ daily NOTIFY dailyChanged)

    // 空气质量（/v7/air/now）
    Q_PROPERTY(QString airAqi READ airAqi NOTIFY airChanged)
    Q_PROPERTY(QString airCategory READ airCategory NOTIFY airChanged)

    // 生活指数（/v7/indices/1d，type=1,2,3,5,9,10），元素为 QVariantMap
    Q_PROPERTY(QVariantList indices READ indices NOTIFY indicesChanged)

public:
    explicit WeatherClient(QObject *parent = nullptr);

    // 立即刷新一次（上一批请求在途时忽略）
    Q_INVOKABLE void refresh();

    // 切换活动城市页：0 = 恢复自动定位；>0 = 切到对应手动城市（写时移到 citylist 首位）
    Q_INVOKABLE void setActiveCityIndex(int index);

    // 追加手动城市（已存在则忽略，去重；name 用于本地城市表未命中时的展示兜底）
    Q_INVOKABLE void addCity(const QString &id, const QString &name);

    // 删除手动城市页（页 0 自动定位页不可删；删的是活动城市时回退到新的
    // citylist[0]，列表清空则恢复自动定位）
    Q_INVOKABLE void removeCity(int tabIndex);

    // 切换自动定位（写共享 gsettings，重建页签并刷新）
    Q_INVOKABLE void setAutoLocate(bool on);

    // 启动/唤起本仓库的 Qt 应用（供小部件右键菜单「打开app」使用）
    Q_INVOKABLE bool launchApp();

    // 本地城市表搜索（china-city-list.csv），返回 [{id, name, province}, ...]
    Q_INVOKABLE QVariantList searchCities(const QString &keyword, int limit = 30);

    QString cityId() const { return m_cityId; }
    void setCityId(const QString &cityId);
    QString cityName() const { return m_cityName; }
    void setCityName(const QString &cityName);
    int refreshInterval() const { return m_refreshInterval; }
    void setRefreshInterval(int minutes);

    QVariantList cityTabs() const { return m_cityTabs; }
    int activeCityIndex() const { return m_activeCityIndex; }

    // 自动定位模式（gsettings autolocate，键缺失时默认 true）；IP 定位进行中；
    // 实际生效的城市名（IP 解析名或 citylist[0] 的城市名）
    bool autoMode() const { return m_autolocate; }
    bool locating() const { return m_locating; }
    QString activeCityName();

    bool loading() const { return m_loading; }
    bool error() const { return m_hasError; }
    QString errorString() const { return m_errorString; }

    QString nowTemp() const { return m_nowTemp; }
    QString nowIcon() const { return m_nowIcon; }
    QString nowText() const { return m_nowText; }
    QString windDir() const { return m_windDir; }
    QString windScale() const { return m_windScale; }
    QString humidity() const { return m_humidity; }
    QString feelsLike() const { return m_feelsLike; }
    QString updateTime() const { return m_updateTime; }
    QVariantList daily() const { return m_daily; }
    QString airAqi() const { return m_airAqi; }
    QString airCategory() const { return m_airCategory; }
    QVariantList indices() const { return m_indices; }

signals:
    void cityIdChanged();
    void cityNameChanged();
    void refreshIntervalChanged();
    void cityTabsChanged();
    void activeCityIndexChanged();
    void autoModeChanged();
    void locatingChanged();
    void activeCityNameChanged();
    void loadingChanged();
    void errorChanged();
    void errorStringChanged();
    void nowChanged();
    void dailyChanged();
    void airChanged();
    void indicesChanged();

private:
    // 发起一个和风 v7 请求（location/lang/type 由本函数拼装），完成时回调 handler
    void fetch(const QString &path, const QString &type,
               const std::function<void(const QJsonObject &)> &handler);
    // 按指定 LocationID 拉取四类天气数据
    void fetchAll(const QString &locationId);
    void finishOne();
    void setError(const QString &message);
    void clearError();

    // IP 自动定位（级联：geoip.ubuntu.com -> myip.ipip.net）
    void startIpLocation();
    void requestUbuntuLookup();
    void requestIpipLocation();
    void finishIpLocation(const QString &city, const QString &province, bool englishName);
    void finishIpLookupFailed();
    // 城市/区县名 -> CSV LocationID 匹配（中文或英文名）
    bool matchCity(const QString &name, bool englishName, QString *matchedId, QString *matchedName);
    // LocationID -> 城市中文名（本地城市表查表；未命中回退 addCity 携带的兜底名）
    QString cityNameFromId(const QString &id);

    // 多城市：旧 kcfg 单城市配置 -> 共享 gsettings 的一次性迁移
    void migrateLegacyCity();
    // 从 gsettings（citylist / autolocate）重建页签模型与活动页
    void rebuildCityTabs();
    // autolocate 读取（旧 schema 缺键时按当前内存值容错，初始默认 true）
    bool readAutoLocate() const;
    // citylist 写入（尾随逗号格式与应用侧一致，index 0 = 最近使用的城市）
    void writeCityList(const QStringList &ids);
    // autolocate 写入（旧 schema 缺键时仅更新内存值，本会话仍可切换）
    void writeAutoLocate(bool on);
    // 当前实际生效的 LocationID（自动 = IP 解析结果；手动 = citylist[0]）
    QString currentLocationId() const;

    void parseNow(const QJsonObject &root);
    void parseDaily(const QJsonObject &root);
    void parseAir(const QJsonObject &root);
    void parseIndices(const QJsonObject &root);

    // 本地城市表（懒加载）
    void ensureCityTableLoaded();
    void onGSettingsChanged(const QString &key); // gsettings 变更回流（间隔/城市列表/自动定位）
    void applyRefreshInterval(int minutes);      // 应用新间隔并重建定时器

    QString m_cityId;   // 旧 kcfg 单城市配置（仅一次性迁移用，非数据源）
    QString m_cityName; // 旧 kcfg 城市名（同上）
    int m_refreshInterval = 30;

    // 多城市状态（缓存自共享 gsettings，重建见 rebuildCityTabs）
    bool m_autolocate = true;        // gsettings autolocate（键缺失时为内存默认）
    QStringList m_manualCityIds;     // 手动城市 LocationID（去重解析，index 0 = 最近使用）
    QVariantList m_cityTabs;         // 城市页签模型（页 0 = 自动定位）
    int m_activeCityIndex = 0;       // 当前活动页
    bool m_legacyMigrated = false;   // 旧 kcfg 迁移已尝试（一次性，防重复导入）
    QHash<QString, QString> m_extraCityNames; // addCity 携带、城市表未命中的城市名兜底

    // IP 自动定位结果（会话内缓存，不写入 kcfg；换网/移动后重启自动更新）
    bool m_locating = false;
    bool m_ipResolved = false;
    QString m_ipCityId;      // 解析出的和风 LocationID
    QString m_ipCityName;   // 解析出的城市名（中文，供展示）
    QString m_ipProvince;   // 解析出的省份（仅展示用）

    // 当前实际用于请求的 LocationID（配置城市或 IP 解析结果）
    QString m_requestLocationId;

    bool m_loading = false;
    int m_activeReplies = 0;
    bool m_hasError = false;
    QString m_errorString;

    // 和风凭据（环境变量读取，不硬编码进代码）
    QString m_apiKey;

    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_timer = nullptr;
    QGSettings *m_gsettings = nullptr; // 刷新间隔/城市列表/自动定位的单一数据源（应用/小部件共用）

    QString m_nowTemp;
    QString m_nowIcon;
    QString m_nowText;
    QString m_windDir;
    QString m_windScale;
    QString m_humidity;
    QString m_feelsLike;
    QString m_updateTime;
    QVariantList m_daily;
    QString m_airAqi;
    QString m_airCategory;
    QVariantList m_indices;

    bool m_cityTableLoaded = false;
    struct CityRecord {
        QString id;
        QString name;
        QString nameEn;
        QString province;
        QString adminDistrict;
    };
    QList<CityRecord> m_cities;
};

#endif // CHINAWEATHER_PLASMOID_WEATHERCLIENT_H
