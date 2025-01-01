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

#include "viewport.h"
#include "ruler.h"

#include "signal.h"
#include "dsosignal.h"
#include "logicsignal.h"
#include "analogsignal.h"
#include "spectrumtrace.h"
#include "../data/logicsnapshot.h"
#include "../sigsession.h"
#include "../dialogs/dsomeasure.h"
#include "decodetrace.h"

#include <QMouseEvent>
#include <QStyleOption>
#include <QPainterPath> 
#include <math.h>
#include <QWheelEvent>
 
#include "../config/appconfig.h"
#include "../dsvdef.h"
#include "../appcontrol.h"
#include "../log.h" 
#include "../ui/langresource.h"
#include "../ui/fn.h"
#include "lissajoustrace.h"

using namespace std;

namespace pv {
namespace view {

const double Viewport::DragDamping = 1.05;
const double Viewport::MinorDragRateUp = 10;

Viewport::Viewport(View &parent, View_type type) :
    QWidget(&parent),
    _view(parent),
    _type(type),
    _need_update(false),
    _sample_received(0),
    _action_type(NO_ACTION),
    _measure_type(NO_MEASURE),
    _cur_sample(0),
    _nxt_sample(1),
    _cur_preX(0),
    _cur_aftX(1),
    _cur_midY(0),
    _hover_index(0),
    _hover_hit(false),
    _dso_xm_valid(false),
    _dso_ym_valid(false),
    _waiting_trig(0),
    _dso_trig_moved(false),
    _curs_moved(false),
    _xcurs_moved(false)
{
	setMouseTracking(true);
	setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);

    //setFixedSize(QSize(600, 400));
    _mm_width = View::Unknown_Str;
    _mm_period = View::Unknown_Str;
    _mm_freq = View::Unknown_Str;
    _mm_duty = View::Unknown_Str;
    _measure_en = true;
    _edge_hit = false;
    _transfer_started = false;
    _timer_cnt = 0;
  
    _sample_received = 0;
    _is_checked_trig = false;

    _lst_wait_tigger_time = high_resolution_clock::now();
    _tigger_wait_times = 0;

    // drag inertial
    _drag_strength = 0;
    _drag_timer.setSingleShot(true);
 
    _cmenu = new QMenu(this);
    QAction *yAction = _cmenu->addAction(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_ADD_Y_CURSOR), "Add Y-cursor"));
    QAction *xAction = _cmenu->addAction(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_ADD_X_CURSOR), "Add X-cursor"));
    _yAction = yAction;
    _xAction = xAction;
 
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(&_trigger_timer, SIGNAL(timeout()),this, SLOT(on_trigger_timer()));
    connect(&_drag_timer, SIGNAL(timeout()),this, SLOT(on_drag_timer())); 
    connect(yAction, SIGNAL(triggered(bool)), this, SLOT(add_cursor_y()));
    connect(xAction, SIGNAL(triggered(bool)), this, SLOT(add_cursor_x()));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)),this, SLOT(show_contextmenu(const QPoint&)));

    ADD_UI(this);
}

Viewport::~Viewport()
{
    REMOVE_UI(this);
}

int Viewport::get_total_height()
{
	int h = 0;
    std::vector<Trace*> traces;
    _view.get_traces(_type, traces);

    for(auto t : traces) {
        h += (int)(t->get_totalHeight());
    }
    h += 2 * View::SignalMargin;

	return h;
}

QPoint Viewport::get_mouse_point()
{
    return _mouse_point;
}

bool Viewport::event(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture)
        return gestureEvent(static_cast<QNativeGestureEvent*>(event));
    return QWidget::event(event);
}

void Viewport::paintEvent(QPaintEvent *event)
{
    (void)event; 

    doPaint();
}

void Viewport::doPaint()
{     
    using pv::view::Signal;
   
    QStyleOption o;
    o.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &o, &p, this);

    QFont font = p.font();
    float fSize = AppConfig::Instance().appOptions.fontSize;
    if (fSize > 10)
        fSize = 10;
    font.setPointSizeF(fSize);
    p.setFont(font);

    _view.session().check_update();
    QColor fore(QWidget::palette().color(QWidget::foregroundRole()));
    QColor back(QWidget::palette().color(QWidget::backgroundRole()));
    fore.setAlpha(View::ForeAlpha);
    _view.set_back(false);
  
    std::vector<Trace*> traces;
    _view.get_traces(_type, traces);

    for(auto t : traces){
        t->paint_back(p, 0, _view.get_view_width(), fore, back);
        if (_view.back_ready())
            break;
    } 

    int mode = _view.session().get_device()->get_work_mode();

    if (mode == LOGIC || _view.session().is_instant()) 
    {
        if (_view.session().is_init_status())
        {
            paintCursors(p);
        }
        else if (_view.session().is_stopped_status())
        {
            paintSignals(p, fore, back);
        }
        else if (_view.session().is_realtime_refresh())
        {  
            _view.session().have_new_realtime_refresh(false); // Try to reset refresh timer.

            if (_view.session().have_view_data() || _view.session().is_instant())
                paintSignals(p, fore, back);
            else
                paintProgress(p, fore, back);
        }
        else if (_view.session().is_running_status()){
            if (_view.session().is_repeat_mode())
            {
                paintSignals(p, fore, back);

                if (!_transfer_started){
                    bool triggered;
                    int captured_progress;
         
                    if (_view.session().get_capture_status(triggered, captured_progress)){
                        _view.show_captured_progress(triggered, captured_progress);
                    }
                }
            }
            else if (_type == TIME_VIEW) {
                _view.repeat_unshow();
                paintProgress(p, fore, back);
            }
        }     
    }
    else {
        paintSignals(p, fore, back);
    }

    for(auto t : traces){
        if (t->enabled())
            t->paint_fore(p, 0, _view.get_view_width(), fore, back);
    }

    if (_view.get_signalHeight() != _curSignalHeight)
            _curSignalHeight = _view.get_signalHeight();

	p.end();
}

void Viewport::paintCursors(QPainter &p)
{ 
    const QRect xrect = _view.get_view_rect();
    auto &cursor_list = _view.get_cursorList();

    if (_view.cursors_shown() && _type == TIME_VIEW) {

        for (auto cursor : cursor_list) {            
            const int64_t cursorX = _view.index2pixel(cursor->index());
            if (xrect.contains(_view.hover_point().x(), _view.hover_point().y()) &&
                    qAbs(cursorX - _view.hover_point().x()) <= HitCursorMargin)
                cursor->paint(p, xrect, 1, _view.session().is_stopped_status());
            else
                cursor->paint(p, xrect, 0, _view.session().is_stopped_status());
        }
    }
}

void Viewport::paintSignals(QPainter &p, QColor fore, QColor back)
{ 
    std::vector<Trace*> traces;
    _view.get_traces(_type, traces);

    if (_view.session().get_device()->get_work_mode() == LOGIC) 
    {
        bool bFirst = true;
        uint64_t end_align_sample;

        for(auto t : traces){
            if (t->enabled()){

                if (t->signal_type() == SR_CHANNEL_LOGIC)
                {
                    LogicSignal *logic_signal = (LogicSignal*)t;
                
                    if (bFirst)
                        end_align_sample = logic_signal->data()->get_ring_sample_count();
            
                    logic_signal->paint_mid_align_sample(p, 0, t->get_view_rect().right(), fore, back, end_align_sample);
                    bFirst = false;
                }
                else{
                    t->paint_mid(p, 0, t->get_view_rect().right(), fore, back);
                }               
            }                
        }
    } 
    else {
        if (_view.scale() != _curScale ||
            _view.offset() != _curOffset ||
            _view.get_signalHeight() != _curSignalHeight ||
            _need_update) {
            _curScale = _view.scale();
            _curOffset = _view.offset();
            _curSignalHeight = _view.get_signalHeight();

            _pixmap = QPixmap(size());
            _pixmap.fill(Qt::transparent);

            QPainter dbp(&_pixmap);

            bool isLissa = false;

            if (_view.session().get_device()->get_work_mode() == DSO)
            {
                auto lis_trace = _view.session().get_lissajous_trace();
                if (lis_trace && lis_trace->enabled()){
                    isLissa = true;
                }
            }
           
            for(auto t : traces)
            {
                if (t->enabled())
                {   
                    if (isLissa && t->signal_type() == SR_CHANNEL_DSO)
                        continue;
                    if (isLissa && t->signal_type() == SR_CHANNEL_MATH)
                        continue;
                    
                    t->paint_mid(dbp, 0, t->get_view_rect().right(), fore, back);
                }                    
            }
            _need_update = false;
        }
        p.drawPixmap(0, 0, _pixmap);
    }

    // plot cursors
    paintCursors(p);

    const QRect xrect = _view.get_view_rect();

    if (_view.xcursors_shown() && _type == TIME_VIEW) {
        auto &xcursor_list = _view.get_xcursorList();
        auto i = xcursor_list.begin();
        int index = 0;
        bool hovered = false;

        while (i != xcursor_list.end()) {
            const double cursorX  = xrect.left() + (*i)->value(XCursor::XCur_Y)*xrect.width();
            const double cursorY0 = xrect.top() + (*i)->value(XCursor::XCur_X0)*xrect.height();
            const double cursorY1 = xrect.top() + (*i)->value(XCursor::XCur_X1)*xrect.height();

            if (!hovered && ((*i)->get_close_rect(xrect).contains(_view.hover_point()) ||
                             (*i)->get_map_rect(xrect).contains(_view.hover_point()))) {
                (*i)->paint(p, xrect, XCursor::XCur_All);
                hovered = true;
            }
            else if(!hovered && xrect.contains(_view.hover_point())) {
                if (qAbs(cursorX - _view.hover_point().x()) <= HitCursorMargin &&
                    _view.hover_point().y() > min(cursorY0, cursorY1) &&
                    _view.hover_point().y() < max(cursorY0, cursorY1)) {
                    (*i)->paint(p, xrect, XCursor::XCur_Y);
                    hovered = true;
                }
                else if (qAbs(cursorY0 - _view.hover_point().y()) <= HitCursorMargin) {
                    (*i)->paint(p, xrect, XCursor::XCur_X0);
                    hovered = true;
                }
                else if (qAbs(cursorY1 - _view.hover_point().y()) <= HitCursorMargin) {
                    (*i)->paint(p, xrect, XCursor::XCur_X1);
                    hovered = true;
                }
                else {
                    (*i)->paint(p, xrect, XCursor::XCur_None);
                }
            }
            else {
                (*i)->paint(p, xrect, XCursor::XCur_None);
            }

            i++;
            index++;
        }
    }

    if (_type == TIME_VIEW) {
        if (_view.trig_cursor_shown()) {
            _view.get_trig_cursor()->paint(p, xrect, 0, false);
        }
        if (_view.search_cursor_shown()) {
            const int64_t searchX = _view.index2pixel(_view.get_search_cursor()->index());
            if (xrect.contains(_view.hover_point().x(), _view.hover_point().y()) &&
                    qAbs(searchX - _view.hover_point().x()) <= HitCursorMargin)
                _view.get_search_cursor()->paint(p, xrect, 1, -1);
            else
                _view.get_search_cursor()->paint(p, xrect, 0, -1);
        }

        // plot zoom rect
        if (_action_type == LOGIC_ZOOM) {
            p.setPen(Qt::NoPen);
            p.setBrush(View::LightBlue);
            p.drawRect(QRectF(_mouse_down_point, _mouse_point));
        }

        //plot measure arrow
        paintMeasure(p, fore, back);

        //plot trigger information
        if (_view.session().get_device()->get_work_mode() == DSO
            && _view.session().is_running_status()) 
        {
            int type;
            bool roll = false;
            QString type_str="";
            bool ret = false;

            _view.session().get_device()->get_config_bool(SR_CONF_ROLL, roll);

            ret = _view.session().get_device()->get_config_byte(SR_CONF_TRIGGER_SOURCE, type);
            if (ret) {
                bool bDot = false;

                if (type == DSO_TRIGGER_AUTO && roll) {
                    type_str = L_S(STR_PAGE_DLG, S_ID(IDS_DLG_AUTO_ROLL), "Auto(Roll)");
                    
                    if (_view.session().is_instant()){
                        type_str += ", ";
                        type_str += L_S(STR_PAGE_DLG, S_ID(IDS_DLG_VIEW_CAPTURE), "Capturing");
                        bDot = true;
                    }
                } 
                else if (type == DSO_TRIGGER_AUTO && !_view.session().trigd()) {
                    type_str = L_S(STR_PAGE_DLG, S_ID(IDS_DLG_AUTO), "Auto");

                    if (_view.session().is_instant()){
                        type_str += ", ";
                        type_str += L_S(STR_PAGE_DLG, S_ID(IDS_DLG_VIEW_CAPTURE), "Capturing");
                        bDot = true;
                    }
                } 
                else if (_waiting_trig > 0) {
                    type_str = L_S(STR_PAGE_DLG, S_ID(IDS_DLG_WAITING_TRIG), "Waiting Trig"); 
                    bDot = true;
                } 
                else {
                    type_str = L_S(STR_PAGE_DLG, S_ID(IDS_DLG_TRIG_D), "Trig'd");
                }

                if (bDot)
                {
                    for (int i = 0; i < _tigger_wait_times; i++){
                        type_str += ".";
                    }

                    high_resolution_clock::time_point cur_time = high_resolution_clock::now();
                    milliseconds timeInterval = std::chrono::duration_cast<milliseconds>(cur_time - _lst_wait_tigger_time);
                    int64_t time_keep =  timeInterval.count();

                    if (time_keep >= 500){
                        _tigger_wait_times++;
                        _lst_wait_tigger_time = cur_time;
                    }

                    if (_tigger_wait_times > 4)
                        _tigger_wait_times = 0;
                }
            }
            p.setPen(fore);
            p.drawText(_view.get_view_rect(), Qt::AlignLeft | Qt::AlignTop, type_str);

            if (_view.session().get_device()->is_hardware())
            {
                if (_view.session().dso_data_is_out_off_range()){
                    QString data_status = L_S(STR_PAGE_DLG, S_ID(IDS_DLG_DATA_OUT_OFF_RANGE), "Out off range");
                    data_status += "! ";
                    p.setPen(QColor(255,0,0,200));
                    p.drawText(_view.get_view_rect(), Qt::AlignRight | Qt::AlignTop, data_status);
                    p.setPen(fore);
                }                 
            }           
        }
    }
}

void Viewport::get_captured_progress(double &progress, int &progress100)
{ 
    const uint64_t sample_limits = _view.session().cur_samplelimits();
    progress = -(_sample_received * 1.0 / sample_limits * 360 * 16);
    progress100 = ceil(progress / -3.6 / 16);
}

void Viewport::paintProgress(QPainter &p, QColor fore, QColor back)
{
    (void)back;

    if (_view.session().get_device()->get_work_mode() == LOGIC
        && _view.session().is_repeat_mode()){
        return;
    }

    using pv::view::Signal;

    double progress = 0;
    int progress100 = 0;
    int captured_progress = 0;

    get_captured_progress(progress, progress100);
 
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::gray);
    p.setBrush(Qt::NoBrush);
    const QPoint cenPos = QPoint(_view.get_view_width() / 2, height() / 2);
    const int radius = min(0.3 * _view.get_view_width(), 0.3 * height());
    p.drawEllipse(cenPos, radius - 2, radius - 2);
    p.setPen(QPen(View::Green, 4, Qt::SolidLine));
    p.drawArc(cenPos.x() - radius, cenPos.y() - radius, 2* radius, 2 * radius, 180 * 16, progress);

    p.setPen(Qt::gray);

    const QPoint logoPoints[] = {
        QPoint(cenPos.x() - 0.75 * radius, cenPos.y()),
        QPoint(cenPos.x() - 0.75 * radius, cenPos.y() + 0.15 * radius),
        QPoint(cenPos.x() - 0.6 * radius, cenPos.y()),
        QPoint(cenPos.x() - 0.6 * radius, cenPos.y() + 0.3 * radius),
        QPoint(cenPos.x() - 0.45 * radius, cenPos.y()),
        QPoint(cenPos.x() - 0.45 * radius, cenPos.y() + 0.45 * radius),
        QPoint(cenPos.x() - 0.3 * radius, cenPos.y()),
        QPoint(cenPos.x() - 0.3 * radius, cenPos.y() + 0.3 * radius),
        QPoint(cenPos.x() - 0.15 * radius, cenPos.y()),
        QPoint(cenPos.x() - 0.15 * radius, cenPos.y() + 0.15 * radius),
        QPoint(cenPos.x() + 0.15 * radius, cenPos.y()),
        QPoint(cenPos.x() + 0.15 * radius, cenPos.y() - 0.15 * radius),
        QPoint(cenPos.x() + 0.3 * radius, cenPos.y()),
        QPoint(cenPos.x() + 0.3 * radius, cenPos.y() - 0.3 * radius),
        QPoint(cenPos.x() + 0.45 * radius, cenPos.y()),
        QPoint(cenPos.x() + 0.45 * radius, cenPos.y() - 0.45 * radius),
        QPoint(cenPos.x() + 0.6 * radius, cenPos.y()),
        QPoint(cenPos.x() + 0.6 * radius, cenPos.y() - 0.3 * radius),
        QPoint(cenPos.x() + 0.75 * radius, cenPos.y()),
        QPoint(cenPos.x() + 0.75 * radius, cenPos.y() - 0.15 * radius)
    };

    const int logoRadius = 10;
    p.drawLine(logoPoints[0], logoPoints[1]);
    p.drawLine(logoPoints[2], logoPoints[3]);
    p.drawLine(logoPoints[4], logoPoints[5]);
    p.drawLine(logoPoints[6], logoPoints[7]);
    p.drawLine(logoPoints[8], logoPoints[9]);
    p.drawLine(logoPoints[10], logoPoints[11]);
    p.drawLine(logoPoints[12], logoPoints[13]);
    p.drawLine(logoPoints[14], logoPoints[15]);
    p.drawLine(logoPoints[16], logoPoints[17]);
    p.drawLine(logoPoints[18], logoPoints[19]);
    p.drawEllipse(logoPoints[1].x() - 0.5 * logoRadius, logoPoints[1].y(),
            logoRadius, logoRadius);
    p.drawEllipse(logoPoints[3].x() - 0.5 * logoRadius, logoPoints[3].y(),
            logoRadius, logoRadius);
    p.drawEllipse(logoPoints[5].x() - 0.5 * logoRadius, logoPoints[5].y(),
            logoRadius, logoRadius);
    p.drawEllipse(logoPoints[7].x() - 0.5 * logoRadius, logoPoints[7].y(),
            logoRadius, logoRadius);
    p.drawEllipse(logoPoints[9].x() - 0.5 * logoRadius, logoPoints[9].y(),
            logoRadius, logoRadius);
    p.drawEllipse(logoPoints[11].x() - 0.5 * logoRadius, logoPoints[11].y() - logoRadius,
            logoRadius, logoRadius);
    p.drawEllipse(logoPoints[13].x() - 0.5 * logoRadius, logoPoints[13].y() - logoRadius,
            logoRadius, logoRadius);
    p.drawEllipse(logoPoints[15].x() - 0.5 * logoRadius, logoPoints[15].y() - logoRadius,
            logoRadius, logoRadius);
    p.drawEllipse(logoPoints[17].x() - 0.5 * logoRadius, logoPoints[17].y() - logoRadius,
            logoRadius, logoRadius);
    p.drawEllipse(logoPoints[19].x() - 0.5 * logoRadius, logoPoints[19].y() - logoRadius,
            logoRadius, logoRadius);

    if (!_transfer_started) {
        const int width = _view.get_view_width();
        const QPoint cenLeftPos = QPoint(width / 2 - 0.05 * width, height() / 2);
        const QPoint cenRightPos = QPoint(width / 2 + 0.05 * width, height() / 2);
        const int trigger_radius = min(0.02 * width, 0.02 * height());

        QColor foreBack = fore;
        foreBack.setAlpha(View::BackAlpha);
        p.setPen(Qt::NoPen);
        p.setBrush((_timer_cnt % 3) == 0 ? fore : foreBack);
        p.drawEllipse(cenLeftPos, trigger_radius, trigger_radius);
        p.setBrush((_timer_cnt % 3) == 1 ? fore : foreBack);
        p.drawEllipse(cenPos, trigger_radius, trigger_radius);
        p.setBrush((_timer_cnt % 3) == 2 ? fore : foreBack);
        p.drawEllipse(cenRightPos, trigger_radius, trigger_radius);

        bool triggered;
         
        if (_view.session().get_capture_status(triggered, captured_progress)){
            p.setPen(View::Blue); 

            QFont font = p.font();
            float fSize = AppConfig::Instance().appOptions.fontSize;
            if (fSize > 10)
                fSize = 10;
            font.setPointSizeF(fSize);
            p.setFont(font);

            QRect status_rect = QRect(cenPos.x() - radius, cenPos.y() + radius * 0.4, radius * 2, radius * 0.5);
            
            if (triggered) {
                p.drawText(status_rect,
                           Qt::AlignCenter | Qt::AlignVCenter,
                           L_S(STR_PAGE_DLG, S_ID(IDS_DLG_TRIGGERED), "Triggered! ") + QString::number(captured_progress) 
                           + L_S(STR_PAGE_DLG, S_ID(IDS_DLG_CAPTURED), "% Captured"));
            }
            else {
                p.drawText(status_rect,
                           Qt::AlignCenter | Qt::AlignVCenter,
                           L_S(STR_PAGE_DLG, S_ID(IDS_DLG_WAITING_FOR_TRIGGER), "Waiting for Trigger! ") + QString::number(captured_progress) 
                           + L_S(STR_PAGE_DLG, S_ID(IDS_DLG_CAPTURED), "% Captured"));
            }

            prgRate(captured_progress);
        }

    }
    else {         
        p.setPen(View::Green);
        QFont font=p.font();
        font.setPointSize(50);
        font.setBold(true);
        p.setFont(font);
        
        p.drawText(_view.get_view_rect(), Qt::AlignCenter | Qt::AlignVCenter, QString::number(progress100)+"%");
        prgRate(progress100);
    }

    p.setPen(QPen(View::Blue, 4, Qt::SolidLine));
    const int int_radius = max(radius - 4, 0);
    p.drawArc(cenPos.x() - int_radius, cenPos.y() - int_radius, 2* int_radius, 2 * int_radius, 180 * 16, -captured_progress*3.6*16);
    QFont font;
    p.setFont(font);

    p.setRenderHint(QPainter::Antialiasing, false);
}

void Viewport::mousePressEvent(QMouseEvent *event)
{
	assert(event);
    
	_mouse_down_point = event->pos();
	_mouse_down_offset = _view.offset();
    _drag_strength = 0;
    _elapsed_time.restart();

    if (_action_type == NO_ACTION
        && event->button() == Qt::RightButton
        && _view.session().is_stopped_status())
    {
        if (_view.session().get_device()->get_work_mode() == LOGIC) {
            set_action(LOGIC_ZOOM);
        }
        else if (_view.session().get_device()->get_work_mode() == DSO) {
            if (_hover_hit) {
                const int64_t index = _view.pixel2index(event->pos().x());
                auto &cursor_list = _view.get_cursorList();
                _view.add_cursor(index);
                _view.show_cursors(true);
            }
        }
    }

    if (_action_type == NO_ACTION &&
        event->button() == Qt::LeftButton &&
        _view.session().get_device()->get_work_mode() == DSO) {

       for(auto s : _view.session().get_signals()) 
       { 
            if (s->signal_type() == SR_CHANNEL_DSO && s->enabled()) {
                DsoSignal *dsoSig = (DsoSignal*)s;
                if (dsoSig->get_trig_rect(0, _view.get_view_width()).contains(_mouse_point)) {
                   _drag_sig = s;
                   set_action(DSO_TRIG_MOVE);
                   dsoSig->select(true);
                   break;
                }
            }
        }
    }

    if (_action_type == NO_ACTION
            && event->button() == Qt::LeftButton) {
        if (_action_type == NO_ACTION && _view.search_cursor_shown()) {
            const int64_t searchX = _view.index2pixel(_view.get_search_cursor()->index());

            if (_view.get_search_cursor()->grabbed()) {
                _view.get_ruler()->rel_grabbed_cursor();
            }
            else if (qAbs(searchX - event->pos().x()) <= HitCursorMargin) {
                _view.get_ruler()->set_grabbed_cursor(_view.get_search_cursor());
                set_action(CURS_MOVE);
            }    
        }
 
        if (_action_type == NO_ACTION && _view.cursors_shown()) {
            auto &cursor_list = _view.get_cursorList();
            auto i = cursor_list.begin();

            while (i != cursor_list.end()) {
                const int64_t cursorX = _view.index2pixel((*i)->index());
                if ((*i)->grabbed()) {
                    _view.get_ruler()->rel_grabbed_cursor();
                }
                else if (qAbs(cursorX - event->pos().x()) <= HitCursorMargin) {
                    _view.get_ruler()->set_grabbed_cursor(*i);
                    set_action(CURS_MOVE);
                    break;
                }
                i++;
            }
        }

        if (_action_type == NO_ACTION && _view.xcursors_shown()) {
            auto &xcursor_list = _view.get_xcursorList();
            auto i = xcursor_list.begin();
            const QRect xrect = _view.get_view_rect();

            while (i != xcursor_list.end()) {
                const double cursorX  = xrect.left() + (*i)->value(XCursor::XCur_Y)*xrect.width();
                const double cursorY0 = xrect.top() + (*i)->value(XCursor::XCur_X0)*xrect.height();
                const double cursorY1 = xrect.top() + (*i)->value(XCursor::XCur_X1)*xrect.height();
                
                if ((*i)->get_close_rect(xrect).contains(_view.hover_point())) {
                    _view.del_xcursor(*i);
                    if (xcursor_list.empty())
                        _view.show_xcursors(false);
                    break;
                }
                else if ((*i)->get_map_rect(xrect).contains(_view.hover_point())) {
                    auto &sigs = _view.session().get_signals();
                    auto s = sigs.begin();
                    bool sig_looped = ((*i)->channel() == NULL);
                    bool no_dsoSig = true;

                    while (true) { 
                        if ((*s)->signal_type() == SR_CHANNEL_DSO && (*s)->enabled()) {
                            view::DsoSignal *dsoSig = (view::DsoSignal*)(*s);
                            no_dsoSig = false;
                            if (sig_looped) {
                                (*i)->set_channel(dsoSig);
                                break;
                            } else if (dsoSig == (*i)->channel()) {
                                sig_looped = true;
                            }
                        }

                        s++;
                        if (s == sigs.end()) {
                            if (no_dsoSig) {
                                (*i)->set_channel(NULL);
                                break;
                            }
                            sig_looped = true;
                            s = sigs.begin();
                        }
                    }
                    break;
                }
                else if ((*i)->grabbed() != XCursor::XCur_None) {
                    (*i)->set_grabbed((*i)->grabbed(), false);
                }
                else if (qAbs(cursorX - _view.hover_point().x()) <= HitCursorMargin &&
                           _view.hover_point().y() > min(cursorY0, cursorY1) &&
                           _view.hover_point().y() < max(cursorY0, cursorY1)) {
                    (*i)->set_grabbed(XCursor::XCur_Y, true);
                    set_action(CURS_MOVE);
                    break;
                }
                else if (qAbs(cursorY0 - _view.hover_point().y()) <= HitCursorMargin) {
                    (*i)->set_grabbed(XCursor::XCur_X0, true);
                    set_action(CURS_MOVE);
                    break;
                }
                else if (qAbs(cursorY1 - _view.hover_point().y()) <= HitCursorMargin) {
                    (*i)->set_grabbed(XCursor::XCur_X1, true);
                    set_action(CURS_MOVE);
                    break;
                }
                i++;
            }
        }
    }
}

void Viewport:: mouseMoveEvent(QMouseEvent *event)
{
	assert(event);
    _hover_hit = false;
    int mode = _view.session().get_device()->get_work_mode();

    if (event->buttons() & Qt::LeftButton) {
        if (_type == TIME_VIEW) {
            if (_action_type == NO_ACTION) {
                int64_t x = _mouse_down_offset + (_mouse_down_point - event->pos()).x();
                _view.set_scale_offset(_view.scale(), x);
            }
            _drag_strength = (_mouse_down_point - event->pos()).x();
        }
        else if (_type == FFT_VIEW) {
            for(auto t: _view.session().get_spectrum_traces()) {
                if(t->enabled()) {
                    double delta = (_mouse_point - event->pos()).x();
                    t->set_offset(delta);
                    break;
                }
            }
        }
    }

    if (_type == TIME_VIEW) {
        if ((event->buttons() & Qt::LeftButton) ||
            !(event->buttons() | Qt::NoButton)) {
            if (_action_type == DSO_TRIG_MOVE) {
                if (_drag_sig && _drag_sig->signal_type() == SR_CHANNEL_DSO) {            
                    view::DsoSignal *dsoSig = (view::DsoSignal*)_drag_sig;
                    dsoSig->set_trig_vpos(event->pos().y());
                    _dso_trig_moved = true;
                }
            }
            
            if (_action_type == CURS_MOVE) {
                TimeMarker* grabbed_marker = _view.get_ruler()->get_grabbed_cursor();
                if (grabbed_marker) {
                    int curX = _view.hover_point().x();
                    uint64_t index0 = 0, index1 = 0, index2 = 0;
                    bool logic = false;

                   for(auto s : _view.session().get_signals()) {                     
                        if (mode == LOGIC && s->signal_type() == SR_CHANNEL_LOGIC) {
                            view::LogicSignal *logicSig = (view::LogicSignal*)s;
                            if (logicSig->measure(event->pos(), index0, index1, index2)) {
                                logic = true;
                                break;
                            }
                        }
                        if (mode == DSO && s->signal_type() == SR_CHANNEL_DSO) {
                            view::DsoSignal *dsoSig = (view::DsoSignal*)s;
                            curX = min(dsoSig->get_view_rect().right(), curX);

                            if (curX < dsoSig->get_view_rect().left()){
                                curX = dsoSig->get_view_rect().left();
                            }
                            break;
                        }
                    }

                    const double pos = _view.pixel2index(curX);
                    const double pos_delta = pos - (uint64_t)pos;
                    const double curP = _view.index2pixel(index0);
                    const double curN = _view.index2pixel(index1);

                    if (logic && (curX - curP < SnapMinSpace || curN - curX < SnapMinSpace)) {
                        if (curX - curP < curN - curX)
                            grabbed_marker->set_index(index0);
                        else
                            grabbed_marker->set_index(index1);
                    }
                    else if ( pos_delta < 0.5) {
                        grabbed_marker->set_index((uint64_t)floor(pos));
                    }
                    else {
                        grabbed_marker->set_index((uint64_t)ceil(pos));
                    }

                    if (grabbed_marker == _view.get_search_cursor()) {
                        _view.set_search_pos(grabbed_marker->index(), false);
                    }

                    _view.cursor_moving();
                    _curs_moved = true;
                }
                else {
                    if (_view.xcursors_shown()) {
                        auto &xcursor_list = _view.get_xcursorList();
                        const QRect xrect = _view.get_view_rect();

                        for (auto xc : xcursor_list) {
                            if (xc->grabbed() != XCursor::XCur_None) {
                                if (xc->grabbed() == XCursor::XCur_Y) {
                                    int hover_x = _view.hover_point().x();

                                    if (hover_x  < xrect.left()){
                                        hover_x = xrect.left();
                                    }
                                    if (hover_x > xrect.right()){
                                        hover_x = xrect.right();
                                    }

                                    double rate = (hover_x - xrect.left()) * 1.0 / xrect.width();                                    
                                    xc->set_value(xc->grabbed(), min(rate, 1.0));
                                }
                                else {
                                    int msy = _view.hover_point().y();
                                    int body_y = _view.get_body_height();
                                    if (msy > body_y)
                                        msy = body_y;
                                     
                                    double rate = (msy - xrect.top()) * 1.0 / xrect.height();
                                    xc->set_value(xc->grabbed(), max(rate, 0.0));
                                }
                                _xcurs_moved = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (!(event->buttons() | Qt::NoButton)) {
            if (_action_type == DSO_XM_STEP1 || _action_type == DSO_XM_STEP2) {
                for(auto s : _view.session().get_signals()) {
                    if (!s->get_view_rect().contains(event->pos())) {
                        clear_dso_xm();
                    }
                    break;
                }
            }

            if (_action_type == DSO_YM)
                _dso_ym_end = event->pos().y();
        }
    }

    _mouse_point = event->pos();

    measure();
   
    update(UpdateEventType::UPDATE_EV_MS_MOVE);
}

void Viewport::set_action(ActionType action)
{
    if (_action_type == LOGIC_JUMP && action != LOGIC_JUMP)
    {
       // int bbb = 0;
      // assert(0);
    }
    _action_type = action;
}

void Viewport::onLogicMouseRelease(QMouseEvent *event)
{
    bool quickScroll = AppConfig::Instance().appOptions.quickScroll;
    bool isMaxWindow = AppControl::Instance()->TopWindowIsMaximized();

    switch (_action_type)
    {
        case NO_ACTION:
        {
            if (event->button() == Qt::LeftButton && _view.session().is_stopped_status()){
                //priority 1
                //try to quick scroll view...
                int curX = event->pos().x();
                int clickX = _mouse_down_point.x();
                int moveLong = ABS_VAL(curX - clickX);                
                int maxWidth = this->geometry().width();
                float mvk = (float) moveLong / (float)maxWidth;

                if (quickScroll){
                    quickScroll = false; 
                    if (isMaxWindow && mvk > 0.4f){
                        quickScroll = true;
                    }
                    else if (!isMaxWindow && mvk > 0.25f){
                        quickScroll = true;
                    }
                }

                if (_action_type == NO_ACTION && quickScroll) {
                    const double strength = _drag_strength*DragTimerInterval*1.0/_elapsed_time.elapsed();
                    if (_elapsed_time.elapsed() < 200 &&
                        abs(_drag_strength) < MinorDragOffsetUp &&
                        abs(strength) > MinorDragRateUp) {
                        _drag_timer.start(DragTimerInterval);
                        set_action(LOGIC_MOVE);
                    }
                    else if (_elapsed_time.elapsed() < 200 &&
                               abs(strength) > DragTimerInterval) {
                        _drag_strength = strength * 5;
                        _drag_timer.start(DragTimerInterval);
                        set_action(LOGIC_MOVE);
                    }
                }

                // priority 2
                if (_action_type == NO_ACTION) {
                    if (_mouse_down_point.x() == event->pos().x()) {
                        const auto &sigs = _view.session().get_signals();

                        for(auto s : sigs) { 
                            if (s->signal_type() == SR_CHANNEL_LOGIC) {
                                view::LogicSignal *logicSig = (view::LogicSignal*)s;
                                if (logicSig->is_by_edge(event->pos(), _edge_start, 10)) {
                                    set_action(LOGIC_JUMP);                                    
                                    _cur_preX = _view.index2pixel(_edge_start);
                                    _cur_preY = logicSig->get_y();
                                    _cur_preY_top = logicSig->get_y() - logicSig->get_totalHeight()/2 - 12;
                                    _cur_preY_bottom = logicSig->get_y() + logicSig->get_totalHeight()/2 + 2;
                                    _cur_aftX = _cur_preX;
                                    _cur_aftY = _cur_preY;

                                    static int times = 0;
                                    times++;
                                    dsv_info("click by edge %d", times);
                                    break;
                                }
                            }
                        }
                    }
                }

                // priority 3
                if (_action_type == NO_ACTION) {
                    if (_mouse_down_point.x() == event->pos().x()) {
                        const auto  &sigs = _view.session().get_signals();

                        for(auto s : sigs) {
                            if (abs(event->pos().y() - s->get_y()) < _view.get_signalHeight()) {
                                set_action(LOGIC_EDGE);
                                _edge_start = _view.pixel2index(event->pos().x());
                                break;
                            }
                        }
                    }
                }
            } 
            break;
        }
        case LOGIC_EDGE:
        {
            set_action(NO_ACTION);
            _edge_rising = 0;
            _edge_falling = 0;
            break;
        }
        case LOGIC_JUMP:
        {
            set_action(NO_ACTION);
            _edge_rising = 0;
            _edge_falling = 0;
            _edge_hit = false;
            break;
        }
        case LOGIC_MOVE:
        {
            if (_mouse_down_point == event->pos()) {
                _drag_strength = 0;
                _drag_timer.stop();
                set_action(NO_ACTION);
            }
            else {
                const double strength = _drag_strength*DragTimerInterval*1.0/_elapsed_time.elapsed();
                if (_elapsed_time.elapsed() < 200 &&
                    abs(_drag_strength) < MinorDragOffsetUp &&
                    abs(strength) > MinorDragRateUp) {
                    _drag_timer.start(DragTimerInterval);
                }
                else if (_elapsed_time.elapsed() < 200 &&
                           abs(strength) > DragTimerInterval) {
                    _drag_strength = strength * 5;
                    _drag_timer.start(DragTimerInterval);
                }
                else {
                    _drag_strength = 0;
                    _drag_timer.stop();
                    set_action(NO_ACTION);
                }
            }
            break;
        }
        case LOGIC_ZOOM:
        {
            if (event->pos().x() != _mouse_down_point.x()) {
                int64_t newOffset = _view.offset() + (min(event->pos().x(), _mouse_down_point.x()));
                const double newScale = max(min(_view.scale() * abs(event->pos().x() - _mouse_down_point.x()) / _view.get_view_width(),
                                                _view.get_maxscale()), _view.get_minscale());
                newOffset = floor(newOffset * (_view.scale() / newScale));
                if (newScale != _view.scale())
                    _view.set_scale_offset(newScale, newOffset);
            }
            set_action(NO_ACTION);
            break;
        }
    } 
}

void Viewport::onDsoMouseRelease(QMouseEvent *event)
{
    switch (_action_type)
    {
        case NO_ACTION:
        {
            if (event->button() == Qt::LeftButton && _hover_hit)
            {
                set_action(DSO_YM);
                _dso_ym_valid = true;
                _dso_ym_sig_index = _hover_sig_index;
                _dso_ym_sig_value = _hover_sig_value;
                _dso_ym_index = _hover_index;
                _dso_ym_start = event->pos().y();
            }
            break;
        }
        case DSO_YM:
        {
            if (event->button() == Qt::LeftButton) {
                _dso_ym_end = event->pos().y();
                set_action(NO_ACTION);
            }
            else if (event->button() == Qt::RightButton) {
                set_action(NO_ACTION);
                _dso_ym_valid = false;
            }
            break;
        }
        case DSO_TRIG_MOVE:
        {
            if (_dso_trig_moved && event->button() == Qt::LeftButton) {
                _drag_sig = NULL;
                set_action(NO_ACTION);
                _dso_trig_moved = false;

                std::vector<Trace*> traces;
                _view.get_traces(ALL_VIEW, traces);

                for(auto t : traces){
                     t->select(false);
                }                   
            }
            break;
        }
        case DSO_XM_STEP0:
        {
            if (event->button() == Qt::LeftButton) {
                set_action(DSO_XM_STEP1);
                _dso_xm_valid = true;
            }
            break;
        }
        case DSO_XM_STEP1:
        {
            if (event->button() == Qt::LeftButton) {
                _dso_xm_index[1] = _view.pixel2index(event->pos().x());
                const uint64_t max_index = max(_dso_xm_index[0], _dso_xm_index[1]);
                _dso_xm_index[0] = min(_dso_xm_index[0], _dso_xm_index[1]);
                _dso_xm_index[1] = max_index;

                set_action(DSO_XM_STEP2);
            }
            else if (event->button() == Qt::RightButton) {
                clear_dso_xm();
                measure_updated();
            }
            break;
        }
        case DSO_XM_STEP2:
        {
            if (event->button() == Qt::LeftButton) {
                _dso_xm_index[2] = _view.pixel2index(event->pos().x());
                uint64_t max_index = max(_dso_xm_index[1], _dso_xm_index[2]);
                _dso_xm_index[1] = min(_dso_xm_index[1], _dso_xm_index[2]);
                _dso_xm_index[2] = max_index;

                max_index = max(_dso_xm_index[0], _dso_xm_index[1]);
                _dso_xm_index[0] = min(_dso_xm_index[0], _dso_xm_index[1]);
                _dso_xm_index[1] = max_index;

                set_action(NO_ACTION);
            }
            else if (event->button() == Qt::RightButton) {
                clear_dso_xm();
                measure_updated();
            }
            break;
        }
    }
}

void Viewport::onAnalogMouseRelease(QMouseEvent *event)
{

}

void Viewport::mouseReleaseEvent(QMouseEvent *event)
{
    assert(event);

    if (_type != TIME_VIEW){
        update(UpdateEventType::UPDATE_EV_MS_UP);
        return;
    }

    int mode = _view.session().get_device()->get_work_mode();

    if (mode == LOGIC){
        onLogicMouseRelease(event);
    }
    else if (mode == DSO){
        onDsoMouseRelease(event);
    }
    else if (mode == ANALOG){
        onAnalogMouseRelease(event);
    }

    if (_action_type == CURS_MOVE) {
        if (_curs_moved && event->button() == Qt::LeftButton) {
            set_action(NO_ACTION);
            _view.get_ruler()->rel_grabbed_cursor();
            _view.cursor_moved();
            _curs_moved = false;
        }
        if (_xcurs_moved && event->button() == Qt::LeftButton) {
            set_action(NO_ACTION);
            auto &xcursor_list = _view.get_xcursorList();
            auto i = xcursor_list.begin();
            
            while (i != xcursor_list.end()) {
                (*i)->rel_grabbed();
                i++;
            }

            _xcurs_moved = false;
        }
    }
   
    /*
    // This code block prevents the cursor from moving.
    if (mode == LOGIC && event->button() == Qt::LeftButton){
        int clickX = _mouse_down_point.x();
        const int64_t index = _view.pixel2index(clickX);
        const int64_t total = _view.session().get_ring_sample_count();
        if (index > total){
            _measure_type = MeasureType::NO_MEASURE;
            set_action(NO_ACTION);
        }
    }
    */

    update(UpdateEventType::UPDATE_EV_MS_UP);
}

void Viewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    assert (event);

    if (!_view.get_view_rect().contains(event->pos()))
        return;

    int mode = _view.session().get_device()->get_work_mode();

    if (mode == LOGIC)
    {
        if (event->button() == Qt::RightButton) {
            if (_view.scale() == _view.get_maxscale())
                _view.set_preScale_preOffset();
            else
                _view.set_scale_offset(_view.get_maxscale(), _view.get_min_offset());
        }
        else if (event->button() == Qt::LeftButton) {
            bool logic = false;
            uint64_t index;
            uint64_t index0 = 0, index1 = 0, index2 = 0;

            if (mode == LOGIC) {
                for(auto s : _view.session().get_signals()) {
                    if (s->signal_type() == SR_CHANNEL_LOGIC) {
                        view::LogicSignal *logicSig  = (view::LogicSignal*)s;
                        if (logicSig->measure(event->pos(), index0, index1, index2)) {
                            logic = true;
                            break;
                        }
                    }
                }
            }
            const double curX = event->pos().x();
            const double curP = _view.index2pixel(index0);
            const double curN = _view.index2pixel(index1);

            if (logic && (curX - curP < SnapMinSpace || curN - curX < SnapMinSpace)) {
                if (curX - curP < curN - curX)
                    index = index0;
                else
                    index = index1;
            }
            else {
                index = _view.pixel2index(curX);
            }

            auto &cursor_list = _view.get_cursorList();
            _view.add_cursor(index);
            _view.show_cursors(true);
        }

        update(UpdateEventType::UPDATE_EV_MS_CLICK);
    }
    else if (_view.session().get_device()->get_work_mode() == DSO
             && _view.session().is_init_status() == false
             && event->button() == Qt::LeftButton) {
        if (_dso_xm_valid) {
            clear_dso_xm();
            measure_updated();
        }
        else if (_action_type == NO_ACTION) {
            for(auto s : _view.session().get_signals()) {
                if (s->get_view_rect().contains(event->pos())) {
                    _dso_xm_index[0] = _view.pixel2index(event->pos().x());
                    _dso_xm_y = event->pos().y();
                    set_action(DSO_XM_STEP0);
                }
                break;
            }
        }
    }
    else if (_view.session().get_device()->get_work_mode() == ANALOG) {
        if (event->button() == Qt::LeftButton) {
            uint64_t index;
            const double curX = event->pos().x();
            index = _view.pixel2index(curX);
            auto &cursor_list = _view.get_cursorList();
            _view.add_cursor(index);
            _view.show_cursors(true);
        }
    }
}

void Viewport::wheelEvent(QWheelEvent *event)
{
    assert(event);

    if (_view.header_is_draging()){
        return;
    }

    int x = 0;  //mouse x pos
    int delta = 0;
    bool isVertical = true;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    x = (int)event->position().x(); 
    int anglex = event->angleDelta().x();
    int angley = event->angleDelta().y();

    if (anglex == 0 || ABS_VAL(angley) >= ABS_VAL(anglex)){
        delta = angley;
        isVertical = true;
    }
    else{
        delta = anglex;
        isVertical = false; //hori direction
    }
#else
    x = event->x();
    delta = event->delta();
    isVertical = event->orientation() == Qt::Vertical;
#endif
    double divisor_zoom = 16;
    double zoom_scale = delta / 80 / divisor_zoom;

    if (ABS_VAL(delta) <= 80){
        zoom_scale = delta > 0 ? 1.5 / divisor_zoom : -1.5 / divisor_zoom;
    }

    if (_type == FFT_VIEW)
    {
        for (auto t : _view.session().get_spectrum_traces())
        { 
            if (t->enabled())
            {
                t->zoom(zoom_scale, x);
                break;
            }
        }
    }
    else if (_type == TIME_VIEW)
    {
        static bool bLstTime = false;

        if (isVertical)
        {
            // Vertical scrolling is interpreted as zooming in/out
#ifdef Q_OS_DARWIN
            static int64_t last_time;

            if (event->source() == Qt::MouseEventSynthesizedBySystem)
            {
                if (!bLstTime)
                {  
                    last_time = QDateTime::currentMSecsSinceEpoch();
                    bLstTime = true;
                }
                else{
                    int64_t cur_time = QDateTime::currentMSecsSinceEpoch();
                    if (cur_time - last_time > 50){
                        double scale = delta > 1.5 ? 1 : (delta < -1.5 ? -1 : 0);
                        _view.zoom(scale, x);
                        last_time = QDateTime::currentMSecsSinceEpoch();
                    }                   
                } 
            }
            else
            {
                _view.zoom(-zoom_scale, x);
            }
#else
            _view.zoom(zoom_scale, x);
#endif
        }
        else
        {   
            bLstTime = false;
            (void)bLstTime;

            // Horizontal scrolling is interpreted as moving left/right
            if (!(event->modifiers() & Qt::ShiftModifier))
                _view.set_scale_offset(_view.scale(), _view.offset() - delta);
        }
    }

    const auto &sigs = _view.session().get_signals();
    for (auto s : sigs)
    {
        if (s->signal_type() == SR_CHANNEL_DSO){   
            view::DsoSignal *dsoSig = (view::DsoSignal*)s;
            dsoSig->auto_end();
        }
    }

    measure();
}

bool Viewport::gestureEvent(QNativeGestureEvent *event)
{
    static double total_scale = 0;
    switch(event->gestureType()) {
        case Qt::BeginNativeGesture:
            break;
        case Qt::EndNativeGesture:
            total_scale = 0;
            break;
        case Qt::ZoomNativeGesture: {
            total_scale += event->value() * 2;
            if (_view.zoom(total_scale, _view.hover_point().x()))
                total_scale = 0;
            }
            break;
        case Qt::SmartZoomNativeGesture:
            _view.zoom(-1, _view.hover_point().x());
            break;
        default:
            return QWidget::event(event);
    }

    measure();
    return true;
}

void Viewport::leaveEvent(QEvent *)
{
    _mouse_point = QPoint(-1, -1);

    if (_action_type == LOGIC_EDGE) {
        _edge_rising = 0;
        _edge_falling = 0;
        set_action(NO_ACTION);
    }
    else if (_action_type == LOGIC_JUMP) {
        _edge_rising = 0;
        _edge_falling = 0;
        set_action(NO_ACTION);
    }
    else if (_action_type == LOGIC_MOVE) {
        _drag_strength = 0;
        _drag_timer.stop();
        set_action(NO_ACTION);
    }
    else if (_action_type == DSO_XM_STEP1 || _action_type == DSO_XM_STEP2) {
        clear_dso_xm();
    }
    else if (_action_type == DSO_YM) {
        _dso_ym_valid = false;
        set_action(NO_ACTION);
    }

    clear_measure();
}

void Viewport::resizeEvent(QResizeEvent*)
{

}

void Viewport::set_receive_len(quint64 length)
{
    if (length == 0) {
        _sample_received = 0;
        start_trigger_timer(333);
        _tigger_wait_times = 0;
        _is_checked_trig = false;
    }
    else {
        stop_trigger_timer();

        if (_sample_received + length > _view.session().cur_samplelimits())
            _sample_received = _view.session().cur_samplelimits();
        else
            _sample_received += length;
    }

    int mode = _view.session().get_device()->get_work_mode();

    if (mode == LOGIC)
    {   
        if (_view.session().get_device()->is_file() == false)
        {
            if (!_is_checked_trig && _view.session().is_triged()){
                _view.get_viewstatus()->set_trig_time(_view.session().get_trig_time());
                _view.get_viewstatus()->update();
                _is_checked_trig = true;
            }
        }

        if (_view.session().is_repeat_mode())
        {
            double progress = 0;
            int progress100 = 0;
            get_captured_progress(progress, progress100);
            _view.show_captured_progress(_transfer_started, progress100);

            if (_view.session().is_single_buffer()){
                if (_view.session().have_new_realtime_refresh(true) == false){
                    return;
                }
            }
            else{
                return;
            }
        }
        else if (_view.session().is_realtime_refresh())
        {
            if (_view.session().have_new_realtime_refresh(true) == false){
                return;
            }
        }      
    }

    if (mode == LOGIC && AppConfig::Instance().appOptions.autoScrollLatestData
        && _view.session().is_realtime_refresh())
    {
        _view.scroll_to_logic_last_data_time();
    }    

    // Received new data, and refresh the view.
    update(UpdateEventType::UPDATE_EV_GENERIC);
}

void Viewport::update(int event)
{
    QWidget::update();
}

void Viewport::clear_measure()
{
    _measure_type = NO_MEASURE;
    update(UpdateEventType::UPDATE_EV_GENERIC);
}

void Viewport::clear_dso_xm()
{
    _dso_xm_valid = false;
    _mm_width = View::Unknown_Str;
    _mm_period = View::Unknown_Str;
    _mm_freq = View::Unknown_Str;
    _mm_duty = View::Unknown_Str;

    set_action(NO_ACTION);
}

void Viewport::measure()
{
    if (_view.session().is_data_lock())
        return;        
    if (_view.session().is_loop_mode() && _view.session().is_working())
        return;
        
    _measure_type = NO_MEASURE;

    if (_type == TIME_VIEW) {
        const uint64_t sample_rate = _view.session().cur_snap_samplerate();

        for(auto s : _view.session().get_signals()) {
            if (s->signal_type() == SR_CHANNEL_LOGIC) {
                view::LogicSignal *logicSig  = (view::LogicSignal*)s;
                
                if (_action_type == NO_ACTION) {
                    if (logicSig->measure(_mouse_point, _cur_sample, _nxt_sample, _thd_sample)) {
                        _measure_type = LOGIC_FREQ;

                        _mm_width = _view.get_ruler()->format_real_time(_nxt_sample - _cur_sample, sample_rate);
                        _mm_period = _thd_sample != 0 ? _view.get_ruler()->format_real_time(_thd_sample - _cur_sample, sample_rate) : View::Unknown_Str;
                        _mm_freq = _thd_sample != 0 ? _view.get_ruler()->format_real_freq(_thd_sample - _cur_sample, sample_rate) : View::Unknown_Str;

                        _cur_preX = _view.index2pixel(_cur_sample);
                        _cur_aftX = _view.index2pixel(_nxt_sample);
                        _cur_thdX = _view.index2pixel(_thd_sample);
                        _cur_midY = logicSig->get_y();

                        _mm_duty = _thd_sample != 0 ? QString::number((_nxt_sample - _cur_sample) * 100.0 / (_thd_sample - _cur_sample), 'f', 2)+"%" :
                                                     View::Unknown_Str;
                        break;
                    }
                    else {
                        _measure_type = NO_MEASURE;
                        _mm_width = View::Unknown_Str;
                        _mm_period = View::Unknown_Str;
                        _mm_freq = View::Unknown_Str;
                        _mm_duty = View::Unknown_Str;
                    }
                }
                else if (_action_type == LOGIC_EDGE) {
                    if (logicSig->edges(_view.hover_point(), _edge_start, _edge_rising, _edge_falling)) {
                        _cur_preX = _view.index2pixel(_edge_start);
                        _cur_aftX = _view.hover_point().x();
                        _cur_midY = logicSig->get_y() - logicSig->get_totalHeight()/2 - 5;

                        _em_rising = L_S(STR_PAGE_DLG, S_ID(IDS_DLG_RISING), "Rising: ") + QString::number(_edge_rising);
                        _em_falling = L_S(STR_PAGE_DLG, S_ID(IDS_DLG_FALLING), "Falling: ") + QString::number(_edge_falling);
                        _em_edges = L_S(STR_PAGE_DLG, S_ID(IDS_DLG_Edges_1), "Edges: ") + QString::number(_edge_rising + _edge_falling);

                        break;
                    }
                }
                else if (_action_type == LOGIC_JUMP) {
                    if (logicSig->edge(_view.hover_point(), _edge_end, 10)) {
                        _cur_aftX = _view.index2pixel(_edge_end);
                        _cur_aftY = logicSig->get_y();
                        _edge_hit = true;
                        break;
                    }
                    else {
                        _cur_preX = _view.index2pixel(_edge_start);
                        _cur_aftX = _view.hover_point().x();
                        _cur_aftY = _view.hover_point().y();
                        _edge_end = _view.pixel2index(_cur_aftX);
                        _edge_hit = false;
                    }
                }
            } 
            else if (s->signal_type() == SR_CHANNEL_DSO) {
                 view::DsoSignal *dsoSig = ( view::DsoSignal*)s;
                if (s->enabled()) {
                    if (_measure_en && dsoSig->measure(_view.hover_point())) {
                        _measure_type = DSO_VALUE;
                    }
                    else {
                        _measure_type = NO_MEASURE;
                    }
                }
            }
            else if (s->signal_type() == SR_CHANNEL_ANALOG) {
                view::AnalogSignal *analogSig = (view::AnalogSignal*)s;
                if (s->enabled()) {
                    if (_measure_en && analogSig->measure(_view.hover_point())) {
                        _measure_type = DSO_VALUE;
                    } else {
                        _measure_type = NO_MEASURE;
                    }
                }
            }
        }
        const auto mathTrace = _view.session().get_math_trace();
        if (mathTrace && mathTrace->enabled()) {
            if (_measure_en && mathTrace->measure(_view.hover_point())) {
                _measure_type = DSO_VALUE;
            } else {
                _measure_type = NO_MEASURE;
            }
        }
    }
    else if (_type == FFT_VIEW) {
        for(auto t : _view.session().get_spectrum_traces()) {
            if(t->enabled()) {
                t->measure(_mouse_point);
            }
        }
    }

    measure_updated();
}

void Viewport::paintMeasure(QPainter &p, QColor fore, QColor back)
{
    QColor active_color = back.black() > 0x80 ? View::Orange : View::Purple;
    _hover_hit = false;
    if (_action_type == NO_ACTION &&
        _measure_type == LOGIC_FREQ) {
        p.setPen(active_color);
        p.drawLine(QLineF(_cur_preX, _cur_midY, _cur_aftX, _cur_midY));
        p.drawLine(QLineF(_cur_preX, _cur_midY, _cur_preX + 2, _cur_midY - 2));
        p.drawLine(QLineF(_cur_preX, _cur_midY, _cur_preX + 2, _cur_midY + 2));
        p.drawLine(QLineF(_cur_aftX - 2, _cur_midY - 2, _cur_aftX, _cur_midY));
        p.drawLine(QLineF(_cur_aftX - 2, _cur_midY + 2, _cur_aftX, _cur_midY));
        if (_thd_sample != 0) {
            p.drawLine(QLineF(_cur_aftX, _cur_midY, _cur_thdX, _cur_midY));
            p.drawLine(QLineF(_cur_aftX, _cur_midY, _cur_aftX + 2, _cur_midY - 2));
            p.drawLine(QLineF(_cur_aftX, _cur_midY, _cur_aftX + 2, _cur_midY + 2));
            p.drawLine(QLineF(_cur_thdX - 2, _cur_midY - 2, _cur_thdX, _cur_midY));
            p.drawLine(QLineF(_cur_thdX - 2, _cur_midY + 2, _cur_thdX, _cur_midY));
        }

        if (_measure_en) {
            int typical_width = p.boundingRect(0, 0, INT_MAX, INT_MAX,
                Qt::AlignLeft | Qt::AlignTop, _mm_width).width();
            typical_width = max(typical_width, p.boundingRect(0, 0, INT_MAX, INT_MAX,
                Qt::AlignLeft | Qt::AlignTop, _mm_period).width());
            typical_width = max(typical_width, p.boundingRect(0, 0, INT_MAX, INT_MAX,
                Qt::AlignLeft | Qt::AlignTop, _mm_freq).width());
            typical_width = max(typical_width, p.boundingRect(0, 0, INT_MAX, INT_MAX,
                Qt::AlignLeft | Qt::AlignTop, _mm_duty).width());
            typical_width = typical_width + 100;

            const double width = _view.get_view_width();
            const double height = _view.viewport()->height();
            const double left = _view.hover_point().x();
            const double top = _view.hover_point().y();
            const double right = left + typical_width;
            const double bottom = top + 80;
            QPointF org_pos = QPointF(right > width ? left - typical_width : left, bottom > height ? top - 80 : top);
            QRectF measure_rect = QRectF(org_pos.x(), org_pos.y(), (double)typical_width, 80.0);
            QRectF measure1_rect = QRectF(org_pos.x(), org_pos.y(), (double)typical_width, 20.0);
            QRectF measure2_rect = QRectF(org_pos.x(), org_pos.y()+20, (double)typical_width, 20.0);
            QRectF measure3_rect = QRectF(org_pos.x(), org_pos.y()+40, (double)typical_width, 20.0);
            QRectF measure4_rect = QRectF(org_pos.x(), org_pos.y()+60, (double)typical_width, 20.0);

            p.setPen(Qt::NoPen);
            p.setBrush(View::LightBlue);
            p.drawRect(measure_rect);

            p.setPen(active_color);
            p.drawText(measure1_rect, Qt::AlignRight | Qt::AlignVCenter,
                       L_S(STR_PAGE_DLG, S_ID(IDS_DLG_WIDTH), "Width: ") + _mm_width);
            p.drawText(measure2_rect, Qt::AlignRight | Qt::AlignVCenter,
                       L_S(STR_PAGE_DLG, S_ID(IDS_DLG_PERIOD), "Period: ") + _mm_period);
            p.drawText(measure3_rect, Qt::AlignRight | Qt::AlignVCenter,
                       L_S(STR_PAGE_DLG, S_ID(IDS_DLG_FREQUENCY), "Frequency: ") + _mm_freq);
            p.drawText(measure4_rect, Qt::AlignRight | Qt::AlignVCenter,
                      L_S(STR_PAGE_DLG, S_ID(IDS_DLG_DUTY_CYCLE), "Duty Cycle: ") + _mm_duty);
        }
    } 

    if (_action_type == NO_ACTION &&
        _measure_type == DSO_VALUE) {

        for(auto s : _view.session().get_signals()) {
            if (s->signal_type() == SR_CHANNEL_DSO) {
                uint64_t index;
                double value;
                view::DsoSignal *dsoSig  = (view::DsoSignal*)s;
                QPointF hpoint;
                if (dsoSig->get_hover(index, hpoint, value)) {
                    p.setPen(QPen(fore, 1, Qt::DashLine));
                    p.setBrush(Qt::NoBrush);
                    p.drawLine(hpoint.x(), dsoSig->get_view_rect().top(),
                               hpoint.x(), dsoSig->get_view_rect().bottom());
                }
            } 
            else if (s->signal_type() == SR_CHANNEL_ANALOG) {
                uint64_t index;
                double value;
                QPointF hpoint;
                view::AnalogSignal* analogSig = (view::AnalogSignal*)s;
                if (analogSig->get_hover(index, hpoint, value)) {
                    p.setPen(QPen(fore, 1, Qt::DashLine));
                    p.setBrush(Qt::NoBrush);
                    p.drawLine(hpoint.x(), analogSig->get_view_rect().top(),
                               hpoint.x(), analogSig->get_view_rect().bottom());
                }
            }
        }
    }

    if (_dso_ym_valid) {
        for(auto s : _view.session().get_signals()) {          
            if (s->signal_type() == SR_CHANNEL_DSO) {
                view::DsoSignal *dsoSig = (view::DsoSignal*)s;
                if (dsoSig->get_index() == _dso_ym_sig_index) {
                    p.setPen(QPen(dsoSig->get_colour(), 1, Qt::DotLine));
                    const int text_height = p.boundingRect(0, 0, INT_MAX, INT_MAX,
                        Qt::AlignLeft | Qt::AlignTop, "W").height();
                    const int64_t x = _view.index2pixel(_dso_ym_index);
                    p.drawLine(x-10, _dso_ym_start,
                               x+10, _dso_ym_start);
                    p.drawLine(x, _dso_ym_start,
                               x, _dso_ym_end);
                    p.drawLine(0, _dso_ym_end,
                               _view.get_view_width(), _dso_ym_end);

                    // -- vertical delta value
                    double hrate = (_dso_ym_start - _dso_ym_end) * 1.0f / _view.get_view_height();
                    double value = hrate  * dsoSig->get_vDialValue() * dsoSig->get_factor() * DS_CONF_DSO_VDIVS;
                    QString value_str = abs(value) > 1000 ? QString::number(value/1000.0, 'f', 2) + "V" : QString::number(value, 'f', 2) + "mV";
                    int value_rect_width = p.boundingRect(0, 0, INT_MAX, INT_MAX,
                                                      Qt::AlignLeft | Qt::AlignVCenter, value_str).width();
                    p.drawText(QRect(x+10, abs(_dso_ym_start+_dso_ym_end)/2, value_rect_width, text_height),
                               value_str);

                    // -- start value
                    value_str = abs(_dso_ym_sig_value) > 1000 ? QString::number(_dso_ym_sig_value/1000.0, 'f', 2) + "V" : QString::number(_dso_ym_sig_value, 'f', 2) + "mV";
                    value_rect_width = p.boundingRect(0, 0, INT_MAX, INT_MAX,
                                                      Qt::AlignLeft | Qt::AlignVCenter, value_str).width();
                    int str_y = value > 0 ? _dso_ym_start : _dso_ym_start - text_height;
                    p.drawText(QRect(x-0.5*value_rect_width, str_y, value_rect_width, text_height),
                               value_str);

                    // -- end value
                    double end_value = _dso_ym_sig_value + value;
                    value_str = abs(end_value) > 1000 ? QString::number(end_value/1000.0, 'f', 2) + "V" : QString::number(end_value, 'f', 2) + "mV";
                    value_rect_width = p.boundingRect(0, 0, INT_MAX, INT_MAX,
                                                      Qt::AlignLeft | Qt::AlignVCenter, value_str).width();
                    str_y = value > 0 ? _dso_ym_end-text_height : _dso_ym_end;
                    p.drawText(QRect(x-0.5*value_rect_width, str_y, value_rect_width, text_height),
                               value_str);
                    break;
                }
            }
        }
    }

    if (_dso_xm_valid) {
        p.setPen(QPen(Qt::red, 1, Qt::DotLine));
        int measure_line_count = 6;
        const int text_height = p.boundingRect(0, 0, INT_MAX, INT_MAX,
            Qt::AlignLeft | Qt::AlignTop, "W").height();
        const uint64_t sample_rate = _view.session().cur_snap_samplerate();
        QLineF *line;
        QLineF *const measure_lines = new QLineF[measure_line_count];
        line = measure_lines;
        int64_t x[DsoMeasureStages];
        int dso_xm_stage = 0;
        if (_action_type == DSO_XM_STEP1)
            dso_xm_stage = 1;
        else if(_action_type == DSO_XM_STEP2)
            dso_xm_stage = 2;
        else
            dso_xm_stage = 3;

        for (int i = 0; i < dso_xm_stage; i++) {
            x[i] = _view.index2pixel(_dso_xm_index[i]);
        }
        measure_line_count = 0;
        if (dso_xm_stage > 0) {
            *line++ = QLine(x[0], _dso_xm_y - 10,
                           x[0], _dso_xm_y + 10);
            measure_line_count += 1;
        }
        if (dso_xm_stage > 1) {
            *line++ = QLine(x[1], _dso_xm_y - 10,
                           x[1], _dso_xm_y + 10);
            *line++ = QLine(x[0], _dso_xm_y,
                           x[1], _dso_xm_y);
            _mm_width = _view.get_ruler()->format_real_time(_dso_xm_index[1] - _dso_xm_index[0], sample_rate);

            // -- width show
            const QString w_ctr = "W="+_mm_width;
            int w_rect_width = p.boundingRect(0, 0, INT_MAX, INT_MAX,
                                              Qt::AlignLeft | Qt::AlignVCenter, w_ctr).width();
            p.drawText(QRect(x[0]+10, _dso_xm_y - text_height, w_rect_width, text_height), w_ctr);
            measure_line_count += 2;
        }
        if (dso_xm_stage > 2) {
            *line++ = QLineF(x[0], _dso_xm_y + 20,
                           x[0], _dso_xm_y + 40);
            *line++ = QLineF(x[0], _dso_xm_y + 30,
                           x[2], _dso_xm_y + 30);
            *line++ = QLineF(x[2], _dso_xm_y + 20,
                           x[2], _dso_xm_y + 40);
            _mm_period = _view.get_ruler()->format_real_time(_dso_xm_index[2] - _dso_xm_index[0], sample_rate);
            _mm_freq = _view.get_ruler()->format_real_freq(_dso_xm_index[2] - _dso_xm_index[0], sample_rate);
            _mm_duty = QString::number((_dso_xm_index[1] - _dso_xm_index[0]) * 100.0 / (_dso_xm_index[2] - _dso_xm_index[0]), 'f', 2)+"%";

            // -- period show
            const QString p_ctr = "P="+_mm_period;
            int p_rect_width = p.boundingRect(0, 0, INT_MAX, INT_MAX,
                                              Qt::AlignLeft | Qt::AlignVCenter, p_ctr).width();
            p.drawText(QRect(x[0]+10, _dso_xm_y + 30 - text_height, p_rect_width, text_height), p_ctr);

            // -- frequency show
            const QString f_ctr = "F="+_mm_freq;
            int f_rect_width = p.boundingRect(0, 0, INT_MAX, INT_MAX,
                                              Qt::AlignLeft | Qt::AlignVCenter, f_ctr).width();
            p.drawText(QRect(x[0]+20 + p_rect_width, _dso_xm_y + 30 - text_height, f_rect_width, text_height), f_ctr);

            // -- duty show
            const QString d_ctr = "D="+_mm_duty;
            int d_rect_width = p.boundingRect(0, 0, INT_MAX, INT_MAX,
                                              Qt::AlignLeft | Qt::AlignVCenter, d_ctr).width();
            p.drawText(QRect(x[1]+10, _dso_xm_y - 0.5*text_height, d_rect_width, text_height), d_ctr);

            measure_line_count += 3;
        }
        p.drawLines(measure_lines, measure_line_count);
        if (dso_xm_stage < DsoMeasureStages) {
            p.drawLine(x[dso_xm_stage-1], _dso_xm_y,
                       _mouse_point.x(), _dso_xm_y);
            p.drawLine(_mouse_point.x(), 0,
                       _mouse_point.x(), height());
        }
        measure_updated();
    }

    if (_action_type == LOGIC_EDGE 
            && _view.session().have_view_data()){
        p.setPen(active_color);
        p.drawLine(QLineF(_cur_preX, _cur_midY-5, _cur_preX, _cur_midY+5));
        p.drawLine(QLineF(_cur_aftX, _cur_midY-5, _cur_aftX, _cur_midY+5));
        p.drawLine(QLineF(_cur_preX, _cur_midY, _cur_aftX, _cur_midY));

        int typical_width = p.boundingRect(0, 0, INT_MAX, INT_MAX,
            Qt::AlignLeft | Qt::AlignTop, _em_edges).width();
        typical_width = max(typical_width, p.boundingRect(0, 0, INT_MAX, INT_MAX,
            Qt::AlignLeft | Qt::AlignTop, _em_rising).width());
        typical_width = max(typical_width, p.boundingRect(0, 0, INT_MAX, INT_MAX,
            Qt::AlignLeft | Qt::AlignTop, _em_falling).width());

        typical_width = typical_width + 60;

        const double width = _view.get_view_width();
        const double height = _view.viewport()->height();
        const double left = _view.hover_point().x();
        const double top = _view.hover_point().y();
        const double right = left + typical_width;
        const double bottom = top + 60;
        QPointF org_pos = QPointF(right > width ? left - typical_width : left, bottom > height ? top - 80 : top);
        QRectF measure_rect = QRectF(org_pos.x(), org_pos.y(), (double)typical_width, 60.0);
        QRectF measure1_rect = QRectF(org_pos.x(), org_pos.y(), (double)typical_width, 20.0);
        QRectF measure2_rect = QRectF(org_pos.x(), org_pos.y()+20, (double)typical_width, 20.0);
        QRectF measure3_rect = QRectF(org_pos.x(), org_pos.y()+40, (double)typical_width, 20.0);

        p.setPen(Qt::NoPen);
        p.setBrush(View::LightBlue);
        p.drawRect(measure_rect);

        p.setPen(active_color);
        p.drawText(measure1_rect, Qt::AlignRight | Qt::AlignVCenter, _em_edges);
        p.drawText(measure2_rect, Qt::AlignRight | Qt::AlignVCenter, _em_rising);
        p.drawText(measure3_rect, Qt::AlignRight | Qt::AlignVCenter, _em_falling);
    }

    if (_action_type == LOGIC_JUMP) {
        p.setPen(active_color);
        p.setBrush(Qt::NoBrush);
        const QPoint pre_points[] = {
            QPoint(_cur_preX, _cur_preY),
            QPoint(_cur_preX-1, _cur_preY-1),
            QPoint(_cur_preX+1, _cur_preY-1),
            QPoint(_cur_preX-1, _cur_preY+1),
            QPoint(_cur_preX+1, _cur_preY+1),
            QPoint(_cur_preX-2, _cur_preY-2),
            QPoint(_cur_preX+2, _cur_preY-2),
            QPoint(_cur_preX-2, _cur_preY+2),
            QPoint(_cur_preX+2, _cur_preY+2),
        };
        p.drawPoints(pre_points, countof(pre_points));
        if (abs(_cur_aftX - _cur_preX) + abs(_cur_aftY - _cur_preY) > 20) {
            if (_edge_hit) {
                const QPoint aft_points[] = {
                    QPoint(_cur_aftX, _cur_aftY),
                    QPoint(_cur_aftX-1, _cur_aftY-1),
                    QPoint(_cur_aftX+1, _cur_aftY-1),
                    QPoint(_cur_aftX-1, _cur_aftY+1),
                    QPoint(_cur_aftX+1, _cur_aftY+1),
                    QPoint(_cur_aftX-2, _cur_aftY-2),
                    QPoint(_cur_aftX+2, _cur_aftY-2),
                    QPoint(_cur_aftX-2, _cur_aftY+2),
                    QPoint(_cur_aftX+2, _cur_aftY+2),
                };
                p.drawPoints(aft_points, countof(aft_points));
            }
            int64_t delta = max(_edge_start, _edge_end) - min(_edge_start, _edge_end);
            QString delta_text = _view.get_index_delta(_edge_start, _edge_end) +
                                 "/" + QString::number(delta);
            QFontMetrics fm = this->fontMetrics();
           
            const int rectW = fm.boundingRect(delta_text).width() + 60;
            const int rectH = fm.height() + 10;
             
            const int rectY = (height() - _view.hover_point().y() < rectH + 20) ? _view.hover_point().y() - 10 - rectH : _view.hover_point().y() + 20;
            const int rectX = (width() - _view.hover_point().x() < rectW) ? _view.hover_point().x() - rectW : _view.hover_point().x();
            QRectF jump_rect = QRectF(rectX, rectY, rectW, rectH);

            p.setPen(Qt::NoPen);
            p.setBrush(View::LightBlue);
            p.drawRect(jump_rect);

            p.setPen(active_color);
            p.setBrush(Qt::NoBrush);
            p.drawText(jump_rect, Qt::AlignCenter | Qt::AlignVCenter, delta_text);

            QPainterPath path(QPoint(_cur_preX, _cur_preY));
            QPoint c1((_cur_preX+_cur_aftX)/2, _cur_preY);
            QPoint c2((_cur_preX+_cur_aftX)/2, _cur_aftY);
            path.cubicTo(c1, c2, QPoint(_cur_aftX, _cur_aftY));
            p.drawPath(path);
        }
    }
}

QString Viewport::get_measure(QString option)
{
    if(option.compare("width") == 0)
        return _mm_width;
    else if (option.compare("period") == 0)
        return _mm_period;
    else if (option.compare("frequency") == 0)
        return _mm_freq;
    else if (option.compare("duty") == 0)
        return _mm_duty;
    else
        return View::Unknown_Str;
}

void Viewport::set_measure_en(int enable)
{
    if (enable == 0)
        _measure_en = false;
    else
        _measure_en = true;
}

void Viewport::start_trigger_timer(int msec)
{
    assert(msec > 0);
    _transfer_started = false;
    _timer_cnt = 0;
    _trigger_timer.start(msec);
}

void Viewport::stop_trigger_timer()
{
    _transfer_started = true;
    _timer_cnt = 0;
    _trigger_timer.stop();
}

void Viewport::on_trigger_timer()
{
    _timer_cnt++;

    if (!_is_checked_trig)
    {
        if (_view.session().get_device()->get_work_mode() == LOGIC
            && _view.session().get_device()->is_file() == false)
        {
            if (_view.session().is_triged()){
                _is_checked_trig = true;
                _view.get_viewstatus()->set_trig_time(_view.session().get_trig_time());
                _view.get_viewstatus()->update();
            }
        }
        else{
            _is_checked_trig = true;
        }
    }

    update(UpdateEventType::UPDATE_EV_GENERIC);  // To refresh the trigger status information.
}

void Viewport::on_drag_timer()
{   
    const int64_t offset = _view.offset();
    const double scale = _view.scale();

    if (_view.session().is_stopped_status()
        && _drag_strength != 0
        && offset < _view.get_max_offset()
        && offset > _view.get_min_offset()) 
    {
        _view.set_scale_offset(scale, offset + _drag_strength);
        _drag_strength /= DragDamping;
        if (_drag_strength != 0)
            _drag_timer.start(DragTimerInterval);
    }
    else if (offset == _view.get_max_offset() ||
             offset == _view.get_min_offset()) {
        _drag_strength = 0;
        _drag_timer.stop();
        set_action(NO_ACTION);
    }
    else if (_action_type == NO_ACTION){
        _drag_strength = 0;
        _drag_timer.stop();
    }
}

void Viewport::set_need_update(bool update)
{
    _need_update = update;
}

void Viewport::show_wait_trigger()
{
    _waiting_trig %= (WaitLoopTime / SigSession::FeedInterval) * 4;
    _waiting_trig++;
    update(UpdateEventType::UPDATE_EV_GENERIC);
}

void Viewport::unshow_wait_trigger()
{   
    _waiting_trig = 0;
    update(UpdateEventType::UPDATE_EV_GENERIC);
}

bool Viewport::get_dso_trig_moved()
{
    return _dso_trig_moved;
}

void Viewport::show_contextmenu(const QPoint& pos)
{
    if(_cmenu &&
       _view.session().get_device()->get_work_mode() == DSO)
    {
        _cur_preX = pos.x();
        _cur_preY = pos.y();
        _cmenu->exec(QCursor::pos());
    }
}

void Viewport::add_cursor_y()
{
    uint64_t index;
    index = _view.pixel2index(_cur_preX); 
    _view.add_cursor(index);
    _view.show_cursors(true);
}

void Viewport::add_cursor_x()
{
    double ypos = (_cur_preY - _view.get_view_rect().top()) * 1.0 / _view.get_view_height();    
    _view.add_xcursor(ypos, ypos);
    _view.show_xcursors(true);
}

void Viewport::UpdateLanguage()
{
    _yAction->setText(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_ADD_Y_CURSOR), "Add Y-cursor"));
    _xAction->setText(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_ADD_X_CURSOR), "Add X-cursor"));
}

void Viewport::UpdateTheme()
{
}

void Viewport::UpdateFont()
{ 
    QFont font = this->font();
    font.setPointSizeF(AppConfig::Instance().appOptions.fontSize);
    _yAction->setFont(font);
    _xAction->setFont(font);
}

} // namespace view
} // namespace pv
