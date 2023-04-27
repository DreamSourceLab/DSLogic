/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 *
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
 * Copyright (C) 2013 DreamSourceLab <support@dreamsourcelab.com>
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

#include <QAction>
#include <QButtonGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QDesktopServices>
#include <QKeyEvent>
#include <QEvent>
#include <QtGlobal>
#include <QApplication>
#include <QStandardPaths>
#include <QScreen>
#include <QTimer>
#include <libusb-1.0/libusb.h>
#include <QGuiApplication>
#include <QTextStream>
#include <QJsonValue>
#include <QJsonArray>

#ifdef _WIN32
#include <QDesktopWidget>
#endif

#include "mainwindow.h"

#include "data/logicsnapshot.h"
#include "data/dsosnapshot.h"
#include "data/analogsnapshot.h"

#include "dialogs/about.h"
#include "dialogs/deviceoptions.h"
#include "dialogs/storeprogress.h"
#include "dialogs/waitingdialog.h"
#include "dialogs/regionoptions.h"

#include "toolbars/samplingbar.h"
#include "toolbars/trigbar.h"
#include "toolbars/filebar.h"
#include "toolbars/logobar.h"
#include "toolbars/titlebar.h"

#include "dock/triggerdock.h"
#include "dock/dsotriggerdock.h"
#include "dock/measuredock.h"
#include "dock/searchdock.h"
#include "dock/protocoldock.h"

#include "view/view.h"
#include "view/trace.h"
#include "view/signal.h"
#include "view/dsosignal.h"
#include "view/logicsignal.h"
#include "view/analogsignal.h"

/* __STDC_FORMAT_MACROS is required for PRIu64 and friends (in C++). */
#include <inttypes.h>
#include <stdint.h>
#include <stdarg.h>
#include <glib.h>
#include <list>
#include "ui/msgbox.h"
#include "config/appconfig.h"
#include "appcontrol.h"
#include "dsvdef.h"
#include "appcontrol.h"
#include "utility/encoding.h"
#include "utility/path.h"
#include "log.h"
#include "sigsession.h"
#include "deviceagent.h"
#include <stdlib.h>
#include "ZipMaker.h"
#include "ui/langresource.h"
#include "mainframe.h"
#include "dsvdef.h"

namespace pv
{

    MainWindow::MainWindow(QWidget *parent)
        : QMainWindow(parent)
    {
        _msg = NULL;

        _session = AppControl::Instance()->GetSession();
        _session->set_callback(this);
        _device_agent = _session->get_device();
        _session->add_msg_listener(this);

        _is_auto_switch_device = false;
        _is_save_confirm_msg = false;

        setup_ui();

        setContextMenuPolicy(Qt::NoContextMenu);

        _key_vaild = false;
        _last_key_press_time = high_resolution_clock::now();
    }

    void MainWindow::setup_ui()
    {
        setObjectName(QString::fromUtf8("MainWindow"));
        setContentsMargins(0, 0, 0, 0);
        layout()->setSpacing(0);

        // Setup the central widget
        _central_widget = new QWidget(this);
        _vertical_layout = new QVBoxLayout(_central_widget);
        _vertical_layout->setSpacing(0);
        _vertical_layout->setContentsMargins(0, 0, 0, 0);
        setCentralWidget(_central_widget);

        // Setup the sampling bar
        _sampling_bar = new toolbars::SamplingBar(_session, this);
        _sampling_bar->setObjectName("sampling_bar");
        _trig_bar = new toolbars::TrigBar(_session, this);
        _trig_bar->setObjectName("trig_bar");
        _file_bar = new toolbars::FileBar(_session, this);
        _file_bar->setObjectName("file_bar");
        _logo_bar = new toolbars::LogoBar(_session, this);
        _logo_bar->setObjectName("logo_bar");

        // trigger dock
        _trigger_dock = new QDockWidget(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_TRIGGER_DOCK_TITLE), "Trigger Setting..."), this);
        _trigger_dock->setObjectName("trigger_dock");
        _trigger_dock->setFeatures(QDockWidget::DockWidgetMovable);
        _trigger_dock->setAllowedAreas(Qt::RightDockWidgetArea);
        _trigger_dock->setVisible(false);
        _trigger_widget = new dock::TriggerDock(_trigger_dock, _session);
        _trigger_dock->setWidget(_trigger_widget);

        _dso_trigger_dock = new QDockWidget(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_TRIGGER_DOCK_TITLE), "Trigger Setting..."), this);
        _dso_trigger_dock->setObjectName("dso_trigger_dock");
        _dso_trigger_dock->setFeatures(QDockWidget::DockWidgetMovable);
        _dso_trigger_dock->setAllowedAreas(Qt::RightDockWidgetArea);
        _dso_trigger_dock->setVisible(false);
        _dso_trigger_widget = new dock::DsoTriggerDock(_dso_trigger_dock, _session);
        _dso_trigger_dock->setWidget(_dso_trigger_widget);

        // Setup _view widget
        _view = new pv::view::View(_session, _sampling_bar, this);
        _vertical_layout->addWidget(_view);

        setIconSize(QSize(40, 40));
        addToolBar(_sampling_bar);
        addToolBar(_trig_bar);
        addToolBar(_file_bar);
        addToolBar(_logo_bar);

        // Setup the dockWidget
        _protocol_dock = new QDockWidget(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_PROTOCOL_DOCK_TITLE), "Protocol"), this);
        _protocol_dock->setObjectName("protocol_dock");
        _protocol_dock->setFeatures(QDockWidget::DockWidgetMovable);
        _protocol_dock->setAllowedAreas(Qt::RightDockWidgetArea);
        _protocol_dock->setVisible(false);
        _protocol_widget = new dock::ProtocolDock(_protocol_dock, *_view, _session);
        _protocol_dock->setWidget(_protocol_widget);

        // measure dock
        _measure_dock = new QDockWidget(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_MEASURE_DOCK_TITLE), "Measurement"), this);
        _measure_dock->setObjectName("measure_dock");
        _measure_dock->setFeatures(QDockWidget::DockWidgetMovable);
        _measure_dock->setAllowedAreas(Qt::RightDockWidgetArea);
        _measure_dock->setVisible(false);
        _measure_widget = new dock::MeasureDock(_measure_dock, *_view, _session);
        _measure_dock->setWidget(_measure_widget);
        // search dock
        _search_dock = new QDockWidget(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_SEARCH_DOCK_TITLE), "Search..."), this);
        _search_dock->setObjectName("search_dock");
        _search_dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        _search_dock->setTitleBarWidget(new QWidget(_search_dock));
        _search_dock->setAllowedAreas(Qt::BottomDockWidgetArea);
        _search_dock->setVisible(false);
        // dock::SearchDock *_search_widget = new dock::SearchDock(_search_dock, *_view, _session);
        _search_widget = new dock::SearchDock(_search_dock, *_view, _session);
        _search_dock->setWidget(_search_widget);

        addDockWidget(Qt::RightDockWidgetArea, _protocol_dock);
        addDockWidget(Qt::RightDockWidgetArea, _trigger_dock);
        addDockWidget(Qt::RightDockWidgetArea, _dso_trigger_dock);
        addDockWidget(Qt::RightDockWidgetArea, _measure_dock);
        addDockWidget(Qt::BottomDockWidgetArea, _search_dock);
        
        // Set the title
        QString title = QApplication::applicationName() + " v" + QApplication::applicationVersion();
        setWindowTitle(QApplication::translate("MainWindow", title.toLocal8Bit().data(), 0));

        // event filter
        _view->installEventFilter(this);
        _sampling_bar->installEventFilter(this);
        _trig_bar->installEventFilter(this);
        _file_bar->installEventFilter(this);
        _logo_bar->installEventFilter(this);
        _dso_trigger_dock->installEventFilter(this);
        _trigger_dock->installEventFilter(this);
        _protocol_dock->installEventFilter(this);
        _measure_dock->installEventFilter(this);
        _search_dock->installEventFilter(this);

        // defaut language
        AppConfig &app = AppConfig::Instance();
        switchLanguage(app._frameOptions.language);
        switchTheme(app._frameOptions.style);

        // UI initial
        _measure_widget->add_dist_measure();

        retranslateUi();

        _sampling_bar->set_view(_view);

        // event
        connect(&_event, SIGNAL(session_error()), this, SLOT(on_session_error()));
        connect(&_event, SIGNAL(signals_changed()), this, SLOT(on_signals_changed()));
        connect(&_event, SIGNAL(receive_trigger(quint64)), this, SLOT(on_receive_trigger(quint64)));
        connect(&_event, SIGNAL(frame_ended()), this, SLOT(on_frame_ended()), Qt::DirectConnection);
        connect(&_event, SIGNAL(frame_began()), this, SLOT(on_frame_began()), Qt::DirectConnection);
        connect(&_event, SIGNAL(decode_done()), this, SLOT(on_decode_done()));
        connect(&_event, SIGNAL(data_updated()), this, SLOT(on_data_updated()));
        connect(&_event, SIGNAL(cur_snap_samplerate_changed()), this, SLOT(on_cur_snap_samplerate_changed()));
        connect(&_event, SIGNAL(receive_data_len(quint64)), this, SLOT(on_receive_data_len(quint64)));
        connect(&_event, SIGNAL(trigger_message(int)), this, SLOT(on_trigger_message(int)));

        // view
        connect(_view, SIGNAL(cursor_update()), _measure_widget, SLOT(cursor_update()));
        connect(_view, SIGNAL(cursor_moving()), _measure_widget, SLOT(cursor_moving()));
        connect(_view, SIGNAL(cursor_moved()), _measure_widget, SLOT(reCalc()));
        connect(_view, SIGNAL(prgRate(int)), this, SIGNAL(prgRate(int)));
        connect(_view, SIGNAL(auto_trig(int)), _dso_trigger_widget, SLOT(auto_trig(int)));

        // trig_bar
        connect(_trig_bar, SIGNAL(sig_protocol(bool)), this, SLOT(on_protocol(bool)));
        connect(_trig_bar, SIGNAL(sig_trigger(bool)), this, SLOT(on_trigger(bool)));
        connect(_trig_bar, SIGNAL(sig_measure(bool)), this, SLOT(on_measure(bool)));
        connect(_trig_bar, SIGNAL(sig_search(bool)), this, SLOT(on_search(bool)));
        connect(_trig_bar, SIGNAL(sig_setTheme(QString)), this, SLOT(switchTheme(QString)));
        connect(_trig_bar, SIGNAL(sig_show_lissajous(bool)), _view, SLOT(show_lissajous(bool)));

        // file toolbar
        connect(_file_bar, SIGNAL(sig_load_file(QString)), this, SLOT(on_load_file(QString)));
        connect(_file_bar, SIGNAL(sig_save()), this, SLOT(on_save()));
        connect(_file_bar, SIGNAL(sig_export()), this, SLOT(on_export()));
        connect(_file_bar, SIGNAL(sig_screenShot()), this, SLOT(on_screenShot()), Qt::QueuedConnection);
        connect(_file_bar, SIGNAL(sig_load_session(QString)), this, SLOT(on_load_session(QString)));
        connect(_file_bar, SIGNAL(sig_store_session(QString)), this, SLOT(on_store_session(QString)));

        // logobar
        connect(_logo_bar, SIGNAL(sig_open_doc()), this, SLOT(on_open_doc()));

        connect(_protocol_widget, SIGNAL(protocol_updated()), this, SLOT(on_signals_changed()));

        // SamplingBar
        connect(_sampling_bar, SIGNAL(sig_store_session_data()), this, SLOT(on_save()));

        //
        connect(_dso_trigger_widget, SIGNAL(set_trig_pos(int)), _view, SLOT(set_trig_pos(int)));

        _logo_bar->set_mainform_callback(this);

        // Try load from file.
        QString ldFileName(AppControl::Instance()->_open_file_name.c_str());
        if (ldFileName != "")
        {
            if (QFile::exists(ldFileName))
            {
                dsv_info("auto load file:%s", ldFileName.toUtf8().data());
                on_load_file(ldFileName);
            }
            else
            {
                dsv_err("file is not exists:%s", ldFileName.toUtf8().data());
                MsgBox::Show(L_S(STR_PAGE_MSG, S_ID(IDS_MSG_OPEN_FILE_ERROR), "Open file error!"), ldFileName, NULL);
            }
        }
        else
        {
            _session->set_default_device();
        }
    }

    //*
    void MainWindow::retranslateUi()
    {
        _trigger_dock->setWindowTitle(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_TRIGGER_DOCK_TITLE), "Trigger Setting..."));
        _dso_trigger_dock->setWindowTitle(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_TRIGGER_DOCK_TITLE), "Trigger Setting..."));
        _protocol_dock->setWindowTitle(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_PROTOCOL_DOCK_TITLE), "Protocol"));
        _measure_dock->setWindowTitle(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_MEASURE_DOCK_TITLE), "Measurement"));
        _search_dock->setWindowTitle(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_SEARCH_DOCK_TITLE), "Search..."));
    }

    void MainWindow::on_load_file(QString file_name)
    {
        try
        {
            if (_device_agent->is_hardware()){
                session_save();
            }

            _session->set_file(file_name);
        }
        catch (QString e)
        {
            show_error(L_S(STR_PAGE_MSG, S_ID(IDS_MSG_FAIL_TO_LOAD), "Failed to load ") + file_name);
            _session->set_default_device();
        }
    }

    void MainWindow::show_error(QString error)
    {
        MsgBox::Show(NULL, error.toStdString().c_str(), this);
    }

    void MainWindow::session_error()
    {
        _event.session_error();
    }

    void MainWindow::on_session_error()
    {
        QString title;
        QString details;
        QString ch_status = "";
        uint64_t error_pattern;

        switch (_session->get_error())
        {
        case SigSession::Hw_err:
            dsv_info("MainWindow::on_session_error(),Hw_err, stop capture");
            _session->stop_capture();
            title = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_HARDWARE_ERROR), "Hardware Operation Failed");
            details = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_HARDWARE_ERROR_DET), 
                      "Please replug device to refresh hardware configuration!");
            break;
        case SigSession::Malloc_err:
            dsv_info("MainWindow::on_session_error(),Malloc_err, stop capture");
            _session->stop_capture();
            title = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_MALLOC_ERROR), "Malloc Error");
            details = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_MALLOC_ERROR_DET), 
                      "Memory is not enough for this sample!\nPlease reduce the sample depth!");
            break;
        case SigSession::Pkt_data_err:
            title = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_PACKET_ERROR), "Packet Error");
            details = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_PACKET_ERROR_DET), 
            "the content of received packet are not expected!");
            _session->refresh(0);
            break;
        case SigSession::Data_overflow:
            dsv_info("MainWindow::on_session_error(),Data_overflow, stop capture");
            _session->stop_capture();
            title = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_DATA_OVERFLOW), "Data Overflow");
            details = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_DATA_OVERFLOW_DET), 
                      "USB bandwidth can not support current sample rate! \nPlease reduce the sample rate!");
            break;
        default:
            title = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_UNDEFINED_ERROR), "Undefined Error");
            details = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_UNDEFINED_ERROR_DET), "Not expected error!");
            break;
        }

        dialogs::DSMessageBox msg(this);

        connect(_session->device_event_object(), SIGNAL(device_updated()), &msg, SLOT(accept()));

        QFont font("Monaco");
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
        msg.mBox()->setFont(font);

        msg.mBox()->setText(title);
        msg.mBox()->setInformativeText(details);
        msg.mBox()->setStandardButtons(QMessageBox::Ok);
        msg.mBox()->setIcon(QMessageBox::Warning);
        msg.exec();

        _session->clear_error();
    }

    void MainWindow::session_save()
    { 
        if (_device_agent->have_instance() == false)
        {
            dsv_info("%s", "There is no need to save the configuration");
            return;
        }

        AppConfig &app = AppConfig::Instance();        

        if (_device_agent->is_hardware()){
            QString sessionFile = genSessionFileName(true);
            on_store_session(sessionFile);
        }

        app._frameOptions.windowState = saveState();
        app.SaveFrame();
    }

    QString MainWindow::genSessionFileName(bool isNewFormat)
    {
#if QT_VERSION >= 0x050400
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
        QString path = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
#endif

        AppConfig &app = AppConfig::Instance();

        QDir dir(path);
        if (dir.exists() == false){
            dir.mkpath(path);
        } 

        QString driver_name = _device_agent->driver_name();
        QString mode_name = QString::number(_device_agent->get_work_mode()); 
        QString lang_name;
        QString base_path = dir.absolutePath() + "/" + driver_name + mode_name;

        if (!isNewFormat){
            lang_name = QString::number(app._frameOptions.language);           
        }

        return base_path + ".ses" + lang_name + ".dsc";
    }

    bool MainWindow::able_to_close()
    {
        if (_device_agent->is_hardware() && _session->have_hardware_data() == false){
            _sampling_bar->commit_settings();
        }
        // not used, refer to closeEvent of mainFrame
        session_save();
        
        if (confirm_to_store_data()){
            on_save();
            return false; 
        } 
        return true;
    }

    void MainWindow::on_protocol(bool visible)
    {
        _protocol_dock->setVisible(visible);

        if (!visible)
            _view->setFocus();
    }

    void MainWindow::on_trigger(bool visible)
    {
        if (_device_agent->get_work_mode() != DSO)
        {
            _trigger_widget->update_view();
            _trigger_dock->setVisible(visible);
            _dso_trigger_dock->setVisible(false);
        }
        else
        {
            _dso_trigger_widget->update_view();
            _trigger_dock->setVisible(false);
            _dso_trigger_dock->setVisible(visible);
        }

        if (!visible)
            _view->setFocus();
    }

    void MainWindow::on_measure(bool visible)
    {
        _measure_dock->setVisible(visible);

        if (!visible)
            _view->setFocus();
    }

    void MainWindow::on_search(bool visible)
    {
        _search_dock->setVisible(visible);
        _view->show_search_cursor(visible);

        if (!visible)
            _view->setFocus();
    }

    void MainWindow::on_screenShot()
    {
        AppConfig &app = AppConfig::Instance();
        QString default_name = app._userHistory.screenShotPath + "/" + APP_NAME + QDateTime::currentDateTime().toString("-yyMMdd-hhmmss");

#ifdef _WIN32
        int x = parentWidget()->pos().x();
        int y = parentWidget()->pos().y();
        int w = parentWidget()->frameGeometry().width();
        int h = parentWidget()->frameGeometry().height();
        QDesktopWidget *desktop = QApplication::desktop();
        QPixmap pixmap = QGuiApplication::primaryScreen()->grabWindow(desktop->winId(), x, y, w, h);
#elif __APPLE__
        int x = parentWidget()->pos().x() + MainFrame::Margin;
        int y = parentWidget()->pos().y() + MainFrame::Margin;
        int w = parentWidget()->geometry().width() - MainFrame::Margin * 2;
        int h = parentWidget()->geometry().height() - MainFrame::Margin * 2;
        QPixmap pixmap = QGuiApplication::primaryScreen()->grabWindow(winId(), x, y, w, h);
#else
        QPixmap pixmap = QGuiApplication::primaryScreen()->grabWindow(winId());
#endif

        QString format = "png";
        QString fileName = QFileDialog::getSaveFileName(
            this,
            L_S(STR_PAGE_DLG, S_ID(IDS_DLG_SAVE_AS), "Save As"),
            default_name,
            "png file(*.png);;jpeg file(*.jpeg)",
            &format);

        if (!fileName.isEmpty())
        {
            QStringList list = format.split('.').last().split(')');
            QString suffix = list.first();

            QFileInfo f(fileName);
            if (f.suffix().compare(suffix))
            {
                //tr
                fileName += "." + suffix;
            }

            pixmap.save(fileName, suffix.toLatin1());

            fileName = path::GetDirectoryName(fileName);

            if (app._userHistory.screenShotPath != fileName)
            {
                app._userHistory.screenShotPath = fileName;
                app.SaveHistory();
            }
        }
    }

    // save file
    void MainWindow::on_save()
    {
        using pv::dialogs::StoreProgress;

        if (_device_agent->have_instance() == false)
        {
            dsv_info("%s", "Have no device, can't to save data.");
            return;
        }

        if (_session->is_working()){
            dsv_info("Save data: stop the current device."); 
            _session->stop_capture();
        }

        _session->set_saving(true);

        StoreProgress *dlg = new StoreProgress(_session, this);
        dlg->save_run(this);
    }

    void MainWindow::on_export()
    {
        using pv::dialogs::StoreProgress;

        if (_session->is_working()){
            dsv_info("Export data: stop the current device."); 
            _session->stop_capture();
        }

        StoreProgress *dlg = new StoreProgress(_session, this);
        dlg->export_run();
    }

    bool MainWindow::on_load_session(QString name)
    {
        if (name == ""){
            dsv_err("%s", "Session file name is empty.");
            assert(false);
        }

        dsv_info("Load session file: \"%s\"", name.toLocal8Bit().data());

        QFile sf(name);
        bool bDone;

        if (!sf.exists()){
            dsv_warn("Warning: session file is not exists: \"%s\"", name.toLocal8Bit().data());
            return false;
        }

        if (!sf.open(QIODevice::ReadOnly))
        {
            dsv_warn("%s", "Warning: Couldn't open session file to load!");
            return false;
        }

        QString sdata = QString::fromUtf8(sf.readAll());
        QJsonDocument sessionDoc = QJsonDocument::fromJson(sdata.toUtf8());

        _protocol_widget->del_all_protocol();
        int ret = load_session_json(sessionDoc, bDone);

        if (ret && _device_agent->get_work_mode() == DSO)
        {
            _dso_trigger_widget->update_view();
        }

        return ret;
    }

    bool MainWindow::gen_session_json(QJsonObject &sessionVar)
    {
        AppConfig &app = AppConfig::Instance();

        GVariant *gvar_opts;
        GVariant *gvar;
        gsize num_opts;

        QString title = QApplication::applicationName() + " v" + QApplication::applicationVersion();

        QJsonArray channelVar;
        sessionVar["Version"] = QJsonValue::fromVariant(SESSION_FORMAT_VERSION);
        sessionVar["Device"] = QJsonValue::fromVariant(_device_agent->driver_name());
        sessionVar["DeviceMode"] = QJsonValue::fromVariant(_device_agent->get_work_mode());
        sessionVar["Language"] = QJsonValue::fromVariant(app._frameOptions.language);
        sessionVar["Title"] = QJsonValue::fromVariant(title);

        gvar_opts = _device_agent->get_config_list(NULL, SR_CONF_DEVICE_SESSIONS);
        if (gvar_opts == NULL)
        {
            dsv_warn("%s", "Device config list is empty. id:SR_CONF_DEVICE_SESSIONS");
            /* Driver supports no device instance sessions. */
            return false;
        }

        const int *const options = (const int32_t *)g_variant_get_fixed_array(
                                        gvar_opts, &num_opts, sizeof(int32_t));

        for (unsigned int i = 0; i < num_opts; i++)
        {
            const struct sr_config_info *const info = _device_agent->get_config_info(options[i]);
            gvar = _device_agent->get_config(NULL, NULL, info->key);
            if (gvar != NULL)
            {
                if (info->datatype == SR_T_BOOL)
                    sessionVar[info->name] = QJsonValue::fromVariant(g_variant_get_boolean(gvar));
                else if (info->datatype == SR_T_UINT64)
                    sessionVar[info->name] = QJsonValue::fromVariant(QString::number(g_variant_get_uint64(gvar)));
                else if (info->datatype == SR_T_UINT8)
                    sessionVar[info->name] = QJsonValue::fromVariant(g_variant_get_byte(gvar));
                 else if (info->datatype == SR_T_INT16)
                    sessionVar[info->name] = QJsonValue::fromVariant(g_variant_get_int16(gvar));
                else if (info->datatype == SR_T_FLOAT) //save as string format
                    sessionVar[info->name] = QJsonValue::fromVariant(QString::number(g_variant_get_double(gvar)));
                else if (info->datatype == SR_T_CHAR)
                    sessionVar[info->name] = QJsonValue::fromVariant(g_variant_get_string(gvar, NULL));
                else if (info->datatype == SR_T_LIST)
                    sessionVar[info->name] =  QJsonValue::fromVariant(g_variant_get_int16(gvar));
                else{
                    dsv_err("Unkown config info type:%d", info->datatype);
                    assert(false);
                }
                g_variant_unref(gvar);                
            }
        }

        for (auto s : _session->get_signals())
        {
            QJsonObject s_obj;
            s_obj["index"] = s->get_index();
            s_obj["type"] = s->get_type();
            s_obj["enabled"] = s->enabled();
            s_obj["name"] = s->get_name();

            if (s->get_colour().isValid())
                s_obj["colour"] = QJsonValue::fromVariant(s->get_colour());
            else
                s_obj["colour"] = QJsonValue::fromVariant("default");

            view::LogicSignal *logicSig = NULL;
            if ((logicSig = dynamic_cast<view::LogicSignal *>(s)))
            {
                s_obj["strigger"] = logicSig->get_trig();
            }
            
            if (s->signal_type() == DSO_SIGNAL)
            {
                view::DsoSignal *dsoSig = (view::DsoSignal*)s;
                s_obj["vdiv"] = QJsonValue::fromVariant(static_cast<qulonglong>(dsoSig->get_vDialValue()));
                s_obj["vfactor"] = QJsonValue::fromVariant(static_cast<qulonglong>(dsoSig->get_factor()));
                s_obj["coupling"] = dsoSig->get_acCoupling();
                s_obj["trigValue"] = dsoSig->get_trig_vrate();
                s_obj["zeroPos"] = dsoSig->get_zero_ratio();
            }
 
            if (s->signal_type() == ANALOG_SIGNAL)
            {
                view::AnalogSignal *analogSig = (view::AnalogSignal*)s;
                s_obj["vdiv"] = QJsonValue::fromVariant(static_cast<qulonglong>(analogSig->get_vdiv()));
                s_obj["vfactor"] = QJsonValue::fromVariant(static_cast<qulonglong>(analogSig->get_factor()));
                s_obj["coupling"] = analogSig->get_acCoupling();
                s_obj["zeroPos"] = analogSig->get_zero_ratio();
                s_obj["mapUnit"] = analogSig->get_mapUnit();
                s_obj["mapMin"] = analogSig->get_mapMin();
                s_obj["mapMax"] = analogSig->get_mapMax();
                s_obj["mapDefault"] = analogSig->get_mapDefault();
            }
            channelVar.append(s_obj);
        }
        sessionVar["channel"] = channelVar;

        if (_device_agent->get_work_mode() == LOGIC)
        {
            sessionVar["trigger"] = _trigger_widget->get_session();
        }

        StoreSession ss(_session);
        QJsonArray decodeJson;
        ss.json_decoders(decodeJson);
        sessionVar["decoder"] = decodeJson;

        if (_device_agent->get_work_mode() == DSO)
        {
            sessionVar["measure"] = _view->get_viewstatus()->get_session();
        }

        if (gvar_opts != NULL)
            g_variant_unref(gvar_opts);

        return true;
    }

    bool MainWindow::load_session_json(QJsonDocument json, bool &haveDecoder)
    {
        haveDecoder = false;

        QJsonObject sessionObj = json.object();

        int mode = _device_agent->get_work_mode();

        // check session file version
        if (!sessionObj.contains("Version"))
        {
            dsv_dbg("%s", "session file version is not exists!");
            return false;
        }

        int format_ver = sessionObj["Version"].toInt();

        if (format_ver < 2)
        {
            dsv_err("%s", "session file version is error!");
            return false;
        }            

        int conf_dev_mode = sessionObj["DeviceMode"].toInt();

        if (_device_agent->is_hardware())
        {
            QString driverName = _device_agent->driver_name();
            QString sessionDevice = sessionObj["Device"].toString();            
            // check device and mode
            if (driverName != sessionDevice || mode != conf_dev_mode)
            {
                MsgBox::Show(NULL, L_S(STR_PAGE_MSG, S_ID(IDS_MSG_NOT_COMPATIBLE), "Session File is not compatible with current device or mode!"), this);
                return false;
            }
        }

        // load device settings
        GVariant *gvar_opts = _device_agent->get_config_list(NULL, SR_CONF_DEVICE_SESSIONS);
        gsize num_opts;

        if (gvar_opts != NULL)
        {
            const int *const options = (const int32_t *)g_variant_get_fixed_array(
                gvar_opts, &num_opts, sizeof(int32_t));

            for (unsigned int i = 0; i < num_opts; i++)
            {
                const struct sr_config_info *info = _device_agent->get_config_info(options[i]);

                if (!sessionObj.contains(info->name))
                    continue;

                GVariant *gvar = NULL;
                int id = 0;

                if (info->datatype == SR_T_BOOL){
                    gvar = g_variant_new_boolean(sessionObj[info->name].toInt());
                }
                else if (info->datatype == SR_T_UINT64){
                    //from string text.
                    gvar = g_variant_new_uint64(sessionObj[info->name].toString().toULongLong());         
                }
                else if (info->datatype == SR_T_UINT8){
                    if (sessionObj[info->name].toString() != "")
                        gvar = g_variant_new_byte(sessionObj[info->name].toString().toUInt());
                    else
                        gvar = g_variant_new_byte(sessionObj[info->name].toInt());                       
                }
                else if (info->datatype == SR_T_INT16){
                    gvar = g_variant_new_int16(sessionObj[info->name].toInt());
                }
                else if (info->datatype == SR_T_FLOAT){
                    if (sessionObj[info->name].toString() != "")
                        gvar = g_variant_new_double(sessionObj[info->name].toString().toDouble());
                    else
                        gvar = g_variant_new_double(sessionObj[info->name].toDouble()); 
                }
                else if (info->datatype == SR_T_CHAR){
                    gvar = g_variant_new_string(sessionObj[info->name].toString().toLocal8Bit().data());
                }
                else if (info->datatype == SR_T_LIST)
                { 
                    id = 0;

                    if (format_ver > 2){
                        // Is new version format.
                        id = sessionObj[info->name].toInt();
                    }
                    else{
                        const char *fd_key = sessionObj[info->name].toString().toLocal8Bit().data();
                        id = ds_dsl_option_value_to_code(conf_dev_mode, info->key, fd_key);
                        if (id == -1){
                            dsv_err("Convert failed, key:\"%s\", value:\"%s\""
                                ,info->name, fd_key);
                            id = 0; //set default value.
                        }
                        else{
                            dsv_info("Convert success, key:\"%s\", value:\"%s\", get code:%d"
                                ,info->name, fd_key, id);
                        }
                    }            
                    gvar = g_variant_new_int16(id);
                }

                if (gvar == NULL)
                {
                    dsv_warn("Warning: session file, failed to parse key:'%s'", info->name);
                    continue;
                }


                if(_device_agent->is_demo())
                {
                    GVariant *gvar1 = _device_agent->get_config(NULL,NULL,SR_CONF_PATTERN_MODE);
                    const char* pattern = g_variant_get_string(gvar1,NULL);
                    if(_device_agent->get_work_mode() != DSO)
                    {
                        if(strcmp(pattern,"UART") && (strcmp(pattern,"SPI")) && (strcmp(pattern,"EERPOM")))
                        {
                            if(info->key == SR_CONF_LIMIT_SAMPLES)
                                gvar = g_variant_new_uint64(SR_MHZ(1));
                            if(info->key == SR_CONF_SAMPLERATE)
                                gvar = g_variant_new_uint64(SR_MHZ(1));
                        }
                    }
                }

                bool bFlag = _device_agent->set_config(NULL, NULL, info->key, gvar);
                if (!bFlag){
                    dsv_err("Set device config option failed, id:%d, code:%d", info->key, id);
                }   
            }
        }

        // load channel settings
        if (mode == DSO)
        {
            for (const GSList *l = _device_agent->get_channels(); l; l = l->next)
            {
                sr_channel *const probe = (sr_channel *)l->data;
                assert(probe);

                for (const QJsonValue &value : sessionObj["channel"].toArray())
                {
                    QJsonObject obj = value.toObject();
                    if (QString(probe->name) == obj["name"].toString() &&
                        probe->type == obj["type"].toDouble())
                    {
                        probe->vdiv = obj["vdiv"].toDouble();
                        probe->coupling = obj["coupling"].toDouble();
                        probe->vfactor = obj["vfactor"].toDouble();
                        probe->trig_value = obj["trigValue"].toDouble();
                        probe->map_unit = g_strdup(obj["mapUnit"].toString().toStdString().c_str());
                        probe->map_min = obj["mapMin"].toDouble();
                        probe->map_max = obj["mapMax"].toDouble();
                        probe->enabled = obj["enabled"].toBool();
                        break;
                    }
                }
            }
        }
        else
        {
            for (const GSList *l = _device_agent->get_channels(); l; l = l->next)
            {
                sr_channel *const probe = (sr_channel *)l->data;
                assert(probe);
                bool isEnabled = false;

                for (const QJsonValue &value : sessionObj["channel"].toArray())
                {
                    QJsonObject obj = value.toObject();

                    if ((probe->index == obj["index"].toDouble()) &&
                        (probe->type == obj["type"].toDouble()))
                    {
                        isEnabled = true;
                        QString chan_name = obj["name"].toString().trimmed();
                        if (chan_name == ""){
                            chan_name = QString::number(probe->index);
                        }
                        
                        probe->enabled = obj["enabled"].toBool();
                        probe->name = g_strdup(chan_name.toStdString().c_str());
                        probe->vdiv = obj["vdiv"].toDouble();
                        probe->coupling = obj["coupling"].toDouble();
                        probe->vfactor = obj["vfactor"].toDouble();
                        probe->trig_value = obj["trigValue"].toDouble();
                        probe->map_unit = g_strdup(obj["mapUnit"].toString().toStdString().c_str());
                        probe->map_min = obj["mapMin"].toDouble();
                        probe->map_max = obj["mapMax"].toDouble();

                        if (obj.contains("mapDefault"))
                        {
                            probe->map_default = obj["mapDefault"].toBool();
                        }

                        break;
                    }
                }
                if (!isEnabled)
                    probe->enabled = false;
            }
        }

        _session->reload();

        // load signal setting
        if (mode == DSO)
        {
            for (auto s : _session->get_signals())
            {
                for (const QJsonValue &value : sessionObj["channel"].toArray())
                {
                    QJsonObject obj = value.toObject();

                    if (s->get_name() ==  obj["name"].toString() &&
                        s->get_type() ==  obj["type"].toDouble())
                    {
                        s->set_colour(QColor(obj["colour"].toString()));
                       
                        if (s->signal_type() == DSO_SIGNAL)
                        {   
                            view::DsoSignal *dsoSig = (view::DsoSignal*)s;
                            dsoSig->load_settings();
                            dsoSig->set_zero_ratio(obj["zeroPos"].toDouble());
                            dsoSig->set_trig_ratio(obj["trigValue"].toDouble());
                            dsoSig->commit_settings();
                        }
                        break;
                    }
                }
            }
        }
        else
        {
            for (auto s : _session->get_signals())
            {
                for (const QJsonValue &value : sessionObj["channel"].toArray())
                {
                    QJsonObject obj = value.toObject();
                    if ((s->get_index() == obj["index"].toDouble()) &&
                        (s->get_type() == obj["type"].toDouble()))
                    {
                        QString chan_name = obj["name"].toString().trimmed();
                        if (chan_name == ""){
                            chan_name = QString::number(s->get_index());
                        }

                        s->set_colour(QColor(obj["colour"].toString()));
                        s->set_name(chan_name);

                        view::LogicSignal *logicSig = NULL;
                        if ((logicSig = dynamic_cast<view::LogicSignal *>(s)))
                        {
                            logicSig->set_trig(obj["strigger"].toDouble());
                        }
 
                        if (s->signal_type() == DSO_SIGNAL)
                        {
                            view::DsoSignal *dsoSig = (view::DsoSignal*)s;
                            dsoSig->load_settings();
                            dsoSig->set_zero_ratio(obj["zeroPos"].toDouble());
                            dsoSig->set_trig_ratio(obj["trigValue"].toDouble());
                            dsoSig->commit_settings();
                        }
 
                        if (s->signal_type() == ANALOG_SIGNAL)
                        {   
                            view::AnalogSignal *analogSig = (view::AnalogSignal*)s;
                            analogSig->set_zero_ratio(obj["zeroPos"].toDouble());
                            analogSig->commit_settings();
                        }

                        break;
                    }
                }
            }
        }

        // update UI settings
        _sampling_bar->update_sample_rate_list();
        _trigger_widget->device_updated();
        _view->header_updated();

        // load trigger settings
        if (sessionObj.contains("trigger"))
        {
            _trigger_widget->set_session(sessionObj["trigger"].toObject());
        }

        // load decoders
        if (sessionObj.contains("decoder"))
        {
            QJsonArray deArray = sessionObj["decoder"].toArray();
            if (deArray.empty() == false)
            {
                haveDecoder = true;
                StoreSession ss(_session);
                ss.load_decoders(_protocol_widget, deArray);
            }
        }

        // load measure
        if (sessionObj.contains("measure"))
        {
            auto *bottom_bar = _view->get_viewstatus();
            bottom_bar->load_session(sessionObj["measure"].toArray());
        }

        if (gvar_opts != NULL)
            g_variant_unref(gvar_opts);

        return true;
    }

    bool MainWindow::on_store_session(QString name)
    {
        if (name == ""){
            dsv_err("%s", "Session file name is empty.");
            assert(false);
        }

        dsv_info("Store session to file: \"%s\"", name.toLocal8Bit().data());

        QFile sessionFile(name);
        if (!sessionFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            dsv_warn("%s", "Warning: Couldn't open session file to write!");
            return false;
        }

        QTextStream outStream(&sessionFile);
        encoding::set_utf8(outStream);

        QJsonObject sessionVar;
        if (!gen_session_json(sessionVar)){
            return false;
        }

        QJsonDocument sessionDoc(sessionVar);
        outStream << QString::fromUtf8(sessionDoc.toJson());
        sessionFile.close();
        return true;
    }

    bool MainWindow::genSessionData(std::string &str)
    {
        QJsonObject sessionVar;
        if (!gen_session_json(sessionVar))
        {
            return false;
        }

        QJsonDocument sessionDoc(sessionVar);
        QString data = QString::fromUtf8(sessionDoc.toJson());
        str.append(data.toLocal8Bit().data());
        return true;
    }

    void MainWindow::restore_dock()
    { 
        // default dockwidget size
        AppConfig &app = AppConfig::Instance();
        QByteArray st = app._frameOptions.windowState;
        if (!st.isEmpty())
        {
            try
            {
                restoreState(st);
            }
            catch (...)
            {
                MsgBox::Show(NULL, L_S(STR_PAGE_MSG, S_ID(IDS_MSG_RE_WIN_ST_ER), "restore window status error!"));
            }
        }

        // Resotre the dock pannel.
        if (_device_agent->have_instance())
            _trig_bar->reload();
    }

    bool MainWindow::eventFilter(QObject *object, QEvent *event)
    {
        (void)object;
    
        if (event->type() == QEvent::KeyPress)
        {
            const auto &sigs = _session->get_signals();
            QKeyEvent *ke = (QKeyEvent *)event;
            
            int modifier = ke->modifiers();
            if(modifier & Qt::ControlModifier || 
               modifier & Qt::ShiftModifier || 
               modifier & Qt::AltModifier)
            {
                return true;
            }

            high_resolution_clock::time_point key_press_time = high_resolution_clock::now();
            milliseconds timeInterval = std::chrono::duration_cast<milliseconds>(key_press_time - _last_key_press_time);
            int64_t time_keep =  timeInterval.count();
            if (time_keep < 200){
                return true;
            }
            _last_key_press_time = key_press_time;           
            
            switch (ke->key())
            {
            case Qt::Key_S:
                _sampling_bar->run_or_stop();
                break;

            case Qt::Key_I:
                _sampling_bar->run_or_stop_instant();
                break;

            case Qt::Key_T:
                _trig_bar->trigger_clicked();
                break;

            case Qt::Key_D:
                _trig_bar->protocol_clicked();
                break;

            case Qt::Key_M:
                _trig_bar->measure_clicked();
                break;

            case Qt::Key_R:
                _trig_bar->search_clicked();
                break;

            case Qt::Key_O:
                _sampling_bar->config_device();
                break;

            case Qt::Key_PageUp:
                _view->set_scale_offset(_view->scale(),
                                        _view->offset() - _view->get_view_width());
                break;
            case Qt::Key_PageDown:
                _view->set_scale_offset(_view->scale(),
                                        _view->offset() + _view->get_view_width());

                break;

            case Qt::Key_Left:
                _view->zoom(1);
                break;

            case Qt::Key_Right:
                _view->zoom(-1);
                break;

            case Qt::Key_0:
                for (auto s : sigs)
                {
                    if (s->signal_type() == DSO_SIGNAL)
                    {
                        view::DsoSignal *dsoSig = (view::DsoSignal*)s;
                        if (dsoSig->get_index() == 0)
                            dsoSig->set_vDialActive(!dsoSig->get_vDialActive());
                        else
                            dsoSig->set_vDialActive(false);
                    }
                }
                _view->setFocus();
                update();
                break;

            case Qt::Key_1:
                for (auto s : sigs)
                {
                    if (s->signal_type() == DSO_SIGNAL)
                    {
                        view::DsoSignal *dsoSig = (view::DsoSignal*)s;
                        if (dsoSig->get_index() == 1)
                            dsoSig->set_vDialActive(!dsoSig->get_vDialActive());
                        else
                            dsoSig->set_vDialActive(false);
                    }
                }
                _view->setFocus();
                update();
                break;

            case Qt::Key_Up:
                for (auto s : sigs)
                {
                    if (s->signal_type() == DSO_SIGNAL){
                        view::DsoSignal *dsoSig = (view::DsoSignal*)s;
                        if (dsoSig->get_vDialActive())
                        {
                            dsoSig->go_vDialNext(true);
                            update();
                            break;
                        }
                    }
                }
                break;

            case Qt::Key_Down:
                for (auto s : sigs)
                {
                    if (s->signal_type() == DSO_SIGNAL){
                        view::DsoSignal *dsoSig = (view::DsoSignal*)s;
                        if (dsoSig->get_vDialActive())
                        {
                            dsoSig->go_vDialPre(true);
                            update();
                            break;
                        }
                    }
                }
                break;

            default:
                QWidget::keyPressEvent((QKeyEvent *)event);
            }
            return true;
        }
        return false;
    }

    void MainWindow::switchLanguage(int language)
    {
        if (language == 0)
            return;
        
        AppConfig &app = AppConfig::Instance();

        if (app._frameOptions.language != language && language > 0)
        {
            app._frameOptions.language = language;
            app.SaveFrame();
            LangResource::Instance()->Load(language);     
        }        

        if (language == LAN_CN)
        {
            _qtTrans.load(":/qt_" + QString::number(language));
            qApp->installTranslator(&_qtTrans);
            _myTrans.load(":/my_" + QString::number(language));
            qApp->installTranslator(&_myTrans);
            retranslateUi();
        }
        else if (language == LAN_EN)
        {
            qApp->removeTranslator(&_qtTrans);
            qApp->removeTranslator(&_myTrans);
            retranslateUi();
        }
        else
        {
            dsv_err("%s%d", "Unknown language code:", language);
        }
    }

    void MainWindow::switchTheme(QString style)
    {
        AppConfig &app = AppConfig::Instance();

        if (app._frameOptions.style != style)
        {
            app._frameOptions.style = style;
            app.SaveFrame();
        }

        QString qssRes = ":/" + style + ".qss";
        QFile qss(qssRes);
        qss.open(QFile::ReadOnly | QFile::Text);
        qApp->setStyleSheet(qss.readAll());
        qss.close();

        data_updated();
    }

    void MainWindow::data_updated()
    {
        _event.data_updated(); // safe call
    }

    void MainWindow::on_data_updated()
    {
        _measure_widget->reCalc();
        _view->data_updated();
    }

    void MainWindow::on_open_doc()
    {
        openDoc();
    }

    void MainWindow::openDoc()
    {
        QDir dir(GetAppDataDir());
        AppConfig &app = AppConfig::Instance();
        int lan = app._frameOptions.language;
        QDesktopServices::openUrl(
            QUrl("file:///" + dir.absolutePath() + "/ug" + QString::number(lan) + ".pdf"));
    }

    void MainWindow::update_capture()
    {
        _view->update_hori_res();
    }

    void MainWindow::cur_snap_samplerate_changed()
    {
        _event.cur_snap_samplerate_changed(); // safe call
    }

    void MainWindow::on_cur_snap_samplerate_changed()
    {
        _measure_widget->cursor_update();
    }

    /*------------------on event end-------*/

    void MainWindow::signals_changed()
    {
        _event.signals_changed(); // safe call
    }

    void MainWindow::on_signals_changed()
    {
        _view->signals_changed();
    }

    void MainWindow::receive_trigger(quint64 trigger_pos)
    {
        _event.receive_trigger(trigger_pos); // save call
    }

    void MainWindow::on_receive_trigger(quint64 trigger_pos)
    {
        _view->receive_trigger(trigger_pos);
    }

    void MainWindow::frame_ended()
    {
        _event.frame_ended(); // save call
    }

    void MainWindow::on_frame_ended()
    {
        _view->receive_end();
    }

    void MainWindow::frame_began()
    {
        _event.frame_began(); // save call
    }

    void MainWindow::on_frame_began()
    {
        _view->frame_began();
    }

    void MainWindow::show_region(uint64_t start, uint64_t end, bool keep)
    {
        _view->show_region(start, end, keep);
    }

    void MainWindow::show_wait_trigger()
    {
        _view->show_wait_trigger();
    }

    void MainWindow::repeat_hold(int percent)
    {
        (void)percent;
        _view->repeat_show();
    }

    void MainWindow::decode_done()
    {
        _event.decode_done(); // safe call
    }

    void MainWindow::on_decode_done()
    {
        _protocol_widget->update_model();
    }

    void MainWindow::receive_data_len(quint64 len)
    {
        _event.receive_data_len(len); // safe call
    }

    void MainWindow::on_receive_data_len(quint64 len)
    {
        _view->set_receive_len(len);
    }

    void MainWindow::receive_header()
    {
    }

    void MainWindow::check_usb_device_speed()
    {
        // USB device speed check
        if (_device_agent->is_hardware())
        {
            int usb_speed = LIBUSB_SPEED_HIGH;
            GVariant *gvar = _device_agent->get_config(NULL, NULL, SR_CONF_USB_SPEED);
            if (gvar != NULL)
            {
                usb_speed = g_variant_get_int32(gvar);
                g_variant_unref(gvar);
            }

            bool usb30_support = false;
            gvar = _device_agent->get_config(NULL, NULL, SR_CONF_USB30_SUPPORT);
            if (gvar != NULL)
            {
                usb30_support = g_variant_get_boolean(gvar);
                g_variant_unref(gvar);

                if (usb30_support && usb_speed == LIBUSB_SPEED_HIGH)
                    show_error(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_CHECK_USB_SPEED_ERROR),
                    "Plug it into a USB 2.0 port will seriously affect its performance.\nPlease replug it into a USB 3.0 port."));
            }
        }
    }

    void MainWindow::trigger_message(int msg)
    {
        _event.trigger_message(msg);
    }

    void MainWindow::on_trigger_message(int msg)
    {
        _session->broadcast_msg(msg);
    }

    void MainWindow::reset_all_view()
    {
        _sampling_bar->reload();
        _view->status_clear();
        _view->reload();
        _view->set_device();
        _trigger_widget->update_view();
        _trigger_widget->device_updated();
        _trig_bar->reload(); 
        _dso_trigger_widget->update_view();
        _measure_widget->reload();
    }

    bool MainWindow::confirm_to_store_data()
    {   
        bool ret = false;
        _is_save_confirm_msg = true;       

        if (_session->have_hardware_data() && _session->is_first_store_confirm())
        {   
            // Only popup one time.
            ret =  MsgBox::Confirm(L_S(STR_PAGE_MSG, S_ID(IDS_MSG_SAVE_CAPDATE), "Save captured data?"));

            if (!ret && _is_auto_switch_device)
            {
                dsv_info("The data save confirm end, auto switch to the new device.");
                _is_auto_switch_device = false;
                _session->set_default_device();
                check_usb_device_speed();
            }
        }

        _is_save_confirm_msg = false;
        return ret;
    }

    void MainWindow::check_session_file_version()
    {
        auto device_agent = _session->get_device();
        if (device_agent->is_file() && device_agent->is_new_device())
        {
            if (device_agent->get_work_mode() == LOGIC)
            {
                GVariant *gvar = device_agent->get_config(NULL, NULL, SR_CONF_FILE_VERSION);
                if (gvar != NULL)
                {
                    int16_t version = g_variant_get_int16(gvar);
                    g_variant_unref(gvar);
                    if (version == 1)
                    {
                        show_error(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_CHECK_SESSION_FILE_VERSION_ERROR), 
                        "Current loading file has an old format. \nThis will lead to a slow loading speed. \nPlease resave it after loaded."));
                    }
                }
            }
        }
    }

    void MainWindow::load_device_config()
    {
        int mode = _device_agent->get_work_mode();

        if (_device_agent->is_hardware())
        { 
            QString ses_name = genSessionFileName(true);

            bool bExist = false;

            QFile sf(ses_name);
            if (!sf.exists()){
                dsv_info("Try to load the low version session file.");
                ses_name =  genSessionFileName(false);
            }
            else{
                bExist = true;
            }

            if (!bExist)
            {
                QFile sf2(ses_name);
                if (!sf2.exists()){
                    dsv_info("Try to load the default session file.");
                    ses_name = _file_bar->genDefaultSessionFile();
                }
            }            

            on_load_session(ses_name);
        }
        else if (_device_agent->is_demo())
        {
            QDir dir(GetResourceDir());
            if (dir.exists())
            { 
                QString ses_name = dir.absolutePath() + "/" 
                            + _device_agent->driver_name() + QString::number(mode) + ".dsc";

                QFile sf(ses_name);
                if (sf.exists()){
                    on_load_session(ses_name);
                }
            }
        }
    }

    QJsonDocument MainWindow::get_session_json_from_file(QString file)
    {
        QJsonDocument sessionDoc;
        QJsonParseError error;

        if (file == ""){
            dsv_err("%s", "File name is empty.");
            assert(false);
        }

        auto f_name = path::ConvertPath(file);
        ZipReader rd(f_name.c_str());
        auto *data = rd.GetInnterFileData("session");

        if (data != NULL)
        {
            QByteArray raw_bytes = QByteArray::fromRawData(data->data(), data->size());
            QString jsonStr(raw_bytes.data());
            QByteArray qbs = jsonStr.toUtf8();
            sessionDoc = QJsonDocument::fromJson(qbs, &error);

            if (error.error != QJsonParseError::NoError)
            {
                QString estr = error.errorString();
                dsv_err("File::get_session(), parse json error:\"%s\"!", estr.toUtf8().data());
            }

            rd.ReleaseInnerFileData(data);
        }

        return sessionDoc;
    }

    QJsonArray MainWindow::get_decoder_json_from_file(QString file)
    {
        QJsonArray dec_array;
        QJsonParseError error;

        if (file == ""){
            dsv_err("%s", "File name is empty.");
            assert(false);
        }

        /* read "decoders" */
        auto f_name = path::ConvertPath(file);
        ZipReader rd(f_name.c_str());
        auto *data = rd.GetInnterFileData("decoders");

        if (data != NULL)
        {
            QByteArray raw_bytes = QByteArray::fromRawData(data->data(), data->size());
            QString jsonStr(raw_bytes.data());
            QByteArray qbs = jsonStr.toUtf8();
            QJsonDocument sessionDoc = QJsonDocument::fromJson(qbs, &error);

            if (error.error != QJsonParseError::NoError)
            {
                QString estr = error.errorString();
                dsv_err("MainWindow::get_decoder_json_from_file(), parse json error:\"%s\"!", estr.toUtf8().data());
            }

            dec_array = sessionDoc.array();
            rd.ReleaseInnerFileData(data);
        }

        return dec_array;
    }

    void MainWindow::update_toolbar_view_status()
    {
        _sampling_bar->update_view_status();
        _file_bar->update_view_status();
        _trig_bar->update_view_status();
    }

    void MainWindow::OnMessage(int msg)
    {
        switch (msg)
        {
        case DSV_MSG_DEVICE_LIST_UPDATED:
            _sampling_bar->update_device_list();
            break;

        case DSV_MSG_START_COLLECT_WORK_PREV:
            if (_device_agent->get_work_mode() == LOGIC)
                _trigger_widget->try_commit_trigger();
            else if (_device_agent->get_work_mode() == DSO)
                _dso_trigger_widget->check_setting();

            _view->capture_init();
            _view->on_state_changed(false);
            break;

        case DSV_MSG_START_COLLECT_WORK:
            update_toolbar_view_status();
            _view->on_state_changed(false);
            _protocol_widget->update_view_status();
            break;
        
        case DSV_MSG_COLLECT_END:
            prgRate(0);
            _view->repeat_unshow();
            _view->on_state_changed(true);
            _protocol_widget->update_view_status();
            break;

        case DSV_MSG_END_COLLECT_WORK:
            update_toolbar_view_status();           
            break;

        case DSV_MSG_CURRENT_DEVICE_CHANGE_PREV:
            _protocol_widget->del_all_protocol();
            _view->reload();
            break;

        case DSV_MSG_CURRENT_DEVICE_CHANGED:
        {
            if (_msg != NULL)
            {
                _msg->close();
                _msg = NULL;
            }
  
            reset_all_view();
            load_device_config();
            _sampling_bar->update_device_list();
            
            _logo_bar->dsl_connected(_session->get_device()->is_hardware());
            update_toolbar_view_status();
            _session->device_event_object()->device_updated();

            if (_device_agent->is_hardware())
                _session->on_load_config_end();
            
            if (_device_agent->get_work_mode() == LOGIC && _device_agent->is_file() == false)
                _view->auto_set_max_scale();

            if (_device_agent->is_file())
            {
                check_session_file_version();

                bool bDoneDecoder = false;
                load_session_json(get_session_json_from_file(_device_agent->path()), bDoneDecoder);

                if (!bDoneDecoder && _device_agent->get_work_mode() == LOGIC){
                    StoreSession ss(_session);
                    QJsonArray deArray = get_decoder_json_from_file(_device_agent->path());
                    ss.load_decoders(_protocol_widget, deArray);
                }
                
                _session->start_capture(true);
            } 
        }
        break;
        /*demo*/
        case DSV_MSG_DEMO_UPDATA:
        {
            if (_msg != NULL)
            {
                _msg->close();
                _msg = NULL;
            }
 
            _sampling_bar->update_device_list();
            reset_all_view();
            load_device_config();
            
            _logo_bar->dsl_connected(_session->get_device()->is_hardware());
            update_toolbar_view_status();
            _session->device_event_object()->device_updated();

            bool bDoneDecoder = false;
           QJsonDocument test;


           GVariant *gvar = _device_agent->get_config(NULL, NULL, SR_CONF_TEST);
           if (gvar != NULL)
           {
               gboolean change = g_variant_get_boolean(gvar);
               if(change)
               {
                   GVariant *gvar1 = _device_agent->get_config(NULL, NULL, SR_CONF_PATTERN_MODE);
                   const char* str1 = g_variant_get_string(gvar1,NULL);
                   {
                       if(!strcmp(str1,"RANDOM") && _device_agent->get_work_mode() == LOGIC)
                       {
                           char *str;
                           str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":0,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Sine\",\"Sample count\":\"100000768\",\"Sample rate\":\"10000\",\"Title\":\"DSView v1.3.0-RC2\",\"Version\":3,\"channel\":[{\"colour\":\"default\",\"enabled\":true,\"index\":0,\"name\":\"0\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":1,\"name\":\"1\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":2,\"name\":\"2\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":3,\"name\":\"3\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":4,\"name\":\"4\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":5,\"name\":\"5\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":6,\"name\":\"6\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":7,\"name\":\"7\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":8,\"name\":\"8\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":9,\"name\":\"9\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":10,\"name\":\"10\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":11,\"name\":\"11\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":12,\"name\":\"12\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":13,\"name\":\"13\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":14,\"name\":\"14\",\"strigger\":0,\"type\":10000},{\"colour\":\"default\",\"enabled\":true,\"index\":15,\"name\":\"15\",\"strigger\":0,\"type\":10000}],\"decoder\":[],\"trigger\":{\"advTriggerMode\":false,\"serialTriggerBits\":0,\"serialTriggerChannel\":0,\"serialTriggerClock\":\"X X X X X X X X X X X X X X X X\",\"serialTriggerData\":\"X X X X X X X X X X X X X X X X\",\"serialTriggerStart\":\"X X X X X X X X X X X X X X X X\",\"serialTriggerStop\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerContiguous0\":false,\"stageTriggerContiguous1\":false,\"stageTriggerContiguous10\":false,\"stageTriggerContiguous11\":false,\"stageTriggerContiguous12\":false,\"stageTriggerContiguous13\":false,\"stageTriggerContiguous14\":false,\"stageTriggerContiguous15\":false,\"stageTriggerContiguous2\":false,\"stageTriggerContiguous3\":false,\"stageTriggerContiguous4\":false,\"stageTriggerContiguous5\":false,\"stageTriggerContiguous6\":false,\"stageTriggerContiguous7\":false,\"stageTriggerContiguous8\":false,\"stageTriggerContiguous9\":false,\"stageTriggerCount0\":1,\"stageTriggerCount1\":1,\"stageTriggerCount10\":1,\"stageTriggerCount11\":1,\"stageTriggerCount12\":1,\"stageTriggerCount13\":1,\"stageTriggerCount14\":1,\"stageTriggerCount15\":1,\"stageTriggerCount2\":1,\"stageTriggerCount3\":1,\"stageTriggerCount4\":1,\"stageTriggerCount5\":1,\"stageTriggerCount6\":1,\"stageTriggerCount7\":1,\"stageTriggerCount8\":1,\"stageTriggerCount9\":1,\"stageTriggerInv00\":0,\"stageTriggerInv01\":0,\"stageTriggerInv010\":0,\"stageTriggerInv011\":0,\"stageTriggerInv012\":0,\"stageTriggerInv013\":0,\"stageTriggerInv014\":0,\"stageTriggerInv015\":0,\"stageTriggerInv02\":0,\"stageTriggerInv03\":0,\"stageTriggerInv04\":0,\"stageTriggerInv05\":0,\"stageTriggerInv06\":0,\"stageTriggerInv07\":0,\"stageTriggerInv08\":0,\"stageTriggerInv09\":0,\"stageTriggerInv10\":0,\"stageTriggerInv11\":0,\"stageTriggerInv110\":0,\"stageTriggerInv111\":0,\"stageTriggerInv112\":0,\"stageTriggerInv113\":0,\"stageTriggerInv114\":0,\"stageTriggerInv115\":0,\"stageTriggerInv12\":0,\"stageTriggerInv13\":0,\"stageTriggerInv14\":0,\"stageTriggerInv15\":0,\"stageTriggerInv16\":0,\"stageTriggerInv17\":0,\"stageTriggerInv18\":0,\"stageTriggerInv19\":0,\"stageTriggerLogic0\":1,\"stageTriggerLogic1\":1,\"stageTriggerLogic10\":1,\"stageTriggerLogic11\":1,\"stageTriggerLogic12\":1,\"stageTriggerLogic13\":1,\"stageTriggerLogic14\":1,\"stageTriggerLogic15\":1,\"stageTriggerLogic2\":1,\"stageTriggerLogic3\":1,\"stageTriggerLogic4\":1,\"stageTriggerLogic5\":1,\"stageTriggerLogic6\":1,\"stageTriggerLogic7\":1,\"stageTriggerLogic8\":1,\"stageTriggerLogic9\":1,\"stageTriggerValue00\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue01\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue010\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue011\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue012\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue013\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue014\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue015\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue02\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue03\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue04\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue05\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue06\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue07\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue08\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue09\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue10\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue11\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue110\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue111\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue112\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue113\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue114\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue115\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue12\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue13\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue14\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue15\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue16\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue17\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue18\":\"X X X X X X X X X X X X X X X X\",\"stageTriggerValue19\":\"X X X X X X X X X X X X X X X X\",\"triggerPos\":1,\"triggerStages\":0,\"triggerTab\":0}}";
                           QByteArray temp(str);
                           test = test.fromJson(temp);
                       }
                       else if(_device_agent->get_work_mode() == ANALOG)
                       {
                           char *str;
                           if(!strcmp(str1,"RANDOM"))
                               str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":2,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Random\",\"Sample count\":\"2048\",\"Sample rate\":\"10\",\"Title\":\"DSView v1.3.0-RC2\",\"Version\":3,\"channel\":[{\"colour\":\"#eeb211\",\"coupling\":1,\"enabled\":true,\"index\":0,\"mapDefault\":true,\"mapMax\":5,\"mapMin\":-5,\"mapUnit\":\"V\",\"name\":\"0\",\"type\":10002,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.37401574803149606},{\"colour\":\"#009925\",\"coupling\":1,\"enabled\":true,\"index\":1,\"mapDefault\":true,\"mapMax\":5,\"mapMin\":-5,\"mapUnit\":\"V\",\"name\":\"1\",\"type\":10002,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.6259842519685039}],\"decoder\":[]}";
                           else if(!strcmp(str1,"SINE"))
                               str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":2,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Sine\",\"Sample count\":\"2048\",\"Sample rate\":\"10\",\"Title\":\"DSView v1.3.0-RC2\",\"Version\":3,\"channel\":[{\"colour\":\"#eeb211\",\"coupling\":1,\"enabled\":true,\"index\":0,\"mapDefault\":true,\"mapMax\":5,\"mapMin\":-5,\"mapUnit\":\"V\",\"name\":\"0\",\"type\":10002,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.37401574803149606},{\"colour\":\"#009925\",\"coupling\":1,\"enabled\":true,\"index\":1,\"mapDefault\":true,\"mapMax\":5,\"mapMin\":-5,\"mapUnit\":\"V\",\"name\":\"1\",\"type\":10002,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.6259842519685039}],\"decoder\":[]}";
                           else if(!strcmp(str1,"SQUARE"))
                               str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":2,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Square\",\"Sample count\":\"2048\",\"Sample rate\":\"10\",\"Title\":\"DSView v1.3.0-RC2\",\"Version\":3,\"channel\":[{\"colour\":\"#eeb211\",\"coupling\":1,\"enabled\":true,\"index\":0,\"mapDefault\":true,\"mapMax\":5,\"mapMin\":-5,\"mapUnit\":\"V\",\"name\":\"0\",\"type\":10002,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.37401574803149606},{\"colour\":\"#009925\",\"coupling\":1,\"enabled\":true,\"index\":1,\"mapDefault\":true,\"mapMax\":5,\"mapMin\":-5,\"mapUnit\":\"V\",\"name\":\"1\",\"type\":10002,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.6259842519685039}],\"decoder\":[]}";
                           else if(!strcmp(str1,"TRIANGLE"))
                               str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":2,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Triangle\",\"Sample count\":\"2048\",\"Sample rate\":\"10\",\"Title\":\"DSView v1.3.0-RC2\",\"Version\":3,\"channel\":[{\"colour\":\"#eeb211\",\"coupling\":1,\"enabled\":true,\"index\":0,\"mapDefault\":true,\"mapMax\":5,\"mapMin\":-5,\"mapUnit\":\"V\",\"name\":\"0\",\"type\":10002,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.37401574803149606},{\"colour\":\"#009925\",\"coupling\":1,\"enabled\":true,\"index\":1,\"mapDefault\":true,\"mapMax\":5,\"mapMin\":-5,\"mapUnit\":\"V\",\"name\":\"1\",\"type\":10002,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.6259842519685039}],\"decoder\":[]}";
                           else if(!strcmp(str1,"SWATOOTH"))
                               str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":2,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Sawtooth\",\"Sample count\":\"2048\",\"Sample rate\":\"10\",\"Title\":\"DSView v1.3.0-RC2\",\"Version\":3,\"channel\":[{\"colour\":\"#eeb211\",\"coupling\":1,\"enabled\":true,\"index\":0,\"mapDefault\":true,\"mapMax\":5,\"mapMin\":-5,\"mapUnit\":\"V\",\"name\":\"0\",\"type\":10002,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.37401574803149606},{\"colour\":\"#009925\",\"coupling\":1,\"enabled\":true,\"index\":1,\"mapDefault\":true,\"mapMax\":5,\"mapMin\":-5,\"mapUnit\":\"V\",\"name\":\"1\",\"type\":10002,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.6259842519685039}],\"decoder\":[]}";
                           QByteArray temp(str);
                           test = test.fromJson(temp);
                       }
                        else if(_device_agent->get_work_mode() == DSO)
                       {
                           char *str;
                           if(!strcmp(str1,"RANDOM"))
                               str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":1,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Random\",\"Sample count\":\"10000\",\"Sample rate\":\"100\",\"Title\":\"DSView v1.3.0-dev7\",\"Version\":3,\"channel\":[{\"colour\":\"#eeb211\",\"coupling\":1,\"enabled\":true,\"index\":0,\"name\":\"0\",\"trigValue\":0.5,\"type\":10001,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.37401574803149606},{\"colour\":\"#009925\",\"coupling\":1,\"enabled\":true,\"index\":1,\"name\":\"1\",\"trigValue\":0.5,\"type\":10001,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.6259842519685039}],\"decoder\":[],\"measure\":[{\"index\":0,\"site\":0,\"type\":1},{\"index\":0,\"site\":1,\"type\":17},{\"index\":0,\"site\":2,\"type\":18},{\"index\":1,\"site\":5,\"type\":1},{\"index\":1,\"site\":6,\"type\":17},{\"index\":1,\"site\":7,\"type\":18}]}";
                           else if(!strcmp(str1,"SINE"))
                               str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":1,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Sine\",\"Sample count\":\"10000\",\"Sample rate\":\"100000000\",\"Title\":\"DSView v1.3.0-dev7\",\"Version\":3,\"channel\":[{\"colour\":\"#eeb211\",\"coupling\":1,\"enabled\":true,\"index\":0,\"name\":\"0\",\"trigValue\":0.40551181102362205,\"type\":10001,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.2795275590551181},{\"colour\":\"#009925\",\"coupling\":1,\"enabled\":true,\"index\":1,\"name\":\"1\",\"trigValue\":0.610236220472441,\"type\":10001,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.7362204724409449}],\"decoder\":[],\"measure\":[{\"index\":0,\"site\":0,\"type\":1},{\"index\":0,\"site\":1,\"type\":17},{\"index\":0,\"site\":2,\"type\":18},{\"index\":1,\"site\":5,\"type\":1},{\"index\":1,\"site\":6,\"type\":17},{\"index\":1,\"site\":7,\"type\":18}]}";
                           else if(!strcmp(str1,"SQUARE"))
                               str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":1,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Square\",\"Sample count\":\"10000\",\"Sample rate\":\"100000000\",\"Title\":\"DSView v1.3.0-dev7\",\"Version\":3,\"channel\":[{\"colour\":\"#eeb211\",\"coupling\":1,\"enabled\":true,\"index\":0,\"name\":\"0\",\"trigValue\":0.5,\"type\":10001,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.37401574803149606},{\"colour\":\"#009925\",\"coupling\":1,\"enabled\":true,\"index\":1,\"name\":\"1\",\"trigValue\":0.5,\"type\":10001,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.6259842519685039}],\"decoder\":[],\"measure\":[{\"index\":0,\"site\":0,\"type\":1},{\"index\":0,\"site\":1,\"type\":17},{\"index\":0,\"site\":2,\"type\":18},{\"index\":1,\"site\":5,\"type\":1},{\"index\":1,\"site\":6,\"type\":17},{\"index\":1,\"site\":7,\"type\":18}]}";
                           else if(!strcmp(str1,"TRIANGLE"))
                               str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":1,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Triangle\",\"Sample count\":\"10000\",\"Sample rate\":\"100000000\",\"Title\":\"DSView v1.3.0-dev7\",\"Version\":3,\"channel\":[{\"colour\":\"#eeb211\",\"coupling\":1,\"enabled\":true,\"index\":0,\"name\":\"0\",\"trigValue\":0.5,\"type\":10001,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.37401574803149606},{\"colour\":\"#009925\",\"coupling\":1,\"enabled\":true,\"index\":1,\"name\":\"1\",\"trigValue\":0.5,\"type\":10001,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.6259842519685039}],\"decoder\":[],\"measure\":[{\"index\":0,\"site\":0,\"type\":1},{\"index\":0,\"site\":1,\"type\":17},{\"index\":0,\"site\":2,\"type\":18},{\"index\":1,\"site\":5,\"type\":1},{\"index\":1,\"site\":6,\"type\":17},{\"index\":1,\"site\":7,\"type\":18}]}";
                           else if(!strcmp(str1,"SWATOOTH"))
                               str = "{\"Device\":\"virtual-demo\",\"DeviceMode\":1,\"Language\":25,\"Max Height\":\"1X\",\"Pattern mode\":\"Sawtooth\",\"Sample count\":\"10000\",\"Sample rate\":\"100000000\",\"Title\":\"DSView v1.3.0-dev7\",\"Version\":3,\"channel\":[{\"colour\":\"#eeb211\",\"coupling\":1,\"enabled\":true,\"index\":0,\"name\":\"0\",\"trigValue\":0.5,\"type\":10001,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.37401574803149606},{\"colour\":\"#009925\",\"coupling\":1,\"enabled\":true,\"index\":1,\"name\":\"1\",\"trigValue\":0.5,\"type\":10001,\"vdiv\":1000,\"vfactor\":1,\"zeroPos\":0.6259842519685039}],\"decoder\":[],\"measure\":[{\"index\":0,\"site\":0,\"type\":1},{\"index\":0,\"site\":1,\"type\":17},{\"index\":0,\"site\":2,\"type\":18},{\"index\":1,\"site\":5,\"type\":1},{\"index\":1,\"site\":6,\"type\":17},{\"index\":1,\"site\":7,\"type\":18}]}";
                           QByteArray temp(str);
                           test = test.fromJson(temp);
                       }
                       else
                       {
                           if(_device_agent->get_work_mode() == DSO)
                           {
                               test = get_session_json_from_file(_device_agent->path());
                               QByteArray tem = test.toJson(QJsonDocument::Compact);
                               char* str1 = tem.data();
                               dsv_info("%s",str1);

                           }
                            test = get_session_json_from_file(_device_agent->path());
                       }
                       load_session_json(test, bDoneDecoder);
                       _device_agent->set_config(NULL, NULL, SR_CONF_TEST,g_variant_new_boolean(FALSE));
                   }
                   g_variant_unref(gvar1);
               }
               g_variant_unref(gvar);
           }

            //reload decoder
            gvar = _device_agent->get_config(NULL, NULL, SR_CONF_PATTERN_MODE);
            if (gvar != NULL)
            {
                const char* str = g_variant_get_string(gvar,NULL);
                if(_device_agent->get_work_mode() == LOGIC && strcmp(str,"RANDOM") && !bDoneDecoder)
                {
                    StoreSession ss(_session);
                    QJsonArray deArray = get_decoder_json_from_file(_device_agent->path());
                    ss.load_decoders(_protocol_widget, deArray);  
                }
                g_variant_unref(gvar);
            }

            //auto start
            gvar = _device_agent->get_config(NULL, NULL,SR_CONF_AUTO_OPEN);
            if(gvar != NULL)
            {
                bool auto_start = g_variant_get_boolean(gvar);
                if(_device_agent->get_work_mode() == LOGIC && auto_start)
                {
                    _session->start_capture(true);
                }
                g_variant_unref(gvar);
            }
        }
        break;

        case DSV_MSG_DEVICE_OPTIONS_UPDATED:
            _trigger_widget->device_updated();
            _measure_widget->reload();
            _view->check_calibration();                      
            break;

        case DSV_MSG_DEVICE_DURATION_UPDATED:
            _trigger_widget->device_updated();
            _view->timebase_changed();
            break;

        case DSV_MSG_DEVICE_MODE_CHANGED:
            /* demo */
            if(_device_agent->is_demo())
            {
                _session->set_device(_device_agent->handle());
                return;
            }  
            _view->mode_changed(); 
            reset_all_view();
            load_device_config(); 
            update_toolbar_view_status();
            _sampling_bar->update_sample_rate_list();

            if (_device_agent->is_hardware())
                _session->on_load_config_end();
            
            if (_device_agent->get_work_mode() == LOGIC)
                _view->auto_set_max_scale();
            break;

        case DSV_MSG_NEW_USB_DEVICE:
            {
                if (_session->get_device()->is_demo() == false)
                {
                    QString msgText = L_S(STR_PAGE_MSG, S_ID(IDS_MSG_TO_SWITCH_DEVICE), "To switch the new device?");
                    
                    if (MsgBox::Confirm(msgText) == false){
                        _sampling_bar->update_device_list(); // Update the list only.
                        return;
                    }
                }

                // The store confirm is not processed.
                if (_is_save_confirm_msg){
                    _is_auto_switch_device = true;
                    _sampling_bar->update_device_list();
                    return;
                }

                if (confirm_to_store_data())
                {
                    _is_auto_switch_device = true;
                    on_save();
                }
                else
                {
                    _session->set_default_device();
                    check_usb_device_speed();
                }
            }           
            break;

        case DSV_MSG_CURRENT_DEVICE_DETACHED:
            // Save current config, and switch to the last device.
            _session->device_event_object()->device_updated();
            session_save();
            _view->hide_calibration();
            if (confirm_to_store_data())
            {
                _is_auto_switch_device = true;
                on_save();
            }
            else
            {
                _session->set_default_device();
            }
            break;

        case DSV_MSG_SAVE_COMPLETE:
            if (_is_auto_switch_device)
            {
                _is_auto_switch_device = false;
                _session->set_default_device();
                if (_session->get_device()->is_new_device())
                    check_usb_device_speed();
            }
            else
            {
                ds_device_handle devh = _sampling_bar->get_next_device_handle();
                if (devh != NULL_HANDLE)
                {
                    dsv_info("%s", "Auto switch to the selected device.");
                    _session->set_device(devh);
                }
            }
            break;

        case DSV_MSG_CLEAR_DECODE_DATA:
            if (_device_agent->get_work_mode() == LOGIC)
                _protocol_widget->reset_view();
            break;
        
        case DSV_MSG_STORE_CONF_PREV:
            if (_device_agent->is_hardware() && _session->have_hardware_data() == false){
                _sampling_bar->commit_settings();
            }
            break;

        case DSV_MSG_END_DEVICE_OPTIONS:            
            break;

        }
    }

} // namespace pv
