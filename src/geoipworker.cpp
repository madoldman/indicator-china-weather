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

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <GeoIP.h>
#include <GeoIPCity.h>

#ifdef   __cplusplus
extern "C" {
#endif

GEOIP_API unsigned long _GeoIP_lookupaddress(const char *host); //_GeoIP_lookupaddress定义在GeoIP_internal.h中，但libgeoip-dev安装时并没有拷贝GeoIP_internal.h文件

#ifdef   __cplusplus
}
#endif

namespace {

const char UbuntuUrl[] = "http://geoip.ubuntu.com/lookup";

const void getIpAndCityByUbuntu(const QString &url, QString &ip, QString &city)
{
    const QString ipPattern = "<Ip>([^<]+)";
    const QString cityPattern = "<City>([^<]+)";

    QNetworkAccessManager *manager = new QNetworkAccessManager();
    QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));

    QEventLoop loop;
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    //QObject::connect(manager, &QNetworkAccessManager::finished, &loop, &QEventLoop::quit);
    //QObject::connect(manager, SIGNAL(finished(QNetworkReply *)), &loop, SLOT(quit()));
    loop.exec();

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray ba = reply->readAll();

        QString reply_content = QString::fromUtf8(ba);
        reply->close();
        reply->deleteLater();
        manager->deleteLater();

        QRegularExpression ipRegular(ipPattern, QRegularExpression::MultilineOption);
        QRegularExpressionMatch match = ipRegular.match(reply_content);
        if (match.hasMatch()) {
            ip = match.captured(1);
        }
        else {
            ip = QString("0.0.0.0");
        }

        QRegularExpression cityRegular(cityPattern, QRegularExpression::MultilineOption);
        match = cityRegular.match(reply_content);
        if (match.hasMatch()) {
            city = match.captured(1);
        }
        else {
            city = QString();
        }
    }
}

const QString getCityFromIpByTaobao(const QString &ip)
{
    QString url = QString("http://ip.taobao.com/service/getIpInfo.php?ip=%1").arg(ip);
    QNetworkAccessManager *manager = new QNetworkAccessManager();
    QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
    QEventLoop loop;
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    //QObject::connect(manager, &QNetworkAccessManager::finished, &loop, &QEventLoop::quit);
    //QObject::connect(manager, SIGNAL(finished(QNetworkReply *)), &loop, SLOT(quit()));
    loop.exec();

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(reply->error() != QNetworkReply::NoError || statusCode != 200) {//200 is normal status
        reply->close();
        reply->deleteLater();
        manager->deleteLater();
        return QString();
    }

    QByteArray ba = reply->readAll();
    reply->close();
    reply->deleteLater();
    manager->deleteLater();

    QJsonParseError err;
    QJsonDocument jsonDocument = QJsonDocument::fromJson(ba, &err);
    if (err.error != QJsonParseError::NoError) {// Json type error
        return QString();
    }
    if (jsonDocument.isNull() || jsonDocument.isEmpty()) {
        return QString();
    }

    QJsonObject jsonObject = jsonDocument.object();
    if (jsonObject.isEmpty() || jsonObject.size() == 0) {
        return QString();
    }

    if (jsonObject.contains("data")) {
        QJsonObject dataObj = jsonObject.value("data").toObject();
        if (dataObj.isEmpty() || dataObj.size() == 0) {
            return QString();
        }
        if (dataObj.contains("city")) {
            return dataObj.value("city").toString();
        }
    }

    return QString();
}

QString cityName = "";
const QString getCityFromIPAddr(const QString &ip)
{
    int charset = GEOIP_CHARSET_UTF8;
//    charset = GEOIP_CHARSET_ISO_8859_1;

    GeoIP *gi = GeoIP_open_type(GEOIP_CITY_EDITION_REV1, GEOIP_STANDARD | GEOIP_SILENCE);
    if (NULL == gi) {
        return QString();
    }
//    gi->charset = charset;

    uint32_t ipnum = _GeoIP_lookupaddress(ip.toStdString().c_str());
    if (ipnum == 0) {
        printf("%s: can't resolve ip ( %s )\n", GeoIPDBDescription[GEOIP_CITY_EDITION_REV1], ip.toStdString().c_str());
        return QString();
    }
    GeoIPRecord *gir = GeoIP_record_by_ipnum(gi, ipnum);
    if (gir) {
        //const char *region = GeoIP_region_name_by_code(gir->country_code, gir->region);
        //qDebug() << "country_name=" << gir->country_name << ",region=" << region << ",gir->city=" << gir->city << ",gir->latitude=" << gir->latitude << ",gir->longitude=" << gir->longitude;
        cityName = QString(gir->city);
        GeoIPRecord_delete(gir);
        GeoIP_delete(gi);
        return cityName;
    }

    GeoIP_delete(gi);

    return QString();
}
//备源：myip.ipip.net 返回纯文本（"当前 IP：x.x.x.x  来自于：中国 湖南 长沙  电信"），
//与小部件解析一致：从「来自于」起按空白切分取省/市字段
const QString getCityFromIPIP()
{
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(QNetworkRequest(QUrl("http://myip.ipip.net")));
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    QString city;
    if (reply->error() == QNetworkReply::NoError) {
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
    reply->deleteLater();
    return city;
}


//主源：geoip.ubuntu.com/lookup 返回 XML（<City>Changsha</City> 等英文字段），无需凭据；
//与面板小部件采用的链路一致。原 pconline + 高德链路已弃用：whois.pconline.com.cn
//常被 WAF 拒绝（403）、硬编码的高德 key 每日配额易超限
const QString getCityFromUbuntu()
{
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(UbuntuUrl)));
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    QString city;
    if (reply->error() == QNetworkReply::NoError) {
        const QString content = QString::fromUtf8(reply->readAll());
        static const QRegularExpression cityRe(QStringLiteral("<City>([^<]+)</City>"));
        const auto match = cityRe.match(content);
        if (match.hasMatch()) {
            city = match.captured(1).trimmed();
        }
    }
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
    //m_networkManager = new QNetworkAccessManager(this);
    //connect(m_networkManager, &QNetworkAccessManager::finished, this, &GeoIpWorker::onReplyFinished);
    connect(this, &GeoIpWorker::requestStartWork, this, &GeoIpWorker::doWork);
}

void GeoIpWorker::doWork()
{
    QString cityName = automaicCity();
    emit automaticLocationFinished(cityName);
}


/*void GeoIpWorker::onReplyFinished(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {

    }
    reply->deleteLater();
}*/
