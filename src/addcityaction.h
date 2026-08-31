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

#ifndef ADDCITYACTION_H
#define ADDCITYACTION_H

#include <QWidget>
#include <QAction>
#include <QRect>
#include <QDBusInterface>
#include "data.h"
#include "citycollectionwidget.h"
class AddCityAction : public QAction
{
    Q_OBJECT
public:
    explicit AddCityAction(QWidget *parent = nullptr);
signals:

    void cityNameLabelSignal(QString cityName);
    void sendCurrentCityId(QString id); //发送到主界面更新主界面天气
    void requestShowCollCityWeather(); //显示收藏城市列表天气
    void requestSetCityWeather(QString weather_data); //发送出去显示主界面城市天气
    void updatecity();

//protected:
//    void enterEvent(QEvent *event) Q_DECL_OVERRIDE;
//    void leaveEvent(QEvent *event) Q_DECL_OVERRIDE;
//    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;//需要此功能的信号槽函数
//    void mouseReleaseEvent(QMouseEvent *event) Q_DECL_OVERRIDE;

private:
    bool is_open_city_collect_widget = false;//需要
    QLabel *addCityLabel = nullptr;
    CityCollectionWidget *m_citycollectionwidget;//需要
    ObserveWeather m_pendingCurrentCity; //主程序当前实际城市天气（收藏对话框懒创建前缓存）
    void onRequestSendDesktopNotify(QString message);

private slots:
    void addCityClick();
public slots:
    //转发主程序当前实际城市天气到收藏对话框的「当前城市」卡片（自动定位时即自动定位城市）
    void setCurrentCityWeather(const ObserveWeather &weather);
signals:

    void noNetWork();//断网
    void requestSetCityName(QString cityName); //需要//在搜索列表中选中一个城市后，左上角城市名需要更改

};

#endif // ADDCITYACTION_H
