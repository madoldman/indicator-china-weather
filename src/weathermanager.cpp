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

#include "weathermanager.h"
#include "weatherworker.h"
#include "geoipworker.h"

#include <QDebug>
#include <QThread>
#include <QFile>
#include <QTimer>

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusReply>

#include <unistd.h>

namespace {

//停止并回收工作线程（线程对象不挂父对象，由本函数在正常退出时 delete）。
//等待超时时不调用 terminate()：对可能阻塞在网络栈/数据库锁里的线程强制终止
//随时可能崩溃；调用时机为应用退出，此时记录告警并有意泄漏线程对象交由进程
//统一回收，风险小于强杀（geoip/weather 链路均已设传输超时，超时仅见于极端异常）
void quitThread(QThread *thread)
{
    if (!thread) {
        return;
    }
    thread->quit();
    if (!thread->wait(2000)) {
        qWarning() << "WeatherManager: worker thread did not finish within 2s;"
                      " leak the QThread instead of calling terminate()";
        return;
    }
    delete thread;
}

} // namespace

WeatherManager::WeatherManager(QObject *parent) : QObject(parent)
{
    m_geoipWorker = new GeoIpWorker();
    m_weatherWorker = new WeatherWorker();
    //线程对象不挂父对象：退出时若线程未能在时限内结束，quitThread 会泄漏而非
    //强杀；若挂到 this 上，~QObject 会连带 delete 仍在运行的 QThread 导致 qFatal
    m_geoipThread = new QThread();
    m_weatherThread = new QThread();
    m_geoipWorker->moveToThread(m_geoipThread);
    m_weatherWorker->moveToThread(m_weatherThread);

    this->initConnections();

    QTimer::singleShot(1, this, [=] {
        m_geoipThread->start();
        m_weatherThread->start();
    });
}

WeatherManager::~WeatherManager()
{
    quitThread(m_geoipThread);
    quitThread(m_weatherThread);
}

void WeatherManager::initConnections()
{
    connect(m_geoipThread, &QThread::finished, m_geoipWorker, &GeoIpWorker::deleteLater);
    connect(m_weatherThread, &QThread::finished, m_weatherWorker, &WeatherWorker::deleteLater);
    connect(m_weatherWorker, &WeatherWorker::nofityNetworkStatus, this, &WeatherManager::nofityNetworkStatus);
    connect(m_geoipWorker, &GeoIpWorker::automaticLocationFinished, this, &WeatherManager::setAutomaticCity);
    connect(m_weatherWorker, &WeatherWorker::responseFailure, this, &WeatherManager::responseFailure);

    connect(m_weatherWorker, SIGNAL(requestSetObserveWeather(ObserveWeather)), this, SIGNAL(requestSetObserveWeather(ObserveWeather)));
    connect(m_weatherWorker, SIGNAL(requestSetForecastWeather(ForecastWeather)), this, SIGNAL(requestSetForecastWeather(ForecastWeather)));
    connect(m_weatherWorker, SIGNAL(requestSetLifeStyle(LifeStyle)), this, SIGNAL(requestSetLifeStyle(LifeStyle)));

    connect(this, SIGNAL(requestShowCollCityWeather()), m_weatherWorker, SLOT(onCityWeatherDataRequest()) );
    connect(m_weatherWorker, &WeatherWorker::requestSetCityWeather, this, &WeatherManager::requestSetCityWeather);

    QDBusConnection::systemBus().connect(QString("org.freedesktop.NetworkManager"),
                                         QString("/org/freedesktop/NetworkManager"),
                                         QString("org.freedesktop.NetworkManager"),
                                         QString("PropertiesChanged"), this, SLOT(onPropertiesChanged(QVariantMap) ) );
}

void WeatherManager::startGetTheWeatherData(QString cityId)
{
    emit m_weatherWorker->requestGetTheWeatherData(cityId);
}

void WeatherManager::startTestNetwork()
{
    emit m_weatherWorker->requestTestNetwork();
}

void WeatherManager::startAutoLocationTask()
{
    emit m_geoipWorker->requestStartWork();
}

//批量拉取城市列表天气简报：复用收藏城市简报管线（requestShowCollCityWeather已连至
//WeatherWorker::onCityWeatherDataRequest，逐城请求/v7/weather/now后以
//"tmp=...,cond_txt=...,cond_code=...,id=...,location=...;"记录格式经requestSetCityWeather下发）
void WeatherManager::startCityListBrief()
{
    emit requestShowCollCityWeather();
}

void WeatherManager::setAutomaticCity(const QString &cityName)
{
    bool autoSuccess = false;
    CitySettingData info;

    if (cityName.isEmpty()) {
        emit this->requestAutoLocationData(info, false);
        return;
    }

    QFile file(":/data/data/china-city-list.csv");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString line = file.readLine();
        line = line.remove("\r").remove("\n");//CSV 为 CRLF，与 ensureCityTableLoaded 保持一致
        while (!line.isEmpty()) {
            QStringList resultList = line.split(",");
            if (resultList.length() < 10) {
                line = file.readLine();
                line = line.remove("\r").remove("\n");
                continue;
            }

            QString id = resultList.at(0);
            if (!id.startsWith("CN")) {
                line = file.readLine();
                line = line.remove("\r").remove("\n");
                continue;
            }

            if (resultList.at(1).compare(cityName, Qt::CaseInsensitive) == 0 ||
                resultList.at(2).compare(cityName, Qt::CaseInsensitive) == 0 ||
                QString(resultList.at(2) + "市").compare(cityName, Qt::CaseInsensitive) == 0 ||
                QString(resultList.at(2) + "区").compare(cityName, Qt::CaseInsensitive) == 0 ||
                QString(resultList.at(2) + "县").compare(cityName, Qt::CaseInsensitive) == 0) {
                id.remove(0, 2);//remove "CN"
                QString name = resultList.at(2);

                info.active = false;
                info.id = id;
                info.name = name;
                info.icon = ":/res/weather_icons/darkgrey/100.png";

                autoSuccess = true;
                break;
            }

            line = file.readLine();
            line = line.remove("\r").remove("\n");
        }
        file.close();
    }
    if (autoSuccess) {
        emit this->requestAutoLocationData(info, true);
    } else {
        emit this->requestAutoLocationData(info, false);
    }
}

void WeatherManager::initConnectionInfo()
{
    QDBusInterface interface( "org.freedesktop.NetworkManager",
                              "/org/freedesktop/NetworkManager",
                              "org.freedesktop.DBus.Properties",
                              QDBusConnection::systemBus() );

    QDBusMessage result = interface.call("Get", "org.freedesktop.NetworkManager", "ActiveConnections");
    const QList<QVariant> outArgs = result.arguments();
    if (outArgs.isEmpty()) {//NetworkManager 未运行等调用失败时无返回参数，直接返回避免越界
        qWarning() << "initConnectionInfo: get ActiveConnections failed:"
                   << result.errorMessage();
        return;
    }
    QVariant first = outArgs.at(0);
    QDBusVariant dbvFirst = first.value<QDBusVariant>();
    QVariant vFirst = dbvFirst.variant();
    QDBusArgument dbusArgs = vFirst.value<QDBusArgument>();

    QDBusObjectPath objPath;
    dbusArgs.beginArray();
    while (!dbusArgs.atEnd()) {
        dbusArgs >> objPath;
        oldPaths.append(objPath);
        qDebug() <<"debug: *****path is: "<< objPath.path();

        QDBusInterface interface( "org.freedesktop.NetworkManager",
                                  objPath.path(),
                                  "org.freedesktop.DBus.Properties",
                                  QDBusConnection::systemBus() );

        QDBusReply<QVariant> reply = interface.call("Get", "org.freedesktop.NetworkManager.Connection.Active", "Type");
        qDebug()<<"debug: *****connection type is: "<<reply.value().toString();
        oldPathInfo.append(reply.value().toString());
    }
    dbusArgs.endArray();
}

void WeatherManager::onPropertiesChanged(QVariantMap qvm)
{
    for(QString keyStr : qvm.keys()) {
        if (keyStr == "ActiveConnections") {
            const QDBusArgument &dbusArg = qvm.value(keyStr).value<QDBusArgument>();
            QList<QDBusObjectPath> newPaths;
            dbusArg >> newPaths;
            QStringList newPathInfo;
            foreach (QDBusObjectPath objPath, newPaths) {
                //qDebug()<<"dbug: bbbbb  "<<objPath.path();

                QDBusInterface interface( "org.freedesktop.NetworkManager",
                                          objPath.path(),
                                          "org.freedesktop.DBus.Properties",
                                          QDBusConnection::systemBus() );

                QDBusReply<QVariant> reply = interface.call("Get", "org.freedesktop.NetworkManager.Connection.Active", "Type");

                if(reply.value().toString() == ""){
                    emit noNetWork();
                }
                newPathInfo.append(reply.value().toString());
            }

            // 当前的网络连接个数由0个增为1个时，触发天气界面更新
            if (newPaths.size() == 1) {
                if (oldPaths.size() == 0) {
                    QTimer::singleShot(4*1000, this, SLOT(onTimeFinished() ));
                }
            }

            bool isChangeOldPathInfo = true;
            for (int k=0; k<newPathInfo.size(); k++) {
                if (newPathInfo.at(k) == "") {
                    isChangeOldPathInfo = false;
                }
            }
            if (isChangeOldPathInfo) {
                oldPathInfo = newPathInfo;
            }
            oldPaths = newPaths;
        }
    }
}

void WeatherManager::onTimeFinished()
{
    emit newNetworkConnectionCreated();
}
