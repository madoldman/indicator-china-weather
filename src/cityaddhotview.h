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

#ifndef CITYADDHOTVIEW_H
#define CITYADDHOTVIEW_H

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include "hotcity.h"
class CityAddHotView : public QWidget
{
    Q_OBJECT

public:
    explicit CityAddHotView(QWidget *parent = 0);
    void ThemeCityHotView(QString str);
public slots:

signals:
    void setHotCity(QString code);

private:
    QLabel *m_addCityhot = nullptr;
//    QHBoxLayout *m_citylayout;
    HotCity *m_addcity11 = nullptr;
    HotCity *m_addcity12 = nullptr;
    HotCity *m_addcity13 = nullptr;
    HotCity *m_addcity14 = nullptr;
    HotCity *m_addcity15 = nullptr;
    HotCity *m_addcity16 = nullptr;
    HotCity *m_addcity17 = nullptr;
    HotCity *m_addcity18 = nullptr;
//    QHBoxLayout *m_citylayout1;
    HotCity *m_addcity21 = nullptr;
    HotCity *m_addcity22 = nullptr;
    HotCity *m_addcity23 = nullptr;
    HotCity *m_addcity24 = nullptr;
    HotCity *m_addcity25 = nullptr;
    HotCity *m_addcity26 = nullptr;
    HotCity *m_addcity27 = nullptr;
    HotCity *m_addcity28 = nullptr;
//    QHBoxLayout *m_citylayout2;
    HotCity *m_addcity31 = nullptr;
    HotCity *m_addcity32 = nullptr;
    HotCity *m_addcity33 = nullptr;
    HotCity *m_addcity34 = nullptr;
    HotCity *m_addcity35 = nullptr;
    HotCity *m_addcity36 = nullptr;
    HotCity *m_addcity37 = nullptr;

};

#endif // CITYADDHOTVIEW_H
