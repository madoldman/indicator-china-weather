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

#ifndef PROMPTWIDGET_H_
#define PROMPTWIDGET_H_

#include <QFrame>
#include <QMovie>

class QLabel;
class QPushButton;

class PromptWidget : public QFrame
{
    Q_OBJECT
public:
    explicit PromptWidget(QWidget *parent = 0);
    explicit PromptWidget(const QString &text, QWidget *parent = 0, const QString &movieSouce = "", bool movie = false);
    ~PromptWidget();

    void setIconAndText(const QString &iconPath, const QString &text);

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event) Q_DECL_OVERRIDE;//Qt6 起 enterEvent 参数改为 QEnterEvent
#else
    void enterEvent(QEvent *event) Q_DECL_OVERRIDE;
#endif
    void leaveEvent(QEvent *event) Q_DECL_OVERRIDE;
    bool event(QEvent *event) Q_DECL_OVERRIDE;
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE;
    void hideEvent(QHideEvent* event) Q_DECL_OVERRIDE;

private:
    QLabel *m_iconLabel = nullptr;
    QLabel *m_textLabel = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QPushButton *m_retryBtn = nullptr;
    QMovie *m_movie = nullptr;
    bool m_isMovie = false;

signals:
    void requestRetryAccessWeather();
};

#endif // PROMPTWIDGET_H_
