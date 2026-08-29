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

#include <QObject>
#include <QString>
#include <QVariantList>

#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QTimer;

/*
 * Plasma 小部件（org.madoldman.chinaweather）的数据后端。
 *
 * 直连和风天气 API v7 的四个请求（now / 7d / air / indices），与托盘应用
 * src/weatherworker.cpp 的端点、认证与字段映射保持一致，但为独立精简实现，
 * 不与托盘应用的 worker/信号体系耦合。
 */
class WeatherClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // 基本配置（cityId 写入即触发刷新；空 = 自动定位模式，非空 = 用户手动配置）
    Q_PROPERTY(QString cityId READ cityId WRITE setCityId NOTIFY cityIdChanged)
    Q_PROPERTY(QString cityName READ cityName WRITE setCityName NOTIFY cityNameChanged)
    Q_PROPERTY(int refreshInterval READ refreshInterval WRITE setRefreshInterval NOTIFY refreshIntervalChanged)

    // 自动定位相关（readonly，供 UI 展示；不回写 kcfg 绑定的属性）
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

    // 自动定位模式（cityId 为空）；IP 定位进行中；实际生效的城市名（配置名或 IP 解析名）
    bool autoMode() const { return m_cityId.isEmpty(); }
    bool locating() const { return m_locating; }
    QString activeCityName() const { return m_cityId.isEmpty() ? m_ipCityName : m_cityName; }

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

    void parseNow(const QJsonObject &root);
    void parseDaily(const QJsonObject &root);
    void parseAir(const QJsonObject &root);
    void parseIndices(const QJsonObject &root);

    // 本地城市表（懒加载）
    void ensureCityTableLoaded();

    QString m_cityId;
    QString m_cityName;
    int m_refreshInterval = 30;

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
