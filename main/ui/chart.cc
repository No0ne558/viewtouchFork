/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998, 2025, 2026
  
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
 * chart.cc - revision 1 (10/20/98)
 * Implementation of chart module
 */

#include "chart.hh"
#include "terminal.hh"
#include <memory>

#ifdef DMALLOC
#include <dmalloc.h>
#endif


/**** ChartCell class ****/
// Constructor
ChartCell::ChartCell()
{
    next  = nullptr;
    fore  = nullptr;
    align = ALIGN_LEFT;
    color = COLOR_DEFAULT;
}

/**** ChartRow class ****/
// Constructor
ChartRow::ChartRow()
{
    next = nullptr;
    fore = nullptr;
    id = 0;
}

/**** Chart class ****/
// Constructor
Chart::Chart()
{
    current_row = nullptr;
}

// Member Functions
int Chart::Clear()
{
    header_list.Purge();
    row_list.Purge();
    return 0;
}

int Chart::AddColumn(const char* name)
{
    auto c_up = std::make_unique<ChartCell>();
    c_up->text.Set(name);
    header_list.AddToTail(c_up.release());
    return 0;
}

int Chart::SortByColumn(int id)
{
    return 0;
}

int Chart::AddRowCell(const char* text)
{
    if (current_row == nullptr)
    {
        auto r_up = std::make_unique<ChartRow>();
        if (row_list.Tail())
            r_up->id = row_list.Tail()->id + 1;
        else
            r_up->id = 1;
        row_list.AddToTail(r_up.release());
        current_row = row_list.Tail();
    }

    ChartRow *r = current_row;
    auto c_up = std::make_unique<ChartCell>();
    c_up->text.Set(text);
    r->cell_list.AddToTail(c_up.release());
    return 0;
}

int Chart::EndRow()
{
    current_row = nullptr;
    return 0;
}
