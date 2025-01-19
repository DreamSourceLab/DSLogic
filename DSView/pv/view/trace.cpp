/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 *
 * Copyright (C) 2013 Joel Holdsworth <joel@airwebreathe.org.uk>
 * Copyright (C) 2014 DreamSourceLab <support@dreamsourcelab.com>
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
#include <math.h> 
#include <QFormLayout>
#include <QApplication>

#include "trace.h"
#include "view.h"
#include "../sigsession.h"
#include "../dsvdef.h"
#include "../config/appconfig.h"
#include "../appcontrol.h"


namespace pv {
namespace view {

const QColor Trace::PROBE_COLORS[8] = {
    QColor(0x50, 0x50, 0x50),	// Black
    QColor(0x8F, 0x52, 0x02),	// Brown
    QColor(0xCC, 0x00, 0x00),	// Red
    QColor(0xF5, 0x79, 0x00),	// Orange
    QColor(0xED, 0xD4, 0x00),	// Yellow
    QColor(0x73, 0xD2, 0x16),	// Green
    QColor(0x34, 0x65, 0xA4),	// Blue
    QColor(0x75, 0x50, 0x7B),	// Violet
};
const int Trace::LabelHitPadding = 2;

Trace::Trace(QString name, uint16_t index, int type) :
    _view(NULL),
	_name(name),
    _v_offset(INT_MAX),
    _v_offset_orig(INT_MAX),
    _type(type),
    _sec_index(0),
    _totalHeight(30),
    _typeWidth(SquareNum)
{
    _index_list.push_back(index);
    _view_index = -1;
}

Trace::Trace(QString name, std::list<int> index_list, int type, int sec_index) :
    _view(NULL),
    _name(name),
    _v_offset(INT_MAX),
    _v_offset_orig(INT_MAX),
    _type(type),
    _index_list(index_list),
    _sec_index(sec_index),
    _totalHeight(30),
    _typeWidth(SquareNum)
{
    _view_index = -1;
}

Trace::Trace(const Trace &t) :
    _view(t._view),
    _name(t._name),
    _colour(t._colour),
    _v_offset(t._v_offset),
    _v_offset_orig(INT_MAX),
    _type(t._type),
    _index_list(t._index_list),
    _sec_index(t._sec_index),
    _old_v_offset(t._old_v_offset),
    _totalHeight(t._totalHeight),
    _typeWidth(t._typeWidth),
    _text_size(t._text_size)
{
    _view_index = -1;
}


int Trace::get_name_width()
{
    QFont font;
    float fSize = AppConfig::Instance().appOptions.fontSize;
    font.setPointSizeF(fSize <= 10 ? fSize : 10);
    QFontMetrics fm(font);

    return fm.boundingRect(get_name()).width();
}

void Trace::set_name(QString name)
{
	_name = name;
}

int Trace::get_index()
{
    if(_index_list.size() == 0){
        assert(false);
    }
    
    return _index_list.front();
}

void Trace::set_index_list(const std::list<int> &index_list)
{
    if (index_list.size() == 0){
        assert(false);
    }

    _index_list = index_list;
}

int Trace::get_zero_vpos()
{
    return _v_offset;
}

void Trace::resize()
{
}

void Trace::set_view(pv::view::View *view)
{
	assert(view);
	_view = view;
    connect(_view, SIGNAL(resize()), this, SLOT(resize()));
}

void Trace::set_viewport(pv::view::Viewport *viewport)
{
    assert(viewport);
    _viewport = viewport;
}
 
void Trace::paint_prepare()
{
    assert(_view);
    _view->set_trig_hoff(0);
}

void Trace::paint_back(QPainter &p, int left, int right, QColor fore, QColor back)
{
    (void)back;

    fore.setAlpha(View::BackAlpha);
    QPen pen(fore);
    pen.setStyle(Qt::DotLine);
    p.setPen(pen);
    const double sigY = get_y();
    p.drawLine(left, sigY, right, sigY);
}

void Trace::paint_mid(QPainter &p, int left, int right, QColor fore, QColor back)
{
	(void)p;
	(void)left;
	(void)right;
    (void)fore;
    (void)back;
}

void Trace::paint_fore(QPainter &p, int left, int right, QColor fore, QColor back)
{
	(void)p;
	(void)left;
	(void)right;
    (void)fore;
    (void)back;
}

void Trace::paint_label(QPainter &p, int right, const QPoint pt, QColor fore)
{
    if (_type == SR_CHANNEL_FFT && !enabled())
        return;

    compute_text_size(p);
    const int y = get_y();

    const QRectF color_rect = get_rect("color", y, right);
    const QRectF name_rect  = get_rect("name",  y, right);
    const QRectF label_rect = get_rect("label", get_zero_vpos(), right);

    // Paint the ColorButton
    QColor foreBack = fore;
    foreBack.setAlpha(View::BackAlpha);
    p.setPen(Qt::transparent);
    p.setBrush(enabled() ? (_colour.isValid() ? _colour : fore) : foreBack);
    p.drawRect(color_rect);
    
    if (_type == SR_CHANNEL_DSO ||
        _type == SR_CHANNEL_MATH) {
        p.setPen(enabled() ?  Qt::white: foreBack);
        p.drawText(color_rect, Qt::AlignCenter | Qt::AlignVCenter, _name);
    }

    if (_type != SR_CHANNEL_DSO) {
        // Paint the signal name
        p.setPen(enabled() ?  fore: foreBack);
        p.drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter, _name);
    }

    // Paint the trigButton
    paint_type_options(p, right, pt, fore);

    // Paint the label
    if (enabled()) {
        const QPointF points[] = {
            label_rect.topLeft(),
            label_rect.topRight(),
            QPointF(right, get_zero_vpos()),
            label_rect.bottomRight(),
            label_rect.bottomLeft()
        };

        p.setPen(Qt::transparent);
        if (_type == SR_CHANNEL_DSO ||
            _type == SR_CHANNEL_FFT ||
            _type == SR_CHANNEL_ANALOG ||
            _type == SR_CHANNEL_MATH) {
            p.setBrush(_colour);
            p.drawPolygon(points, countof(points));
        } else {
            QColor color = PROBE_COLORS[*_index_list.begin() % countof(PROBE_COLORS)];
            p.setBrush(color);
            p.drawPolygon(points, countof(points));
        }

        p.setPen(Qt::white);
        const QPointF arrow_points[] = {
            QPoint(label_rect.right(), label_rect.center().y()),
            QPoint(label_rect.right(), label_rect.center().y()-1),
            QPoint(label_rect.right(), label_rect.center().y()+1),
            QPoint(label_rect.right(), label_rect.center().y()-2),
            QPoint(label_rect.right(), label_rect.center().y()+2),
            QPoint(label_rect.right(), label_rect.center().y()-3),
            QPoint(label_rect.right(), label_rect.center().y()+3),
            QPoint(label_rect.right(), label_rect.center().y()-4),
            QPoint(label_rect.right(), label_rect.center().y()+4),
            QPoint(label_rect.right()-1, label_rect.center().y()-3),
            QPoint(label_rect.right()-1, label_rect.center().y()+3),
            QPoint(label_rect.right()+1, label_rect.center().y()-3),
            QPoint(label_rect.right()+1, label_rect.center().y()+3),
            QPoint(label_rect.right()-1, label_rect.center().y()-2),
            QPoint(label_rect.right()-1, label_rect.center().y()+2),
            QPoint(label_rect.right()+1, label_rect.center().y()-2),
            QPoint(label_rect.right()+1, label_rect.center().y()+2),
            QPoint(label_rect.right()-2, label_rect.center().y()-2),
            QPoint(label_rect.right()-2, label_rect.center().y()+2),
            QPoint(label_rect.right()+2, label_rect.center().y()-2),
            QPoint(label_rect.right()+2, label_rect.center().y()+2),
        };
        if (label_rect.contains(pt) || selected())
            p.drawPoints(arrow_points, countof(arrow_points));

        // Paint the text
        p.setPen(Qt::white);
        if (_type == SR_CHANNEL_GROUP)
            p.drawText(label_rect, Qt::AlignCenter | Qt::AlignVCenter, "G");
        else if (_type == SR_CHANNEL_DECODER)
            p.drawText(label_rect, Qt::AlignCenter | Qt::AlignVCenter, "D");
        else if (_type == SR_CHANNEL_FFT)
            p.drawText(label_rect, Qt::AlignCenter | Qt::AlignVCenter, "F");
        else if (_type == SR_CHANNEL_MATH)
            p.drawText(label_rect, Qt::AlignCenter | Qt::AlignVCenter, "M");
        else
            p.drawText(label_rect, Qt::AlignCenter | Qt::AlignVCenter, QString::number(_index_list.front()));
    }
}

void Trace::paint_type_options(QPainter &p, int right, const QPoint pt, QColor fore)
{
    (void)p;
    (void)right;
    (void)pt;
    (void)fore;
}

bool Trace::mouse_double_click(int right, const QPoint pt)
{
    (void)right;
    (void)pt;
    return false;
}

bool Trace::mouse_press(int right, const QPoint pt)
{
    (void)right;
    (void)pt;
    return false;
}

bool Trace::mouse_wheel(int right, const QPoint pt, const int shift)
{
    (void)right;
    (void)pt;
    (void)shift;
    return false;
}

int Trace::pt_in_rect(int y, int right, const QPoint &point)
{
    const QRectF color = get_rect("color", y, right);
    const QRectF name  = get_rect("name", y, right);
    const QRectF label = get_rect("label", get_zero_vpos(), right);

    if (color.contains(point) && enabled())
        return COLOR;
    else if (name.contains(point) && enabled())
        return NAME;
    else if (label.contains(point) && enabled())
        return LABEL;
    else
        return 0;
}

void Trace::compute_text_size(QPainter &p)
{
    _text_size = QSize(
        p.boundingRect(QRectF(), 0, "99").width(),
        p.boundingRect(QRectF(), 0, "99").height());
}

QRect Trace::get_view_rect()
{
    assert(_view);
    return QRect(0, 0, _view->viewport()->width(), _view->viewport()->height());
}

QColor Trace::get_text_colour()
{
	return (_colour.lightness() > 64) ? Qt::black : Qt::white;
}

void Trace::on_text_changed(const QString &text)
{
	set_name(text);
	text_changed();
}

void Trace::on_colour_changed(const QColor &colour)
{
	set_colour(colour);
	colour_changed();
}

int Trace::rows_size()
{
    return 1;
}

QRectF Trace::get_rect(const char *s, int y, int right)
{
    const QSizeF color_size(get_leftWidth() - Margin, SquareWidth);
   // const QSizeF name_size(right - get_leftWidth() - get_rightWidth(), SquareWidth);
    const QSizeF name_size(right - get_leftWidth() - get_rightWidth(), SquareWidth);
    const QSizeF label_size(SquareWidth, SquareWidth);

    if (!strcmp(s, "name"))
        return QRectF(
            get_leftWidth(),
            y - name_size.height() / 2,
            name_size.width(), name_size.height());
    else if (!strcmp(s, "label"))
        return QRectF(
            right - 1.5f * label_size.width(),
            y - label_size.height() / 2,
            label_size.width(), label_size.height());
    else if (!strcmp(s, "color"))
        return QRectF(
            2,
            y - color_size.height() / 2,
            color_size.width(), color_size.height());
    else
        return QRectF(
            2,
            y - SquareWidth / 2,
            SquareWidth, SquareWidth);
}

} // namespace view
} // namespace pv
