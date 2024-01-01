//
// Aspia Project
// Copyright (C) 2016-2024 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#ifndef CLIENT_UI_SYS_INFO_SYS_INFO_LOCAL_USER_GROUPS_H
#define CLIENT_UI_SYS_INFO_SYS_INFO_LOCAL_USER_GROUPS_H

#include "client/ui/sys_info/sys_info_widget.h"
#include "ui_sys_info_widget_local_user_groups.h"

namespace client {

class SysInfoWidgetLocalUserGroups : public SysInfoWidget
{
    Q_OBJECT

public:
    explicit SysInfoWidgetLocalUserGroups(QWidget* parent = nullptr);
    ~SysInfoWidgetLocalUserGroups() override;

    // SysInfo implementation.
    std::string category() const override;
    void setSystemInfo(const proto::system_info::SystemInfo& system_info) override;
    QTreeWidget* treeWidget() override;

private slots:
    void onContextMenu(const QPoint& point);

private:
    Ui::SysInfoLocalUserGroups ui;
};

} // namespace client

#endif // CLIENT_UI_SYS_INFO_SYS_INFO_LOCAL_USER_GROUPS_H
