/*
 * Copyright (C) 2020, KylinSoft Co., Ltd.
 *
 * Authors:
 *  Kobe Lee    lixiang@kylinos.cn/kobe24_lixiang@126.com
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

#ifndef WEATHERWORKER_H
#define WEATHERWORKER_H

#define CHINAWEATHERDATA "org.china-weather-data.settings"

#include <QObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QHostInfo>
#include <QStandardPaths>
#include <QTimer>
#include <QHash>
#include <functional>

#include "data.h"

#include <QGSettings>

class WeatherWorker : public QObject
{
    Q_OBJECT

public:
    explicit WeatherWorker(QObject *parent = 0);
    ~WeatherWorker();

public slots:
    void onWeatherDataRequest(const QString &cityId);
    void onCityWeatherDataRequest();

    void networkLookedUp(const QHostInfo &host);
    void onResponseTestNetwork();

private:
    QNetworkAccessManager *m_networkManager = nullptr;

    // 和风天气凭据（环境变量注入，见 data.h 中 QWeather 命名空间）
    QString m_apiKey;
    QString m_credentialId;//仅作标识保存，API Key 认证不使用

    // 和风 v7 请求构造与解析
    bool ensureApiKeyAvailable();
    QNetworkRequest makeApiRequest(const QUrl &url);
    QUrl buildApiUrl(const QString &host, const QString &path, const QString &location, const QString &type = QString());
    void fetchApi(const QUrl &url, std::function<void(const QJsonObject &, int)> handler);
    QJsonObject readReplyJson(QNetworkReply *reply, int *httpStatus = nullptr);
    void finishDetailReply();

    // 单城市综合数据：now / 7d / air / indices 四个请求
    int m_activeDetailReplies = 0;//在途请求数，全部结束后才接受下一次请求
    void parseNowReply(const QJsonObject &root, const QString &cityId, int httpStatus);
    void parseForecastReply(const QJsonObject &root);
    void parseAirReply(const QJsonObject &root);
    void parseIndicesReply(const QJsonObject &root);

    // air 数据可能晚于 now 到达，缓存实况便于回填后重发
    ObserveWeather m_observeCache;
    bool m_observeCacheValid = false;
    QString m_airAqi;
    QString m_airCategory;

    // 收藏城市列表批量简报：逐城请求 /v7/weather/now（串行 + 小间隔）
    qint64 m_cityBatchId = 0;//批次号，新批次使旧批次回调失效
    QStringList m_pendingCityIds;
    QString m_cityWeatherData;
    void requestNextPendingCity(qint64 batchId);
    QString buildCitySimpleData(const QString &cityId, const QJsonObject &root);

    // v7 响应不返回城市名称，从内置 china-city-list.csv 提供 LocationID 查询
    void ensureCityTableLoaded();
    QString cityNameFromId(const QString &cityId) const;
    QString cityProvinceFromId(const QString &cityId) const;
    QHash<QString, QString> m_cityNameTable;
    QHash<QString, QString> m_cityProvinceTable;
    bool m_cityTableLoaded = false;

    // getstting初始化、值获取、 设置getsetting值
    void initGsetting();
    QString getCityList();
    void setCityWeatherNow(QString str);
    QGSettings  *m_pWeatherData= nullptr;

signals:
    void setLocationData();
    void requestSetObserveWeather(ObserveWeather observeweather);
    void requestSetForecastWeather(ForecastWeather forecastweather);
    void requestSetLifeStyle(LifeStyle lifestyle);

    void requestGetTheWeatherData(QString cityId);
    void requestGetCityWeatherData(QString cityIds);
    void requestTestNetwork();
    void nofityNetworkStatus(const QString &status);
    void responseFailure(int code);

    void requestSetCityWeather(QString weather_data);
};

#endif // WEATHERWORKER_H
