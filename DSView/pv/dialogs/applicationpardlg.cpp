/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 * 
 * Copyright (C) 2021 DreamSourceLab <support@dreamsourcelab.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "applicationpardlg.h"
#include "dsdialog.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QString>
#include <QFontDatabase>
#include <QGroupBox>
#include <QLabel>
#include <vector>
#include <QGridLayout>
#include <QDoubleSpinBox>

#include "../config/appconfig.h"
#include "../ui/langresource.h"
#include "../appcontrol.h"
#include "../sigsession.h"
#include "../ui/dscombobox.h"
#include "../log.h"

namespace pv
{
namespace dialogs
{

ApplicationParamDlg::ApplicationParamDlg()
{
   
}

ApplicationParamDlg::~ApplicationParamDlg()
{
}

void ApplicationParamDlg::bind_font_name_list(QComboBox *box, QString v)
{   
    int selDex = -1;

    QString defName(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_DEFAULT_FONT), "Default"));
    box->addItem(defName);

    if (_font_name_list.size() == 0)
    {
        QFontDatabase fDataBase;
        _font_name_list = fDataBase.families();
    }
   
    for (QString family : _font_name_list) {
        if (family.indexOf("[") == -1)
        {
            box->addItem(family);

            if (selDex == -1 && family == v){
                selDex = box->count() - 1;
            }
        }
    }

    if (selDex == -1)
        selDex = 0;

    box->setCurrentIndex(selDex);
}

void ApplicationParamDlg::bind_font_size_list(QComboBox *box, float size)
{   
    int selDex = -1;

    float minSize = 0;
    float maxSize = 0;

    AppConfig::GetFontSizeRange(&minSize, &maxSize);

    for(int i=minSize; i<=maxSize; i++)
    {
        box->addItem(QString::number(i));
        if (i == size){
            selDex = box->count() - 1;
        }
    }
    if (selDex == -1)
        selDex = 2;
    box->setCurrentIndex(selDex);
}

bool ApplicationParamDlg::ShowDlg(QWidget *parent) {
    DSDialog dlg(parent, true, true);
    dlg.setTitle(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_DISPLAY_OPTIONS), "Display options"));
    dlg.setMinimumSize(300, 230);

    QVBoxLayout *lay = new QVBoxLayout();
    lay->setContentsMargins(0, 10, 0, 20);
    lay->setSpacing(8);

    // show config
    AppConfig &app = AppConfig::Instance();

    QCheckBox *ck_quickScroll = new QCheckBox();
    ck_quickScroll->setChecked(app.appOptions.quickScroll);

    QCheckBox *ck_trigInMid = new QCheckBox();
    ck_trigInMid->setChecked(app.appOptions.trigPosDisplayInMid);

    QCheckBox *ck_profileBar = new QCheckBox();
    ck_profileBar->setChecked(app.appOptions.displayProfileInBar);

    QCheckBox *ck_abortData = new QCheckBox();
    ck_abortData->setChecked(app.appOptions.swapBackBufferAlways);

    QCheckBox *ck_autoScrollLatestData = new QCheckBox();
    ck_autoScrollLatestData->setChecked(app.appOptions.autoScrollLatestData);

    QComboBox *ftCbSize = new DsComboBox();
    ftCbSize->setFixedWidth(50);
    bind_font_size_list(ftCbSize, app.appOptions.fontSize);

    QDoubleSpinBox *sb_scrollSpeed = new QDoubleSpinBox();
    sb_scrollSpeed->setRange(0.01, 10.0);
    sb_scrollSpeed->setSingleStep(0.05);
    sb_scrollSpeed->setValue(app.appOptions.scrollSpeed);

    // Logic group
    QGroupBox *logicGroup = new QGroupBox(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_GROUP_LOGIC), "Logic"));
    QGridLayout *logicLay = new QGridLayout();
    logicLay->setContentsMargins(10, 15, 15, 10);
    logicLay->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    logicGroup->setLayout(logicLay);
    logicLay->addWidget(new QLabel(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_QUICK_SCROLL), "Quick scroll")), 0, 0, Qt::AlignLeft);
    logicLay->addWidget(ck_quickScroll, 0, 1, Qt::AlignRight);
    logicLay->addWidget(new QLabel(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_USE_ABORT_DATA_REPEAT), "Used abort data")), 1, 0, Qt::AlignLeft);
    logicLay->addWidget(ck_abortData, 1, 1, Qt::AlignRight);
    logicLay->addWidget(new QLabel(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_AUTO_SCROLL_LATEAST_DATA), "Auto scroll latest")), 2, 0, Qt::AlignLeft);
    logicLay->addWidget(ck_autoScrollLatestData, 2, 1, Qt::AlignRight);
    lay->addWidget(logicGroup);

    // Scope group
    QGroupBox *dsoGroup = new QGroupBox(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_GROUP_DSO), "Scope"));
    QGridLayout *dsoLay = new QGridLayout();
    dsoLay->setContentsMargins(10, 15, 15, 10);
    dsoLay->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    dsoGroup->setLayout(dsoLay);
    dsoLay->addWidget(new QLabel(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_TRIG_DISPLAY_MIDDLE), "Trig pos in middle")), 0, 0, Qt::AlignLeft);
    dsoLay->addWidget(ck_trigInMid, 0, 1, Qt::AlignRight);
    lay->addWidget(dsoGroup);

    // UI
    QGroupBox *uiGroup = new QGroupBox(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_GROUP_UI), "UI"));
    QGridLayout *uiLay = new QGridLayout();
    uiLay->setContentsMargins(10, 15, 15, 10);
    uiLay->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    uiGroup->setLayout(uiLay);
    uiLay->addWidget(new QLabel(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_DISPLAY_PROFILE_IN_BAR), "Profile in bar")), 0, 0, Qt::AlignLeft);
    uiLay->addWidget(ck_profileBar, 0, 1, Qt::AlignRight);
    uiLay->addWidget(new QLabel(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_FONT_SIZE), "Font size")), 1, 0, Qt::AlignLeft);
    uiLay->addWidget(ftCbSize, 1, 1, Qt::AlignRight);
    uiLay->addWidget(new QLabel(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_SCROLL_SPEED), "Scroll speed")), 2, 0, Qt::AlignLeft);
    uiLay->addWidget(sb_scrollSpeed, 2, 1, Qt::AlignRight);
    lay->addWidget(uiGroup);

    dlg.layout()->addLayout(lay);
    dlg.exec();
    bool ret = dlg.IsClickYes();

    // save config
    if (ret) {
        bool bAppChanged = false;
        bool bFontChanged = false;
        float fSize = ftCbSize->currentText().toFloat();
        float scrollSpeed = sb_scrollSpeed->value();

        if (app.appOptions.quickScroll != ck_quickScroll->isChecked()) {
            app.appOptions.quickScroll = ck_quickScroll->isChecked();
            bAppChanged = true;
        }
        if (app.appOptions.trigPosDisplayInMid != ck_trigInMid->isChecked()) {
            app.appOptions.trigPosDisplayInMid = ck_trigInMid->isChecked();
            bAppChanged = true;
        }
        if (app.appOptions.displayProfileInBar != ck_profileBar->isChecked()) {
            app.appOptions.displayProfileInBar = ck_profileBar->isChecked();
            bAppChanged = true;
        }
        if (app.appOptions.swapBackBufferAlways != ck_abortData->isChecked()) {
            app.appOptions.swapBackBufferAlways = ck_abortData->isChecked();
            bAppChanged = true;
        }
        if (app.appOptions.fontSize != fSize) {
            app.appOptions.fontSize = fSize;
            bFontChanged = true;
        }
        if (app.appOptions.autoScrollLatestData != ck_autoScrollLatestData->isChecked()) {
            app.appOptions.autoScrollLatestData = ck_autoScrollLatestData->isChecked();
            bAppChanged = true;
        }
        if (app.appOptions.scrollSpeed != scrollSpeed) {
            app.appOptions.scrollSpeed = scrollSpeed;
            bAppChanged = true;
        }

        if (bAppChanged) {
            app.SaveApp();
            AppControl::Instance()->GetSession()->broadcast_msg(DSV_MSG_APP_OPTIONS_CHANGED);
        }

        if (bFontChanged) {
            if (!bAppChanged) {
                app.SaveApp();
            }
            AppControl::Instance()->GetSession()->broadcast_msg(DSV_MSG_FONT_OPTIONS_CHANGED);
        }
    }

    return ret;
}
 

} //
}//

