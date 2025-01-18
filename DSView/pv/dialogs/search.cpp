/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 *
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

#include "search.h"
#include "../view/logicsignal.h"
#include <assert.h>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QFontDatabase>
#include "../ui/langresource.h"
 

namespace pv {
namespace dialogs {

SearchEdgeFlagEdit::SearchEdgeFlagEdit(QWidget *parent)
    :QLineEdit(parent)
{
}

void SearchEdgeFlagEdit::focusInEvent(QFocusEvent *e)
{ 
    QLineEdit::focusInEvent(e);

    QTimer::singleShot(50, this, [this](){  
        selectAll();
    });    
}

Search::Search(QWidget *parent, SigSession *session, std::map<uint16_t, QString> pattern) :
    DSDialog(parent),
    _session(session)
{

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    QRegularExpression value_rx("[10XRFCxrfc]+");
    QValidator *value_validator = new QRegularExpressionValidator(value_rx, this);

    search_buttonBox.addButton(QDialogButtonBox::Ok);
    search_buttonBox.addButton(QDialogButtonBox::Cancel);

    QGridLayout *search_layout = new QGridLayout();
    search_layout->setVerticalSpacing(0);

    int index = 0;

    for(auto s :  _session->get_signals()) {
        if (s->signal_type() == SR_CHANNEL_LOGIC) {
            view::LogicSignal *logicSig = (view::LogicSignal*)s;
            QLineEdit *search_lineEdit = new SearchEdgeFlagEdit(this);
            if (pattern.find(logicSig->get_index()) != pattern.end())
                search_lineEdit->setText(pattern[logicSig->get_index()]);
            else
                search_lineEdit->setText("X");
            search_lineEdit->setValidator(value_validator);
            search_lineEdit->setMaxLength(1);
            search_lineEdit->setInputMask("X");
            search_lineEdit->setFont(font);
            _search_lineEdit_vec.push_back(search_lineEdit);

            search_layout->addWidget(new QLabel(logicSig->get_name()+":"), index, 0, Qt::AlignRight);
            search_layout->addWidget(new QLabel(QString::number(logicSig->get_index())), index, 1, Qt::AlignRight);
            search_layout->addWidget(search_lineEdit, index, 2);

            connect(search_lineEdit, SIGNAL(editingFinished()), this, SLOT(format()));

            index++;
        }
    }

    search_layout->addWidget(new QLabel(" "), index,0);
    //tr
    search_layout->addWidget(new QLabel(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_SEARCH_LABEL), 
                            "X: Don't care\n0: Low level\n1: High level\nR: Rising edge\nF: Falling edge\nC: Rising/Falling edge")), 0, 3, index, 1);
    search_layout->addWidget(&search_buttonBox, index+1, 3);
    search_layout->setColumnStretch(3, 100);

    layout()->addLayout(search_layout);
    setTitle(L_S(STR_PAGE_DLG, S_ID(IDS_DLG_SEARCH_OPTIONS), "Search Options"));

    connect(&search_buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
    connect(&search_buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
}

Search::~Search()
{
}

void Search::accept()
{
    using namespace Qt;

    QDialog::accept();
}

void Search::format()
{
    QLineEdit *sc = qobject_cast<QLineEdit *>(sender());
    sc->setText(sc->text().toUpper());
}

std::map<uint16_t, QString> Search::get_pattern()
{
    std::map<uint16_t, QString> pattern;

    int index = 0;
    for(auto s :_session->get_signals()) {
        if (s->signal_type() == SR_CHANNEL_LOGIC) {
            view::LogicSignal *logicSig = (view::LogicSignal*)s;
            pattern[logicSig->get_index()] = _search_lineEdit_vec[index]->text();
            index++;
        }
    }

    return pattern;
}

} // namespace dialogs
} // namespace pv
