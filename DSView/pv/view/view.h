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


#ifndef DSVIEW_PV_VIEW_VIEW_H
#define DSVIEW_PV_VIEW_VIEW_H

#include <stdint.h>
#include <set>
#include <vector>
   
#include <QScrollArea>
#include <QSizeF>
#include <QDateTime>
#include <QSplitter>

 
#include "../toolbars/samplingbar.h"
#include "../data/signaldata.h"
#include "../view/viewport.h"
#include "cursor.h"
#include "xcursor.h"
#include "signal.h"
#include "viewstatus.h"
#include "../dsvdef.h" 
#include "../interface/icallbacks.h"
#include "../ui/uimanager.h"

class DeviceAgent;

namespace pv {

namespace toolbars {
    class SamplingBar;
}

namespace dialogs {
    class Calibration;
    class Lissajous;
}

class SigSession;

namespace view {

class Header;
class DevMode;
class Ruler;
class Trace;
class Viewport;
class LissajousFigure;

//created by MainWindow
class View : public QScrollArea, public IUiWindow
{
	Q_OBJECT

private:
	static const int LabelMarginWidth;
	static const int RulerHeight;

	static const int MaxScrollValue;
    static const int HeightUnit;

public:
    //static const int SignalHeight;
	static const int SignalMargin;
	static const int SignalSnapGridSize;

	static const QColor CursorAreaColour;
	static const QSizeF LabelPadding;
    static const QString Unknown_Str;

    static const int WellSamplesPerPixel = 2048;
    static constexpr double MaxViewRate = 1.0;
    static const int MaxPixelsPerSample = 100;

    static const int StatusHeight = 20;
    static const int DsoStatusHeight = 55;

    static const int ForeAlpha = 200;
    static const int BackAlpha = 100;
    static const QColor Red;
    static const QColor Orange;
    static const QColor Blue;
    static const QColor Green;
    static const QColor Purple;
    static const QColor LightBlue;
    static const QColor LightRed;

public:
    explicit View(SigSession *session, pv::toolbars::SamplingBar *sampling_bar, QWidget *parent = 0);

    ~View();

	inline SigSession& session(){
        return *_session;
    }

	/**
	 * Returns the view time scale in seconds per pixel.
	 */
	inline double scale(){
        return _scale;
    }

    inline double get_minscale(){
        return _minscale;
    }

    inline double get_maxscale(){
        return _maxscale;
    }

    void auto_set_max_scale();

	/**
     * Returns the pixels offset of the left edge of the view
	 */
    inline int64_t offset(){
        return _offset;
    }

    /**
     * trigger position fix
     */
    inline double trig_hoff(){
        return _trig_hoff;
    }

    inline void set_trig_hoff(double hoff){
        _trig_hoff = hoff;
    }

    int64_t get_min_offset();
    int64_t get_max_offset();
    int64_t get_logic_lst_data_offset();

    void capture_init();

	void zoom(double steps);
    bool zoom(double steps, int offset);

	/**
	 * Sets the scale and offset.
	 * @param scale The new view scale in seconds per pixel.
	 * @param offset The view time offset in seconds.
	 */
    void set_scale_offset(double scale, int64_t offset);
    void set_preScale_preOffset();

    void get_traces(int type, std::vector<Trace*> &traces);

	/**
	 * Returns true if cursors are displayed. false otherwise.
	 */
	inline bool cursors_shown(){
        return _show_cursors;
    }

    inline bool trig_cursor_shown(){
        return _show_trig_cursor;
    }

    inline bool search_cursor_shown(){
        return _show_search_cursor;
    }

    inline int get_spanY(){
        return _spanY;
    }

    inline int get_signalHeight(){
        return _signalHeight;
    }

    int headerWidth();

    inline Ruler* get_ruler(){
        return _ruler;
    }

	/**
	 * Shows or hides the cursors.
	 */
	void show_cursors(bool show = true);

    inline const QPoint& hover_point(){
        return _hover_point;
    }

	void normalize_layout();

    void show_trig_cursor(bool show = true);

    void show_search_cursor(bool show = true);

    /*
     * cursorList
     */
    std::list<Cursor*>& get_cursorList();

    void add_cursor(QColor color, uint64_t sampleIndex);
    void add_cursor(uint64_t sampleIndex);
    void del_cursor(Cursor* cursor);
    void add_xcursor(double value0, double value1);
    void del_xcursor(XCursor* xcursor);

    void clear_cursors();
    void set_cursor_middle(int index);

    inline Cursor* get_trig_cursor(){
        return _trig_cursor;
    }

    Cursor* get_cursor_by_index(int index);

    inline Cursor* get_search_cursor(){
        return _search_cursor;
    }

    inline bool get_search_hit(){
        return _search_hit;
    }

    void set_search_pos(uint64_t search_pos, bool hit);

    inline uint64_t get_search_pos(){
        return _search_pos;
    }

    void scroll_to_logic_last_data_time();

    /*
     * horizental cursors
     */
    inline bool xcursors_shown(){
        return _show_xcursors;
    }

    inline void show_xcursors(bool show){
        _show_xcursors = show;
    }

    inline std::list<XCursor*>& get_xcursorList(){
        return _xcursorList;
    }
 
    void set_update(Viewport *viewport, bool need_update);
    void set_all_update(bool need_update);

    uint64_t get_cursor_samples(int index);
    QString get_cm_time(int index);
    QString get_cm_delta(int index1, int index2);
    QString get_index_delta(uint64_t start, uint64_t end);

    void on_state_changed(bool stop);

    QRect get_view_rect();
    int get_view_width();
    int get_view_height();

    double get_hori_res();

    QString get_measure(QString option);

    void viewport_update();

    void show_captured_progress(bool triggered, int progress);

    bool get_dso_trig_moved();

    inline ViewStatus* get_viewstatus(){
        return _viewbottom;
    }

    /*
     * back paint status
     */
    inline bool back_ready(){
        return _back_ready;
    }

    inline void set_back(bool ready){
        _back_ready = ready;
    }

    /*
     * untils
     */
    double index2pixel(uint64_t index, bool has_hoff = true);
    uint64_t pixel2index(double pixel);

    int get_cursor_index_by_key(uint64_t key);

    void check_calibration();

    void update_view_port();

    inline void update_all_trace_postion(){
        signals_changed(NULL);
    }

    inline Viewport* get_time_view(){
        return _time_viewport;
    }

    void update_font();
    void check_measure();
    bool header_is_draging();

      //IUiWindow
    void UpdateLanguage() override;
    void UpdateTheme() override;
    void UpdateFont() override;

    bool view_is_ready();

signals:
	void hover_point_changed();
    void cursor_update();
    void xcursor_update();
    void cursor_moving();
    void cursor_moved();
    void measure_updated();
    void prgRate(int progress);
    void resize();
    void auto_trig(int index);   

private:
    static bool compare_trace_v_offsets( const Trace *a, const Trace *b);
    void get_scroll_layout(int64_t &length, int64_t &offset);	
	void update_scroll();
    void update_margins();   
    void set_scale(double scale);

    void clear();
    void reconstruct();  
	bool eventFilter(QObject *object, QEvent *event);
	bool viewportEvent(QEvent *e);
	void resizeEvent(QResizeEvent *e);

public:
    static bool compare_trace_view_index(const Trace *a, const Trace *b);

    static bool compare_trace_y(const Trace *a, const Trace *b);

    void signals_changed(const Trace* eventTrace);
 
public slots:
    void reload();
    void set_measure_en(int enable);
  
    void data_updated();
    void update_scale_offset();
    void show_region(uint64_t start, uint64_t end, bool keep);
    // -- calibration
    void hide_calibration();
    void status_clear();
    void repeat_unshow();

    // -- repeat
    void repeat_show();
    // --
    void timebase_changed();
    // --
    void vDial_updated();

    void dso_factor_updated();
    
    // --
    void update_hori_res();

    //
    void header_updated();

    void receive_trigger(quint64 trig_pos1);

    void receive_end();

    void frame_began();

    void mode_changed();

private slots:

	void h_scroll_value_changed(int value);
	void v_scroll_value_changed(int value);

	void marker_time_changed();
    void on_traces_moved();   
    void set_trig_pos(int percent);
 
    // calibration for oscilloscope
    void show_calibration();
    // lissajous figure
    void show_lissajous(bool show);
    void on_measure_updated();

    void splitterMoved(int pos, int index);
    void on_calibration_closed();

private:
    void set_trig_cursor_posistion(uint64_t percent);
    void make_cursors_order();

public:
    void show_wait_trigger();
    void set_device();
    void set_receive_len(uint64_t len);
    int  get_body_width();
    int  get_body_height();

private:
	SigSession                  *_session;
    pv::toolbars::SamplingBar   *_sampling_bar;

    QWidget                 *_viewcenter;
    ViewStatus              *_viewbottom;
    QSplitter               *_vsplitter;
    Viewport                *_time_viewport;
    Viewport                *_fft_viewport;
    Viewport                *_active_viewport;
    LissajousFigure         *_lissajous;
    std::list<QWidget *>    _viewport_list;
    std::map<int, int>      _trace_view_map;
	Ruler                   *_ruler;
	Header                  *_header;
    DevMode                 *_devmode;
    

	/// The view time scale in seconds per pixel.
	double      _scale;
    double      _preScale;
    double      _maxscale;
    double      _minscale;

    /// The pixels offset of the left edge of the view
    int64_t     _offset;
    int64_t     _preOffset;
    int         _spanY;
    int         _signalHeight;
    bool        _updating_scroll;

    // trigger position fix
    double      _trig_hoff;

	bool        _show_cursors;
    std::list<Cursor*> _logic_cursors;
    std::list<Cursor*> _dso_cursors;
    Cursor      *_trig_cursor;
    bool        _show_trig_cursor;
    Cursor      *_search_cursor;
    bool        _show_search_cursor;
    uint64_t    _search_pos;
    bool        _search_hit;

    bool        _show_xcursors;
    std::list<XCursor*> _xcursorList;

    QPoint      _hover_point;
    dialogs::Calibration *_cali;

    bool        _dso_auto;
    bool        _show_lissajous;
    bool        _back_ready;
    DeviceAgent *_device_agent;
};

} // namespace view
} // namespace pv

#endif // DSVIEW_PV_VIEW_VIEW_H
