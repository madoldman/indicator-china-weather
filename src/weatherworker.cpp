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

#include "weatherworker.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QFile>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QVariant>
#include <chrono>
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
#include <QNetworkInformation>
#else
#include <QNetworkConfigurationManager>
#endif

namespace {

// 和风天气 v7 响应的 code 字段为字符串，"200" 表示成功
bool isV7Success(const QJsonObject &root)
{
    return root.value("code").toString() == QStringLiteral("200");
}

} // namespace

WeatherWorker::WeatherWorker(QObject *parent) :
    QObject(parent)
{
    m_networkManager = new QNetworkAccessManager(this);

    // 和风天气凭据从环境变量读取（QWEATHER_API_KEY / QWEATHER_CREDENTIAL_ID，见 data.h）
    m_apiKey = QWeather::apiKey();
    m_credentialId = QWeather::credentialId();

    connect(this, &WeatherWorker::requestTestNetwork, this, &WeatherWorker::onResponseTestNetwork);
    connect(this, &WeatherWorker::requestGetTheWeatherData, this, &WeatherWorker::onWeatherDataRequest);

    initGsetting();
}

WeatherWorker::~WeatherWorker()
{
    m_networkManager->deleteLater();
}

void WeatherWorker::onResponseTestNetwork()
{
    // Qt6 移除了 QNetworkConfigurationManager，改用 QNetworkInformation 判断联网状态
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
    bool online = !QNetworkInformation::loadDefaultBackend() // 后端不可用时退回 DNS 探测
                  || QNetworkInformation::instance()->reachability()
                         >= QNetworkInformation::Reachability::Site;
#else
    QNetworkConfigurationManager mgr;
    bool online = mgr.isOnline();
#endif
    if (online) {//判断网络是否有连接，不一定能上网，如果连接了，则开始检查互联网是否可以ping通
        QHostInfo::lookupHost("www.baidu.com", this, SLOT(networkLookedUp(QHostInfo)));
    }
    else {
        emit nofityNetworkStatus("Fail");//物理网线未连接
    }
}

void WeatherWorker::networkLookedUp(const QHostInfo &host)
{
    if(host.error() != QHostInfo::NoError) {
        //qDebug() << "test network failed, errorCode:" << host.error();
        emit this->nofityNetworkStatus(host.errorString());
    }
    else {
        //qDebug() << "test network success, the server's ip:" << host.addresses().first().toString();
        emit this->nofityNetworkStatus("OK");
    }
}

//校验和风天气凭据；缺失时告警并返回 false，调用方应跳过全部和风请求
bool WeatherWorker::ensureApiKeyAvailable()
{
    if (m_apiKey.isEmpty()) {
        qWarning() << "未设置 QWEATHER_API_KEY 环境变量，无法请求和风天气数据";
        return false;
    }
    return true;
}

//构造带认证头的请求；勿手动设置 Accept-Encoding，Qt 需自行管理该头才会透明解压 gzip 响应
QNetworkRequest WeatherWorker::makeApiRequest(const QUrl &url)
{
    QNetworkRequest request;
    request.setUrl(url);
    request.setRawHeader("X-QW-Api-Key", m_apiKey.toUtf8());
    request.setTransferTimeout(std::chrono::milliseconds(15000));
    return request;
}

//构造 v7 请求 URL；中文/特殊字符经 QUrlQuery 自动百分号编码，手拼字符串会 400
QUrl WeatherWorker::buildApiUrl(const QString &host, const QString &path, const QString &location, const QString &type)
{
    QUrl url(host + path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("location"), location);
    query.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh"));//保证中文字段
    if (!type.isEmpty()) {
        query.addQueryItem(QStringLiteral("type"), type);
    }
    url.setQuery(query);
    return url;
}

//发起请求并在完成时回调解析；httpStatus 为 HTTP 状态码（网络层错误为 0）
void WeatherWorker::fetchApi(const QUrl &url, std::function<void(const QJsonObject &, int)> handler)
{
    QNetworkReply *reply = m_networkManager->get(makeApiRequest(url));
    connect(reply, &QNetworkReply::finished, this, [=]() {
        int httpStatus = 0;
        const QJsonObject root = readReplyJson(reply, &httpStatus);
        handler(root, httpStatus);
        finishDetailReply();
    });
}

//读取并解析回复 JSON，失败返回空对象（Qt 对 gzip 响应已透明解压）
QJsonObject WeatherWorker::readReplyJson(QNetworkReply *reply, int *httpStatus)
{
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus) {
        *httpStatus = statusCode;
    }
    qDebug() << "Reply value of getting weather data by URL is: " << statusCode;

    QJsonObject root;
    const QByteArray ba = reply->readAll();
    reply->close();
    reply->deleteLater();

    QJsonParseError err;
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(ba, &err);
    if (err.error != QJsonParseError::NoError) {// Json type error
        qDebug() << "Json type error";
        return root;
    }
    if (jsonDocument.isNull() || jsonDocument.isEmpty()) {
        qDebug() << "Json null or empty!";
        return root;
    }
    root = jsonDocument.object();
    if (root.isEmpty() || root.size() == 0) {
        qDebug() << "Json object null or empty!";
        return QJsonObject();
    }
    return root;
}

void WeatherWorker::finishDetailReply()
{
    if (m_activeDetailReplies > 0) {
        m_activeDetailReplies -= 1;
    }
}

//利用连接请求网络数据：直连和风天气 API v7
//单城市综合数据拆分为 now / 7d / air / indices 四个请求
void WeatherWorker::onWeatherDataRequest(const QString &cityId)
{
    if (cityId.isEmpty()) {
        return;
    }
    if (!ensureApiKeyAvailable()) {
        return;
    }
    if (m_activeDetailReplies > 0) {//上一批请求仍在途，丢弃本次请求
        return;
    }
    m_activeDetailReplies = 4;

    ensureCityTableLoaded();
    m_observeCache = ObserveWeather();
    m_observeCacheValid = false;

    fetchApi(buildApiUrl(QWeather::DEVAPI_HOST, QStringLiteral("/v7/weather/now"), cityId),
             [=](const QJsonObject &root, int httpStatus) { parseNowReply(root, cityId, httpStatus); });
    fetchApi(buildApiUrl(QWeather::DEVAPI_HOST, QStringLiteral("/v7/weather/7d"), cityId),
             [=](const QJsonObject &root, int) { parseForecastReply(root); });
    fetchApi(buildApiUrl(QWeather::DEVAPI_HOST, QStringLiteral("/v7/air/now"), cityId),
             [=](const QJsonObject &root, int) { parseAirReply(root); });
    //生活指数：1运动 2洗车 3穿衣 5紫外线 9感冒 10空气污染扩散
    fetchApi(buildApiUrl(QWeather::DEVAPI_HOST, QStringLiteral("/v7/indices/1d"), cityId, QStringLiteral("1,2,3,5,9,10")),
             [=](const QJsonObject &root, int) { parseIndicesReply(root); });
}

//处理实况天气 v7 响应（/v7/weather/now）
void WeatherWorker::parseNowReply(const QJsonObject &root, const QString &cityId, int httpStatus)
{
    ObserveWeather m_observeweather;

    if (root.isEmpty() || !isV7Success(root)) {
        if (root.isEmpty()) {//网络/JSON 层错误；HTTP 200 但 JSON 异常时与旧实现一致传 0
            emit responseFailure(httpStatus == 200 ? 0 : httpStatus);
        }
        else {//和风返回的错误码，如 401/402/404
            emit responseFailure(root.value("code").toString().toInt());
        }
        return;
    }

    m_observeweather.id = cityId;
    m_observeweather.city = cityNameFromId(cityId);//v7 响应不含城市名，由本地城市表提供

    const QJsonObject now = root.value("now").toObject();
    if (now.isEmpty()) {
        //数据异常时下发占位数据，与旧实现一致
        m_observeweather.tmp = "-";
        m_observeweather.wind_sc = "-";
        m_observeweather.cond_txt = "-";
        m_observeweather.wind_dir = "-";
        m_observeweather.fl = "-";
        m_observeweather.cloud = "-";
        m_observeweather.serveTime = "-";
    }
    else {
        m_observeweather.tmp = now.value("temp").toString();
        m_observeweather.wind_sc = now.value("windScale").toString();
        m_observeweather.cond_txt = now.value("text").toString();
        m_observeweather.vis = now.value("vis").toString();
        m_observeweather.hum = now.value("humidity").toString();
        m_observeweather.cond_code = now.value("icon").toString();
        m_observeweather.wind_deg = now.value("wind360").toString();
        m_observeweather.pcpn = now.value("precip").toString();
        m_observeweather.pres = now.value("pressure").toString();
        m_observeweather.wind_spd = now.value("windSpeed").toString();
        m_observeweather.wind_dir = now.value("windDir").toString();
        m_observeweather.fl = now.value("feelsLike").toString();
        m_observeweather.cloud = now.value("cloud").toString();

        const QDateTime updated = QDateTime::fromString(root.value("updateTime").toString(), Qt::ISODate);
        m_observeweather.serveTime = updated.isValid()
                ? updated.toString("yyyy-MM-dd HH:mm")
                : root.value("updateTime").toString();
    }
    m_observeweather.air = m_airCategory;//最近一次 air/now 的空气质量类别

    //写入配置文件供其他组件调用（保持旧字段顺序与格式）
    QString weatherNow;
    weatherNow.append(m_observeweather.serveTime + ",");//时间
    weatherNow.append(m_observeweather.id + ",");//省市编码
    weatherNow.append(m_observeweather.city + ",");//城市名称
    weatherNow.append(m_observeweather.cond_txt + ",");//天气情况
    weatherNow.append(m_observeweather.hum + "%,");//湿度
    weatherNow.append(m_observeweather.tmp + "℃,");//温度
    weatherNow.append(m_observeweather.wind_dir + ",");//风向
    weatherNow.append(m_observeweather.wind_sc + "级,");//风力
    weatherNow.append(cityProvinceFromId(cityId) + ",");//省份
    weatherNow.append(m_observeweather.cond_code);
    setCityWeatherNow(weatherNow);

    m_observeCache = m_observeweather;
    m_observeCacheValid = !now.isEmpty();

    emit this->requestSetObserveWeather(m_observeweather);//用于设置主窗口
}

//处理预报天气 v7 响应（/v7/weather/7d）
void WeatherWorker::parseForecastReply(const QJsonObject &root)
{
    if (!isV7Success(root)) {
        //与旧实现一致：获取失败时下发一条全“-”的占位预报
        ForecastWeather m_forecastweather;
        m_forecastweather.uv_index = "-";
        m_forecastweather.wind_spd = "-";
        m_forecastweather.sr = "-";
        m_forecastweather.wind_sc = "-";
        m_forecastweather.ms = "-";
        m_forecastweather.cond_txt_d = "-";
        m_forecastweather.vis = "-";
        m_forecastweather.ss = "-";
        m_forecastweather.hum = "-";
        m_forecastweather.cond_txt_n = "-";
        m_forecastweather.pop = "-";//v7 预报不再提供降水概率，保留占位以兼容结构
        m_forecastweather.wind_deg = "-";
        m_forecastweather.pcpn = "-";
        m_forecastweather.wind_dir = "-";
        m_forecastweather.cond_code_d = "-";
        m_forecastweather.mr = "-";
        m_forecastweather.date = "-";
        m_forecastweather.tmp_max = "-";
        m_forecastweather.cond_code_n = "-";
        m_forecastweather.pres = "-";
        m_forecastweather.tmp_min = "-";
        m_forecastweather.dateTime = "-";
        emit this->requestSetForecastWeather(m_forecastweather);
        return;
    }

    const QJsonArray daily = root.value("daily").toArray();
    if (daily.isEmpty()) {
        return;
    }

    //所有天次共用基准日期（当天），界面按该日期推算各天显示
    const QString baseDate = daily.at(0).toObject().value("fxDate").toString();

    for (const QJsonValue &value : daily) {
        const QJsonObject d = value.toObject();
        ForecastWeather m_forecastweather;
        m_forecastweather.cond_code_d = d.value("iconDay").toString();
        m_forecastweather.cond_code_n = d.value("iconNight").toString();
        m_forecastweather.cond_txt_d = d.value("textDay").toString();
        m_forecastweather.cond_txt_n = d.value("textNight").toString();
        m_forecastweather.forcast_date = d.value("fxDate").toString();
        m_forecastweather.hum = d.value("humidity").toString();
        m_forecastweather.mr = d.value("moonrise").toString();
        m_forecastweather.ms = d.value("moonset").toString();
        m_forecastweather.pcpn = d.value("precip").toString();
        m_forecastweather.pres = d.value("pressure").toString();
        m_forecastweather.sr = d.value("sunrise").toString();
        m_forecastweather.ss = d.value("sunset").toString();
        m_forecastweather.tmp_max = d.value("tempMax").toString();
        m_forecastweather.tmp_min = d.value("tempMin").toString();
        m_forecastweather.uv_index = d.value("uvIndex").toString();
        m_forecastweather.vis = d.value("vis").toString();
        m_forecastweather.wind_deg = d.value("wind360Day").toString();
        m_forecastweather.wind_dir = d.value("windDirDay").toString();
        m_forecastweather.wind_sc = d.value("windScaleDay").toString();
        m_forecastweather.wind_spd = d.value("windSpeedDay").toString();
        m_forecastweather.date = d.value("fxDate").toString();
        m_forecastweather.dateTime = baseDate;

        emit this->requestSetForecastWeather(m_forecastweather);
    }
}

//处理实时空气质量 v7 响应（/v7/air/now）
void WeatherWorker::parseAirReply(const QJsonObject &root)
{
    if (!isV7Success(root)) {
        return;
    }
    const QJsonObject now = root.value("now").toObject();
    if (now.isEmpty()) {
        return;
    }
    m_airAqi = now.value("aqi").toString();
    m_airCategory = now.value("category").toString();

    //air 数据晚于 now 到达时回填缓存并重新下发，保证 ObserveWeather.air 可用
    if (m_observeCacheValid) {
        m_observeCache.air = m_airCategory;
        emit this->requestSetObserveWeather(m_observeCache);
    }
}

//处理生活指数 v7 响应（/v7/indices/1d），填充与旧 s6 语义一致的生活指数
void WeatherWorker::parseIndicesReply(const QJsonObject &root)
{
    if (!isV7Success(root)) {
        return;
    }
    LifeStyle m_lifestyle;
    const QJsonArray daily = root.value("daily").toArray();
    for (const QJsonValue &value : daily) {
        const QJsonObject item = value.toObject();
        const QString type = item.value("type").toString();
        const QString brf = item.value("category").toString();
        const QString txt = item.value("text").toString();
        if (type == "3") {//穿衣指数
            m_lifestyle.drsg_brf = brf;
            m_lifestyle.drsg_txt = txt;
        }
        else if (type == "9") {//感冒指数
            m_lifestyle.flu_brf = brf;
            m_lifestyle.flu_txt = txt;
        }
        else if (type == "5") {//紫外线指数
            m_lifestyle.uv_brf = brf;
            m_lifestyle.uv_txt = txt;
        }
        else if (type == "2") {//洗车指数
            m_lifestyle.cw_brf = brf;
            m_lifestyle.cw_txt = txt;
        }
        else if (type == "10") {//空气污染扩散条件指数
            m_lifestyle.air_brf = brf;
            m_lifestyle.air_txt = txt;
        }
        else if (type == "1") {//运动指数
            m_lifestyle.sport_brf = brf;
            m_lifestyle.sport_txt = txt;
        }
    }
    emit this->requestSetLifeStyle(m_lifestyle);
}

//获取收藏城市天气数据：逐城请求 /v7/weather/now，串行 + 200ms 间隔
void WeatherWorker::onCityWeatherDataRequest()
{
    if (!ensureApiKeyAvailable()) {
        return;
    }

    const QStringList cityList = getCityList().split(",", Qt::SkipEmptyParts);
    if (cityList.isEmpty()) {
        return;
    }

    ++m_cityBatchId;//使仍在途的旧批次回调失效
    m_pendingCityIds = cityList;
    m_cityWeatherData.clear();
    ensureCityTableLoaded();
    requestNextPendingCity(m_cityBatchId);
}

//逐城请求收藏列表简报；全部完成后按旧格式整体下发
void WeatherWorker::requestNextPendingCity(const qint64 batchId)
{
    if (batchId != m_cityBatchId) {//已被新批次取代
        return;
    }
    if (m_pendingCityIds.isEmpty()) {
        if (!m_cityWeatherData.isEmpty()) {
            emit requestSetCityWeather(m_cityWeatherData);
        }
        return;
    }

    const QString cityId = m_pendingCityIds.takeFirst();
    QNetworkReply *reply = m_networkManager->get(makeApiRequest(
            buildApiUrl(QWeather::DEVAPI_HOST, QStringLiteral("/v7/weather/now"), cityId)));
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (batchId != m_cityBatchId) {//已被新批次取代，丢弃过期数据
            reply->close();
            reply->deleteLater();
            return;
        }
        m_cityWeatherData.append(buildCitySimpleData(cityId, readReplyJson(reply)));
        //串行且加 200ms 间隔，避免一次刷出几十个请求
        QTimer::singleShot(200, this, [=]() { requestNextPendingCity(batchId); });
    });
}

//构造与旧代理 simple_s6 兼容的逐城简报串：
//“tmp=26,cond_txt=阴,cond_code=104,id=101250101,location=长沙;”
//城市名由本地城市表提供（v7 响应不含城市名）；失败时填占位值保证计数与布局不变
QString WeatherWorker::buildCitySimpleData(const QString &cityId, const QJsonObject &root)
{
    if (isV7Success(root)) {
        const QJsonObject now = root.value("now").toObject();
        if (!now.isEmpty()) {
            return QString("tmp=%1,cond_txt=%2,cond_code=%3,id=%4,location=%5;")
                    .arg(now.value("temp").toString(),
                         now.value("text").toString(),
                         now.value("icon").toString(),
                         cityId,
                         cityNameFromId(cityId));
        }
    }
    return QString("tmp=-,cond_txt=-,cond_code=-,id=%1,location=-;").arg(cityId);
}

//v7 响应不返回城市名称，从内置 china-city-list.csv 建立 LocationID 查询表
void WeatherWorker::ensureCityTableLoaded()
{
    if (m_cityTableLoaded) {
        return;
    }
    m_cityTableLoaded = true;

    QFile file(":/data/data/china-city-list.csv");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "open china-city-list.csv failed";
        return;
    }
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).remove("\r").remove("\n");
        const QStringList resultList = line.split(",");
        if (resultList.length() < 8) {
            continue;
        }
        const QString id = resultList.at(0);
        if (!id.startsWith("CN")) {
            continue;
        }
        const QString locationId = id.mid(2);//remove "CN"
        m_cityNameTable.insert(locationId, resultList.at(2));
        m_cityProvinceTable.insert(locationId, resultList.at(7));
    }
    file.close();
}

QString WeatherWorker::cityNameFromId(const QString &cityId) const
{
    return m_cityNameTable.value(cityId);
}

QString WeatherWorker::cityProvinceFromId(const QString &cityId) const
{
    return m_cityProvinceTable.value(cityId);
}

void WeatherWorker::initGsetting()
{
    if(QGSettings::isSchemaInstalled(CHINAWEATHERDATA))
        m_pWeatherData = new QGSettings(CHINAWEATHERDATA);
    return;
}

QString WeatherWorker::getCityList()
{
    QString str="";
    if (m_pWeatherData != nullptr) {
        QStringList keyList = m_pWeatherData->keys();
        if (keyList.contains("citylist"))
        {
            str = m_pWeatherData->get("citylist").toString();
        }
    }
    return str;
}

void WeatherWorker::setCityWeatherNow(QString str)
{
    if (m_pWeatherData != nullptr) {
        m_pWeatherData->set("weather", str);
    }
}
