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
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QPainter>
#include <QFile>
#include <QHBoxLayout>
#include <QWheelEvent>

int tempNumsOfCityInSearchResultList = 0;//搜索列表中城市数量

MainWindow::MainWindow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Wayland 会话下 Motif 等 X11 窗口属性无效，必须以 FramelessWindowHint 声明无边框，
    // 否则 KWin 会给窗口加标题栏；X11 下 Qt 也会据此自动套用无边框，与点击时的 Motif 设置一致
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    // 用户手册功能
    mDaemonIpcDbus = new DaemonDbus();

    //先注册结构体，这样才能作为信号与槽的参数
    qRegisterMetaType<ObserveWeather>();
    qRegisterMetaType<ForecastWeather>();
    qRegisterMetaType<LifeStyle>();

    //设置主界面样式
    this->setFixedSize(865,910);
    this->setFocusPolicy(Qt::ClickFocus);//this->setFocusPolicy(Qt::NoFocus);//设置焦点类型
    this->setWindowTitle(tr("Weather"));
    this->setAttribute(Qt::WA_TranslucentBackground);//设置窗口背景透明
//    QPainterPath path;
//    auto rect = this->rect();
//    rect.adjust(1, 1, -1, -1);
//    path.addRoundedRect(rect, 6, 6);
//    setProperty("blurRegion", QRegion(path.toFillPolygon().toPolygon()));
//    this->setStyleSheet("QWidget{border:none;border-radius:6px;}");
    titleWid = new QWidget(this);
    titleLayout = new QHBoxLayout();
    cityLabel = new QLabel(this);
    cityLabel->setStyleSheet("font:36px;color:white;");
    cityLabel->setAlignment(Qt::AlignCenter);
    cityLabel->hide(); //已被城市轮播m_cityStack取代，不再显示

    m_menu = new menuModule(this);
    connect(m_menu,&menuModule::menuModuleClose,this,&MainWindow::closeActivated);
    connect(m_menu, &menuModule::refreshIntervalChanged, this, &MainWindow::onRefreshIntervalChanged);
    // 菜单「自动定位」开关：勾选 = 自动定位成为当前城市（轮播页0并定位）；
    // 取消 = 显式关闭自动定位，citylist[0] 成为当前城市（唯一关闭自动定位的入口）
    connect(m_menu, &menuModule::autoLocateToggled, this, [=] (bool on) {
        if (on) {
            onCityPageActivated(0); //切换到自动定位页并开始定位
        } else {
            setAutoLocate(false);
            const QString headCityId = getCityList().split(",", Qt::SkipEmptyParts).value(0);
            if (!headCityId.isEmpty()) {
                switchToManualCity(headCityId); //浏览并定位到citylist首个手动城市
            }
        }
    });
    m_menu->addCityAction->setText(tr("Add City"));
    connect(m_menu->addCityAction, &AddCityAction::requestSetCityName, this, [=] (QString cityName) {
        cityLabel->setText(cityName);//一会设置个label用于显示地名
    });



    //左上角按钮
    m_leftupcitybtn = new LeftUpCityBtn(ui->widget_normal);
    m_leftupcitybtn->hide();

    logolb = new QLabel(ui->widget_normal);
    logolb->setFixedSize(100,24);
    logolb->setText(tr("Weather"));
    logolb->setStyleSheet("font-size:14px;color:white;");

    logoBtn = new QPushButton(ui->widget_normal);
    logoBtn->setFixedSize(24,24);//重置图标大小
    logoBtn->setIcon(QIcon::fromTheme("indicator-china-weather", QIcon(":/res/control_icons/logo_24.png")));
    logoBtn->setIconSize(QSize(24,24));
    logoBtn->setFocusPolicy(Qt::NoFocus);
    //左上角搜索框
    m_leftupsearchbox = new LeftUpSearchBox(ui->widget_normal);
    //设置其他控件样式
    this->initControlQss();

    //托盘图标已下线（常驻托盘由 Plasma 小部件取代）；
    //应用本体保留，可由 Plasmoid 右键菜单「打开应用」或桌面启动器唤起
    this->createTrayIcon();
    //添加托盘菜单
    m_mainMenu = new QMenu;
//    m_mainMenu->addSeparator();
    m_openAction = new QAction(tr("Open Weather"),this);//打开麒麟天气
    m_quitAction = new QAction(tr("Exit"),this);//退出
    m_mainMenu->addAction(m_openAction);
    m_openAction->setIcon(QIcon::fromTheme(QString("indicator-china-weather"), QIcon(QString(":/res/control_icons/indicator-china-weather_min.png"))));
//    m_openAction->setIcon(QIcon(QString(":/res/control_icons/logo_24.png")) );
    m_mainMenu->addAction(m_quitAction);

    m_quitAction->setIcon(QIcon::fromTheme(QString("exit-symbolic"), QIcon(QString(":/res/control_icons/quit_normal.png"))) );
//    m_quitAction->setIcon(QIcon(QString(":/res/control_icons/quit_normal.png")));
    connect(m_openAction, &QAction::triggered, this, [=] {
        if(this->isHidden() || this->isMinimized()){
            handleIconClickedSub();
        }
        else{
            return;
        }
    });
    //connect(m_quitAction, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_quitAction, &QAction::triggered, this,&MainWindow::closeActivated);
    m_trayIcon->setContextMenu(m_mainMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);

    //主界面搜索列表
    m_searchView = new LeftUpSearchView(ui->widget_normal);
    m_delegate = new LeftUpSearchDelegate(m_searchView);
    m_proxyModel = new QSortFilterProxyModel(m_searchView);
    m_model = new QStandardItemModel();
    m_searchView->setEditTriggers(QAbstractItemView::NoEditTriggers);
//    m_searchView->move(100, 49);//2020.12.22
    m_searchView->resize(178,205);

    m_searchView->hide();
    m_searchView->move(605,37);
//    m_searchView->move(605,49);
    m_hintWidget = new PromptWidget(this);
    m_hintWidget->setIconAndText(":/res/control_icons/network_warn.png", tr("Network not connected"));//网络未连接
    m_hintWidget->move((this->width() - m_hintWidget->width())/2, 100);
    m_hintWidget->setVisible(false);

    m_locationWorker = new LocationWorker(this);
    m_weatherManager = new WeatherManager(this);
    m_weatherManager->initConnectionInfo(); //get information about network connection
    initConnections(); //建立信号与槽的连接

    onRefreshMainWindowWeather();//软件启动时先获取一次网络数据

    m_refreshweather = new QTimer(this); //定时更新主界面天气
    m_refreshweather->setTimerType(Qt::PreciseTimer);
    QObject::connect(m_refreshweather, SIGNAL(timeout()), this, SLOT(onRefreshMainWindowWeather()));
    // 刷新间隔取 gsettings refresh-interval（菜单「刷新间隔」可改，改后经信号实时生效）
    int intervalMinutes = 20;
    if (QGSettings::isSchemaInstalled(APPDATA)) {
        QGSettings setting(APPDATA);
        intervalMinutes = setting.get("refresh-interval").toInt();
        if (intervalMinutes <= 0) intervalMinutes = 20;
    }
    m_refreshweather->start(intervalMinutes * 60 * 1000);
    // 小部件配置页修改刷新间隔（写 gsettings）也实时同步本应用定时器
    if (QGSettings::isSchemaInstalled(APPDATA)) {
        auto *setting = new QGSettings(QByteArray(APPDATA), QByteArray(), this);
        connect(setting, &QGSettings::changed, this, [this](const QString &key) {
            if (key != QLatin1String("refresh-interval") || !m_refreshweather) {
                return;
            }
            QGSettings s(APPDATA);
            const int v = s.get("refresh-interval").toInt();
            if (v > 0) {
                m_refreshweather->start(v * 60 * 1000);
            }
        });
    }
    // 收藏城市简报去抖：短时间内连续rebuild轮播只触发一批简报请求
    m_cityBriefTimer = new QTimer(this);
    m_cityBriefTimer->setSingleShot(true);
    m_cityBriefTimer->setInterval(300);
    connect(m_cityBriefTimer, &QTimer::timeout, this, [this] () {
        if (m_weatherManager) {
            m_weatherManager->startCityListBrief();
        }
    });

    initGsetting();//初始化Gsetting
    rebuildCityStack();//构建初始城市轮播（置于initGsetting之后，保证gsettings可读）
}

MainWindow::~MainWindow()
{
    delete ui;
}

// 实现键盘响应
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // F1快捷键打开用户手册
    if (event->key() == Qt::Key_F1) {
        if (!mDaemonIpcDbus->daemonIsNotRunning()){
            //F1快捷键打开用户手册，如kylin-recorder
            //由于是小工具类，下面的showGuide参数要填写"tools/indicator-china-weather"
            mDaemonIpcDbus->showGuide("tools/indicator-china-weather");
        }
    }
}

//非中文系统环境无法启动
void MainWindow::judgeSystemLanguage()
{
    QLocale locale;
    //获取系统语言环境
    if( locale.language() == QLocale::Chinese ) {
        qDebug() << "Chinese system";
        return;
    } else {
        qDebug() << "Non-chinese system";
        exit(0);
    }
}

//初始化各控件样式
void MainWindow::initControlQss()
{
    m_leftupsearchbox->setFixedWidth(150);
    titleLayout->addSpacing(4);
    titleLayout->addWidget(logoBtn);//麒麟天气logo
    titleLayout->addSpacing(4);
    titleLayout->addWidget(logolb);//麒麟天气标签
    titleLayout->addStretch();//添加伸缩
    titleLayout->addWidget(m_leftupsearchbox);//麒麟天气搜索栏
    titleLayout->addSpacing(4);
    titleLayout->addWidget(m_menu->menuButton);//设置按钮
    titleLayout->addWidget(ui->btnMinimize);
    titleLayout->addWidget(ui->btnCancel);
    titleLayout->setSpacing(4);
    titleLayout->setContentsMargins(4, 4, 4, 4);
    titleWid->setLayout(titleLayout);
    titleWid->setFixedWidth(865);
    titleWid->move(0,0);
    logoBtn->setStyleSheet("QPushButton{border:0px;border-radius:4px;background:transparent;}"
                             "QPushButton::hover{border:0px;border-radius:4px;background:transparent;}"
                             "QPushButton::pressed{border:0px;border-radius:4px;background:transparent;}");
//    ui->centralwidget->setStyleSheet("#centralwidget{border:1px solid rgba(38,38,38,0.15);border-radius:6px;background:rgba(19,19,20,0);}");
    ui->centralwidget->setStyleSheet("#centralwidget{color:white;background-image:url(':/res/background/weather-clear.png');background-repeat:no-repeat;}");
    ui->centralwidget->move(0,0);
    ui->centralwidget->setFixedSize(865,910);
    ui->btnMinimize->setIcon(QIcon::fromTheme(":/res/control_icons/dark-window-min.svg"));
    ui->btnMinimize->setFixedSize(30,30);
    ui->btnMinimize->setToolTip(tr("minimize"));
    ui->btnMinimize->setStyleSheet("QPushButton{border:0px;border-radius:4px;background:transparent;}"
                               "QPushButton:Hover{border:0px;border-radius:4px;background:transparent;background-color:rgba(0,0,0,0.1);}"
                               "QPushButton:Pressed{border:0px;border-radius:4px;background:transparent;background-color:rgba(0,0,0,0.15);}");
    ui->btnMinimize->setFocusPolicy(Qt::NoFocus);//设置焦点类型
    ui->btnCancel->setToolTip(tr("close"));
    ui->btnCancel->setIcon(QIcon::fromTheme(":/res/control_icons/dark-window-close.svg"));
    ui->btnCancel->setFixedSize(30,30);
    ui->btnCancel->setStyleSheet("QPushButton{border:0px;border-radius:4px;background:transparent;}"
                               "QPushButton:Hover{border:0px;border-radius:4px;background:transparent;background-color:#F86457;}"
                               "QPushButton:Pressed{border:0px;border-radius:4px;background:transparent;background-color:#E44C50;}");
    ui->btnCancel->setFocusPolicy(Qt::NoFocus);//设置焦点类型

    ui->lbCurrTmp->setStyleSheet("QLabel{border:none;background:transparent;font-size:110px;font-weight:300;color:rgba(255,255,255,1);line-height:100px;}");
    ui->lbCurrTmp->setAlignment(Qt::AlignCenter);

    ui->lbCurrTmpUnit->setStyleSheet("QLabel{border:none;background:transparent;font-size:24px;color:rgba(255,255,255,1);line-height:14px;}");
    ui->lbCurrTmpUnit->setAlignment(Qt::AlignCenter);

    ui->lbCurrWea->setStyleSheet("QLabel{border:none;background:transparent;font-size:20px;color:rgba(255,255,255,1);line-height:14px;}");

    ui->lbCurrHum->setStyleSheet("QLabel{border:none;background:transparent;font-size:14px;color:rgba(255,255,255,1);line-height:14px;}");
    ui->lbCurrHum->setAlignment(Qt::AlignCenter);

    m_scrollarea = new QScrollArea(ui->centralwidget);
    m_scrollarea->setFocusPolicy(Qt::NoFocus);//设置焦点类型
    m_scrollarea->setFixedSize(858, 605); // 内容高约 600（16 项生活指数），含边距 605 一屏完整显示
    m_scrollarea->move(4, 290);
    m_scrollarea->setStyleSheet("QScrollArea{border:none;border-radius:4px;background:transparent;color:rgba(255,255,255,1);}");

    m_scrollarea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollarea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);


    m_scrollarea->verticalScrollBar()->setProperty("drawScrollBarGroove",false);
//    m_scrollarea->verticalScrollBar()->setStyleSheet("QScrollBar:vertical{margin:0px 2px 0px 2px;width:13px;background:rgba(255,255,255,0);border-radius:6px;}"
//                                                     "QScrollBar::up-arrow:vertical{height:0px;}"
//                                                     "QScrollBar::sub-line:vertical{border:0px solid;height:0px}"
//                                                     "QScrollBar::sub-page:vertical{background:transparent;}"
//                                                     "QScrollBar::handle:vertical{width:6px;background:rgba(255,255,255,0.2);border-radius:3px;}"
//                                                     "QScrollBar::handle:vertical:hover{width:6px;background:rgba(255,255,255,0.2);border-radius:3px;}"
//                                                     "QScrollBar::handle:vertical:pressed{width:6px;background:rgba(255,255,255,0.2);border-radius:3px;}"
//                                                     "QScrollBar::add-page:vertical{background:transparent;}"
//                                                     "QScrollBar::add-line:vertical{border:0px solid;height:0px}"
//                                                     "QScrollBar::down-arrow:vertical{height:0px;}");

    m_scrollwidget = new QWidget(m_scrollarea);
    m_scrollwidget->resize(858, 600); // 与 informationwidget.ui 高度一致，16 项生活指数内容高约 587，600 一屏容纳
    m_scrollwidget->setStyleSheet("QWidget{border:none;border-radius:4px;background:transparent;color:rgba(255,255,255,1);}");
//    m_scrollwidget->setStyleSheet("QWidget{border:2px;border-radius:4px;background:transparent;color:rgba(255,255,255,1);}");
    m_scrollarea->setWidget(m_scrollwidget);
    m_scrollwidget->move(0, 0);
    m_information = new Information(m_scrollwidget);
    m_information->move(0,0);

    // 旧的浮动实况天气控件被城市轮播取代，保留在.ui中但不再显示
    ui->lbCurrTmp->setVisible(false);
    ui->lbCurrTmpUnit->setVisible(false);
    ui->lbCurrWea->setVisible(false);
    ui->lbCurrHum->setVisible(false);

    // 城市轮播：覆盖实况天气区域的透明容器（标题栏之下、七天预报区之上），
    // 须在搜索下拉框m_searchView创建之前创建，保证下拉框浮于其上、不被遮挡点击
    m_cityStack = new QStackedWidget(ui->widget_normal);
    m_cityStack->setGeometry(0, 58, 865, 230);
    m_cityStack->setStyleSheet("QStackedWidget{border:none;background:transparent;}");
    m_cityStack->installEventFilter(this);
}

void MainWindow::initConnections()
{
    connect(m_leftupsearchbox, &LeftUpSearchBox::textChanged, this, [this] () {
        if (m_leftupsearchbox->text().size() == 0){
            m_searchView->hide();
        }else{
            m_searchView->show();
            onSearchBoxEdited();
        }
    });

    connect(m_leftupsearchbox,&LeftUpSearchBox::lineEditKeyEvent,m_searchView,&LeftUpSearchView::dealSearchBoxKeyPress);
    //1*****addCityAction替换原来的m_leftupcitybtn*****
    connect(m_searchView, SIGNAL(requestSetCityName(QString)), m_menu->addCityAction, SIGNAL(requestSetCityName(QString)) );
//    connect(m_searchView, SIGNAL(requestSetCityName(QString)), m_leftupcitybtn, SIGNAL(requestSetCityName(QString)) );

    connect(m_searchView, &LeftUpSearchView::requestSetNewCityWeather, this, [=] (QString id) {
        // 搜索列表选中城市：关闭自动定位，城市移至citylist首位并拉取天气
        switchToManualCity(id);
    });
    //2*****addCityAction替换原来的m_leftupcitybtn*****
    connect(m_menu->addCityAction,&AddCityAction::sendCurrentCityId, this, [=] (QString id) {
        if(this->isHidden() || this->isMinimized()){
            handleIconClickedSub(); //显示在屏幕中央
        }
        // 收藏城市窗口选中城市：关闭自动定位，城市移至citylist首位并拉取天气
        switchToManualCity(id);
    });
//    connect(m_leftupcitybtn, &LeftUpCityBtn::sendCurrentCityId, this, [=] (QString id) {
//        if(this->isHidden()){
//            handleIconClickedSub(); //显示在屏幕中央
//        }
//        m_weatherManager->startGetTheWeatherData(id);
//    });

    //3*****addCityAction替换原来的m_leftupcitybtn*****
    connect(m_menu->addCityAction, SIGNAL(requestShowCollCityWeather()), m_weatherManager, SIGNAL(requestShowCollCityWeather()));
//    connect(m_leftupcitybtn, SIGNAL(requestShowCollCityWeather()), m_weatherManager, SIGNAL(requestShowCollCityWeather()));

    //同步主界面和收藏界面当前城市信息
    //4*****addCityAction替换原来的m_leftupcitybtn*****
    connect(this,&MainWindow::updatecity,m_menu->addCityAction,&AddCityAction ::updatecity);
//    connect(this,&MainWindow::updatecity,m_leftupcitybtn,&LeftUpCityBtn ::updatecity);

    //获取传过来的收藏城市的天气数据，并传给显示收藏城市窗口（经本窗口中转以插当前城市）
    connect(m_weatherManager, &WeatherManager::requestSetCityWeather, this, [=] (const QString &weather_data) {
        // 城市轮播页简报：更新各城市页温度/天气/城市名
        onSetCityWeatherBrief(weather_data);
        // 收藏城市对话框：自动定位开启时把实际当前城市（自动定位城市）插到简报最前，
        // 使 strList[0]=当前城市、citylist 各城市按收藏城市显示；关闭自动定位时原样转发
        const QString enriched = isAutoLocateEnabled() ? prependCurrentCityBrief(weather_data) : weather_data;
        emit m_menu->addCityAction->requestSetCityWeather(enriched);
    });
    //没有网络的时候发送信号到收藏城市界面阻断动作进行
    connect(m_weatherManager,&WeatherManager::noNetWork,m_menu->addCityAction,&AddCityAction::noNetWork);
    //收到信号带来的数据时，更新主界面天气数据
    //（托盘图标已下线，不再随天气数据显示到托盘）
    connect(m_weatherManager, &WeatherManager::requestSetObserveWeather, this, [=] (ObserveWeather observerdata) {
        this->onSetObserveWeather(observerdata);
    });

    connect(m_weatherManager, &WeatherManager::requestSetForecastWeather, this, [=] (ForecastWeather forecastweather) {

        this->onSetForecastWeather(forecastweather);
    });

    connect(m_weatherManager, &WeatherManager::requestSetLifeStyle, this, [=] (LifeStyle lifestyle) {

        this->onSetLifeStyle(lifestyle);
    });

    //获取天气数据时发生了异常
    connect(m_weatherManager, &WeatherManager::responseFailure, this, [=] (int code) {
        onHandelAbnormalSituation("Get weather data failed!");

        m_hintWidget->setVisible(true);

        if (code == 0) {
            m_hintWidget->setIconAndText(":/res/control_icons/network_warn.png", tr("Incorrect access address"));//访问地址异常
        } else {
            m_hintWidget->setIconAndText(":/res/control_icons/network_warn.png", QString(tr("Network error code:%1")).arg(QString::number(code)));//网络错误代码
        }
    });

    //根据获取到网络探测的结果分别处理
    connect(m_weatherManager, &WeatherManager::nofityNetworkStatus, this, [=] (const QString &status) {
        if (status == "OK") {
            // 网络可用时按当前模式仅拉取当前城市：自动定位模式只开始定位（定位成功后再拉取），
            // 手动模式只拉取citylist[0]（为空时跳过），避免并发双请求的竞态
            fetchCurrentCityWeather(true);

        } else {
            if (status == "Fail") {
                onHandelAbnormalSituation("Without wired Carrier");
            } else {
                onHandelAbnormalSituation("Unable to access the Internet");
            }
            emit m_weatherManager->responseFailure(404);
        }
    });

    //自动定位成功后，更新城市轮播的自动定位页城市，并开始获取天气数据
    connect(m_weatherManager, &WeatherManager::requestAutoLocationData, this, [=] (const CitySettingData &info, bool success) {
        if (success) {
            //自动定位城市成功后，更新自动定位页显示，然后获取天气数据
            updateAutoLocatedCity(info.id, info.name);
            m_weatherManager->startGetTheWeatherData(info.id);
        } else {
            // 自动定位失败后回退拉取citylist首个手动城市，保证主界面仍有数据可显示
            const QString fallbackCityId = getCityList().split(",", Qt::SkipEmptyParts).value(0);
            if (!fallbackCityId.isEmpty()) {
                m_weatherManager->startGetTheWeatherData(fallbackCityId);
            }
        }
    });

    //网络连接由无到有时触发天气界面更新
    connect(m_weatherManager, &WeatherManager::newNetworkConnectionCreated, this, [=] () {
        qDebug()<<"需要更新天气界面";
        onRefreshMainWindowWeather();
    });

    connect(m_hintWidget, &PromptWidget::requestRetryAccessWeather, this, [=] () {
        qDebug()<<"debug: retry to refreah mainwindow weather";
        onRefreshMainWindowWeather();
    });
}

//托盘图标已下线：不再在系统托盘常驻显示。
//保留 QSystemTrayIcon 对象仅为兼容既有引用（菜单/图标设置等），始终不可见
void MainWindow::createTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setToolTip(QString(tr("Weather")));
//    m_trayIcon->setIcon(QIcon::fromTheme(QString("999"), QIcon(QString(":/res/weather_icons/white/999.png"))) );
    m_trayIcon->setVisible(false);
    m_trayIcon->hide();
}

//托盘图标被点击
void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch(reason){
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::MiddleClick:
        if(this->isHidden() || this->isMinimized()){
            //this->showNormal();
            //handleIconClicked(); //靠近任务栏显示
            handleIconClickedSub(); //显示在屏幕中央
        }else{
            this->showMinimized();
        }
        break;
    case QSystemTrayIcon::DoubleClick:
        this->showMinimized();
        break;
    case QSystemTrayIcon::Context:
        //右键点击托盘图标弹出菜单
        //showTrayIconMenu(); //显示右键菜单
        m_mainMenu->show();
        break;
    default:
        break;
    }
}
void MainWindow::closeActivated()
{
    //托盘退出默认关闭开机自启
    QString autostart=QStandardPaths::standardLocations(QStandardPaths::HomeLocation)[0]+"/.config/autostart/indicator-china-weather.desktop";
    QFile file(autostart);
    if(!file.exists())
    {
        QString path="/etc/xdg/autostart/indicator-china-weather.desktop";
        QFileInfo file2(path);
        if(!file2.exists())
        {
            qDebug()<<"/etc/xdg/autostart/目录下无麒麟天气快捷方式";
        }
        else
        {
            QFile::copy(path,autostart);
            if(file.exists())
            {
                if(file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
                {
                    file.write("\nHidden=true\n");
                    file.close();
                }
            }
        }
    }

    qApp->quit();
    return;
}
//处理点击托盘图标事件
void MainWindow::handleIconClicked(){
    qDebug()<<"MainWindow::handleIconClicked";
    // Qt6 移除了 QDesktopWidget，改用 QScreen 获取光标所在屏幕几何
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    QRect desk_rect = screen->geometry();
    int desk_x = desk_rect.width();
    int desk_y = desk_rect.height();
    int x = this->width();
    int y = this->height();
//    this->move(desk_x/2-x/2+desk_rect.left(),desk_y/2-y/2+desk_rect.top());
    this->showNormal();
    this->raise();
    this->activateWindow();
}
//void MainWindow::handleIconClicked()
//{
//    QRect availableGeometry = qApp->primaryScreen()->availableGeometry();
//    QRect screenGeometry = qApp->primaryScreen()->geometry();

//    QDesktopWidget* desktopWidget = QApplication::desktop();
//    //QRect deskMainRect = desktopWidget->availableGeometry(0);//获取可用桌面大小
//    QRect screenMainRect = desktopWidget->screenGeometry(0);//获取设备屏幕大小
//    //QRect deskDupRect = desktopWidget->availableGeometry(1);//获取可用桌面大小
//    //QRect screenDupRect = desktopWidget->screenGeometry(1);//获取设备屏幕大小

//    //qDebug()<<"screenGeometry: "<<screenGeometry;
//    //qDebug()<<"availableGeometry: "<<availableGeometry;
//    //qDebug()<<"deskMainRect: "<<deskMainRect;
//    //qDebug()<<"screenMainRect: "<<screenMainRect;
//    //qDebug()<<"deskDupRect: "<<deskDupRect;
//    //qDebug()<<"screenDupRect: "<<screenDupRect;

//    int m = m_weatherManager->getTaskBarHeight("height");
//    int n = m_weatherManager->getTaskBarPos("position");
//    int d = 2; //窗口边沿到任务栏距离

//    if (screenGeometry.width() == availableGeometry.width() && screenGeometry.height() == availableGeometry.height()){
//        if(n == 0){
//            //任务栏在下侧
//            this->move(availableGeometry.x() + availableGeometry.width() - this->width(), screenMainRect.y() + availableGeometry.height() - this->height() - m - d);
//        }else if(n == 1){
//            //任务栏在上侧
//            this->move(availableGeometry.x() + availableGeometry.width() - this->width(), screenMainRect.y() + screenGeometry.height() - availableGeometry.height() + m + d);
//        } else if (n == 2){
//            //任务栏在左侧
//            if (screenGeometry.x() == 0){//主屏在左侧
//                this->move(m + d, screenMainRect.y() + screenMainRect.height() - this->height());
//            }else{//主屏在右侧
//                this->move(screenMainRect.x() + m + d, screenMainRect.y() + screenMainRect.height() - this->height());
//            }
//        } else if (n == 3){
//            //任务栏在右侧
//            if (screenGeometry.x() == 0){//主屏在左侧
//                this->move(screenMainRect.width() - this->width() - m - d, screenMainRect.y() + screenMainRect.height() - this->height());
//            }else{//主屏在右侧
//                this->move(screenMainRect.x() + screenMainRect.width() - this->width() - m - d, screenMainRect.y() + screenMainRect.height() - this->height());
//            }
//        }
//    } else if (availableGeometry.x() == screenGeometry.x() && availableGeometry.y() == screenGeometry.y()) { //panel in right or bottom
//        this->move(availableGeometry.x() + availableGeometry.width() - this->width() - d, availableGeometry.y() + availableGeometry.height() - this->height() - d);
//    } else {
//        if (availableGeometry.x() > 0) {//panel in left
//            this->move(availableGeometry.x() + d, availableGeometry.y() + availableGeometry.height()  - this->height());
//        }
//        else {//panel in top
//            this->move(availableGeometry.x() + availableGeometry.width() - this->width(), availableGeometry.y() + d);
//        }
//    }

//    this->showNormal();
//    this->raise();
//    this->activateWindow();
//}

void MainWindow::handleIconClickedSub()
{
    qDebug()<<"MainWindow::handleIconClickedSub";
//    QRect availableGeometry = qApp->primaryScreen()->availableGeometry();
//    this->move((availableGeometry.width() - this->width())/2, (availableGeometry.height() - this->height())/2);

//    this->showNormal();
//    this->raise();
//    this->activateWindow();

    MotifWmHints hints;
    hints.flags = MWM_HINTS_FUNCTIONS|MWM_HINTS_DECORATIONS;
    hints.functions = MWM_FUNC_ALL;
    hints.decorations = MWM_DECOR_BORDER;
    XAtomHelper::getInstance()->setWindowMotifHint(this->winId(), hints);

    if(this->m_menu && this->m_menu->aboutWindow){
        this->m_menu->aboutWindow->close();
    }
    this->showNormal();
    this->raise();
    this->activateWindow();
    // show 之后再定位：此时窗口尺寸已按 setFixedSize 生效，用窗口实际尺寸
    // 居中于光标所在屏幕的可用区域，并约束底部不超出屏幕（避免内容被裁）
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect desk = screen->availableGeometry();
        int x = desk.left() + (desk.width() - this->width()) / 2;
        int y = desk.top() + (desk.height() - this->height()) / 2;
        y = qMax(desk.top(), qMin(y, desk.top() + desk.height() - this->height()));
        this->move(x, y);
    }

}

//定时更新主界面天气
void MainWindow::onRefreshMainWindowWeather()
{
    //开始测试网络情况
    m_weatherManager->startTestNetwork();
}

//处理因网络异常等未获取到天气数据的情况
void MainWindow::onHandelAbnormalSituation(QString abnormalText){
    qDebug()<<"debug: network state: "<<abnormalText;
    setAbnormalMainWindow();
}

//处理异常时的主界面显示
void MainWindow::setAbnormalMainWindow()
{
//    m_trayIcon->setIcon(QIcon::fromTheme(QString("999"), QIcon(QString(":/res/weather_icons/white/999.png"))) );
    m_trayIcon->hide();
//    m_openAction->setIcon(QIcon::fromTheme(QString("999"), QIcon(QString(":/res/weather_icons/white/999.png"))) );
    ui->lbCurrTmp->setText("");
    ui->lbCurrTmpUnit->setText("");
    ui->lbCurrWea->setText("");
    ui->lbCurrHum->setText("");
    cityLabel->setText("");//baibai
    //同时清空城市轮播各页的简报，与旧控件异常时清空显示的行为保持一致
    for (CityPage &page : m_cityPages) {
        setCityPageTmp(page, QString());
        page.condLabel->setText("");
        page.humLabel->setText("");
    }

    ForecastWeather abnormalForecastweather;
    for (int i=0; i<7; i++) {
        abnormalForecastweather.uv_index = "N/A";
        abnormalForecastweather.wind_spd = "N/A";
        abnormalForecastweather.sr = "N/A";
        abnormalForecastweather.wind_sc = "N/A";
        abnormalForecastweather.ms = "N/A";
        abnormalForecastweather.cond_txt_d = "N/A";
        abnormalForecastweather.vis = "N/A";
        abnormalForecastweather.ss = "N/A";
        abnormalForecastweather.hum = "N/A";
        abnormalForecastweather.cond_txt_n = "N/A";
        abnormalForecastweather.pop = "N/A";
        abnormalForecastweather.wind_deg = "N/A";
        abnormalForecastweather.pcpn = "N/A";
        abnormalForecastweather.wind_dir = "N/A ";
        abnormalForecastweather.cond_code_d = "999";
        abnormalForecastweather.mr = "N/A";
        abnormalForecastweather.date = "N/A";
        abnormalForecastweather.tmp_max = "N/A";
        abnormalForecastweather.cond_code_n = "999";
        abnormalForecastweather.pres = "N/A";
        abnormalForecastweather.tmp_min = "N/A";

        onSetForecastWeather(abnormalForecastweather);
    }

    LifeStyle abnormalLifestyle;
    abnormalLifestyle.drsg_brf = "N/A";
    abnormalLifestyle.flu_brf = "N/A";
    abnormalLifestyle.uv_brf = "N/A";
    abnormalLifestyle.cw_brf = "N/A";
    abnormalLifestyle.air_brf = "N/A";
    abnormalLifestyle.sport_brf = "N/A";
    onSetLifeStyle(abnormalLifestyle);
}

//更新主界面搜索列表
void MainWindow::onSearchBoxEdited()
{
    searchCityName();

    m_searchView->setItemDelegate(m_delegate); //为视图设置委托
    m_searchView->setSpacing(1); //为视图设置控件间距
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterRole(Qt::UserRole);
    m_proxyModel->setDynamicSortFilter(true);
    m_searchView->setModel(m_proxyModel); //为委托设置模型
    m_searchView->setViewMode(QListView::IconMode); //设置Item图标显示
    m_searchView->setDragEnabled(false); //控件不允许拖动
}
void MainWindow::searchCityName()
{
    const QString inputText = m_leftupsearchbox->text().trimmed().toLower();

    QList<LocationData> searchResultList;
    searchResultList = m_locationWorker->exactMatchCity(inputText);

    if (searchResultList.isEmpty() || inputText.isEmpty()) {
        m_model->clear();//清空上一次搜索结果
//        m_searchView->resize(178,55);//只保留一行大小

        // 更改没有搜索结果时下拉框的长度/宽度
        m_searchView->resize(151,55);//只保留一行大小
        //m_searchView->hide();//或一行不保留，无提示
        QStandardItem *Item = new QStandardItem;

        ItemData itemData;
        itemData.cityName = QString("无匹配城市");//无匹配搜索结果时，提示用户无结果

//        itemData.cityProvince = QString("请重新输入");
        Item->setData(QVariant::fromValue(itemData),Qt::UserRole); //整体存取

        m_model->appendRow(Item); //追加Item
        m_searchView->setAttribute(Qt::WA_TransparentForMouseEvents, true);//无结果时点击搜索栏无效果
        qDebug()<<"fail to search city information";
    }
    else {
        delete m_model;
        m_model = new QStandardItemModel();
        // 改成全局变量
        tempNumsOfCityInSearchResultList = 0;//搜索列表中城市数量
        foreach(LocationData m_locationdata, searchResultList){
            tempNumsOfCityInSearchResultList++;//计数
            QStandardItem *Item = new QStandardItem;

            ItemData itemData;

            itemData.cityId = QString(m_locationdata.id);
            itemData.cityName = QString(m_locationdata.city);
            itemData.cityProvince = QString(m_locationdata.province);
            Item->setData(QVariant::fromValue(itemData),Qt::UserRole); //整体存取

            m_model->appendRow(Item); //追加Item
            m_searchView->setAttribute(Qt::WA_TransparentForMouseEvents,false);//有结果时点击搜索栏有效果
        }
        if ( tempNumsOfCityInSearchResultList > 4 )//默认显示4行，结果数大于4时，按默认大小显示
        {
//            m_searchView->resize(178,205);
//            m_searchView->resize(178,315);
            // 整个搜索下拉框的宽度
            m_searchView->resize(151,210);
//            m_searchView->resize(151,310);
        }
        else//结果小于4时，按城市数量显示行数
        {
//            m_searchView->resize(178,tempNumsOfCityInSearchResultList * 50 + 2);
            m_searchView->resize(151,tempNumsOfCityInSearchResultList * 50 + 8);
        }
    }
}

//设置生活指数
void MainWindow::onSetLifeStyle(LifeStyle m_lifestyle)
{
    m_information->onSetLifeStyle(m_lifestyle);
}

//设置预报天气
void MainWindow::onSetForecastWeather(ForecastWeather m_forecastweather)
{
    m_information->onSetForecastWeather(m_forecastweather);
}

//设置实况天气显示
void MainWindow::onSetObserveWeather(ObserveWeather m_observeweather)
{
    m_currentObserve = m_observeweather; //记录当前实际城市天气，供收藏简报插最前/当前城市卡片
    if (m_observeweather.tmp != "") {
        m_hintWidget->setVisible(false);
    }

    //主界面UI变化,控件间的距离自适应
    m_searchView->hide();
    m_leftupsearchbox->setText("");
    int m_size = m_observeweather.city.size();
//    m_leftupsearchbox->move(100 + 15*(m_size-2), 18);
//    m_searchView->move(605 + 15*(m_size-2), 49);//2020.12.22修改搜索视图位置

    int code  = m_observeweather.cond_code.toInt();
    convertCodeToTrayIcon(m_observeweather.cond_code);
    QString picStr = convertCodeToBackgroud(code);
    QString picQss = "#centralwidget{color:white;background-image:url(" + picStr + ");background-repeat:no-repeat;}";
    ui->centralwidget->setStyleSheet(picQss);

    QString strHum = "湿度 " + m_observeweather.hum + "%   " + m_observeweather.wind_dir + " " + m_observeweather.wind_sc + "级";//  Humidity-湿度

    // 更新城市轮播中匹配页的简报（城市名/温度/天气/湿度），旧的浮动控件已被轮播页取代
    for (CityPage &page : m_cityPages) {
        if (page.id != m_observeweather.id) {
            continue;
        }
        if (!m_observeweather.city.isEmpty()) {
            setCityPageName(page, m_observeweather.city);
        }
        setCityPageTmp(page, m_observeweather.tmp);
        page.condLabel->setText(m_observeweather.cond_txt);
        page.humLabel->setText(strHum);//湿度和风级标签
    }

    if (m_observeweather.city != "") {
//        emit m_leftupcitybtn ->requestSetCityName(m_observeweather.city); //更新左上角按钮显示的城市
        emit m_menu->addCityAction->requestSetCityName(m_observeweather.city); //更新中间Label显示的城市
        //收藏对话框的「当前城市」卡片同步为实际当前城市（自动定位时即自动定位城市），
        //与主窗口保持一致，覆盖其仅以citylist[0]为当前城市的旧逻辑
        m_menu->addCityAction->setCurrentCityWeather(m_observeweather);
    }

    // 自动定位模式下当前城市由定位决定，不写入citylist；仅在手动模式把当前城市持久化到citylist[0]
    if (isAutoLocateEnabled()) {
        return;
    }

    //更新保存城市列表文件china-weather-data
    QStringList readCityIdList = getCityList().split(",");

    //若收藏城市列表中已经有搜索的新城市，则去掉。减1因为readCityIdList最后一项为空
//    for (int i=1; i<readCityIdList.size()-1; i++) {
//        QString str = readCityIdList.at(i);
//        if (str == m_observeweather.id) {
//            readCityIdList.removeOne(m_observeweather.id);
//            break;
//        }
//    }

    //将上一个当前城市放入收藏列表中
//    QString oldCurrentCityId = readCityIdList.at(0);
//    if  (oldCurrentCityId != m_observeweather.id) {
//        if (readCityIdList.size() == 10) {
//            //收藏列表已经有8个城市，替换最后一个
//            readCityIdList.replace(8, oldCurrentCityId);
//        }
//        if (readCityIdList.size() <= 9) {
//            //收藏列表少于8个城市，将上一个当前城市放入末尾
//            readCityIdList.append(oldCurrentCityId);
//        }
//    }
    //将列表中第一个城市设置为当前搜索的新城市
    readCityIdList.replace(0, m_observeweather.id);

    QString writeCityId = "";
    foreach (QString strCity, readCityIdList) {
        if (strCity != "") {
            writeCityId.append(strCity);
            writeCityId.append(",");
        }
    }
    setCityList(writeCityId);
}

//根据天气情况设置托盘图标
void MainWindow::convertCodeToTrayIcon(QString code)
{
    if (code.isEmpty() || code == "-") {
//        onRefreshMainWindowWeather();
//       m_trayIcon->setIcon(QIcon::fromTheme(QString("999"), QIcon(QString(":/res/weather_icons/white/999.png"))) );
//        m_openAction->setIcon(QIcon::fromTheme(QString("999"), QIcon(QString(":/res/weather_icons/white/999.png"))));
        m_trayIcon->hide();
        return;
    }

    //QString strIcon = QString(":/res/weather_icons/white/%1.png").arg(code);
    //m_trayIcon->setIcon(QIcon(strIcon));
    m_trayIcon->setIcon(QIcon::fromTheme(QString("%1").arg(code), QIcon(QString(":/res/weather_icons/white/%1.png").arg(code))) );
//    m_openAction->setIcon(QIcon::fromTheme(QString("%1").arg(code), QIcon(QString(":/res/weather_icons/white/%1.png").arg(code))));
}

//根据天气情况设置主界面背景贴图
QString MainWindow::convertCodeToBackgroud(int code)
{
    if (code == 100 || code == 900) {
        QTime current_time = QTime::currentTime();
        int hour = current_time.hour();
        if (hour>=6 && hour<= 18){
            return ":/res/background/weather-clear.png";
        } else {
            return ":/res/background/weather-clear-night.png";
        }
    }
    else if (code <= 103 && code >= 101) {
        return ":/res/background/weather-few-clouds.png";
    }
    else if (code == 104 || code == 901) {
        return ":/res/background/weather-overcast.png";
    }
    else if (code <= 204 && code >= 200) {
        return ":/res/background/weather-clear.png";
    }
    else if (code <= 213 && code >= 205) {
        return ":/res/background/weather-overcast.png";
    }
    else if (code <= 399 && code >= 300) {
        return ":/res/background/weather-rain.png";
    }
    else if (code <= 499 && code >= 400) {
        return ":/res/background/weather-snow.png";
    }
    else if (code <= 502 && code >= 500) {
        return ":/res/background/weather-fog.png";
    }
    else if (code <= 508 && code >= 503) {
        return ":/res/background/weather-sandstorm.png";
    }
    else if (code <= 515 && code >= 509) {
        return ":/res/background/weather-fog.png";
    }
    else {
        QTime current_time = QTime::currentTime();
        int hour = current_time.hour();
        if (hour>=6 && hour<= 18){
            return ":/res/background/weather-clear.png";
        } else {
            return ":/res/background/weather-clear-night.png";
        }
    }
}

void MainWindow::on_btnMinimize_clicked()
{
    QWidget::showMinimized();
    m_leftupsearchbox->clear();
    //this->setVisible(false);
}

void MainWindow::on_btnCancel_clicked()
{
    m_leftupsearchbox->clear();
    this->setVisible(false);
}

//void MainWindow::mousePressEvent(QMouseEvent *event){
//    if(event->button() == Qt::LeftButton){
//        this->isPress = true;
//        this->winPos = this->pos();
//        this->dragPos = event->globalPos();
//        event->accept();
//    }
//}

//void MainWindow::mouseReleaseEvent(QMouseEvent *event){
//    this->isPress = false;
//    this->setCursor(Qt::ArrowCursor);
//    return ;
//}

//void MainWindow::mouseMoveEvent(QMouseEvent *event){
//    if(this->isPress){
//        this->move(this->winPos - (this->dragPos - event->globalPos()));
//        this->setCursor(Qt::ClosedHandCursor);
//        event->accept();
//    }
//}
//鼠标点击外部，收起搜索列表
bool MainWindow::event(QEvent *event)
{
//    if(m_searchView == nullptr)return QWidget::event(event);
    if (event->type() == QEvent::MouseButtonPress)
    {
        m_leftupsearchbox->clear();
    }
    return QWidget::event(event);
}

void MainWindow::initGsetting()
{
    if(QGSettings::isSchemaInstalled(CHINAWEATHERDATA))
    {
        m_pWeatherData = new QGSettings(CHINAWEATHERDATA);
        firstGetCityList=m_pWeatherData->get("citylist").toString();
        //监听citylist/autolocate键的value是否发生了变化（轮播切换/菜单/收藏窗口/小部件侧都可能写入）：
        //统一走onWeatherDataGsettingChanged重建轮播并按需拉取当前城市天气（内部去重避免重复拉取）
        connect(m_pWeatherData, &QGSettings::changed, this, [=] (const QString &key)
        {
            onWeatherDataGsettingChanged(key);
        });
    }
    if(QGSettings::isSchemaInstalled(FITTHEMEWINDOW))
    {
        m_pThemeStyle = new QGSettings(FITTHEMEWINDOW);
        connect(m_pThemeStyle,&QGSettings::changed,this, [=] (const QString &key)
        {
            if(key == "styleName")
            {

                    setThemeStyle();
            }
        });
    }
    setThemeStyle();
    return;

}
void MainWindow::setThemeStyle()
{
  if(m_pThemeStyle==nullptr)return;
  nowThemeStyle = m_pThemeStyle->get("styleName").toString();

  m_leftupsearchbox->ThemeLeftUpSearchBox(nowThemeStyle);
  m_searchView->ThemeLeftUpSearchView(nowThemeStyle);
//    if("ukui-dark" == nowThemeStyle|| "ukui-black" == nowThemeStyle)
//    {
//        m_mainMenu ->setStyleSheet("QMenu {border:1px solid rgba(207,207,207,1);border-radius:4px;background-color:rgba(255,255,255,0.6);margin:1px;padding:5px;}"
//                          "QMenu::item {color: rgba(0,0,0,0.6);}"
//                          "QMenu::item:selected {border-radius:4px;background-color:rgba(0,0,0,0.25);}"
//                          QMenu::item:pressed {border-radius:4px;background-color: rgba(0,0,0,0.25);}");
//        m_mainMenu ->setStyleSheet("QMenu {margin:2px;padding:5px;}");
//  }
//    else if("ukui-default" ==nowThemeStyle || "ukui-white" == nowThemeStyle || "ukui-light" == nowThemeStyle)
//    {
//        m_mainMenu ->setStyleSheet("QMenu {background-color:rgba(0,0,0,0.6);margin:1px;padding:5px;}"
//                                    "QMenu::item {color: rgb(225,225,225);}"
//                                    "QMenu::item:selected {border-radius:4px;background-color:rgba(255,255,255,0.25);}"
//                                    QMenu::item:pressed {border-radius:4px;background-color: rgba(255,255,255,0.25);}");
//          m_mainMenu ->setStyleSheet("QMenu {margin:2px;padding:5px;}");
//  }
//QMenu::icon{position:absolute;padding-left:10px;padding-top:5px;padding-bottom:5px;}

}
QString MainWindow::getCityList()
{
    QString str="";
    if (m_pWeatherData != nullptr) {
        QStringList keyList = m_pWeatherData->keys();
        if (keyList.contains("citylist")) {
            str = m_pWeatherData->get("citylist").toString();
        }
    }
    return str;
}

void MainWindow::setCityList(QString str)
{
    m_pWeatherData->set("citylist", str);
}

//读取autolocate键：key缺失表示旧编译schema尚未重装，按数据契约默认开启
bool MainWindow::isAutoLocateEnabled()
{
    if (m_pWeatherData == nullptr || !m_pWeatherData->keys().contains("autolocate")) {
        return true;
    }
    return m_pWeatherData->get("autolocate").toBool();
}

//写入autolocate键（旧schema无此key时跳过写入，会话内状态仍经信号流转）
void MainWindow::setAutoLocate(bool on)
{
    if (m_pWeatherData == nullptr || !m_pWeatherData->keys().contains("autolocate")) {
        return;
    }
    m_pWeatherData->set("autolocate", on);
}

//gsettings citylist/autolocate变化的统一处理：重建轮播并按需拉取当前城市天气
//浏览/添加手动城市（写入citylist）不改变自动定位：autolocate只由菜单「自动定位」
//开关显式切换，保证开启自动定位时「当前城市」始终是自动定位页而非手动城市
void MainWindow::onWeatherDataGsettingChanged(const QString &key)
{
    if (key != "citylist" && key != "autolocate") {
        return;
    }

    const QString nowCityList = getCityList();
    const QString nowHeadCityId = nowCityList.split(",", Qt::SkipEmptyParts).value(0);
    const QString firstHeadCityId = firstGetCityList.split(",", Qt::SkipEmptyParts).value(0);

    rebuildCityStack();
    fetchCurrentCityWeather();

    if (nowHeadCityId != firstHeadCityId) {
        emit updatecity(); //通知收藏城市窗口刷新显示
        firstGetCityList = nowCityList;
    }
}

//按autolocate/citylist拉取当前城市天气：自动定位模式只开始定位（成功后再拉取），
//手动模式只拉取citylist[0]（为空时跳过）。记录最近拉取状态，使自身写gsettings
//触发的changed回调能跳过重复拉取（避免轮播切换时双重请求）
void MainWindow::fetchCurrentCityWeather(bool force)
{
    const bool autoLocate = isAutoLocateEnabled();
    const QString manualCityId = autoLocate ? QString() : getCityList().split(",", Qt::SkipEmptyParts).value(0);

    if (!force && autoLocate == m_appliedFetchAuto && manualCityId == m_appliedFetchCityId) {
        return;
    }
    m_appliedFetchAuto = autoLocate;
    m_appliedFetchCityId = manualCityId;

    if (autoLocate) {
        m_weatherManager->startAutoLocationTask();
    } else if (!manualCityId.isEmpty()) {
        m_weatherManager->startGetTheWeatherData(manualCityId);
    }
}

//创建一个城市轮播页：透明容器，居中纵向排布城市名/大温度/天气/湿度
void MainWindow::createCityPage(const QString &id, const QString &name, bool isAuto)
{
    QWidget *page = new QWidget(m_cityStack);
    page->setStyleSheet("QWidget{border:none;border-radius:4px;background:transparent;}");
    page->installEventFilter(this);

    CityPage cityPage;
    cityPage.id = id;
    cityPage.name = name;
    cityPage.isAuto = isAuto;
    cityPage.pageWidget = page;

    cityPage.nameLabel = new QLabel(page);
    cityPage.nameLabel->setAlignment(Qt::AlignCenter);
    cityPage.nameLabel->setFixedHeight(40);
    cityPage.nameLabel->setStyleSheet("QLabel{border:none;background:transparent;font-size:34px;font-weight:bold;color:rgba(255,255,255,1);}");
    cityPage.nameLabel->setText(isAuto ? (name.isEmpty() ? tr("自动定位") : name + tr(" · 自动")) : name);

    //大温度与单位同行，保持原居中大温度的显示样式
    QHBoxLayout *tmpLayout = new QHBoxLayout();
    tmpLayout->setSpacing(0);
    cityPage.tmpLabel = new QLabel(page);
    cityPage.tmpLabel->setAlignment(Qt::AlignCenter);
    cityPage.tmpLabel->setFixedHeight(100);
    cityPage.tmpLabel->setStyleSheet("QLabel{border:none;background:transparent;font-size:110px;font-weight:300;color:rgba(255,255,255,1);line-height:100px;}");
    cityPage.unitLabel = new QLabel(page);
    cityPage.unitLabel->setText("℃");
    cityPage.unitLabel->setStyleSheet("QLabel{border:none;background:transparent;font-size:24px;color:rgba(255,255,255,1);}");
    cityPage.unitLabel->setVisible(false); //随温度一起显示
    tmpLayout->addStretch();
    tmpLayout->addWidget(cityPage.tmpLabel);
    tmpLayout->addWidget(cityPage.unitLabel, 0, Qt::AlignTop | Qt::AlignLeft);
    tmpLayout->addStretch();

    cityPage.condLabel = new QLabel(page);
    cityPage.condLabel->setAlignment(Qt::AlignCenter);
    cityPage.condLabel->setFixedHeight(24);
    cityPage.condLabel->setStyleSheet("QLabel{border:none;background:transparent;font-size:20px;color:rgba(255,255,255,1);}");

    cityPage.humLabel = new QLabel(page);
    cityPage.humLabel->setAlignment(Qt::AlignCenter);
    cityPage.humLabel->setFixedHeight(18);
    cityPage.humLabel->setStyleSheet("QLabel{border:none;background:transparent;font-size:14px;color:rgba(255,255,255,1);}");

    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 30, 0, 0);
    pageLayout->setSpacing(6);
    pageLayout->addWidget(cityPage.nameLabel);
    pageLayout->addLayout(tmpLayout);
    pageLayout->addWidget(cityPage.condLabel);
    pageLayout->addWidget(cityPage.humLabel);

    //各label对鼠标透明，保证拖动/滚轮事件直接落在页面上
    cityPage.nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    cityPage.tmpLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    cityPage.unitLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    cityPage.condLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    cityPage.humLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_cityPages.append(cityPage);
    m_cityStack->addWidget(page);
}

//按gsettings重建城市轮播：page0为自动定位页，pages 1..n为citylist顺序的手动城市；
//活动位置为自动定位时0、手动模式时头城市所在页
void MainWindow::rebuildCityStack()
{
    if (m_cityStack == nullptr) {
        return;
    }

    //记住当前浏览页的城市ID（空=自动定位页），重建后尽量恢复浏览位置，
    //避免写citylist触发的gsettings回调（异步重建）把浏览中的手动城市页重置回自动页
    const QString activeViewId = m_activeViewCityId;

    //清空旧页
    while (m_cityStack->count() > 0) {
        QWidget *oldPage = m_cityStack->widget(0);
        m_cityStack->removeWidget(oldPage);
        oldPage->deleteLater();
    }
    m_cityPages.clear();

    const bool autoLocate = isAutoLocateEnabled();
    const QStringList cityIds = getCityList().split(",", Qt::SkipEmptyParts);

    //page0为自动定位页，定位成功后显示定位到的城市名
    createCityPage(m_autoCityId, m_autoCityName.isEmpty() ? tr("自动定位") : m_autoCityName, true);

    int currentIndex = 0;
    const QString headCityId = cityIds.value(0);
    QStringList addedIds;
    for (const QString &cityId : cityIds) {
        if (cityId.isEmpty() || addedIds.contains(cityId)) {
            continue; //跳过异常列表中的重复城市ID（收藏窗口可能写入重复项）
        }
        addedIds.append(cityId);
        const QString cityName = cityNameFromId(cityId); //城市表无匹配时回退显示LocationID
        createCityPage(cityId, cityName.isEmpty() ? cityId : cityName, false);
    }

    //活动页：优先恢复重建前的浏览位置；该页已不存在（如城市被删）或从未浏览时，
    //回退到默认当前城市——自动定位开启时为自动页0，否则为citylist[0]对应页
    if (!activeViewId.isEmpty()) {
        for (int i = 1; i < m_cityPages.size(); ++i) {
            if (!m_cityPages.at(i).isAuto && m_cityPages.at(i).id == activeViewId) {
                currentIndex = i;
                break;
            }
        }
    } else if (!autoLocate && !headCityId.isEmpty()) {
        for (int i = 1; i < m_cityPages.size(); ++i) {
            if (!m_cityPages.at(i).isAuto && m_cityPages.at(i).id == headCityId) {
                currentIndex = i;
                break;
            }
        }
    }
    m_cityStack->setCurrentIndex(currentIndex);
    //记录当前活动页的城市ID（空=自动定位页），供下次重建恢复浏览位置
    m_activeViewCityId = (currentIndex > 0) ? m_cityPages.at(currentIndex).id : QString();

    //重建后拉取各城市天气简报，使每个页面都有基础数据（去抖合并连续重建触发的请求）
    scheduleCityListBrief();
}

//轮播页被激活（拖动/滚轮/菜单切换）：同步当前城市
void MainWindow::onCityPageActivated(int index)
{
    if (index < 0 || index >= m_cityPages.size()) {
        return;
    }
    const QString cityId = m_cityPages.at(index).id;
    const bool isAutoPage = m_cityPages.at(index).isAuto;
    if (isAutoPage) {
        //切换到自动定位页：开启autolocate并开始定位
        m_activeViewCityId.clear();
        if (!isAutoLocateEnabled()) {
            setAutoLocate(true);
        }
        m_appliedFetchAuto = true;
        m_appliedFetchCityId.clear();
        m_weatherManager->startAutoLocationTask();
        m_cityStack->setCurrentIndex(index);
    } else {
        switchToManualCity(cityId);
    }
}

//浏览/添加手动城市：城市移至citylist首位（去重、最多8个）并拉取其天气；
//不改变自动定位状态——自动定位开启时「当前城市」仍是自动定位页，
//手动城市仅作为可浏览页；关闭自动定位只能经菜单「自动定位」开关显式操作
void MainWindow::switchToManualCity(const QString &cityId)
{
    if (cityId.isEmpty()) {
        return;
    }

    //浏览位置记录为该城市，供重建轮播时恢复
    m_activeViewCityId = cityId;

    QStringList cityIds = getCityList().split(",", Qt::SkipEmptyParts);
    if (cityIds.value(0) != cityId) {
        cityIds.removeAll(cityId);
        cityIds.prepend(cityId);
        while (cityIds.size() > 8) {
            cityIds.removeLast();
        }
        setCityList(cityIds.join(",") + ",");
    }

    //记录本次拉取状态后再发起请求，使自身写gsettings触发的changed回调可跳过重复拉取。
    //自动定位开启时浏览手动城市，拉取状态记为「自动」：避免把这次手动浏览误当成
    //模式切换而触发自动定位重解析（双请求）
    if (isAutoLocateEnabled()) {
        m_appliedFetchAuto = true;
        m_appliedFetchCityId.clear();
    } else {
        m_appliedFetchAuto = false;
        m_appliedFetchCityId = cityId;
    }
    m_weatherManager->startGetTheWeatherData(cityId);

    rebuildCityStack();
    //重建后该城市位于首页（page1），显式指向该页（旧schema无autolocate键时同样生效）
    for (int i = 0; i < m_cityPages.size(); ++i) {
        if (m_cityPages.at(i).id == cityId) {
            m_cityStack->setCurrentIndex(i);
            break;
        }
    }
}

//记录/更新自动定位到的城市（轮播重建时恢复自动定位页显示）
void MainWindow::updateAutoLocatedCity(const QString &cityId, const QString &cityName)
{
    m_autoCityId = cityId;
    m_autoCityName = cityName;
    for (CityPage &page : m_cityPages) {
        if (page.isAuto) {
            page.id = cityId;
            setCityPageName(page, cityName);
            break;
        }
    }
}

//更新某页温度显示（单位随温度显示/隐藏）
void MainWindow::setCityPageTmp(CityPage &page, const QString &tmp)
{
    page.tmpLabel->setText(tmp);
    page.unitLabel->setVisible(!tmp.isEmpty());
}

//统一设置城市页名：自动定位页始终带「·自动」标记（未定位到城市时显示「自动定位」），
//与面板小部件自动定位 tab 的标记保持一致，保证 App 中自动定位入口始终可见可辨
void MainWindow::setCityPageName(CityPage &page, const QString &name)
{
    page.name = name;
    page.nameLabel->setText(page.isAuto
                                ? (name.isEmpty() ? tr("自动定位") : name + tr(" · 自动"))
                                : name);
}

//解析收藏城市天气简报并更新轮播中匹配的页面（含中文城市名）
//格式见WeatherWorker::buildCitySimpleData："tmp=26,cond_txt=阴,cond_code=104,id=101250101,location=长沙;"逐城拼接
void MainWindow::onSetCityWeatherBrief(QString weather_data)
{
    if (m_cityPages.isEmpty() || weather_data.isEmpty()) {
        return;
    }

    const QStringList records = weather_data.split(";", Qt::SkipEmptyParts);
    for (const QString &record : records) {
        QString tmp;
        QString condTxt;
        QString cityId;
        QString location;
        const QStringList fields = record.split(",");
        for (const QString &field : fields) {
            const QStringList pair = field.split("=");
            if (pair.size() != 2) {
                continue;
            }
            if (pair.at(0) == "tmp") {
                tmp = pair.at(1);
            } else if (pair.at(0) == "cond_txt") {
                condTxt = pair.at(1);
            } else if (pair.at(0) == "id") {
                cityId = pair.at(1);
            } else if (pair.at(0) == "location") {
                location = pair.at(1);
            }
        }
        if (cityId.isEmpty()) {
            continue;
        }
        for (CityPage &page : m_cityPages) {
            if (page.id != cityId) {
                continue;
            }
            setCityPageTmp(page, tmp);
            page.condLabel->setText(condTxt);
            if (!location.isEmpty() && location != "-") {
                setCityPageName(page, location);
            }
        }
    }
}

//去抖发起收藏城市简报批量拉取
void MainWindow::scheduleCityListBrief()
{
    if (m_cityBriefTimer) {
        m_cityBriefTimer->start();
    }
}

//自动定位时把主程序实际当前城市（自动定位城市）的简报插到收藏简报最前面，
//使收藏对话框 strList[0]=当前城市、citylist 各城市按收藏城市显示；
//否则其仅以 citylist[0] 为当前城市，自动定位场景下 citylist[0] 会被当前城市占用而漏显示
QString MainWindow::prependCurrentCityBrief(const QString &batch)
{
    if (m_currentObserve.id.isEmpty()) {
        return batch;
    }
    const QString brief = QString("tmp=%1,cond_txt=%2,cond_code=%3,id=%4,location=%5;")
            .arg(m_currentObserve.tmp, m_currentObserve.cond_txt,
                 m_currentObserve.cond_code, m_currentObserve.id, m_currentObserve.city);
    return brief + batch;
}

//从内置城市表按LocationID解析城市名，未找到返回空（调用方回退显示ID）
QString MainWindow::cityNameFromId(const QString &id)
{
    if (id.isEmpty()) {
        return QString();
    }
    QFile file(":/data/data/china-city-list.csv");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).remove("\r").remove("\n");
        const QStringList resultList = line.split(",");
        if (resultList.length() < 8 || !resultList.at(0).startsWith("CN")) {
            continue;
        }
        if (resultList.at(0).mid(2) == id) { //去掉"CN"前缀后比较
            return resultList.at(2);
        }
    }
    file.close();
    return QString();
}

//轮播页交互事件过滤器：左右拖动（阈值40px）或滚动滚轮切换城市页
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_cityStack != nullptr) {
        bool watchedByCarousel = (watched == m_cityStack);
        if (!watchedByCarousel) {
            for (const CityPage &page : m_cityPages) {
                if (page.pageWidget == watched) {
                    watchedByCarousel = true;
                    break;
                }
            }
        }

        if (watchedByCarousel) {
            switch (event->type()) {
            case QEvent::MouseButtonPress: {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (mouseEvent->button() == Qt::LeftButton) {
                    m_swiping = true;
                    m_swipeStartX = mouseEvent->pos().x();
                    return true;
                }
                break;
            }
            case QEvent::MouseButtonRelease: {
                if (!m_swiping) {
                    break;
                }
                m_swiping = false;
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                const int offsetX = mouseEvent->pos().x() - m_swipeStartX;
                if (qAbs(offsetX) >= 40) { //拖动距离阈值，避免误把点击当成滑动
                    //向右拖切换上一页，向左拖切换下一页
                    const int targetIndex = m_cityStack->currentIndex() + (offsetX > 0 ? -1 : 1);
                    if (targetIndex >= 0 && targetIndex < m_cityStack->count()) {
                        onCityPageActivated(targetIndex);
                    }
                }
                return true;
            }
            case QEvent::Wheel: {
                auto *wheelEvent = static_cast<QWheelEvent *>(event);
                const QPoint angleDelta = wheelEvent->angleDelta();
                const int delta = qAbs(angleDelta.x()) > qAbs(angleDelta.y()) ? angleDelta.x() : angleDelta.y();
                if (delta != 0) {
                    //上滚（含左滚）切换上一页，下滚切换下一页
                    const int targetIndex = m_cityStack->currentIndex() + (delta > 0 ? -1 : 1);
                    if (targetIndex >= 0 && targetIndex < m_cityStack->count()) {
                        onCityPageActivated(targetIndex);
                    }
                }
                return true;
            }
            default:
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

//菜单「刷新间隔」变更：重启自动刷新定时器
void MainWindow::onRefreshIntervalChanged(int minutes)
{
    if (m_refreshweather && minutes > 0) {
        m_refreshweather->start(minutes * 60 * 1000);
    }
}
