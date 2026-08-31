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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#define CHINAWEATHERDATA "org.china-weather-data.settings"
#define FITTHEMEWINDOW "org.ukui.style"

#include "mainwindow.h"
#include "leftupcitybtn.h"
#include "leftupsearchbox.h"
#include "leftupsearchview.h"
#include "leftupsearchdelegate.h"
#include "informationwidget.h"
#include "locationworker.h"
#include "weathermanager.h"
#include "promptwidget.h"
#include "data.h"
#include "citycollectionwidget.h"//需要

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <xcb/xcb.h>

#include <QMainWindow>
#include <QStandardPaths>
#include <QMouseEvent>
#include <QDebug>
#include <QButtonGroup>
#include <QSortFilterProxyModel>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QSystemTrayIcon>
#include <QRect>
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QTime>
#include <QTimer>
#include <QFileInfo>
#include <QLocale>
#include <QPainterPath>
#include <QMenu>
#include <QAction>
#include <QToolButton>

#include <QPushButton>
#include <QLabel>

#include <QGSettings>
#include "daemondbus.h"
#include "menumodule.h"
#include "xatom-helper.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    void createTrayIcon();
    void handleIconClicked();
    void handleIconClickedSub();

    void onRefreshMainWindowWeather();
    void onRefreshIntervalChanged(int minutes);
    void onHandelAbnormalSituation(QString abnormalText);

    void onSetForecastWeather(ForecastWeather m_forecastweather);
    void onSetObserveWeather(ObserveWeather m_observeweather);
    void onSetLifeStyle(LifeStyle m_lifestyle);

private slots:
    void on_btnMinimize_clicked();

    void on_btnCancel_clicked();

    void iconActivated(QSystemTrayIcon::ActivationReason reason);
    void closeActivated();

private:

    //*****城市轮播（替换旧的浮动cityLabel/实况天气控件）*****
    //轮播页数据结构：page0为自动定位页，pages 1..n为citylist顺序的手动城市
    struct CityPage
    {
        QString id;       // LocationID（自动定位页在定位成功前为空）
        QString name;     //城市名
        bool isAuto;      //是否为自动定位页
        QWidget *pageWidget = nullptr;
        QLabel *nameLabel = nullptr;
        QLabel *tmpLabel = nullptr;
        QLabel *unitLabel = nullptr;
        QLabel *condLabel = nullptr;
        QLabel *humLabel = nullptr;
    };

    // 用户手册功能
    DaemonDbus *mDaemonIpcDbus;

    Ui::MainWindow *ui;
    QScrollArea *m_scrollarea = nullptr;
    QWidget *m_scrollwidget = nullptr;
    LeftUpCityBtn *m_leftupcitybtn = nullptr;
    LeftUpSearchBox *m_leftupsearchbox = nullptr;
    WeatherManager* m_weatherManager = nullptr;
    PromptWidget *m_hintWidget = nullptr;
    PromptWidget *m_movieWidget = nullptr;
    LeftUpSearchView *m_searchView = nullptr;
    LeftUpSearchDelegate *m_delegate = nullptr;
    QSortFilterProxyModel* m_proxyModel = nullptr;
    QStandardItemModel *m_model = nullptr;
    LocationWorker *m_locationWorker = nullptr;
    Information *m_information;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QTimer *m_refreshweather;
    QMenu *m_mainMenu = nullptr;
    QAction *m_openAction;
    QAction *m_quitAction;

    //*****2020.12.19增加
    QPushButton *logoBtn;
    QLabel *logolb;

    QWidget *titleWid;
    QHBoxLayout *titleLayout;

    QPushButton *setBtn;
    QMenu *menu ;
//    AddCityAction *addCityAction;
    QAction *aboutAction;
    QList<QAction *> actions ;
    //*****2020.12.19增加
    menuModule *m_menu = nullptr;

    void judgeSystemLanguage();

    void onSearchBoxEdited();
    void searchCityName();

    void initControlQss();
    void initConnections();

    void setAbnormalMainWindow();

    QString convertCodeToBackgroud(int code);
    void convertCodeToTrayIcon(QString code);

    // 键盘响应事件
    void keyPressEvent(QKeyEvent *event);

//    void mousePressEvent(QMouseEvent *event);
//    void mouseReleaseEvent(QMouseEvent *event);
//    void mouseMoveEvent(QMouseEvent *event);

    bool event(QEvent *event);
    // 轮播页交互事件过滤器：左右拖动/滚轮切换城市页
    bool eventFilter(QObject *watched, QEvent *event);
    bool isPress;
    QPoint winPos;
    QPoint dragPos;

    // getstting初始化、值获取、 设置getsetting值
    void initGsetting();
    QString getCityList();
    void setCityList(QString str);
    void setThemeStyle();

    // autolocate键读写（key缺失表示旧编译schema未重装：读取按契约默认开启，写入跳过）
    bool isAutoLocateEnabled();
    void setAutoLocate(bool on);

    // 城市轮播：构建页/切换城市/更新简报
    void createCityPage(const QString &id, const QString &name, bool isAuto);
    void rebuildCityStack();
    void onCityPageActivated(int index);
    void switchToManualCity(const QString &cityId);
    void updateAutoLocatedCity(const QString &cityId, const QString &cityName);
    void setCityPageTmp(CityPage &page, const QString &tmp);
    void setCityPageName(CityPage &page, const QString &name); //自动页名始终带「·自动」标记
    void onSetCityWeatherBrief(QString weather_data);
    QString prependCurrentCityBrief(const QString &batch); //自动定位时把当前城市插到收藏简报最前
    void scheduleCityListBrief();
    QString cityNameFromId(const QString &id);

    // 按autolocate/citylist拉取当前城市天气（记录最近拉取状态避免重复拉取）
    void fetchCurrentCityWeather(bool force = false);
    // gsettings citylist/autolocate变化的统一处理
    void onWeatherDataGsettingChanged(const QString &key);

    QGSettings  *m_pWeatherData= nullptr;
    QGSettings  *m_pThemeStyle= nullptr;
    QString firstGetCityList="";

    QString nowThemeStyle;

    QLabel *cityLabel;

    //城市轮播成员：容器/页数据/简报去抖定时器/拖动手势状态/拉取去重与定位缓存
    QStackedWidget *m_cityStack = nullptr;  //城市轮播容器，左右拖动/滚轮切换城市
    QList<CityPage> m_cityPages;            //轮播各页对应的数据
    QTimer *m_cityBriefTimer = nullptr;     //收藏城市简报去抖，合并连续rebuild触发的简报请求
    bool m_swiping = false;                //轮播页拖动手势进行中
    int m_swipeStartX = 0;                   //拖动起始x坐标
    bool m_appliedFetchAuto = false;        //最近一次拉取为自动定位模式（gsettings changed去重）
    QString m_appliedFetchCityId;            //最近一次拉取的手动城市ID（gsettings changed去重）
    QString m_autoCityId;                   //最近一次自动定位到的城市（rebuild时恢复自动定位页显示）
    QString m_autoCityName;                 //最近一次自动定位到的城市中文名
    QString m_activeViewCityId;             //当前浏览页的城市ID（空=自动定位页）；重建轮播时恢复浏览位置
    ObserveWeather m_currentObserve;        //主程序当前实际城市天气（自动定位时即自动定位城市），供收藏简报插最前

    bool is_open_city_collect_widget = false;
    CityCollectionWidget *m_citycollectionwidget;
signals:
    void sendCurrentCityId(QString id);//发送到主界面更新主界面天气
    void requestShowCollCityWeather(); //显示收藏城市列表天气
    void requestSetCityWeather(QString weather_data); //发送出去显示主界面城市天气
    void updatecity();
    void requestSetCityName(QString cityName);//在搜索列表中选中一个城市后，左上角城市名需要更改



};

#endif // MAINWINDOW_H
