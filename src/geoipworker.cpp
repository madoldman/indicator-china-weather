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

#include "geoipworker.h"

#include <QDebug>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <chrono>

namespace {

constexpr std::chrono::milliseconds kGeoIpTimeoutMs{10000};//与和风请求（15s）、小部件链路（10s）同量级

//备源：myip.ipip.net 返回纯文本（"当前 IP：x.x.x.x  来自于：中国 湖南 长沙  电信"），
//与小部件解析一致：从「来自于」起按空白切分取省/市字段
const QString getCityFromIPIP()
{
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl("http://myip.ipip.net"));
    request.setTransferTimeout(kGeoIpTimeoutMs);//HTTP 明文连接，Qt6 默认无传输超时，必须显式设置
    QNetworkReply *reply = manager.get(request);
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    //兜底：即使 finished 因任何原因永不触发，超时后也强制退出，避免 geoip 线程被无限阻塞
    QTimer::singleShot(kGeoIpTimeoutMs, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();

    QString city;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        const QString str = QString::fromUtf8(reply->readAll());
        const int from = str.indexOf(QStringLiteral("来自于"));
        if (from >= 0) {
            const QStringList parts = str.mid(from + 3)
                .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            // parts 形如 ["：中国","湖南","长沙","电信"]（末位可能缺运营商）
            if (parts.size() >= 3) {
                city = parts.at(2);
            }
        }
    }
    if (!reply->isFinished()) {
        reply->abort();//兜底超时退出时终止仍在途的请求，避免 manager 析构时仍有活动连接
    }
    reply->close();
    reply->deleteLater();
    return city;
}


//主源：geoip.ubuntu.com/lookup 返回 XML（<City>Changsha</City> 等英文字段），无需凭据；
//与面板小部件采用的链路一致。原 pconline + 高德链路已弃用：whois.pconline.com.cn
//常被 WAF 拒绝（403）、硬编码的高德 key 每日配额易超限
const QString getCityFromUbuntu()
{
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl("http://geoip.ubuntu.com/lookup"));
    request.setTransferTimeout(kGeoIpTimeoutMs);//HTTP 明文连接，Qt6 默认无传输超时，必须显式设置
    QNetworkReply *reply = manager.get(request);
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    //兜底：即使 finished 因任何原因永不触发，超时后也强制退出，避免 geoip 线程被无限阻塞
    QTimer::singleShot(kGeoIpTimeoutMs, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();

    QString city;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        const QString content = QString::fromUtf8(reply->readAll());
        static const QRegularExpression cityRe(QStringLiteral("<City>([^<]+)</City>"));
        const auto match = cityRe.match(content);
        if (match.hasMatch()) {
            city = match.captured(1).trimmed();
        }
    }
    if (!reply->isFinished()) {
        reply->abort();//兜底超时退出时终止仍在途的请求，避免 manager 析构时仍有活动连接
    }
    reply->close();
    reply->deleteLater();
    return city;
}

//IP 自动定位：主源 geoip.ubuntu.com（英文城市名，WeatherManager::setAutomaticCity
//按 city_en 匹配），失败/Unknown 时回退 myip.ipip.net（中文城市名，按 city_CN 匹配）
const QString automaicCity()
{
    qDebug() << "开始自动定位";
    QString city = getCityFromUbuntu();
    if (city.isEmpty() || city.compare("Unknown", Qt::CaseInsensitive) == 0) {
        city = getCityFromIPIP();
    }
    return city;
}
} // namespace

GeoIpWorker::GeoIpWorker(QObject* parent) : QObject(parent)
{
    connect(this, &GeoIpWorker::requestStartWork, this, &GeoIpWorker::doWork);
}

void GeoIpWorker::doWork()
{
    QString cityName = automaicCity();
    emit automaticLocationFinished(cityName);
}
