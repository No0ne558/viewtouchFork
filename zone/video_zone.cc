/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998  
  
 *   This program is free software: you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation, either version 3 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful, 
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 *   GNU General Public License for more details. 
 * 
 *   You should have received a copy of the GNU General Public License 
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. 
 *
 * video_zone.cc
 * Zones related in some way to video.  For example, VideoTargetZone is used
 *  to determine which food types get sent to the Kitchen Video reports.
 */

#include "video_zone.hh"
#include "terminal.hh"
#include "labels.hh"
#include "locale.hh"
#include "settings.hh"
#include "system.hh"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

// Constructor
VideoTargetZone::VideoTargetZone()
{
    FnTrace("VideoTargetZone::VideoTargetZone()");

    phrases_changed = 0;
    AddFields();
}

// Member Functions
int VideoTargetZone::AddFields()
{
    FnTrace("VideoTargetZone::AddFields()");

    int i = 0;

    for (i = 0; FamilyName[i] != NULL; i++)
    {
        AddListField(MasterLocale->Translate(FamilyName[i]),
                     PrinterIDName, PrinterIDValue);
    }

    return 0;
}

RenderResult VideoTargetZone::Render(Terminal *term, int update_flag)
{
    FnTrace("VideoTargetZone::Render()");

    if (phrases_changed < term->system_data->phrases_changed)
    {
        Purge();
        AddFields();
        phrases_changed = term->system_data->phrases_changed;
    }

    FormZone::Render(term, update_flag);
    TextC(term, 0, name.Value(), color[0]);
    return RENDER_OKAY;
}

int VideoTargetZone::LoadRecord(Terminal *term, int record)
{
    FnTrace("VideoTargetZone::LoadRecord()");
    Settings *settings = term->GetSettings();
    FormField *form = FieldList();
    int idx = 0;
    while (FamilyName[idx])
    {
        form->active = (settings->family_group[FamilyValue[idx]] != SALESGROUP_NONE);
        form->Set(settings->video_target[idx]);
        form = form->next;
        ++idx;
    }
    return 0;
}

int VideoTargetZone::SaveRecord(Terminal *term, int record, int write_file)
{
    FnTrace("VideoTargetZone::SaveRecord()");
    Settings *settings = term->GetSettings();
    FormField *form = FieldList();
    int idx = 0;
    while (FamilyName[idx])
    {
        form->Get(settings->video_target[idx]);
        form = form->next;
        ++idx;
    }
    if (write_file)
        settings->Save();
    return 0;
}
