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

#include "addcityaction.h"


AddCityAction::AddCityAction(QWidget *parent) : QAction(parent)
{
    emit cityNameLabelSignal("N/A");//需要//此处为加载时N/A
    connect(this,&AddCityAction::triggered,this,&AddCityAction::addCityClick);
    connect(this,&AddCityAction::noNetWork,this,[=] {
        if(is_open_city_collect_widget){
        if(QLocale::system().name() == "zh_CN"){
        onRequestSendDesktopNotify("网络已断开");
    }else{
        onRequestSendDesktopNotify("Network disconnected");
    }}
    });
}
//转发主程序当前实际城市天气到收藏对话框「当前城市」卡片；收藏对话框尚未创建时缓存，
//创建后（addCityClick）再应用，保证卡片显示与主程序当前城市一致
void AddCityAction::setCurrentCityWeather(const ObserveWeather &weather)
{
    m_pendingCurrentCity = weather;
    if (m_citycollectionwidget) {
        m_citycollectionwidget->setCurrentCityWeather(weather);
    }
}

void AddCityAction::addCityClick()
{
   
    if (!is_open_city_collect_widget) {

        m_citycollectionwidget = new CityCollectionWidget();
        QRect availableGeometry = this->parentWidget()->geometry();
        m_citycollectionwidget->move(availableGeometry.center().x() -m_citycollectionwidget->rect().center().x() -420,availableGeometry.center().y() -m_citycollectionwidget->rect().center().y() + 155);
//        m_citycollectionwidget->move(m_citycollectionwidget->x()-400,m_citycollectionwidget->y() + 155);
//        qDebug()<<"2!"<<m_citycollectionwidget->rect().center().x()<<m_citycollectionwidget->rect().center().y();

        //接收来自收藏城市窗口发来的信号，再发送一个信号到主窗口
        connect(m_citycollectionwidget, &CityCollectionWidget::sendCurrentCityId, this, &AddCityAction::sendCurrentCityId);

        //给收藏城市窗口发送信号进行更新
        connect(this,&AddCityAction::updatecity,m_citycollectionwidget,&CityCollectionWidget::updatecity);
        connect(this,&AddCityAction::noNetWork,m_citycollectionwidget,&CityCollectionWidget::noNetWork);
        //requestShowCollCityWeather信号会一直传到weathermanager.cpp，用于获取收藏列表中城市的实时天气
        connect(m_citycollectionwidget, &CityCollectionWidget::requestShowCollCityWeather, this, &AddCityAction::requestShowCollCityWeather);

        //接收来自主窗口的天气数据，再发送到收藏城市列表窗口
        connect(this, SIGNAL(requestSetCityWeather(QString)), m_citycollectionwidget, SLOT(onRequestSetCityWeather(QString)));

        //应用主程序当前实际城市天气到「当前城市」卡片（创建前已由 setCurrentCityWeather 缓存）
        m_citycollectionwidget->setCurrentCityWeather(m_pendingCurrentCity);

        //收到关闭窗口的消息
        connect(m_citycollectionwidget, &CityCollectionWidget::requestChangeWidgetState, this, [this] () {
            //判空：关闭信号可能被连续发出两次（如快速双击关闭按钮，Qt 会发两次 clicked）。
            //deleteLater 是延迟销毁，第二次触发时对象还在 DeferredDelete 队列里未真正销毁，
            //但指针已在首次回调置空——不判空会对空指针调用 deleteLater，直接崩溃
            if (!m_citycollectionwidget) {
                return;
            }
            is_open_city_collect_widget = false;
            //先置空再销毁：置空后即使回调重入或经 setCurrentCityWeather 等路径访问成员，
            //也只会命中上面的判空返回，不会解引用已进入销毁队列的对象（use-after-free）。
            //addCityClick 检测 is_open_city_collect_widget==false 后会重新 new 并覆盖此成员，
            //「关闭后再打开」行为不受影响
            CityCollectionWidget *dialog = m_citycollectionwidget;
            m_citycollectionwidget = nullptr;
            dialog->deleteLater(); //销毁窗口
        });
        emit requestShowCollCityWeather(); //发送该信号，显示收藏城市窗口各城市的事实天气
        is_open_city_collect_widget = true; //为真时无法打开新的收藏窗口

        //! \brief 模态对话框会阻塞，发信号和设置is_open_city_collect_widget都要在exec之前
        m_citycollectionwidget->exec();
    }
    //! \note CityCollectionWidget打开后会阻塞主窗口，是不是可以把这个判断删掉了
    else{
        if(nullptr != m_citycollectionwidget){
            m_citycollectionwidget->show();
            QRect availableGeometry = this->parentWidget()->geometry();
            m_citycollectionwidget->move(availableGeometry.center().x() -m_citycollectionwidget->rect().center().x() -420,availableGeometry.center().y() -m_citycollectionwidget->rect().center().y() + 155);

            m_citycollectionwidget->activateWindow();
            return;
        }
    }
}
void AddCityAction::onRequestSendDesktopNotify(QString message)
{
    QDBusInterface iface("org.freedesktop.Notifications",
                         "/org/freedesktop/Notifications",
                         "org.freedesktop.Notifications",
                         QDBusConnection::sessionBus());
    QList<QVariant> args;
    if(QLocale::system().name() == "zh_CN")
    {
        args<<(tr("麒麟天气"))
           <<((unsigned int) 0)
          <<("麒麟天气")
         <<tr("麒麟天气") //显示的是什么类型的信息  控制面板-更新提示
        <<message //显示的具体信息
        <<QStringList()
        <<QVariantMap()
        <<(int)-1;
    }else{
        args<<(tr("indicator-china-weather"))
           <<((unsigned int) 0)
          <<("indicator-china-weather")
         <<tr("indicator-china-weather") //显示的是什么类型的信息  控制面板-更新提示
        <<message //显示的具体信息
        <<QStringList()
        <<QVariantMap()
        <<(int)-1;
    }
    iface.callWithArgumentList(QDBus::AutoDetect,"Notify",args);
}
