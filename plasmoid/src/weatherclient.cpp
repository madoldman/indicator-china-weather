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

// Plasmoid 包内城市表（由打包安装到系统小部件目录）。绝对路径由 CMake 按
// 安装前缀注入编译定义 CHINAWEATHER_PACKAGED_CITY_CSV；未注入时（脱离构建
// 系统单独编译等场景）回退到默认 /usr 前缀
#ifndef CHINAWEATHER_PACKAGED_CITY_CSV
#define CHINAWEATHER_PACKAGED_CITY_CSV \
    "/usr/share/plasma/plasmoids/org.madoldman.chinaweather/contents/data/china-city-list.csv"
#endif
constexpr char kPackagedCityCsv[] = CHINAWEATHER_PACKAGED_CITY_CSV;

// IP 定位失败后的退避重试节奏：首次 30s，逐次翻倍封顶 300s；不设次数上限，
// 网络长时间未就绪时持续自愈，封顶保证退避到位后不再产生高频请求
constexpr int kIpRetryInitialSeconds = 30;
constexpr int kIpRetryMaxSeconds = 300;

// 和风 v7 响应的 code 字段为字符串，"200" 表示成功
bool isV7Success(const QJsonObject &root)
{
    return root.value(QStringLiteral("code")).toString() == QStringLiteral("200");
}

// gsettings-qt6 的 changed 信号把多词键归一化为驼峰（refresh-interval -> refreshInterval），
// 而 get/set 用 schema 原键名；比较前统一去掉 '-' 并转小写，避免键名风格差异导致分支失配
QString normalizedGsettingsKey(const QString &key)
{
    return QString(key).remove(QLatin1Char('-')).toLower();
}

} // namespace

WeatherClient::WeatherClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    // IP 定位失败的退避重试定时器：单次触发，调度与节奏见 scheduleIpRetry；
    // 必须先于 rebuildCityTabs 创建（后者在非自动定位时会停止未决重试）
    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &WeatherClient::retryIpLocation);

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
    // 构造路径只应用、不写回：setRefreshInterval 会经 qBound 钳制后把值写回
    // gsettings——用户经 gsettings CLI 设置的 <5 或 >360 超范围值会被小部件在
    // 每次加载时单方面改写为 5/360。这里直接按读到的值应用（创建周期定时器），
    // 写回仅发生在用户显式选择间隔时（配置页走 setRefreshInterval）
    applyRefreshInterval(m_refreshInterval);
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
// 出厂默认（北京）且 kcfg 配置了非默认城市时导入，避免覆盖应用侧已保存的城市；
// 旧配置仍是出厂默认北京时无需迁移——此时写回的 citylist 与默认值完全相同，
// 「citylist 已非默认」的幂等判断永远不成立，会每次重启重复迁移并把
// autolocate 翻回 false，导致重启后自动定位失效
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
    if (m_cityId == QStringLiteral("101010100")) {
        return; // 出厂默认北京：没有用户自定义城市需要迁移，保持 autolocate 不变
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
    // 非自动定位（本侧切换或应用侧经 gsettings 回流改写）：IP 定位重试不再
    // 有意义，停止未决重试并复位退避节奏，同时放弃在途的 IP 定位请求
    if (!m_autolocate) {
        stopIpRetry();
        abortIpLocation();
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
    // 用户明确驻留手动城市：停止未决的 IP 定位重试，并放弃在途的 IP 定位请求
    // （其完成回调经 finishIpLocation 的活动页复核也不会再拉取数据，这里主动
    // abort + 复位 locating，避免后台继续请求 IP 接口、面板一直显示「正在定位…」）；
    // 回到自动页时 refresh() 会重新发起定位，失败后按退避节奏重建重试
    stopIpRetry();
    abortIpLocation();
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
    // 用户显式选择路径（配置页）：写入 gsettings（单一数据源），应用菜单与
    // 小部件任一侧修改都会经 QGSettings::changed 回流到另一侧；随后立即应用
    // 一次，不依赖信号时序
    if (m_gsettings) {
        m_gsettings->set(QStringLiteral("refresh-interval"), clamped);
    }
    applyRefreshInterval(clamped);
}

void WeatherClient::applyRefreshInterval(int minutes)
{
    if (minutes <= 0) {
        return;
    }
    // 值变化时先同步属性并通知（headless 实例同样同步：配置页的间隔下拉框
    // 依赖该属性展示 gsettings 当前值）
    const bool changed = minutes != m_refreshInterval;
    if (changed) {
        m_refreshInterval = minutes;
        emit refreshIntervalChanged();
    }
    // headless 实例（配置对话框的辅助客户端）无 UI 消费者：不创建周期定时器，
    // 也就永远不会周期性发起不可见的网络请求
    if (m_headless) {
        return;
    }
    // 定时器已在运行且值未变时无需处理；定时器缺失时（构造函数以 gsettings
    // 读到的同值调用进来）仍要落到创建分支，否则整个会话没有周期刷新
    if (!changed && m_timer) {
        return;
    }

    // 按当前间隔创建/重建定时器（重建前先停掉旧的，避免双份周期刷新）
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

// headless（无 UI 消费者）切换：开启时停掉周期定时器，恢复时按当前间隔补建；
// 网络请求的抑制统一在 refresh() 入口判断
void WeatherClient::setHeadless(bool headless)
{
    if (headless == m_headless) {
        return;
    }
    m_headless = headless;
    emit headlessChanged();
    if (m_headless) {
        if (m_timer) {
            m_timer->stop();
            m_timer->deleteLater();
            m_timer = nullptr;
        }
    } else {
        applyRefreshInterval(m_refreshInterval);
    }
}

void WeatherClient::onGSettingsChanged(const QString &key)
{
    if (normalizedGsettingsKey(key) == QLatin1String("refreshinterval") && m_gsettings) {
        const int stored = m_gsettings->get(QStringLiteral("refresh-interval")).toInt();
        if (stored > 0) {
            applyRefreshInterval(stored);
        }
        return;
    }
    // 城市列表/自动定位变更（应用侧或另一小部件实例修改）：重建页签，
    // 实际生效的城市变化时立即刷新
    if (normalizedGsettingsKey(key) == QLatin1String("citylist")
        || normalizedGsettingsKey(key) == QLatin1String("autolocate")) {
        const QString previousLocationId = currentLocationId();
        rebuildCityTabs();
        if (currentLocationId() != previousLocationId) {
            refresh();
        }
    }
}

void WeatherClient::refresh()
{
    // headless 实例（配置对话框的辅助客户端）没有展示数据的界面：不发起任何
    // 网络请求，数据刷新由面板主实例经 gsettings changed 链路统一完成
    if (m_headless) {
        return;
    }
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
            // 定位窗口内不再保留旧批次（可能是刚浏览过的手动城市）：同样换代 +
            // abort，避免自动页在定位期间继续显示旧城市的数据（与 fetchAll 同一
            // 代际机制）；定位成功后 finishIpLocation 会发起新批次
            ++m_requestGeneration;
            abortBatchReplies();
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
    // 上一批请求仍在途时不再静默丢弃本次请求（旧实现直接 return，城市切换会被
    // 在途批次吞掉：面板页签/标题已指向新城市，数据却停留在上一个城市，直到
    // 下一个刷新周期才恢复）。这里先递增请求代际使旧批次回调整体失效，再 abort
    // 在途 reply（abort 可能同步触发 finished，必须先换代），最后以新目标城市
    // 重新发起完整批次——最新一次请求永远胜出
    ++m_requestGeneration;
    abortBatchReplies();

    clearError();
    // 记录本批次的目标城市（fetch 拼装 URL 时读取）
    m_requestLocationId = locationId;
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

// abort 当前批次全部在途请求并清空批次列表。前置条件：m_requestGeneration 已
// 递增——abort 可能同步触发 finished，被 abort 的回调经代际检查走「过期」分支
// （不解析、不置错误、不触碰新批次的计数状态）；若 finished 异步到达亦同样安全
void WeatherClient::abortBatchReplies()
{
    if (m_batchReplies.isEmpty()) {
        return;
    }
    // 批次作废：loading 一并复位（调用方随后发起新批次时会重新置位）；先复位
    // 再 abort，使同步触发的过期回调（不修改 loading）不会与新批次状态交叠
    if (m_loading) {
        m_loading = false;
        emit loadingChanged();
    }
    const QList<QNetworkReply *> replies = m_batchReplies;
    m_batchReplies.clear();
    for (QNetworkReply *reply : replies) {
        reply->abort();
        reply->deleteLater();
    }
}

//--------- IP 自动定位（级联：geoip.ubuntu.com -> myip.ipip.net，均走 HTTPS 且无需凭据） ---------

void WeatherClient::startIpLocation()
{
    if (m_locating) {
        return; // 定位请求已在途
    }
    m_locating = true;
    emit locatingChanged();
    requestUbuntuLookup();
}

// 主动放弃在途 IP 定位（浏览手动城市/离开自动定位时调用）：先断开本对象的
// 回调链（abort 会触发 finished，不先断开会把级联走到备源/完成处理），再
// abort 并释放，最后复位 locating 状态
void WeatherClient::abortIpLocation()
{
    if (m_ipReply) {
        QNetworkReply *reply = m_ipReply;
        m_ipReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (m_locating) {
        m_locating = false;
        emit locatingChanged();
    }
}

// 主源：geoip.ubuntu.com/lookup 返回 XML（<City>Changsha</City> 等英文字段）
void WeatherClient::requestUbuntuLookup()
{
    QNetworkRequest request{QUrl(QStringLiteral("https://geoip.ubuntu.com/lookup"))};
    request.setTransferTimeout(std::chrono::milliseconds(10000));
    QNetworkReply *reply = m_nam->get(request);
    m_ipReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_ipReply = nullptr;
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
    QNetworkRequest request{QUrl(QStringLiteral("https://myip.ipip.net"))};
    request.setTransferTimeout(std::chrono::milliseconds(10000));
    QNetworkReply *reply = m_nam->get(request);
    m_ipReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_ipReply = nullptr;
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
    // 复核定位结果是否仍被需要：发起定位后用户可能已浏览到手动城市页签
    // （m_activeCityIndex > 0），或已通过 gsettings 离开自动定位（正常情况下
    // 在途请求已被 abortIpLocation 放弃，这里是兜底复核）——此时 IP 城市不再
    // 对应当前活动页，直接丢弃结果，避免覆盖手动城市的展示数据
    if (!m_autolocate || m_activeCityIndex > 0) {
        m_locating = false;
        emit locatingChanged();
        return;
    }

    ensureCityTableLoaded();
    QString matchedId;
    QString matchedName;
    if (!matchCity(city, englishName, &matchedId, &matchedName)) {
        m_locating = false;
        emit locatingChanged();
        // 城市表未命中并非网络问题，重试不会改变结果：取消未决重试即可，
        // 不再调度新的重试
        stopIpRetry();
        setError(QStringLiteral("公网 IP 定位到的城市（%1）未在本地城市表中找到，请手动选择城市。").arg(city));
        return;
    }

    m_ipCityId = matchedId;
    m_ipCityName = matchedName;
    m_ipProvince = province;
    m_ipResolved = true;
    m_locating = false;
    emit locatingChanged();
    // 定位成功：退避重试完成使命，停止未决重试并复位节奏，下次网络故障从
    // 初始延迟重新退避
    stopIpRetry();
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
    // 网络/解析失败（区别于上面的城市表未命中）：调度退避重试，网络恢复后
    // 自动自愈，无需用户展开面板或重启小部件；仅在自动定位模式下重试有意义，
    // 提示文案据此区分「将自动重试」与「需手动选择」
    if (scheduleIpRetry()) {
        setError(QStringLiteral("公网 IP 定位失败，将自动重试；也可在小部件设置中手动选择城市。"));
    } else {
        setError(QStringLiteral("公网 IP 定位失败，请在小部件设置中手动选择城市。"));
    }
}

// IP 定位失败后的退避重试调度：首次 30s，逐次翻倍封顶 300s（30→60→120→240→300…），
// 不设次数上限——网络可能长时间未就绪，需持续自愈。仅在仍处于自动定位、尚未
// 解析出城市且无定位请求在途时调度，任一条件不满足即放弃且不留悬挂定时器
bool WeatherClient::scheduleIpRetry()
{
    if (!m_autolocate || m_ipResolved || m_locating) {
        return false;
    }
    m_retryDelaySeconds = m_retryDelaySeconds <= 0
                              ? kIpRetryInitialSeconds
                              : qMin(m_retryDelaySeconds * 2, kIpRetryMaxSeconds);
    // 单次定时器：重复调度时以新的退避延迟重新武装，替换旧的更短延迟
    m_retryTimer->start(std::chrono::seconds(m_retryDelaySeconds));
    return true;
}

// 退避重试触发点：等待期间状态可能已变化（已定位成功/已切手动城市），复核
// 前置条件后再走既有的 startIpLocation（其内保留 m_locating 防重入），避免
// 状态变化后仍发出无效的 IP 请求
void WeatherClient::retryIpLocation()
{
    if (!m_autolocate || m_ipResolved || m_locating) {
        return;
    }
    // 尝试期间离开错误态：由 locating 驱动「正在定位」的中性展示；若本次仍
    // 失败，finishIpLookupFailed 会再次给出提示并调度下一轮
    clearError();
    startIpLocation();
}

// 停止未决的 IP 定位重试并复位退避节奏（定位成功、切手动城市/页签时调用）
void WeatherClient::stopIpRetry()
{
    if (m_retryTimer) {
        m_retryTimer->stop();
    }
    m_retryDelaySeconds = 0;
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
    // 记录发起批次：请求登记到批次列表（完成/abort 时移除），并捕获当时的请求
    // 代际供完成回调判过期（fetchAll 在换代后立即发起新批次，本代际即批次代际）
    const int generation = m_requestGeneration;
    m_batchReplies.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, handler]() {
        m_batchReplies.removeOne(reply);
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        reply->close();
        reply->deleteLater();

        // 过期批次（发起后目标城市已切换，或本批次已被新请求 abort 取代——
        // abort 前代际已递增）：整体丢弃，不解析、不置错误、不计数，避免旧城市
        // 数据串入当前展示，也避免 abort 误报「网络不可达」
        if (generation != m_requestGeneration) {
            return;
        }

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

// 批次内单个请求完成：全部完成（批次列表清空）后结束 loading 状态。
// 过期批次的回调不会走到这里（fetch 回调先做代际检查）；被 abort 的批次在
// abortBatchReplies 中已整体复位计数与 loading，不影响新批次
void WeatherClient::finishOne()
{
    if (!m_batchReplies.isEmpty() || !m_loading) {
        return;
    }
    m_loading = false;
    emit loadingChanged();
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
        // 省英文名前缀（CSV Province_EN 列，小写存储）：支持按省名拼音搜出
        // 该省全部城市（如 hunan -> 长沙/株洲…），排名低于城市名直配
        } else if (record.provinceEn.startsWith(keywordLower, Qt::CaseInsensitive)) {
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
        record.provinceEn = columns.at(6);
        record.province = columns.at(7);
        record.adminDistrict = columns.at(9);
        m_cities.append(record);
    }
    file.close();
}
