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

#include <assert.h>
#include <limits.h>
#include <cmath>

#include <QEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <algorithm>

#include "groupsignal.h"
#include "decodetrace.h"
#include "header.h"
#include "devmode.h"
#include "ruler.h"
#include "signal.h"
#include "dsosignal.h"
#include "view.h"
#include "viewport.h"
#include "spectrumtrace.h"
#include "lissajoustrace.h"
#include "analogsignal.h"

#include "../sigsession.h"
#include "../data/logicsnapshot.h"
#include "../dialogs/calibration.h"
#include "../dialogs/lissajousoptions.h"
#include "../dsvdef.h"
#include "../log.h"
#include "../config/appconfig.h"
#include "../appcontrol.h"


using namespace std;

namespace pv {
namespace view {

const int View::LabelMarginWidth = 70;
const int View::RulerHeight = 50;

const int View::MaxScrollValue = INT_MAX / 2;
const int View::HeightUnit = 20; // also serves as minimum signal height

const int View::SignalMargin = 3;
const int View::SignalSnapGridSize = 10;

const QColor View::CursorAreaColour(220, 231, 243);
const QSizeF View::LabelPadding(4, 4);
const QString View::Unknown_Str = "########";

const QColor View::Red = QColor(213, 15, 37, 255);
const QColor View::Orange = QColor(238, 178, 17, 255);
const QColor View::Blue = QColor(17, 133, 209,  255);
const QColor View::Green = QColor(0, 153, 37, 255);
const QColor View::Purple = QColor(109, 50, 156, 255);
const QColor View::LightBlue = QColor(17, 133, 209, 200);
const QColor View::LightRed = QColor(213, 15, 37, 200);


View::View(SigSession *session, pv::toolbars::SamplingBar *sampling_bar, QWidget *parent) :
    QScrollArea(parent),
    _sampling_bar(sampling_bar),
    _scale(10),
    _preScale(1e-6),
    _maxscale(1e9),
    _minscale(1e-15),
	_offset(0),
    _preOffset(0),
	_updating_scroll(false),
    _trig_hoff(0),
	_show_cursors(false),
    _search_hit(false),
    _show_xcursors(false),
    _hover_point(-1, -1),
    _dso_auto(true),
    _show_lissajous(false),
    _back_ready(false)
{  
   _trig_cursor = NULL;
   _search_cursor = NULL;
   _cali = NULL;

   _session = session;
   _device_agent = session->get_device();

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
//    setWidgetResizable(true);
//    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  
    // trace viewport map
    _trace_view_map[SR_CHANNEL_LOGIC] = TIME_VIEW;
    _trace_view_map[SR_CHANNEL_GROUP] = TIME_VIEW;
    _trace_view_map[SR_CHANNEL_DECODER] = TIME_VIEW;
    _trace_view_map[SR_CHANNEL_ANALOG] = TIME_VIEW;
    _trace_view_map[SR_CHANNEL_DSO] = TIME_VIEW;
    _trace_view_map[SR_CHANNEL_FFT] = FFT_VIEW;
    _trace_view_map[SR_CHANNEL_LISSAJOUS] = TIME_VIEW;
    _trace_view_map[SR_CHANNEL_MATH] = TIME_VIEW;

    _active_viewport = NULL;
    _ruler = new Ruler(*this);
    _header = new Header(*this);
    _devmode = new DevMode(this, session);
    
    setViewportMargins(headerWidth(), RulerHeight, 0, 0);

    // windows splitter
    _time_viewport = new Viewport(*this, TIME_VIEW);
    _time_viewport->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    _time_viewport->setMinimumHeight(100);
  
    _fft_viewport = new Viewport(*this, FFT_VIEW);
    _fft_viewport->setVisible(false);
    _fft_viewport->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    _fft_viewport->setMinimumHeight(100);
 
    _vsplitter = new QSplitter(this);
    _vsplitter->setOrientation(Qt::Vertical);
    _vsplitter->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    _viewport_list.push_back(_time_viewport);
    _vsplitter->addWidget(_time_viewport);
    _vsplitter->setCollapsible(0, false);
    _vsplitter->setStretchFactor(0, 2);
    _viewport_list.push_back(_fft_viewport);
    _vsplitter->addWidget(_fft_viewport);
    _vsplitter->setCollapsible(1, false);
    _vsplitter->setStretchFactor(1, 1);

    _viewcenter = new QWidget(this);
    _viewcenter->setContentsMargins(0,0,0,0);
    QGridLayout* layout = new QGridLayout(_viewcenter);
    layout->setSpacing(0);
    layout->setContentsMargins(0,0,0,0);
    _viewcenter->setLayout(layout);
    layout->addWidget(_vsplitter, 0, 0);
    _viewbottom = new ViewStatus(_session, *this);
    _viewbottom->setFixedHeight(StatusHeight);
    layout->addWidget(_viewbottom, 1, 0);

#ifdef Q_OS_DARWIN
    QWidget *lineSpan = new QWidget(this);
    lineSpan->setFixedHeight(10);
    layout->addWidget(lineSpan, 2, 0);
#endif

    setViewport(_viewcenter);

    _time_viewport->installEventFilter(this);
    _fft_viewport->installEventFilter(this);
	_ruler->installEventFilter(this);
	_header->installEventFilter(this);
    _devmode->installEventFilter(this);

    //tr
    _viewcenter->setObjectName("ViewArea_center");
    _ruler->setObjectName("ViewArea_ruler");
    _header->setObjectName("ViewArea_header");

    QColor fore(QWidget::palette().color(QWidget::foregroundRole()));
    fore.setAlpha(View::BackAlpha);

    _show_trig_cursor = false;
    _trig_cursor = new Cursor(*this, -1, 0);
    _trig_cursor->set_colour(View::LightRed);
    _show_search_cursor = false;
    _search_pos = 0;
    _search_cursor = new Cursor(*this, -1, _search_pos);
    _search_cursor->set_colour(fore);

	connect(horizontalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(h_scroll_value_changed(int)));
	connect(verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(v_scroll_value_changed(int)));

    connect(_time_viewport, SIGNAL(measure_updated()),this, SLOT(on_measure_updated()));
    connect(_time_viewport, SIGNAL(prgRate(int)), this, SIGNAL(prgRate(int)));
    connect(_fft_viewport, SIGNAL(measure_updated()), this, SLOT(on_measure_updated()));

    connect(_vsplitter, SIGNAL(splitterMoved(int,int)), this, SLOT(splitterMoved(int, int)));
      
    connect(_header, SIGNAL(traces_moved()),this, SLOT(on_traces_moved()));
    connect(_header, SIGNAL(header_updated()),this, SLOT(header_updated()));

    ADD_UI(this);
}

View::~View()
{
    DESTROY_OBJECT(_trig_cursor);
    DESTROY_OBJECT(_search_cursor);
    REMOVE_UI(this);
}

void View::show_wait_trigger()
{
    _time_viewport->show_wait_trigger();
}

void View::set_device()
{
    _devmode->set_device();
}

void View::capture_init()
{
    int width = get_view_width();
    if (width == 0){
        return;
    }

    int mode = _device_agent->get_work_mode();

    if (mode == DSO)
        show_trig_cursor(true);
    else if (!_session->is_repeating())
        show_trig_cursor(false);
 
    _maxscale = _session->cur_sampletime() / (width * MaxViewRate);

    if (mode == ANALOG){
        set_scale_offset(_maxscale, 0);
    }
    
    status_clear();

    _trig_hoff = 0;
}

void View::zoom(double steps)
{
    int width = get_view_width();
    if (width > 0)
    {
        zoom(steps, width / 2);
    }
}

void View::set_update(Viewport *viewport, bool need_update)
{
    viewport->set_need_update(need_update);
}

void View::set_all_update(bool need_update)
{
   _time_viewport->set_need_update(need_update);
   _fft_viewport->set_need_update(need_update);
}

double View::get_hori_res()
{
    return _sampling_bar->get_hori_res();
}

void View::update_hori_res()
{
    if (_device_agent->get_work_mode() == DSO) {
        _sampling_bar->hori_knob(0);
    }
}

bool View::zoom(double steps, int offset)
{
    int width = get_view_width();
    if (width == 0){
        return false;
    }

    bool ret = true;
    _preScale = _scale;
    _preOffset = _offset;

    if (_device_agent->get_work_mode() != DSO) {
        _scale *= std::pow(3.0/2.0, -steps);
        _scale = max(min(_scale, _maxscale), _minscale);
    } 
    else {
        if (_session->is_running_status() && _session->is_instant()){
            return ret;
        }

        double hori_res = -1;
        if(steps > 0.5)
            hori_res = _sampling_bar->hori_knob(-1);
        else if (steps < -0.5)
            hori_res = _sampling_bar->hori_knob(1);

        if (hori_res > 0) {
            const double scale = _session->cur_view_time() / width;
            _scale = max(min(scale, _maxscale), _minscale);
        } 
        else {
            ret = false;
        }
    }

    _offset = floor((_offset + offset) * (_preScale / _scale) - offset);
    _offset = max (min(_offset, get_max_offset()), get_min_offset());

    if (_scale != _preScale || _offset != _preOffset) {
        _header->update();
        _ruler->update();
        viewport_update();
        update_scroll();
    }

    return ret;
}

void View::timebase_changed()
{
    int width = get_view_width();
    if (width == 0){
        return;
    }

    if (_device_agent->get_work_mode() != DSO){
        return;
    }

    double scale = this->scale();
    double hori_res = _sampling_bar->get_hori_res();

    if (hori_res > 0){
        scale = _session->cur_view_time() / width;
    }

    set_scale_offset(scale, this->offset());
}

void View::set_scale_offset(double scale, int64_t offset)
{
    _preScale = _scale;
    _preOffset = _offset;

    _scale = max(min(scale, _maxscale), _minscale);
    _offset = floor(max(min(offset, get_max_offset()), get_min_offset()));

    if (_scale != _preScale || _offset != _preOffset) {
        update_scroll();
        _header->update();
        _ruler->update();
        viewport_update();
    }
}

void View::set_preScale_preOffset()
{ 
    set_scale_offset(_preScale, _preOffset);
}

void View::get_traces(int type, std::vector<Trace*> &traces)
{
    assert(_session);

    auto &sigs = _session->get_signals();
 
    const auto &decode_sigs = _session->get_decode_signals();
 
    const auto &spectrums = _session->get_spectrum_traces();
 
    for(auto t : sigs) {
        if (type == ALL_VIEW || _trace_view_map[t->get_type()] == type)
            traces.push_back(t);
    }
 
    for(auto t : decode_sigs) {
        if (type == ALL_VIEW || _trace_view_map[t->get_type()] == type)
            traces.push_back(t);
    }

    for(auto t : spectrums) {
        if (type == ALL_VIEW || _trace_view_map[t->get_type()] == type)
            traces.push_back(t);
    }

    auto lissajous = _session->get_lissajous_trace();
    if (lissajous && lissajous->enabled() &&
        (type == ALL_VIEW || _trace_view_map[lissajous->get_type()] == type)){
        traces.push_back(lissajous);
    }

    auto math = _session->get_math_trace();
    if (math && math->enabled() &&
        (type == ALL_VIEW || _trace_view_map[math->get_type()] == type)){
        traces.push_back(math);
    }

    sort(traces.begin(), traces.end(), compare_trace_v_offsets);
}

bool View::compare_trace_v_offsets(const Trace *a, const Trace *b)
{
    assert(a);
    assert(b);

    Trace *a1 = const_cast<Trace*>(a);
    Trace *b1 = const_cast<Trace*>(b);
    int v1 = 0;
    int v2 = 0;

    if (a1->get_type() != b1->get_type()){
        v1 = a1->get_type();
        v2 = b1->get_type();
    } 
    else if (a1->get_type() == SR_CHANNEL_DSO || a1->get_type() == SR_CHANNEL_ANALOG){
        v1 = a1->get_index();
        v2 = b1->get_index();
    } 
    else{
        v1 = a1->get_v_offset();
        v2 = b1->get_v_offset();
    }
    return v1 < v2;
}

bool View::compare_trace_view_index(const Trace *a, const Trace *b)
{
    assert(a);
    assert(b);

    Trace *a1 = const_cast<Trace*>(a);
    Trace *b1 = const_cast<Trace*>(b);
    return a1->get_view_index() < b1->get_view_index();
}

bool View::compare_trace_y(const Trace *a, const Trace *b)
{
    assert(a);
    assert(b);

    Trace *a1 = const_cast<Trace*>(a);
    Trace *b1 = const_cast<Trace*>(b);
    return a1->get_v_offset() < b1->get_v_offset();
}

void View::show_cursors(bool show)
{
	_show_cursors = show;
	_ruler->update();
    viewport_update();
}

void View::show_trig_cursor(bool show)
{
    _show_trig_cursor = show;
    _ruler->update();
    viewport_update();
}

void View::show_search_cursor(bool show)
{
    _show_search_cursor = show;
    _ruler->update();
    viewport_update();
}

void View::status_clear()
{
    _time_viewport->clear_dso_xm();
    _time_viewport->clear_measure();
    _viewbottom->clear();
}

void View::repeat_unshow()
{
    _viewbottom->repeat_unshow();
}

void View::frame_began()
{
    _search_hit = false;
    _search_pos = 0;
    set_search_pos(_search_pos, _search_hit);
}

void View::receive_end()
{
    if (_device_agent->get_work_mode() == LOGIC) {
        bool rle = false;
        uint64_t actual_samples;
        bool ret;

        ret = _device_agent->get_config_bool(SR_CONF_RLE, rle);
      
        if (ret && rle) {
            ret = _device_agent->get_config_uint64(SR_CONF_ACTUAL_SAMPLES, actual_samples);
            if (ret) {
                if (actual_samples != _session->cur_samplelimits()) {
                    _viewbottom->set_rle_depth(actual_samples);
                }
            }
        }       
    }
    _time_viewport->unshow_wait_trigger();
}


void View::receive_trigger(quint64 trig_pos1)
{
    (void)trig_pos1;
    uint64_t trig_pos = _session->get_trigger_pos();
    set_trig_cursor_posistion(trig_pos);
}

void View::set_trig_cursor_posistion(uint64_t trig_pos)
{   
    const double time = trig_pos * 1.0 / _session->cur_snap_samplerate();
    _trig_cursor->set_index(trig_pos);

    int width = get_view_width();
    assert(width > 0);

    if (ds_trigger_get_en() ||
        _device_agent->is_virtual() ||
        _device_agent->get_work_mode() == DSO) {
        _show_trig_cursor = true;

        AppConfig &app = AppConfig::Instance();
        if (app.appOptions.trigPosDisplayInMid){
            set_scale_offset(_scale,  (time / _scale) - (width / 2));
        }
    }

    _ruler->update();
    viewport_update();
}

void View::set_trig_pos(int percent)
{
    uint64_t index = _session->cur_samplelimits() * percent / 100;

    if (_session->have_view_data() == false
        || _session->is_working()){
        set_trig_cursor_posistion(index);
    }
}

void View::set_search_pos(uint64_t search_pos, bool hit)
{ 
    QColor fore(QWidget::palette().color(QWidget::foregroundRole()));
    fore.setAlpha(View::BackAlpha);

    const double time = search_pos * 1.0 / _session->cur_snap_samplerate();
    _search_pos = search_pos;
    _search_hit = hit;
    _search_cursor->set_index(search_pos);
    _search_cursor->set_colour(hit ? View::Blue : fore);

    int width = get_view_width();
    assert(width);

    if (hit) {
        set_scale_offset(_scale,  (time / _scale) - (width / 2));
        _ruler->update();
        viewport_update();
    }
}

void View::normalize_layout()
{   
    int v_min = INT_MAX;
    std::vector<Trace*> traces;
    get_traces(ALL_VIEW, traces);
	
    for(auto t : traces){
          v_min = min(t->get_v_offset(), v_min);
    }

	const int delta = -min(v_min, 0);

    for(auto t : traces){
        t->set_v_offset(t->get_v_offset() + delta);
    }        

    verticalScrollBar()->setSliderPosition(delta);
	v_scroll_value_changed(verticalScrollBar()->sliderPosition());
}

void View::get_scroll_layout(int64_t &length, int64_t &offset)
{
    length = ceil(_session->cur_snap_sampletime() / _scale);
    offset = _offset;
}

void View::update_scroll()
{
    assert(_viewcenter);

    int width = get_view_width();
    if (width == 0) {
        return;
    }

    const QSize areaSize = QSize(width, get_view_height());

	// Set the horizontal scroll bar
    int64_t length = 0;
    int64_t offset = 0;
	get_scroll_layout(length, offset);
    length = max(length - areaSize.width(), (int64_t)0);

	horizontalScrollBar()->setPageStep(areaSize.width() / 2);

	_updating_scroll = true;

	if (length < MaxScrollValue) {
		horizontalScrollBar()->setRange(0, length);
		horizontalScrollBar()->setSliderPosition(offset);
	} else {
		horizontalScrollBar()->setRange(0, MaxScrollValue);
		horizontalScrollBar()->setSliderPosition(
            _offset * 1.0  / length * MaxScrollValue);
	}

    // Set up vertical scrollbar
    std::vector<Trace*> traces;
    get_traces(ALL_VIEW, traces);

    // Calculate total required height for all traces
    int total_height = 0;
    for (auto t : traces) {
        if (t->enabled())
            total_height += t->get_totalHeight() + 2 * SignalMargin;
    }

    // Make sure we can scroll the last signal past the status bar
    total_height += StatusHeight;

    // Enable vertical scrolling if total height exceeds viewport
    if (total_height > areaSize.height()) {
        verticalScrollBar()->setRange(0, total_height - areaSize.height());
        verticalScrollBar()->setPageStep(areaSize.height());
    } else {
        verticalScrollBar()->setRange(0, 0);
    }
    _updating_scroll = false;
}

void View::update_scale_offset()
{   
    int width = get_view_width();
    if (width == 0){
        return;
    }

    if (_device_agent->get_work_mode() != DSO) {
        _maxscale = _session->cur_sampletime() / (width * MaxViewRate);     
        _minscale = (1.0 / _session->cur_snap_samplerate()) / MaxPixelsPerSample;
    }
    else {
        _scale = _session->cur_view_time() / width;     
        _maxscale = 1e9;
        _minscale = 1e-15;
    }

    _scale = max(min(_scale, _maxscale), _minscale);
    _offset = max(min(_offset, get_max_offset()), get_min_offset());

    _preScale = _scale;
    _preOffset = _offset;

    _ruler->update();
    viewport_update();
}

void View::mode_changed()
{
    if (_device_agent->is_virtual())
        _scale = WellSamplesPerPixel * 1.0 / _session->cur_snap_samplerate();
    _scale = max(min(_scale, _maxscale), _minscale);
}

void View::signals_changed(const Trace* eventTrace)
{
    double actualMargin = SignalMargin;
    int total_rows = 0;
    int label_size = 0;
    uint8_t max_height = HeightUnit;
    std::vector<Trace*> time_traces;
    std::vector<Trace*> fft_traces;
    std::vector<Trace*> traces;
    std::vector<Trace*> logic_traces;
    std::vector<Trace*> decoder_traces;

    (void)eventTrace;

    get_traces(ALL_VIEW, traces);

    for(auto t : traces) {
        if (_trace_view_map[t->get_type()] == TIME_VIEW){
            time_traces.push_back(t);
        }
        else if (_trace_view_map[t->get_type()] == FFT_VIEW){
            if (t->enabled())
                fft_traces.push_back(t);
        }

        if (t->get_type() == SR_CHANNEL_LOGIC)
            logic_traces.push_back(t);
        else if (t->get_type() == SR_CHANNEL_DECODER)
            decoder_traces.push_back(t);
    }

    if (!fft_traces.empty()) {
        if (!_fft_viewport->isVisible()) {
            _fft_viewport->setVisible(true);
            _fft_viewport->clear_measure();
            _viewport_list.push_back(_fft_viewport);
            _vsplitter->refresh();
        }

        for(auto t : fft_traces) {
            t->set_view(this);
            t->set_viewport(_fft_viewport);
            t->set_totalHeight(_fft_viewport->height());
            t->set_v_offset(_fft_viewport->geometry().bottom());
        }
    }
    else {
        _fft_viewport->setVisible(false);
        _vsplitter->refresh();

        // Find the _fft_viewport in the stack
        std::list< QWidget *>::iterator iter = _viewport_list.begin();

        for(unsigned int i = 0; i < _viewport_list.size(); i++, iter++){
            if ((*iter) == _fft_viewport)
                break;
        }

        // Delete the element
        if (iter != _viewport_list.end())
            _viewport_list.erase(iter);
    }

    if (!time_traces.empty() && _time_viewport) {
        for(auto t : time_traces) {
            if (dynamic_cast<DsoSignal*>(t) || t->enabled())
                total_rows += t->rows_size();
            if (t->rows_size() != 0)
                label_size++;
        }

        const double height = (_time_viewport->height()
                               - 2 * actualMargin * label_size) * 1.0 / total_rows;

        if (_device_agent->have_instance() == false){
            assert(false);
        }
        
        int mode = _device_agent->get_work_mode();

        if (mode == LOGIC) {
            int v;
            bool ret;

            ret = _device_agent->get_config_byte(SR_CONF_MAX_HEIGHT_VALUE, v);
            if (ret) {
                max_height = (v + 1) * HeightUnit;
            }
            if (height < 2*actualMargin) {
                //actualMargin /= 2;
                _signalHeight = max((double)HeightUnit, (_time_viewport->height()
                                          - 2 * actualMargin * label_size) * 1.0 / total_rows);
            }
            else {
                _signalHeight = max((double)HeightUnit, (height >= max_height) ? max_height : height);
            }
        }
        else if (_device_agent->get_work_mode() == DSO) {
            _signalHeight = max((double)HeightUnit, (_header->height()
                             - horizontalScrollBar()->height()
                             - 2 * actualMargin * label_size) * 1.0 / total_rows);
        }
        else {
            _signalHeight = (int)((height <= 0) ? 1 : height);
        }

        _spanY = _signalHeight + 2 * actualMargin;
        int next_v_offset = actualMargin;
        
        //Make list by view-index;
        if (mode == LOGIC)
        {   
            time_traces.clear();

            std::vector<Trace*> all_traces;

            for(auto t : logic_traces){
                all_traces.push_back(t);
            }

            for(auto t : decoder_traces){
                if (t->get_view_index() != -1)
                    all_traces.push_back(t);
                else
                    time_traces.push_back(t);
            }

            sort(all_traces.begin(), all_traces.end(), compare_trace_view_index);    

            for(auto t : all_traces){
                time_traces.push_back(t);
            }
        }

        for(auto t : time_traces) {
            t->set_view(this);
            t->set_viewport(_time_viewport);

            if (t->rows_size() == 0)
                continue;

            const double traceHeight = _signalHeight*t->rows_size();
            t->set_totalHeight((int)traceHeight);
            t->set_v_offset(next_v_offset + 0.5 * traceHeight + actualMargin);
            next_v_offset += traceHeight + 2 * actualMargin;

            if (t->signal_type() == SR_CHANNEL_DSO)
            {
                auto sig = dynamic_cast<view::DsoSignal*>(t);
                sig->set_scale(sig->get_view_rect().height());              
            }
            else if (t->signal_type() == SR_CHANNEL_ANALOG)
            {
                auto sig = dynamic_cast<view::AnalogSignal*>(t);
                sig->set_scale(sig->get_totalHeight());
            }
        }
        _time_viewport->clear_measure();
        _session->update_dso_data_scale();
    }

    header_updated();
    normalize_layout();
    update_scale_offset();
    data_updated();
}

bool View::eventFilter(QObject *object, QEvent *event)
{
	const QEvent::Type type = event->type();
	if (type == QEvent::MouseMove) {
		const QMouseEvent *const mouse_event = (QMouseEvent*)event;
        if (object == _ruler || object == _time_viewport || object == _fft_viewport) {
            //_hover_point = QPoint(mouse_event->x(), 0);
            double cur_periods = (mouse_event->pos().x() + _offset) * _scale / _ruler->get_min_period();
            int integer_x = round(cur_periods) * _ruler->get_min_period() / _scale - _offset;
            double cur_deviate_x = qAbs(mouse_event->pos().x() - integer_x);
            if (_device_agent->get_work_mode() == LOGIC &&
                cur_deviate_x < 10)
                _hover_point = QPoint(integer_x, mouse_event->pos().y());
            else
                _hover_point = mouse_event->pos();
        } else if (object == _header)
			_hover_point = QPoint(0, mouse_event->y());
		else
			_hover_point = QPoint(-1, -1);

		hover_point_changed();
	} else if (type == QEvent::Leave) {
		_hover_point = QPoint(-1, -1);
		hover_point_changed();
	}

	return QObject::eventFilter(object, event);
}

bool View::viewportEvent(QEvent *e)
{
	switch(e->type()) {
	case QEvent::Paint:
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	case QEvent::MouseButtonDblClick:
	case QEvent::MouseMove:
	case QEvent::Wheel:
    case QEvent::Gesture:
		return false;

	default:
		return QAbstractScrollArea::viewportEvent(e);
	}
}

int View::headerWidth()
{
    int headerWidth = _header->get_nameEditWidth();

    std::vector<Trace*> traces;
    get_traces(ALL_VIEW, traces);

    if (!traces.empty()) 
    {
        for(auto t : traces){
            int w = t->get_name_width() + t->get_leftWidth() + t->get_rightWidth();
            headerWidth = max(w, headerWidth);
        }
    }

    setViewportMargins(headerWidth, RulerHeight, 0, 0);

    return headerWidth;
}

void View::resizeEvent(QResizeEvent*)
{
    int width = get_view_width();

    if (width == 0){
        return;
    }

    reconstruct();
    setViewportMargins(headerWidth(), RulerHeight, 0, 0);
    update_margins();
    update_scroll();
    signals_changed(NULL);

    if (_device_agent->get_work_mode() == DSO){
        _scale = _session->cur_view_time() / width;
    }

    if (_device_agent->get_work_mode() != DSO)
        _maxscale = _session->cur_sampletime() / (width * MaxViewRate);
    else
        _maxscale = 1e9;

    _scale = min(_scale, _maxscale);

    _ruler->update();
    _header->header_resize();
    set_update(_time_viewport, true);
    set_update(_fft_viewport, true);
    resize();
}

void View::h_scroll_value_changed(int value)
{
	if (_updating_scroll)
		return;

    _preOffset = _offset;

	const int range = horizontalScrollBar()->maximum();
	if (range < MaxScrollValue)
        _offset = value;
	else 
    {
        int64_t length = 0;
        int64_t offset = 0;
		get_scroll_layout(length, offset);
        _offset = floor(value * 1.0 / MaxScrollValue * length);
	}

    _offset = max(min(_offset, get_max_offset()), get_min_offset());

    if (_offset != _preOffset) {
        _ruler->update();
        viewport_update();
    }
}

void View::v_scroll_value_changed(int value)
{
    // Update vertical positions of all traces based on scroll value
    std::vector<Trace*> traces;
    get_traces(ALL_VIEW, traces);

    for (auto t : traces) {
        if (t->enabled()) {
            t->update_v_scroll(-value);
        }
    }

    _header->update();
    viewport_update();
}

void View::data_updated()
{
    setViewportMargins(headerWidth(), RulerHeight, 0, 0);
    update_margins();

	// Update the scroll bars
	update_scroll();

    // update scale & offset
    update_scale_offset();

	// Repaint the view
    _time_viewport->unshow_wait_trigger();
    set_update(_time_viewport, true);
    set_update(_fft_viewport, true);
    viewport_update();
    _ruler->update();
}

void View::update_margins()
{
    int width = get_view_width();

    if (width > 0)
    {
        _ruler->setGeometry(_viewcenter->x(), 0,  width, _viewcenter->y());
        _header->setGeometry(0, _viewcenter->y(), _viewcenter->x(), _viewcenter->height());
        _devmode->setGeometry(0, 0, _viewcenter->x(), _viewcenter->y());
    } 
}

void View::header_updated()
{
    headerWidth();
    update_margins();

    // Update the scroll bars
    update_scroll();

    viewport_update();
    _header->update();
}

void View::marker_time_changed()
{
	_ruler->update();
    viewport_update();
}

void View::on_traces_moved()
{
	update_scroll();
    set_update(_time_viewport, true);
    viewport_update();
}

void View::make_cursors_order()
{
    int dex = 1;
 
    for (auto cursor :  get_cursorList())
    {
        cursor->set_order(dex++);
    }

    dex = 1;
    for (auto cursor :  get_xcursorList())
    {
        cursor->set_order(dex++);
    }
}

void View::add_cursor(QColor color, uint64_t sampleIndex)
{
    Cursor *newCursor = new Cursor(*this, -1, sampleIndex);
    get_cursorList().push_back(newCursor);
    make_cursors_order();
    cursor_update();
}

void View::add_cursor(uint64_t sampleIndex)
{
    static int lastOrder = 1;
    Cursor *newCursor = new Cursor(*this, lastOrder++, sampleIndex);
    get_cursorList().push_back(newCursor);
    make_cursors_order();
    cursor_update();
}

void View::del_cursor(Cursor* cursor)
{
    assert(cursor);

    get_cursorList().remove(cursor);
    delete cursor;
    make_cursors_order();

    cursor_update();
}

void View::clear_cursors()
{
    auto &lst = get_cursorList();
    for (auto c : lst){
        delete c;
    }

    lst.clear();
}

void View::add_xcursor(double value0, double value1)
{
    static int lastXCursorOrder = 1;
    XCursor *newXCursor = new XCursor(*this, lastXCursorOrder++, value0, value1);
    _xcursorList.push_back(newXCursor);
    make_cursors_order();
    xcursor_update();
}

void View::del_xcursor(XCursor* xcursor)
{
    assert(xcursor);

    _xcursorList.remove(xcursor);
    delete xcursor;
    make_cursors_order();
    xcursor_update();
}

void View::set_cursor_middle(int index)
{
    auto &lst = get_cursorList();
    int size = lst.size();
    assert(index < size);

    int width = get_view_width();
   // if (width > 0);

    auto i = lst.begin();

    while (index-- != 0){
        i++;
    }

    set_scale_offset(_scale, (*i)->index() / (_session->cur_snap_samplerate() * _scale) - (width / 2));
}

void View::on_measure_updated()
{
    _active_viewport = dynamic_cast<Viewport *>(sender());
    measure_updated();
}

QString View::get_measure(QString option)
{
    if (_active_viewport) {
        return _active_viewport->get_measure(option);
    }
    return Unknown_Str;
}

QString View::get_cm_time(int index)
{
    uint64_t sampleIndex = get_cursor_samples(index);
    uint64_t sampleRate = _session->cur_snap_samplerate();
    return _ruler->format_real_time(sampleIndex, sampleRate);
}

QString View::get_cm_delta(int index1, int index2)
{
    if (index1 == index2)
        return "0";

    uint64_t samples1 = get_cursor_samples(index1);
    uint64_t samples2 = get_cursor_samples(index2);
    uint64_t delta_sample = (samples1 > samples2) ? samples1 - samples2 : samples2 - samples1;
    return _ruler->format_real_time(delta_sample, _session->cur_snap_samplerate());
}

QString View::get_index_delta(uint64_t start, uint64_t end)
{
    if (start == end)
        return "0";

    uint64_t delta_sample = (start > end) ? start - end : end - start;
    return _ruler->format_real_time(delta_sample, _session->cur_snap_samplerate());
}

uint64_t View::get_cursor_samples(int index)
{
    auto &lst = get_cursorList();
    assert(index < (int)lst.size());

    uint64_t ret = 0;
    int curIndex = 0;
    for (list<Cursor*>::iterator i = lst.begin();
         i != lst.end(); i++) {
        if (index == curIndex) {
            ret = (*i)->index();
        }
        curIndex++;
    }
    return ret;
}

void View::set_measure_en(int enable)
{
    _time_viewport->set_measure_en(enable);
    _fft_viewport->set_measure_en(enable);
}

void View::on_state_changed(bool stop)
{
    if (stop) {
        _time_viewport->stop_trigger_timer();
        _fft_viewport->stop_trigger_timer();
    }
    update_scale_offset();
}

QRect View::get_view_rect()
{
    if (_device_agent->get_work_mode() == DSO) {
        const auto &sigs = _session->get_signals();
        if(sigs.size() > 0) {
            return sigs[0]->get_view_rect();
        }
    }

    return _viewcenter->rect();
}

int View::get_view_width()
{
    int view_width = 0;
    if (_device_agent->get_work_mode() == DSO) {
        for(auto s : _session->get_signals()) {
            view_width = max(view_width, s->get_view_rect().width());
        }
    }
    else {
        view_width = _viewcenter->width();
    }

    if (view_width == 0){
        view_width = 1;
    }

    return view_width;
}

int View::get_view_height()
{
    int view_height = 0;
    if (_device_agent->get_work_mode() == DSO) {
        for(auto s : _session->get_signals()) {
            view_height = max(view_height, s->get_view_rect().height());
        }
    }
    else {
        view_height = _viewcenter->height();
    }

    return view_height;
}

int64_t View::get_min_offset()
{
    int width = get_view_width();
    assert(width > 0);

    if (MaxViewRate > 1)
        return floor(width * (1 - MaxViewRate));
    else
        return 0;
}

int64_t View::get_max_offset()
{
    int width = get_view_width();
    assert(width > 0);

    return ceil((_session->cur_snap_sampletime() / _scale) -
                (width * MaxViewRate));
}

int64_t View::get_logic_lst_data_offset(){
    int width = get_view_width();
    assert(width > 0);

    return ceil((_session->get_logic_data_view_time() / _scale) -
                (width * MaxViewRate));
}

void View::scroll_to_logic_last_data_time()
{
    set_scale_offset(scale(), get_logic_lst_data_offset() + 10);
}

// -- calibration dialog
void View::show_calibration()
{
    if (_cali != NULL){
        _cali->deleteLater();
        _cali = NULL;
    }

    _cali = new pv::dialogs::Calibration(this);
    connect(_cali, SIGNAL(sig_closed()), this, SLOT(on_calibration_closed()));
    _cali->update_device_info();
    _cali->show();
}

void View::on_calibration_closed()
{
    if (_cali != NULL){
        _cali->deleteLater();
        _cali = NULL;
    }
}

void View::hide_calibration()
{
    on_calibration_closed();
}

void View::vDial_updated()
{
    if (_cali != NULL) {
        _cali->update_device_info();
    }

    auto math_trace = _session->get_math_trace();
    if (math_trace && math_trace->enabled()) {
        math_trace->update_vDial();
    }
}

void View::dso_factor_updated()
{
    auto math_trace = _session->get_math_trace();
    if (math_trace && math_trace->enabled()) {
        math_trace->update_vDial();
    }
}

// -- lissajous figure
void View::show_lissajous(bool show)
{
    _show_lissajous = show;
    signals_changed(NULL);
}

void View::show_region(uint64_t start, uint64_t end, bool keep)
{
    assert(start <= end);

    int width = get_view_width();
    if (width == 0){
        return;
    }

    if (keep) {
        set_all_update(true);
        update();
    }
    else if (_session->get_map_zoom() == 0) {
        const double ideal_scale = (end-start) * 2.0 / _session->cur_snap_samplerate() / width;
        const double new_scale = max (min(ideal_scale, _maxscale), _minscale);
        const double new_off = (start + end)  * 0.5 / (_session->cur_snap_samplerate() * new_scale) - (width / 2);
        set_scale_offset(new_scale, new_off);
    }
    else {
        const double new_scale = scale();
        const double new_off = (start + end)  * 0.5 / (_session->cur_snap_samplerate() * new_scale) - (width/ 2);
        set_scale_offset(new_scale, new_off);
    }
}

void View::viewport_update()
{
    _viewcenter->update();
    for(QWidget *viewport : _viewport_list)
        viewport->update();
}

void View::splitterMoved(int pos, int index)
{
    (void)pos;
    (void)index;
    signals_changed(NULL);
}

void View::reload()
{
    clear();

    /*
     * if headerwidth not change, viewport height will not be updated
     * lead to a wrong signal height
     */
    reconstruct();
}

void View::clear()
{
    show_trig_cursor(false);

    if (_device_agent->get_work_mode() != DSO) {
        show_xcursors(false);
    } else {
        if (!get_xcursorList().empty())
            show_xcursors(true);
    }
}

void View::reconstruct()
{
    if (_device_agent->get_work_mode() == DSO)
        _viewbottom->setFixedHeight(DsoStatusHeight);
    else
        _viewbottom->setFixedHeight(StatusHeight);
    _viewbottom->reload();
}

void View::repeat_show()
{
    _viewbottom->update();
}

void View::show_captured_progress(bool triggered, int progress)
{
    _viewbottom->set_capture_status(triggered, progress);
    _viewbottom->update();
}

bool View::get_dso_trig_moved()
{
    return _time_viewport->get_dso_trig_moved();
}

double View::index2pixel(uint64_t index, bool has_hoff)
{
    const uint64_t rateValue = session().cur_snap_samplerate();
    const double scaleValue = scale();
    const int64_t offsetValue = offset();    
    const double hoffValue = trig_hoff();

    double pixels = 0;

    const double samples_per_pixel = rateValue * scaleValue;

    if (has_hoff){
        pixels = index / samples_per_pixel - offsetValue + hoffValue / samples_per_pixel;
    }
    else{
        pixels = index / samples_per_pixel - offsetValue;
    }

    /*
    const double samples_per_pixel = session().cur_snap_samplerate() * scale();
    double pixels;
    if (has_hoff)
        pixels = index/samples_per_pixel - offset() + trig_hoff()/samples_per_pixel;
    else
        pixels = index/samples_per_pixel - offset();
        */

    return pixels;
}

uint64_t View::pixel2index(double pixel)
{   
    const uint64_t rateValue = session().cur_snap_samplerate();
    const double scaleValue = scale();
    const int64_t offsetValue = offset();    
    const double hoffValue = trig_hoff();
 
    const double samples_per_pixel = rateValue * scaleValue;
    const double index = (pixel + offsetValue) * samples_per_pixel - hoffValue;

    const uint64_t sampleIndex = (uint64_t)std::round(index);

    return sampleIndex;

    //const double samples_per_pixel = session().cur_snap_samplerate() * scale();
    //uint64_t index = (pixel + offset()) * samples_per_pixel - trig_hoff();
}

void View::set_receive_len(uint64_t len)
{
    if (_time_viewport)
        _time_viewport->set_receive_len(len);
        
    if (_fft_viewport && _session->get_device()->get_work_mode() == DSO)
        _fft_viewport->set_receive_len(len);
}

int View::get_cursor_index_by_key(uint64_t key)
{
    auto &lst = get_cursorList();

    int dex = 0;
    for (auto c : lst){
        if (c->get_key() == key){
            return dex;
        }
        ++dex;
    }
    return -1;
}

void View::check_calibration()
{
     if (_device_agent->get_work_mode() == DSO){
        bool cali = false;
        _device_agent->get_config_bool(SR_CONF_CALI, cali);
            
        if (cali) {
            show_calibration();
        }           
    }
}

void View::set_scale(double scale)
{
    if (scale < _minscale)
        scale = _minscale;
    if (scale > _maxscale)
        scale = _maxscale;

     if (_scale != scale)
     {
        _scale = scale;
        _header->update();
        _ruler->update();
        viewport_update();
        update_scroll();
    }
}

void View::auto_set_max_scale()
{
    const double limitTime = _session->cur_sampletime();
    const int width = get_view_width();

    if (width > 0)
    {
        _maxscale =  limitTime / (width * MaxViewRate);
        set_scale(_maxscale);
    }  
}

int  View::get_body_width()
{
    if (_time_viewport != NULL)
        return _time_viewport->width();
    return 0;
}

int  View::get_body_height()
{
     if (_time_viewport != NULL)
        return _time_viewport->height();
    return 0;
}

 void View::update_view_port()
 {
    if (_time_viewport)
        _time_viewport->update(UpdateEventType::UPDATE_EV_GENERIC);
 }

 void View::update_font()
 {
    headerWidth();
 }

void View::check_measure()
{
    _time_viewport->measure();
    _time_viewport->update(UpdateEventType::UPDATE_EV_GENERIC);
}

std::list<Cursor*>& View::get_cursorList()
{   
    if (_session->get_device()->get_work_mode() == LOGIC){
        return _logic_cursors;
    }
    else{
        return _dso_cursors;
    }
}

bool View::header_is_draging()
{
    return _header->mouse_is_down();
}

Cursor* View::get_cursor_by_index(int index)
{
    int dex = 0;
    auto &cursors = get_cursorList();

    for (auto c : cursors){
        if (dex == index){
            return c;
        }
        dex++;
    }
    return NULL;
}

void View::UpdateLanguage()
{
     
}

void View::UpdateTheme()
{
    
}

void View::UpdateFont()
{  
    update_font();
}

bool View::view_is_ready()
{
    int w = get_view_width();
    return w > 0;
}

} // namespace view
} // namespace pv
