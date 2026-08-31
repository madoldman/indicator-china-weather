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

#include "menumodule.h"
#include "xatom-helper.h"
#include <QActionGroup>//Qt6 中 QAction 相关类不再随其它头文件传递引入

menuModule::menuModule(QWidget *parent = nullptr) : QWidget(parent)
{
    init();
}

void menuModule::init(){
    initAction();
    setStyle();
}

void menuModule::initAction(){

    bodyAppName = new QLabel();
    titleBtnClose = new QPushButton;
    bodyAppVersion = new QLabel();
    bodySupport = new QLabel();
    titleText = new QLabel();
    iconSize = QSize(30,30);
    menuButton = new QPushButton;
    menuButton->setToolTip(tr("menu"));
//    menuButton->setIcon(QIcon::fromTheme("application-menu"));
    menuButton->setIcon(QIcon(":/res/control_icons/menu.png"));
    menuButton->setIconSize(QSize(30,30));
    menuButton->setProperty("setIconHighlightEffectDefaultColor", QColor(Qt::white));
    menuButton->setStyleSheet("color:rgba(255,255,255，1);");
    menuButton->setFlat(true);
    menuButton->setFixedSize(iconSize);

    m_menu = new QMenu();

    addCityAction = new AddCityAction(m_menu);
    QList<QAction *> actions ;
    QAction *actionTheme = new QAction(m_menu);
    actionTheme->setText(tr("Theme"));
    QAction *actionHelp = new QAction(m_menu);
    actionHelp->setText(tr("Help"));
    QAction *actionAbout = new QAction(m_menu);
    actionAbout->setText(tr("About"));
    QAction *actionQuit = new QAction(m_menu);
    actionQuit->setText(tr("Quit"));
    QAction *actionInterval = new QAction(m_menu);
    actionInterval->setText(tr("刷新间隔"));
    QAction *actionAddPanel = new QAction(m_menu);
    actionAddPanel->setText(tr("添加小部件到面板"));
    //「自动定位」可勾选菜单项：读写 gsettings autolocate，切换后经信号通知主窗口
    autoLocateAction = new QAction(m_menu);
    autoLocateAction->setText(tr("自动定位"));
    autoLocateAction->setCheckable(true);
    actions<<addCityAction<<autoLocateAction<<actionInterval<<actionAddPanel<<actionHelp<<actionAbout<<actionQuit;
    m_menu->addActions(actions);
//    互斥按钮组
    QMenu *themeMenu = new QMenu;
    QActionGroup *themeMenuGroup = new QActionGroup(this);
    QAction *autoTheme = new QAction("Auto",this);
    themeMenuGroup->addAction(autoTheme);
    themeMenu->addAction(autoTheme);
    autoTheme->setCheckable(true);
    QAction *lightTheme = new QAction("Light",this);
    themeMenuGroup->addAction(lightTheme);
    themeMenu->addAction(lightTheme);
    lightTheme->setCheckable(true);
    QAction *darkTheme = new QAction("Dark",this);
    themeMenuGroup->addAction(darkTheme);
    themeMenu->addAction(darkTheme);
    darkTheme->setCheckable(true);
    QList<QAction* > themeActions;
    themeActions<<autoTheme<<lightTheme<<darkTheme;
    actionTheme->setMenu(themeMenu);
    menuButton->setMenu(m_menu);
    connect(m_menu,&QMenu::triggered,this,&menuModule::triggerMenu);
    initGsetting();
    setThemeFromLocalThemeSetting(themeActions);
    themeUpdate();
    connect(themeMenu,&QMenu::triggered,this,&menuModule::triggerThemeMenu);

    // 刷新间隔子菜单：读写 gsettings refresh-interval，改后经信号同步主窗口定时器
    intervalMenu = new QMenu;
    QActionGroup *intervalGroup = new QActionGroup(this);
    const QList<int> intervalChoices = {5, 10, 20, 30, 60};
    int currentInterval = 20;
    if (m_pGsettingThemeStatus) {
        currentInterval = m_pGsettingThemeStatus->get("refresh-interval").toInt();
        if (currentInterval <= 0) currentInterval = 20;
    }
    for (int minutes : intervalChoices) {
        QAction *choice = new QAction(tr("%1 分钟").arg(minutes), intervalMenu);
        choice->setCheckable(true);
        choice->setData(minutes);
        intervalGroup->addAction(choice);
        intervalMenu->addAction(choice);
        if (minutes == currentInterval) choice->setChecked(true);
    }
    // 非预设值兜底项：gsettings 里的值不在预设档位时（如小部件旧值），菜单打开时显示并勾选
    customIntervalAction = new QAction(intervalMenu);
    customIntervalAction->setCheckable(true);
    customIntervalAction->setVisible(false);
    intervalGroup->addAction(customIntervalAction);
    intervalMenu->addAction(customIntervalAction);
    actionInterval->setMenu(intervalMenu);
    connect(intervalMenu,&QMenu::triggered,this,&menuModule::triggerIntervalMenu);
    connect(m_menu,&QMenu::aboutToShow,this,&menuModule::refreshIntervalCheckedState);
    connect(actionAddPanel,&QAction::triggered,this,&menuModule::addPanelAction);

    // 自动定位开关：写 gsettings autolocate 并经信号通知主窗口；
    // 用 triggered 而非 toggled，避免 aboutToShow 回显勾选时反向触发切换
    connect(autoLocateAction, &QAction::triggered, this, [this] () {
        const bool on = autoLocateAction->isChecked();
        if (m_pGsettingThemeStatus && m_pGsettingThemeStatus->keys().contains("autolocate")) {
            m_pGsettingThemeStatus->set("autolocate", on);
        }
        emit autoLocateToggled(on);
    });
    connect(m_menu,&QMenu::aboutToShow,this,&menuModule::refreshAutoLocateCheckedState);
}

void menuModule::setThemeFromLocalThemeSetting(QList<QAction* > themeActions)
{
#if DEBUG_MENUMODULE
//    confPath = "org.kylin-usb-creator-data.settings";
#endif
    m_pGsettingThemeStatus = new QGSettings(APPDATA);
    QString appConf = m_pGsettingThemeStatus->get("thememode").toString();
    if("lightonly" == appConf){
        themeStatus = themeLightOnly;
        themeActions[1]->setChecked(true);   //程序gsetting中为浅色only的时候就给浅色按钮设置checked
    }else if("darkonly" == appConf){
        themeStatus = themeBlackOnly;
        themeActions[2]->setChecked(true);
    }else{
        themeStatus = themeAuto;
        themeActions[0]->setChecked(true);
    }
}

void menuModule::themeUpdate(){
    if(themeStatus == themeLightOnly)
    {
        setThemeLight();
    }else if(themeStatus == themeBlackOnly){
        setThemeDark();
    }else{
        setStyleByThemeGsetting();
    }
}

void menuModule::setStyleByThemeGsetting(){
    // org.ukui.style 为 UKUI 桌面专属 schema，Plasma 等无此 schema 的环境回退浅色主题
    if (!m_pGsettingThemeData) {
        setThemeLight();
        return;
    }
    QString nowThemeStyle = m_pGsettingThemeData->get("styleName").toString();
    if("ukui-dark" == nowThemeStyle || "ukui-black" == nowThemeStyle)
    {
        setThemeDark();
    }else{
        setThemeLight();
    }
}

void menuModule::triggerMenu(QAction *act){


    QString str = act->text();
    if(tr("Quit") == str){
        emit menuModuleClose();
    }else if(tr("About") == str){
        aboutAction();
    }else if(tr("Help") == str){
        helpAction();
    }
}

// 菜单每次打开时按 gsettings 当前值刷新勾选，保证应用侧与小部件侧状态一致
void menuModule::refreshIntervalCheckedState()
{
    if (!m_pGsettingThemeStatus || !intervalMenu) {
        return;
    }
    const int current = qMax(1, m_pGsettingThemeStatus->get("refresh-interval").toInt());
    const QList<int> presets = {5, 10, 20, 30, 60};
    const bool isPreset = presets.contains(current);
    for (QAction *a : intervalMenu->actions()) {
        const int v = a->data().toInt();
        if (v > 0) {
            a->setChecked(v == current);
        }
    }
    if (customIntervalAction) {
        customIntervalAction->setText(tr("%1 分钟（当前）").arg(current));
        customIntervalAction->setChecked(!isPreset);
        customIntervalAction->setVisible(!isPreset);
    }
}

// 菜单每次打开时按 gsettings autolocate 当前值刷新「自动定位」勾选
// （旧编译 schema 无此 key 时按数据契约默认开启）
void menuModule::refreshAutoLocateCheckedState()
{
    if (!autoLocateAction) {
        return;
    }
    bool enabled = true;
    if (m_pGsettingThemeStatus && m_pGsettingThemeStatus->keys().contains("autolocate")) {
        enabled = m_pGsettingThemeStatus->get("autolocate").toBool();
    }
    autoLocateAction->setChecked(enabled);
}

void menuModule::triggerIntervalMenu(QAction *act){
    const int minutes = act->data().toInt();
    if (minutes <= 0) {
        return;
    }
    if (m_pGsettingThemeStatus) {
        m_pGsettingThemeStatus->set("refresh-interval", minutes);
    }
    emit refreshIntervalChanged(minutes);
}

// 经 plasmashell 脚本接口把天气小部件快捷加入面板（已在面板上则不重复添加）
void menuModule::addPanelAction(){
    const QString script = QStringLiteral(
        "var found = false;"
        "for (var i in panels()) {"
        "    var p = panels()[i];"
        "    for (var k in p.widgetIds) {"
        "        var w = p.widgetById(p.widgetIds[k]);"
        "        if (w && w.type == 'org.madoldman.chinaweather') found = true;"
        "    }"
        "}"
        "if (!found && panels().length > 0) { panels()[0].addWidget('org.madoldman.chinaweather'); }"
        "found ? 'exists' : 'added'");
    QProcess gdbus;
    gdbus.start(QStringLiteral("gdbus"), {QStringLiteral("call"), QStringLiteral("--session"),
        QStringLiteral("--dest"), QStringLiteral("org.kde.plasmashell"),
        QStringLiteral("--object-path"), QStringLiteral("/PlasmaShell"),
        QStringLiteral("--method"), QStringLiteral("org.kde.PlasmaShell.evaluateScript"),
        script});
    gdbus.waitForFinished(5000);
    const QString out = QString::fromUtf8(gdbus.readAllStandardOutput());
    if (out.contains(QStringLiteral("added"))) {
        QMessageBox::information(this, tr("天气"), tr("已将天气小部件添加到面板。"));
    } else if (out.contains(QStringLiteral("exists"))) {
        QMessageBox::information(this, tr("天气"), tr("面板上已有天气小部件。"));
    } else {
        QMessageBox::warning(this, tr("天气"), tr("添加失败：未检测到可用的 Plasma 面板。"));
    }
}

void menuModule::triggerThemeMenu(QAction *act){
    if(!m_pGsettingThemeStatus)
    {
        m_pGsettingThemeStatus = new QGSettings(APPDATA);  //m_pGsettingThemeStatus指针重复使用避免占用栈空间
    }
    QString str = act->text();
    if("Light" == str){
        themeStatus = themeLightOnly;
        if (m_pGsettingThemeData)
            disconnect(m_pGsettingThemeData,&QGSettings::changed,this,&menuModule::dealSystemGsettingChange);
        m_pGsettingThemeStatus->set("thememode","lightonly");
//        disconnect()
        setThemeLight();
    }else if("Dark" == str){
        themeStatus = themeBlackOnly;
        if (m_pGsettingThemeData)
            disconnect(m_pGsettingThemeData,&QGSettings::changed,this,&menuModule::dealSystemGsettingChange);
        m_pGsettingThemeStatus->set("thememode","darkonly");
        setThemeDark();
    }else{
        themeStatus = themeAuto;
        m_pGsettingThemeStatus->set("thememode","auto");
        initGsetting();
//        updateTheme();
        themeUpdate();
    }
}

void menuModule::aboutAction(){
//    关于点击事件处理
    initAbout();
}

void menuModule::helpAction(){
//    帮助点击事件处理

    appName = "tools/indicator-china-weather";
    if(!ipcDbus){
        ipcDbus = new DaemonDbus();
    }

    if(!ipcDbus->daemonIsNotRunning()){
        ipcDbus->showGuide(appName);
    }
}

void menuModule::initAbout(){
    aboutWindow->deleteLater();
    aboutWindow = new QWidget();
    aboutWindow->setWindowModality(Qt::ApplicationModal);
    aboutWindow->setWindowFlag(Qt::Tool);
    if(themeNow == themeBlack)
        aboutWindow->setStyleSheet(".QWidget{background-color:rgba(0,0,0,1);}");
    else if(themeNow == themeLight)
        aboutWindow->setStyleSheet(".QWidget{background-color:rgba(255,255,255,1);}");


//    aboutWindow->setAttribute(Qt::WA_DeleteOnClose);
    MotifWmHints hints;
    hints.flags = MWM_HINTS_FUNCTIONS|MWM_HINTS_DECORATIONS;
    hints.functions = MWM_FUNC_ALL;
    hints.decorations = MWM_DECOR_BORDER;
    XAtomHelper::getInstance()->setWindowMotifHint(aboutWindow->winId(), hints);
    aboutWindow->setFixedSize(420,324);
    aboutWindow->setMinimumHeight(324);
    QVBoxLayout *mainlyt = new QVBoxLayout();
    QHBoxLayout *titleLyt = initTitleBar();
    QVBoxLayout *bodylyt = initBody();
    mainlyt->setContentsMargins(0, 0, 0, 0);
    mainlyt->addLayout(titleLyt);
    mainlyt->addLayout(bodylyt);
    mainlyt->addStretch();
    aboutWindow->setLayout(mainlyt);
    //TODO:在app中央显示
    QRect availableGeometry = this->parentWidget()->geometry();
    aboutWindow->move(availableGeometry.center()-aboutWindow->rect().center());
    aboutWindow->show();
}

QHBoxLayout* menuModule::initTitleBar(){

    appShowingName = tr("weather");
    iconPath = ":/res/control_icons/indicator-china-weather.svg";

    QPushButton *titleIcon = new QPushButton();
    titleIcon->setFixedSize(QSize(24,24));
    titleIcon->setIcon(QIcon::fromTheme("indicator-china-weather", QIcon(":/res/control_icons/logo_24.png")));
    titleIcon->setStyleSheet("QPushButton{border:0px;border-radius:4px;background:transparent;}"
                             "QPushButton::hover{border:0px;border-radius:4px;background:transparent;}"
                             "QPushButton::pressed{border:0px;border-radius:4px;background:transparent;}");
    titleIcon->setIconSize(QSize(24,24));

    connect(titleBtnClose,&QPushButton::clicked,[=](){aboutWindow->close();});
    QHBoxLayout *hlyt = new QHBoxLayout;
    titleText->setText(tr("Weather"));
    hlyt->setSpacing(0);
    hlyt->setContentsMargins(4, 4, 4, 4);
    hlyt->addSpacing(3);
    hlyt->addWidget(titleIcon,0,Qt::AlignCenter); //居下显示
    hlyt->addSpacing(8);
    hlyt->addWidget(titleText,0,Qt::AlignCenter);
    hlyt->addStretch();
    hlyt->addWidget(titleBtnClose,0,Qt::AlignBottom);
    return hlyt;
}

QVBoxLayout* menuModule::initBody(){
    appVersion = "3.1.1";


    QPushButton *bodyIcon = new QPushButton();
    bodyIcon->setFixedSize(96,96);

    bodyIcon->setIcon(QIcon::fromTheme("indicator-china-weather", QIcon(":/res/control_icons/logo_24.png")));
    bodyIcon->setStyleSheet("QPushButton{border:0px;border-radius:4px;background:transparent;}"
                            "QPushButton::hover{border:0px;border-radius:4px;background:transparent;}"
                            "QPushButton::pressed{border:0px;border-radius:4px;background:transparent;}");
    bodyIcon->setIconSize(QSize(96,96));

    bodyAppName->setFixedHeight(28);
    bodyAppName->setText(tr(appShowingName.toLocal8Bit()));
    bodyAppVersion->setFixedHeight(24);
    bodyAppVersion->setText(tr("Version: ") + appVersion);
    bodyAppVersion->setAlignment(Qt::AlignLeft);

    connect(bodySupport,&QLabel::linkActivated,this,[=](const QString url){
        QDesktopServices::openUrl(QUrl(url));
    });
    bodySupport->setFixedHeight(24);
    bodySupport->setOpenExternalLinks(true);
    bodySupport->setContextMenuPolicy(Qt::NoContextMenu);
    QVBoxLayout *vlyt = new QVBoxLayout;
    vlyt->setContentsMargins(0, 0, 0, 0);
    vlyt->setSpacing(0);
    vlyt->addSpacing(20);
    vlyt->addWidget(bodyIcon,0,Qt::AlignHCenter);
    vlyt->addSpacing(16);
    vlyt->addWidget(bodyAppName,0,Qt::AlignHCenter);
    vlyt->addSpacing(12);
    vlyt->addWidget(bodyAppVersion,0,Qt::AlignHCenter);
    vlyt->addSpacing(12);
    vlyt->addWidget(bodySupport,0,Qt::AlignHCenter);
    vlyt->addStretch();
    return vlyt;
}

void menuModule::setStyle(){
    menuButton->setStyleSheet("QPushButton{border:0px;border-radius:4px;background:transparent;}"
                              "QPushButton:Hover{border:0px;border-radius:4px;background:transparent;background-color:rgba(0,0,0,0.1);}"
                              "QPushButton:Pressed{border:0px;border-radius:4px;background:transparent;background-color:rgba(0,0,0,0.15);}"
                              "QPushButton::menu-indicator{image:None;}");
}

void menuModule::initGsetting(){
    if(QGSettings::isSchemaInstalled(FITTHEMEWINDOW)){
        m_pGsettingThemeData = new QGSettings(FITTHEMEWINDOW);
        connect(m_pGsettingThemeData,&QGSettings::changed,this,&menuModule::dealSystemGsettingChange);
    }

}

void menuModule::dealSystemGsettingChange(const QString key){
    if(key == "styleName"){
        refreshThemeBySystemConf();
    }
}

void menuModule::refreshThemeBySystemConf(){
    QString themeNow = m_pGsettingThemeData->get("styleName").toString();
    qDebug()<<"themenow:"<<themeNow;
    if("ukui-dark" == themeNow || "ukui-black" == themeNow){
        setThemeDark();
    }else{
        setThemeLight();
    }
}

void menuModule::setThemeDark(){
    qDebug()<<"Dark";
    themeNow = themeBlack;
    if(aboutWindow){
        if(themeNow == themeBlack)
            aboutWindow->setStyleSheet(".QWidget{background-color:rgba(0,0,0,1);}");
        else if(themeNow == themeLight)
            aboutWindow->setStyleSheet(".QWidget{background-color:rgba(255,255,255,1);}");
    }
    titleText->setStyleSheet("color:rgba(255,255,255,1);font-size:14px;");
    bodyAppName->setStyleSheet("color:rgba(255,255,255,1);font-size:18px;");
    bodyAppVersion->setStyleSheet("color:rgba(255,255,255,1);font-size:14px;");
    bodySupport->setStyleSheet("color:rgba(255,255,255,1);font-size:14px;");
    titleBtnClose->setIcon(QIcon::fromTheme(":/res/control_icons/dark-window-close.svg"));
    titleBtnClose->setIconSize(QSize(16,16));
    titleBtnClose->setFixedSize(30,30);
    titleBtnClose->setStyleSheet("QPushButton{border:0px;border-radius:4px;background:transparent;}"
                               "QPushButton:Hover{border:0px;border-radius:4px;background:transparent;background-color:#F86457;}"
                               "QPushButton:Pressed{border:0px;border-radius:4px;background:transparent;background-color:#E44C50;}");
    bodySupport->setText(tr("Service & Support: ") +
                         "<a href=\"mailto://support@kylinos.cn\""
                         "style=\"color:rgba(255,255,255,1)\">"
                         "support@kylinos.cn</a>");
}

void menuModule::setThemeLight(){
    qDebug()<<"settheme Light";
    themeNow = themeLight;
    if(aboutWindow){
        if(themeNow == themeBlack)
            aboutWindow->setStyleSheet(".QWidget{background-color:rgba(0,0,0,1);}");
        else if(themeNow == themeLight)
            aboutWindow->setStyleSheet(".QWidget{background-color:rgba(255,255,255,1);}");
    }
    titleText->setStyleSheet("color:rgba(0,0,0,1);font-size:14px;");
    bodyAppName->setStyleSheet("color:rgba(0,0,0,1);font-size:18px;");
    bodyAppVersion->setStyleSheet("color:rgba(0,0,0,1);font-size:14px;");
    bodySupport->setStyleSheet("color:rgba(0,0,0,1);font-size:14px;");

    titleBtnClose->setIcon(QIcon::fromTheme(":/res/control_icons/close_black.png"));
    titleBtnClose->setFixedSize(30,30);
    titleBtnClose->setIconSize(QSize(30,30));
    titleBtnClose->setStyleSheet("QPushButton{border:0px;border-radius:4px;background:transparent;}"
                               "QPushButton:Hover{border:0px;border-radius:4px;background:transparent;background-color:#F86457;}"
                               "QPushButton:Pressed{border:0px;border-radius:4px;background:transparent;background-color:#E44C50;}");
    bodySupport->setText(tr("Service & Support: ") +
                         "<a href=\"mailto://support@kylinos.cn\""
                         "style=\"color:rgba(0,0,0,1)\">"
                         "support@kylinos.cn</a>");

}








