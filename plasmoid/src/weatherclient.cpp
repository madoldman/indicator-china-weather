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

#include "weatherclient.h"

#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QGSettings>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <chrono>
#include <functional>

namespace {

// 与托盘应用 data.h 保持一致的和风天气 v7 数据 API 主机
constexpr char kDevApiHost[] = "https://devapi.qweather.com";

// Plasmoid 包内城市表（由打包安装到系统小部件目录）
constexpr char kPackagedCityCsv[] =
    "/usr/share/plasma/plasmoids/org.madoldman.chinaweather/contents/data/china-city-list.csv";

// 和风 v7 响应的 code 字段为字符串，"200" 表示成功
bool isV7Success(const QJsonObject &root)
{
    return root.value(QStringLiteral("code")).toString() == QStringLiteral("200");
}

} // namespace

WeatherClient::WeatherClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    // 和风天气凭据从环境变量读取（缺失时 refresh() 会置错误状态，不发请求）。
    // 凭据 ID（QWEATHER_CREDENTIAL_ID）仅用于 JWT 认证，API Key 方式不使用，这里不读取。
    m_apiKey = qEnvironmentVariable("QWEATHER_API_KEY").trimmed();

    // 刷新间隔/城市列表/自动定位的单一数据源 = gsettings org.china-weather-data.settings，
    // 与应用菜单共用；监听变更实现「任一侧修改，两侧同时生效」
    if (QGSettings::isSchemaInstalled("org.china-weather-data.settings")) {
        m_gsettings = new QGSettings("org.china-weather-data.settings", QByteArray(), this);
        const int stored = m_gsettings->get(QStringLiteral("refresh-interval")).toInt();
        if (stored > 0) {
            m_refreshInterval = stored;
        }
        connect(m_gsettings, &QGSettings::changed, this, &WeatherClient::onGSettingsChanged);
    }
    // 城市页签与活动页（citylist / autolocate；旧 schema 缺键时按默认值容错）
    rebuildCityTabs();
    setRefreshInterval(m_refreshInterval);
}

void WeatherClient::setCityId(const QString &cityId)
{
    if (cityId == m_cityId) {
        return;
    }
    m_cityId = cityId;
    emit cityIdChanged();
    // kcfg 单城市配置已不是数据源：仅在首次收到旧配置值时做一次性迁移
    migrateLegacyCity();
}

void WeatherClient::setCityName(const QString &cityName)
{
    if (cityName == m_cityName) {
        return;
    }
    // 仅为兼容旧 kcfg 绑定保留，不再参与展示与请求逻辑
    m_cityName = cityName;
    emit cityNameChanged();
}

//--------- 多城市管理（共享 gsettings：citylist / autolocate） ---------
// 城市列表格式与应用侧一致：逗号分隔的 LocationID，index 0 = 最近使用的城市，
// 允许尾随逗号（如 "101010100,"）；空串 / "," = 无手动城市

// 旧 kcfg 单城市配置 -> 共享 gsettings 的一次性迁移：仅当 citylist 仍是
// 出厂默认（北京）且 kcfg 配置了非空城市时导入，避免覆盖应用侧已保存的城市
void WeatherClient::migrateLegacyCity()
{
    if (m_legacyMigrated) {
        return; // 一次性：本实例只尝试导入一次
    }
    m_legacyMigrated = true;
    if (!m_gsettings || m_cityId.isEmpty()
        || !m_gsettings->keys().contains(QStringLiteral("citylist"))) {
        return;
    }
    QString stored = m_gsettings->get(QStringLiteral("citylist")).toString().trimmed();
    while (stored.endsWith(QLatin1Char(','))) {
        stored.chop(1);
    }
    if (stored != QStringLiteral("101010100")) {
        return; // 应用侧已自定义过城市列表，不覆盖
    }
    m_gsettings->set(QStringLiteral("citylist"), m_cityId + QStringLiteral(","));
    if (m_gsettings->keys().contains(QStringLiteral("autolocate"))) {
        m_gsettings->set(QStringLiteral("autolocate"), false);
    }
    // 写入会经 onGSettingsChanged 回流重建页签；这里再显式重建+刷新一次，不依赖信号时序
    rebuildCityTabs();
    refresh();
}

// 从 gsettings 重建城市页签模型与活动页（页 0 = 自动定位，其余 = 手动城市）
void WeatherClient::rebuildCityTabs()
{
    const QString previousName = activeCityName();
    const bool previousAuto = m_autolocate;

    m_autolocate = readAutoLocate();

    // citylist -> 手动城市 ID（去空段，按首次出现去重，保序）
    QStringList ids;
    if (m_gsettings && m_gsettings->keys().contains(QStringLiteral("citylist"))) {
        const QStringList parts = m_gsettings->get(QStringLiteral("citylist"))
                                      .toString()
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString id = part.trimmed();
            if (!id.isEmpty() && !ids.contains(id)) {
                ids.append(id);
            }
        }
    }
    m_manualCityIds = ids;

    // 页签模型：页 0 的名称在 IP 定位成功后替换为解析出的城市名；
    // 与 App 轮播自动页一致，始终带「·自动」标记（未定位到时显示「自动定位」）
    QVariantList tabs;
    QVariantMap autoTab;
    autoTab.insert(QStringLiteral("id"), QString());
    autoTab.insert(QStringLiteral("name"),
                   m_ipResolved && !m_ipCityName.isEmpty()
                       ? m_ipCityName + QStringLiteral("·自动")
                       : QStringLiteral("自动定位"));
    autoTab.insert(QStringLiteral("isAuto"), true);
    tabs.append(autoTab);
    for (const QString &id : std::as_const(ids)) {
        const QString name = cityNameFromId(id);
        QVariantMap tab;
        tab.insert(QStringLiteral("id"), id);
        tab.insert(QStringLiteral("name"), name.isEmpty() ? id : name);
        tab.insert(QStringLiteral("isAuto"), false);
        tabs.append(tab);
    }
    if (tabs != m_cityTabs) {
        m_cityTabs = tabs;
        emit cityTabsChanged();
    }

    // 活动页：自动定位（或无手动城市）-> 0；手动 -> citylist[0] 对应页
    const int newIndex = (!m_autolocate && !ids.isEmpty()) ? 1 : 0;
    if (newIndex != m_activeCityIndex) {
        m_activeCityIndex = newIndex;
        emit activeCityIndexChanged();
    }
    if (m_autolocate != previousAuto) {
        emit autoModeChanged();
    }
    if (activeCityName() != previousName) {
        emit activeCityNameChanged();
    }
}

// 读 autolocate；旧 schema 未安装该键时按当前内存值容错（初始默认 true）
bool WeatherClient::readAutoLocate() const
{
    if (m_gsettings && m_gsettings->keys().contains(QStringLiteral("autolocate"))) {
        return m_gsettings->get(QStringLiteral("autolocate")).toBool();
    }
    return m_autolocate;
}

// 写 citylist（尾随逗号与应用侧格式一致；键缺失的旧 schema 下静默忽略）
void WeatherClient::writeCityList(const QStringList &ids)
{
    if (!m_gsettings || !m_gsettings->keys().contains(QStringLiteral("citylist"))) {
        return;
    }
    m_gsettings->set(QStringLiteral("citylist"),
                     ids.join(QLatin1Char(',')) + QStringLiteral(","));
}

// 写 autolocate（键缺失的旧 schema 下仅更新内存值，本会话仍可切换）
void WeatherClient::writeAutoLocate(bool on)
{
    m_autolocate = on;
    if (!m_gsettings || !m_gsettings->keys().contains(QStringLiteral("autolocate"))) {
        return;
    }
    m_gsettings->set(QStringLiteral("autolocate"), on);
}

void WeatherClient::setActiveCityIndex(int index)
{
    if (index <= 0) {
        setAutoLocate(true); // 页 0 = 自动定位：恢复自动定位为当前城市
        return;
    }
    if (index - 1 >= m_manualCityIds.size()) {
        return; // 越界：无对应手动城市
    }
    // 浏览手动城市页签：仅切换当前显示（fetchAll 拉取该城），不写共享 gsettings，
    // 不改变自动定位状态——自动定位仍是配置的当前城市，浏览不影响 App 的当前城市
    m_activeCityIndex = index;
    emit activeCityIndexChanged();
    refresh();
}

void WeatherClient::addCity(const QString &id, const QString &name)
{
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty() || m_manualCityIds.contains(trimmed)) {
        return; // 空值或已存在（去重）
    }
    // 与应用侧收藏容量一致：最多 8 个手动城市，超出时丢弃最旧的
    QStringList ids = m_manualCityIds;
    while (ids.size() >= 8) {
        ids.removeLast();
    }
    ids.append(trimmed);
    if (!name.trimmed().isEmpty()) {
        m_extraCityNames.insert(trimmed, name.trimmed()); // 城市表未命中时的展示兜底
    }
    writeCityList(ids);
    rebuildCityTabs();
}

void WeatherClient::removeCity(int tabIndex)
{
    // 页 0 为自动定位页不可删；越界忽略
    if (tabIndex <= 0 || tabIndex > m_manualCityIds.size()) {
        return;
    }
    const bool removedActive = (tabIndex == m_activeCityIndex);
    QStringList ids = m_manualCityIds;
    ids.removeAt(tabIndex - 1);
    writeCityList(ids);
    // 删除的恰为当前生效城市：仍有剩余则继续用新的 citylist[0]，否则回退自动定位
    if (removedActive) {
        writeAutoLocate(ids.isEmpty());
    }
    rebuildCityTabs();
    if (removedActive) {
        refresh();
    }
}

void WeatherClient::setAutoLocate(bool on)
{
    writeAutoLocate(on);
    rebuildCityTabs();
    refresh();
}

// 当前实际生效的 LocationID（自动 = IP 解析结果；手动 = citylist[0]）
QString WeatherClient::currentLocationId() const
{
    if (m_autolocate) {
        return m_ipResolved ? m_ipCityId : QString();
    }
    return m_manualCityIds.isEmpty() ? QString() : m_manualCityIds.first();
}

// 实际生效的城市名（随活动页签：自动页 = IP 解析名；手动页 = 该城城市名）
QString WeatherClient::activeCityName()
{
    if (m_activeCityIndex <= 0 || m_manualCityIds.isEmpty()) {
        return m_ipCityName;
    }
    const QString id = m_manualCityIds.value(m_activeCityIndex - 1);
    if (id.isEmpty()) {
        return QString();
    }
    const QString name = cityNameFromId(id);
    return name.isEmpty() ? id : name;
}

// LocationID -> 城市中文名：查本地城市表；未命中回退 addCity 携带的兜底名
QString WeatherClient::cityNameFromId(const QString &id)
{
    ensureCityTableLoaded();
    for (const CityRecord &record : std::as_const(m_cities)) {
        if (record.id == id) {
            return record.name;
        }
    }
    return m_extraCityNames.value(id);
}

void WeatherClient::setRefreshInterval(int minutes)
{
    const int clamped = qBound(5, minutes, 360);
    // 写入 gsettings（单一数据源）：应用菜单与小部件任一侧修改都会经
    // QGSettings::changed 回流到另一侧；随后立即应用一次，不依赖信号时序
    if (m_gsettings) {
        m_gsettings->set(QStringLiteral("refresh-interval"), clamped);
    }
    applyRefreshInterval(clamped);
}

void WeatherClient::applyRefreshInterval(int minutes)
{
    if (minutes <= 0 || minutes == m_refreshInterval) {
        return;
    }
    m_refreshInterval = minutes;
    emit refreshIntervalChanged();

    // 按新间隔重建定时器
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }
    m_timer = new QTimer(this);
    m_timer->setInterval(std::chrono::minutes(m_refreshInterval));
    connect(m_timer, &QTimer::timeout, this, &WeatherClient::refresh);
    m_timer->start();
}

void WeatherClient::onGSettingsChanged(const QString &key)
{
    if (key == QLatin1String("refresh-interval") && m_gsettings) {
        const int stored = m_gsettings->get(QStringLiteral("refresh-interval")).toInt();
        if (stored > 0) {
            applyRefreshInterval(stored);
        }
        return;
    }
    // 城市列表/自动定位变更（应用侧或另一小部件实例修改）：重建页签，
    // 实际生效的城市变化时立即刷新
    if (key == QLatin1String("citylist") || key == QLatin1String("autolocate")) {
        const QString previousLocationId = currentLocationId();
        rebuildCityTabs();
        if (currentLocationId() != previousLocationId) {
            refresh();
        }
    }
}

void WeatherClient::refresh()
{
    if (m_apiKey.isEmpty()) {
        setError(QStringLiteral(
            "未设置 QWEATHER_API_KEY 环境变量，无法请求和风天气数据；配置方式见 README「和风天气凭据配置」章节。"));
        return;
    }
    // 按当前活动页签拉取：页 0 = 自动定位（先 IP 解析再拉取，结果仅缓存本会话），
    // 其余 = 对应手动城市。自动定位仍是配置的当前城市，浏览手动城市不影响 autolocate
    if (m_activeCityIndex <= 0 || m_manualCityIds.isEmpty()) {
        if (m_ipResolved) {
            fetchAll(m_ipCityId);
        } else {
            startIpLocation();
        }
        return;
    }
    fetchAll(m_manualCityIds.value(m_activeCityIndex - 1));
}

void WeatherClient::fetchAll(const QString &locationId)
{
    if (locationId.isEmpty()) {
        return;
    }
    if (m_activeReplies > 0) {
        return; // 上一批请求仍在途，忽略本次
    }

    clearError();
    m_requestLocationId = locationId;
    m_activeReplies = 5;
    m_loading = true;
    emit loadingChanged();

    // 生活指数：type=0 请求全部 16 类（1运动 2洗车 3穿衣 4钓鱼 5紫外线 6旅游 7花粉过敏
    // 8舒适度 9感冒 10空气污染扩散 11空调 12太阳镜 13化妆 14晾晒 15交通 16防晒）
    fetch(QStringLiteral("/v7/weather/now"), QString(),
          [this](const QJsonObject &root) { parseNow(root); });
    fetch(QStringLiteral("/v7/weather/7d"), QString(),
          [this](const QJsonObject &root) { parseDaily(root); });
    // 24 小时逐小时预报（hourly 数组，每项为 1 小时预报）
    fetch(QStringLiteral("/v7/weather/24h"), QString(),
          [this](const QJsonObject &root) { parseHourly(root); });
    fetch(QStringLiteral("/v7/air/now"), QString(),
          [this](const QJsonObject &root) { parseAir(root); });
    fetch(QStringLiteral("/v7/indices/1d"), QStringLiteral("0"),
          [this](const QJsonObject &root) { parseIndices(root); });
}

//--------- IP 自动定位（级联：geoip.ubuntu.com -> myip.ipip.net，均无需凭据） ---------

void WeatherClient::startIpLocation()
{
    if (m_locating) {
        return; // 定位请求已在途
    }
    m_locating = true;
    emit locatingChanged();
    requestUbuntuLookup();
}

// 主源：geoip.ubuntu.com/lookup 返回 XML（<City>Changsha</City> 等英文字段）
void WeatherClient::requestUbuntuLookup()
{
    QNetworkRequest request{QUrl(QStringLiteral("http://geoip.ubuntu.com/lookup"))};
    request.setTransferTimeout(std::chrono::milliseconds(10000));
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray data = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
        reply->deleteLater();

        const QString content = QString::fromUtf8(data);
        static const QRegularExpression cityRe(QStringLiteral("<City>([^<]+)</City>"));
        static const QRegularExpression regionRe(QStringLiteral("<RegionName>([^<]+)</RegionName>"));
        const auto cityMatch = cityRe.match(content);
        const QString city = cityMatch.hasMatch() ? cityMatch.captured(1).trimmed() : QString();
        if (city.isEmpty() || city == QLatin1String("Unknown")) {
            requestIpipLocation();
            return;
        }
        const auto regionMatch = regionRe.match(content);
        finishIpLocation(city, regionMatch.hasMatch() ? regionMatch.captured(1).trimmed() : QString(), true);
    });
}

// 备源：myip.ipip.net 返回纯文本（“当前 IP：x.x.x.x  来自于：中国 湖南 长沙  电信”）
void WeatherClient::requestIpipLocation()
{
    QNetworkRequest request{QUrl(QStringLiteral("http://myip.ipip.net"))};
    request.setTransferTimeout(std::chrono::milliseconds(10000));
    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QString content = reply->error() == QNetworkReply::NoError
                                    ? QString::fromUtf8(reply->readAll()) : QString();
        reply->deleteLater();

        // 解析“来自于：中国 <省> <市> <运营商>”中的省/市字段
        const int from = content.indexOf(QStringLiteral("来自于"));
        if (from < 0) {
            finishIpLookupFailed();
            return;
        }
        const QStringList parts = content.mid(from + 3).split(QRegularExpression(QStringLiteral("\\s+")),
                                                              Qt::SkipEmptyParts);
        // parts 形如 [“：中国”, “湖南”, “长沙”, “电信”]（首个含冒号前缀，末位可能缺运营商）
        if (parts.size() < 3) {
            finishIpLookupFailed();
            return;
        }
        const QString province = parts.at(1);
        const QString city = parts.at(2);
        if (city.isEmpty()) {
            finishIpLookupFailed();
            return;
        }
        finishIpLocation(city, province, false);
    });
}

void WeatherClient::finishIpLocation(const QString &city, const QString &province, bool englishName)
{
    ensureCityTableLoaded();
    QString matchedId;
    QString matchedName;
    if (!matchCity(city, englishName, &matchedId, &matchedName)) {
        m_locating = false;
        emit locatingChanged();
        setError(QStringLiteral("公网 IP 定位到的城市（%1）未在本地城市表中找到，请手动选择城市。").arg(city));
        return;
    }

    m_ipCityId = matchedId;
    m_ipCityName = matchedName;
    m_ipProvince = province;
    m_ipResolved = true;
    m_locating = false;
    emit locatingChanged();
    emit activeCityNameChanged();
    // 自动定位页名称同步为解析出的城市名（页签模型页 0，带「·自动」标记）
    if (!m_cityTabs.isEmpty()) {
        QVariantMap autoTab = m_cityTabs.at(0).toMap();
        autoTab.insert(QStringLiteral("name"), m_ipCityName + QStringLiteral("·自动"));
        m_cityTabs.replace(0, autoTab);
        emit cityTabsChanged();
    }
    qWarning() << "IP 自动定位成功：" << city << "->" << matchedName << "(" << matchedId << ")";
    fetchAll(m_ipCityId);
}

void WeatherClient::finishIpLookupFailed()
{
    m_locating = false;
    emit locatingChanged();
    setError(QStringLiteral("公网 IP 定位失败，请在小部件设置中手动选择城市。"));
}

//城市/区县名 -> CSV LocationID 匹配：先精确匹配城市中文名（去掉“市”后缀），
//再匹配区县名（Admin_district_CN），英文名忽略大小写匹配 City_EN
bool WeatherClient::matchCity(const QString &name, bool englishName,
                              QString *matchedId, QString *matchedName)
{
    const QString cleaned = name.trimmed();
    const QString zhName = cleaned.endsWith(QStringLiteral("市"))
                              ? cleaned.left(cleaned.size() - 1) : cleaned;
    if (cleaned.isEmpty()) {
        return false;
    }

    if (!englishName) {
        for (const CityRecord &record : std::as_const(m_cities)) {
            if (record.name == zhName) {
                *matchedId = record.id;
                *matchedName = record.name;
                return true;
            }
        }
        for (const CityRecord &record : std::as_const(m_cities)) {
            if (record.adminDistrict == cleaned) {
                *matchedId = record.id;
                *matchedName = record.name; // 展示用城市名，定位到区县时仍显示所属城市
                return true;
            }
        }
    } else {
        for (const CityRecord &record : std::as_const(m_cities)) {
            if (record.nameEn.compare(cleaned, Qt::CaseInsensitive) == 0) {
                *matchedId = record.id;
                *matchedName = record.name;
                return true;
            }
        }
    }
    return false;
}

bool WeatherClient::launchApp()
{
    const QString executable =
        QStandardPaths::findExecutable(QStringLiteral("indicator-china-weather"));
    if (executable.isEmpty()) {
        qWarning() << "未找到 indicator-china-weather 可执行文件，请先安装本仓库的打包";
        return false;
    }
    // 带上 showmainwindow 参数：未运行时直接显示主窗口；
    // 已在运行时由 QtSingleApplication 把消息转发给首个实例并唤起其主窗口
    const bool ok = QProcess::startDetached(executable, {QStringLiteral("showmainwindow")});
    if (!ok) {
        qWarning() << "启动 indicator-china-weather 失败：" << executable;
    }
    return ok;
}

//构造 v7 请求 URL；中文/特殊字符经 QUrlQuery 自动百分号编码，手拼字符串会 400
void WeatherClient::fetch(const QString &path, const QString &type,
                          const std::function<void(const QJsonObject &)> &handler)
{
    QUrl url(QString::fromLatin1(kDevApiHost) + path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("location"), m_requestLocationId);
    query.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh"));
    if (!type.isEmpty()) {
        query.addQueryItem(QStringLiteral("type"), type);
    }
    url.setQuery(query);

    QNetworkRequest request;
    request.setUrl(url);
    request.setRawHeader("X-QW-Api-Key", m_apiKey.toUtf8());
    // 不手动设置 Accept-Encoding：Qt 需自行管理该头才会透明解压 gzip 响应
    request.setTransferTimeout(std::chrono::milliseconds(15000));

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, handler]() {
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        reply->close();
        reply->deleteLater();

        QJsonObject root;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            root = doc.object();
        }

        if (root.isEmpty()) {
            setError(QStringLiteral("请求 %1 失败：%2")
                         .arg(reply->url().path(),
                              httpStatus == 0 ? QStringLiteral("网络不可达或响应无法解析")
                                              : QStringLiteral("HTTP %1").arg(httpStatus)));
        } else if (!isV7Success(root)) {
            setError(QStringLiteral("和风天气返回错误码 %1（%2）")
                         .arg(root.value(QStringLiteral("code")).toString(),
                              reply->url().path()));
        } else {
            handler(root);
        }
        finishOne();
    });
}

void WeatherClient::finishOne()
{
    if (m_activeReplies > 0) {
        m_activeReplies -= 1;
    }
    if (m_activeReplies == 0 && m_loading) {
        m_loading = false;
        emit loadingChanged();
    }
}

void WeatherClient::setError(const QString &message)
{
    if (!m_hasError || m_errorString != message) {
        m_hasError = true;
        m_errorString = message;
        emit errorChanged();
        emit errorStringChanged();
    }
}

void WeatherClient::clearError()
{
    if (m_hasError) {
        m_hasError = false;
        m_errorString.clear();
        emit errorChanged();
        emit errorStringChanged();
    }
}

void WeatherClient::parseNow(const QJsonObject &root)
{
    const QJsonObject now = root.value(QStringLiteral("now")).toObject();
    if (now.isEmpty()) {
        return;
    }

    m_nowTemp = now.value(QStringLiteral("temp")).toString();
    m_nowIcon = now.value(QStringLiteral("icon")).toString();
    m_nowText = now.value(QStringLiteral("text")).toString();
    m_windDir = now.value(QStringLiteral("windDir")).toString();
    m_windScale = now.value(QStringLiteral("windScale")).toString();
    m_humidity = now.value(QStringLiteral("humidity")).toString();
    m_feelsLike = now.value(QStringLiteral("feelsLike")).toString();
    m_updateTime = root.value(QStringLiteral("updateTime")).toString();
    emit nowChanged();
}

void WeatherClient::parseDaily(const QJsonObject &root)
{
    const QJsonArray daily = root.value(QStringLiteral("daily")).toArray();
    if (daily.isEmpty()) {
        return;
    }

    QVariantList list;
    list.reserve(daily.size());
    for (const QJsonValue &value : daily) {
        const QJsonObject d = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("date"), d.value(QStringLiteral("fxDate")).toString());
        item.insert(QStringLiteral("iconDay"), d.value(QStringLiteral("iconDay")).toString());
        item.insert(QStringLiteral("iconNight"), d.value(QStringLiteral("iconNight")).toString());
        item.insert(QStringLiteral("textDay"), d.value(QStringLiteral("textDay")).toString());
        item.insert(QStringLiteral("textNight"), d.value(QStringLiteral("textNight")).toString());
        item.insert(QStringLiteral("tempMax"), d.value(QStringLiteral("tempMax")).toString());
        item.insert(QStringLiteral("tempMin"), d.value(QStringLiteral("tempMin")).toString());
        item.insert(QStringLiteral("windDirDay"), d.value(QStringLiteral("windDirDay")).toString());
        item.insert(QStringLiteral("windScaleDay"), d.value(QStringLiteral("windScaleDay")).toString());
        list.append(item);
    }
    m_daily = list;
    emit dailyChanged();
}

void WeatherClient::parseHourly(const QJsonObject &root)
{
    const QJsonArray hourly = root.value(QStringLiteral("hourly")).toArray();
    if (hourly.isEmpty()) {
        return;
    }

    QVariantList list;
    list.reserve(hourly.size());
    for (const QJsonValue &value : hourly) {
        const QJsonObject h = value.toObject();
        QVariantMap item;
        item.insert(QStringLiteral("time"), h.value(QStringLiteral("fxTime")).toString());
        item.insert(QStringLiteral("temp"), h.value(QStringLiteral("temp")).toString());
        item.insert(QStringLiteral("icon"), h.value(QStringLiteral("icon")).toString());
        item.insert(QStringLiteral("text"), h.value(QStringLiteral("text")).toString());
        item.insert(QStringLiteral("windDir"), h.value(QStringLiteral("windDir")).toString());
        item.insert(QStringLiteral("windScale"), h.value(QStringLiteral("windScale")).toString());
        list.append(item);
    }
    m_hourly = list;
    emit hourlyChanged();
}

void WeatherClient::parseAir(const QJsonObject &root)
{
    const QJsonObject now = root.value(QStringLiteral("now")).toObject();
    if (now.isEmpty()) {
        return;
    }
    m_airAqi = now.value(QStringLiteral("aqi")).toString();
    m_airCategory = now.value(QStringLiteral("category")).toString();
    emit airChanged();
}

void WeatherClient::parseIndices(const QJsonObject &root)
{
    const QJsonArray daily = root.value(QStringLiteral("daily")).toArray();

    // 生活指数固定展示顺序：穿衣(3) 洗车(2) 感冒(9) 紫外线(5) 空气指数(10) 运动(1)
    // + 钓鱼(4) 旅游(6) 花粉过敏(7) 舒适度(8) 空调(11) 太阳镜(12) 化妆(13) 晾晒(14)
    // 交通(15) 防晒(16)，共 16 类全覆盖（前 6 项保持既有视觉顺序）
    static const struct {
        const char *type;
        const char *name;
    } kIndexOrder[] = {
        {"3", "穿衣指数"}, {"2", "洗车指数"}, {"9", "感冒指数"},
        {"5", "紫外线指数"}, {"10", "空气指数"}, {"1", "运动指数"},
        {"4", "钓鱼指数"}, {"6", "旅游指数"}, {"7", "花粉过敏指数"},
        {"8", "舒适度指数"}, {"11", "空调开启指数"}, {"12", "太阳镜指数"},
        {"13", "化妆指数"}, {"14", "晾晒指数"}, {"15", "交通指数"},
        {"16", "防晒指数"},
    };

    QHash<QString, QJsonObject> byType;
    for (const QJsonValue &value : daily) {
        const QJsonObject item = value.toObject();
        byType.insert(item.value(QStringLiteral("type")).toString(), item);
    }

    QVariantList list;
    for (const auto &entry : kIndexOrder) {
        const QJsonObject item = byType.value(QString::fromLatin1(entry.type));
        QVariantMap mapped;
        mapped.insert(QStringLiteral("type"),
                      item.value(QStringLiteral("type")).toString().toInt()); // 整数 type，供 QML 数值比较
        mapped.insert(QStringLiteral("name"), QString::fromUtf8(entry.name));
        mapped.insert(QStringLiteral("category"), item.value(QStringLiteral("category")).toString());
        mapped.insert(QStringLiteral("text"), item.value(QStringLiteral("text")).toString());
        list.append(mapped);
    }
    m_indices = list;
    emit indicesChanged();
}

QVariantList WeatherClient::searchCities(const QString &keyword, int limit)
{
    QVariantList results;
    const QString trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) {
        return results;
    }
    ensureCityTableLoaded();
    if (m_cities.isEmpty()) {
        qWarning() << "城市表加载失败，无法搜索城市（期望位于" << kPackagedCityCsv << "）";
        return results;
    }

    const QString keywordLower = trimmed.toLower();
    struct Match {
        int score = 0;
        const CityRecord *record = nullptr;
    };
    QList<Match> matches;
    for (const CityRecord &record : std::as_const(m_cities)) {
        int score = -1;
        if (record.name == trimmed) {
            score = 0;
        } else if (record.name.startsWith(trimmed)) {
            score = 1;
        } else if (record.nameEn.startsWith(keywordLower, Qt::CaseInsensitive)) {
            score = 2;
        } else if (record.name.contains(trimmed)) {
            score = 3;
        } else if (record.province.contains(trimmed) && record.nameEn.startsWith(keywordLower)) {
            score = 4;
        } else if (record.nameEn.contains(keywordLower)) {
            score = 5;
        }
        if (score >= 0) {
            matches.append({score, &record});
        }
    }

    std::stable_sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
        if (a.score != b.score) {
            return a.score < b.score;
        }
        return a.record->name < b.record->name;
    });

    const int count = qBound(0, limit, static_cast<int>(matches.size()));
    for (int i = 0; i < count; ++i) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), matches.at(i).record->id);
        item.insert(QStringLiteral("name"), matches.at(i).record->name);
        item.insert(QStringLiteral("province"), matches.at(i).record->province);
        results.append(item);
    }
    return results;
}

//从内置 china-city-list.csv 建立城市查询表（列序与托盘应用 ensureCityTableLoaded 一致）
void WeatherClient::ensureCityTableLoaded()
{
    if (m_cityTableLoaded) {
        return;
    }
    m_cityTableLoaded = true;

    QStringList candidates;
    const QString overridePath = qEnvironmentVariable("CHINAWEATHER_CITY_CSV");
    if (!overridePath.isEmpty()) {
        candidates << overridePath;
    }
    candidates << QString::fromLatin1(kPackagedCityCsv);

    QFile file;
    for (const QString &candidate : std::as_const(candidates)) {
        file.setFileName(candidate);
        if (file.exists()) {
            break;
        }
        file.setFileName(QString());
    }
    if (file.fileName().isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "open china-city-list.csv failed";
        return;
    }

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).remove(QLatin1Char('\r')).remove(QLatin1Char('\n'));
        const QStringList columns = line.split(QLatin1Char(','));
        if (columns.length() < 11) {
            continue;
        }
        const QString id = columns.at(0);
        if (!id.startsWith(QLatin1String("CN"))) {
            continue;
        }
        CityRecord record;
        record.id = id.mid(2); // 去掉 "CN" 前缀，即和风 LocationID
        record.nameEn = columns.at(1);
        record.name = columns.at(2);
        record.province = columns.at(7);
        record.adminDistrict = columns.at(9);
        m_cities.append(record);
    }
    file.close();
}
