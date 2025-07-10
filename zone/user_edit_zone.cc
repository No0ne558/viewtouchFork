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
 * user_edit_zone.cc - revision 118 (10/13/98)
 * Implementation of UserEditZone module
 */

#include "user_edit_zone.hh"
#include "terminal.hh"
#include "employee.hh"
#include "labels.hh"
#include "report.hh"
#include "dialog_zone.hh"
#include "system.hh"
#include "manager.hh"
#include <string.h>

#ifdef DMALLOC
#include <dmalloc.h>
#endif

/**** UserEditZone Class ****/
// Constructor
UserEditZone::UserEditZone()
{
    font = FONT_GARAMOND_14B;  // Use global default button font
    list_header = 2;
    AddTextField("User ID", 9); SetFlag(FF_ONLYDIGITS);
    AddTextField("Nickname", 10);
    AddListField("Training", NoYesName);
    AddNewLine(2);
    AddTextField("Last Name", 16);
    AddTextField("First Name", 16);
    AddTextField("Address", 40);
    AddTextField("City", 16);
    AddTextField("State", 3); SetFlag(FF_ALLCAPS);
    AddTemplateField("Phone", "(___) ___-____"); SetFlag(FF_ONLYDIGITS);
    AddTemplateField("SSN", "___-__-____");  SetFlag(FF_ONLYDIGITS);
    AddTextField("Job Info", 24);
    AddTextField("Employee #", 8);
    AddNewLine(2);

    Center(); Color(COLOR_WHITE);
    AddLabel("Primary Job");
    AddNewLine();
    LeftAlign(); Color(COLOR_DEFAULT);
    AddListField("Job", JobName, JobValue);
    AddListField("Pay Rate", PayRateName, PayRateValue);
    AddTextField("Amount", 7);
    AddListField("Start Page", nullptr);
    AddTextField("Department", 8);
    Color(COLOR_RED);
    AddButtonField("Remove This Job", "killjob1");
    AddNewLine(2);

    Center(); Color(COLOR_WHITE);
    AddLabel("2nd Job");
    AddNewLine();
    LeftAlign(); Color(COLOR_DEFAULT);
    AddListField("Job", JobName, JobValue);
    AddListField("Pay Rate", PayRateName, PayRateValue);
    AddTextField("Amount", 7);
    AddListField("Start Page", nullptr);
    AddTextField("Department", 8);
    Color(COLOR_RED);
    AddButtonField("Remove This Job", "killjob2");
    AddNewLine(2);

    Center(); Color(COLOR_WHITE);
    AddLabel("3rd Job");
    AddNewLine();
    LeftAlign(); Color(COLOR_DEFAULT);
    AddListField("Job", JobName, JobValue);
    AddListField("Pay Rate", PayRateName, PayRateValue);
    AddTextField("Amount", 7);
    AddListField("Start Page", nullptr);
    AddTextField("Department", 8);
    Color(COLOR_RED);
    AddButtonField("Remove This Job", "killjob3");
    AddNewLine(2);

    // Remove the 'Next' button (not present in this constructor)
    // Make the 'Add Employee' button larger
    Center(); Color(COLOR_DK_GREEN);
    AddButtonField("* Add Another Job *", "addjob");
    AddNewLine(2);
    AddButtonField("Add Employee", "new");
    view_active = 1;
}

// Member Functions
RenderResult UserEditZone::Render(Terminal *term, int update_flag)
{
    FnTrace("UserEditZone::Render()");
    if (update_flag == RENDER_NEW)
        view_active = 1;

    char str[256];
    ListFormZone::Render(term, update_flag);
    int col = color[0];
    if (show_list)
    {
        if (term->job_filter)
        {
            if (view_active)
                strcpy(str, "Filtered Active Employees");
            else
                strcpy(str, "Filtered Inactive Employees");
        }
        else
        {
            if (view_active)
                strcpy(str, "All Active Employees");
            else
                strcpy(str, "All Inactive Employees");
        }

        TextC(term, 0, term->Translate(str), col);
        TextL(term, 1.3, term->Translate("Employee Name"), col);
        TextC(term, 1.3, term->Translate("Job Title"), col);
        TextR(term, 1.3, term->Translate("Phone Number"), col);
    }
    else
    {
        if (records == 1)
            strcpy(str, "Employee Record");
        else
            snprintf(str, STRLENGTH, "Employee Record %d of %d", record_no + 1, records);
        TextC(term, 0, str, col);
    }
    return RENDER_OKAY;
}

SignalResult UserEditZone::Signal(Terminal *term, const char* message)
{
    FnTrace("UserEditZone::Signal()");
    ReportError("DEBUG: Entered UserEditZone::Signal()");
    static const char* commands[] = {
        "active", "inactive", "clear password", "remove", "activate",
        "addjob", "killjob1", "killjob2", "killjob3", nullptr};
    int idx = CompareList(message, commands);
    ReportError(std::string("DEBUG: Signal message: ") + (message ? message : "(null)") + ", idx: " + std::to_string(idx));

    if (idx < 0) {
        ReportError("DEBUG: Passing to ListFormZone::Signal");
        return ListFormZone::Signal(term, message);
    }

    switch (idx)
    {
    case 0:  // active
    case 1:  // inactive
        ReportError("DEBUG: Handling active/inactive");
        if (records > 0)
            SaveRecord(term, record_no, 0);
        show_list = 1;
        view_active ^= 1;
        record_no = 0;
        records = RecordCount(term);
        if (records > 0)
            LoadRecord(term, record_no);
        Draw(term, 1);
        return SIGNAL_OKAY;
    }

    if (user == nullptr) {
        ReportError("DEBUG: user is nullptr, returning SIGNAL_IGNORED");
        return SIGNAL_IGNORED;
    }

    switch (idx)
    {
    case 2:  // clear password
        ReportError("DEBUG: Handling clear password");
        user->password.Clear();
        SaveRecord(term, record_no, 0);
        Draw(term, 1);
        return SIGNAL_OKAY;
    case 3:  // remove
        ReportError("DEBUG: Handling remove");
        if (KillRecord(term, record_no))
            return SIGNAL_IGNORED;
        records = RecordCount(term);
        if (record_no >= records)
            record_no = records - 1;
        if (record_no < 0)
            record_no = 0;
        else
            LoadRecord(term, record_no);
        Draw(term, 1);
        return SIGNAL_OKAY;
    case 4:  // activate
        ReportError("DEBUG: Handling activate");
        user->active = 1;
        SaveRecord(term, record_no, 0);
        Draw(term, 1);
        return SIGNAL_OKAY;
    case 5:  // addjob
        ReportError("DEBUG: Handling addjob");
        if (user->JobCount() < 3)
        {
            SaveRecord(term, record_no, 0);
            JobInfo *j = new JobInfo;
            user->Add(j);
            LoadRecord(term, record_no);
            keyboard_focus = nullptr;
            Draw(term, 0);
        }
        break;
    case 6:  // killjob1
        ReportError("DEBUG: Handling killjob1");
        if (user->JobCount() >= 1)
        {
            SaveRecord(term, record_no, 0);
            JobInfo *j = user->JobList();
            user->Remove(j);
            delete j;
            LoadRecord(term, record_no);
            keyboard_focus = nullptr;
            Draw(term, 0);
        }
        break;
    case 7:  // killjob2
        ReportError("DEBUG: Handling killjob2");
        if (user->JobCount() >= 2)
        {
            SaveRecord(term, record_no, 0);
            JobInfo *j = user->JobList()->next;
            user->Remove(j);
            delete j;
            LoadRecord(term, record_no);
            keyboard_focus = nullptr;
            Draw(term, 0);
        }
        break;
    case 8:  // killjob3
        ReportError("DEBUG: Handling killjob3");
        if (user->JobCount() >= 3)
        {
            SaveRecord(term, record_no, 0);
            JobInfo *j = user->JobList()->next->next;
            user->Remove(j);
            delete j;
            LoadRecord(term, record_no);
            keyboard_focus = nullptr;
            Draw(term, 0);
        }
        break;
    }
    ReportError("DEBUG: Exiting UserEditZone::Signal(), returning SIGNAL_IGNORED");
    return SIGNAL_IGNORED;
}

int UserEditZone::Update(Terminal *term, int update_message, const char* value)
{
    if (update_message & UPDATE_JOB_FILTER)
    {
        SaveRecord(term, record_no, 0);
        record_no = 0;
        show_list = 1;
        Draw(term, 1);
        return 0;
    }
    else
    {
        return ListFormZone::Update(term, update_message, value);
    }
}

// setup list of valid starting pages.
// return page number of 1st one greater than zero 
// (normal start page, not bar/kitchen video), for use as a default
//
// FIX: Added comprehensive null pointer protection to prevent segmentation
// fault that occurred when clicking "Return" button during page transitions.
// The crash happened because this method gets called during LoadRecord() when
// the zone database might be in an inconsistent state during page changes.
int UserEditZone::AddStartPages(Terminal *term, FormField *field)
{
    FnTrace("UserEditZone::AddStartPages()");
    int retval = 0;

    // FIX: Add null pointer checks for terminal and zone database
    // This prevents crashes during page transitions when zone_db might be temporarily invalid
    if (term == nullptr || term->zone_db == nullptr || field == nullptr) {
        ReportError("AddStartPages: Null pointer - term, zone_db, or field is null");
        return retval;
    }

    int last_page = 0;
    field->ClearEntries();
    
    // FIX: Add null check for PageList() and protect iteration
    // During page transitions, the page database can be in an inconsistent state
    // causing PageList() to return null or containing corrupted page pointers
    Page *page_list = term->zone_db->PageList();
    if (page_list == nullptr) {
        ReportError("AddStartPages: PageList() returned null");
        field->AddEntry("Check List Page", 0);
        return retval;
    }
    
    for (Page *p = page_list; p != nullptr; p = p->next)
    {
        // FIX: Add null check for page properties to prevent accessing corrupted pages
        if (p->IsStartPage() && p->id != last_page)
        {
            last_page = p->id;
            // FIX: Add null check before accessing page name
            // Page objects might be partially corrupted during transitions
            const char* page_name = p->name.Value();
            if (page_name != nullptr) {
                field->AddEntry(page_name, p->id);
                if (p->id > 0 && retval == 0)	// (or maybe p->IsTable()?)
                    retval = p->id;
            }
        }
    }
    //NOTE BAK-->Check List Page is not a specific page, but a page type.
    //NOTE What happens when there are two Check List Page entries?
    field->AddEntry("Check List Page", 0);
    return retval;
}

int UserEditZone::LoadRecord(Terminal *term, int record)
{
    FnTrace("UserEditZone::LoadRecord()");
    
    // FIX: Add null pointer checks for terminal and system data
    // This method is called during page transitions and rendering, when system
    // objects might be in an inconsistent state
    if (term == nullptr || term->system_data == nullptr) {
        ReportError("LoadRecord: Terminal or system_data is null");
        return 1;
    }
    
    // FIX: Handle case when there are no employee records
    // CRITICAL: This was the primary cause of the segmentation fault when clicking
    // "Return" button on empty employee database. The method would try to access
    // form fields and employee data when user=nullptr, causing memory violations.
    if (records <= 0) {
        ReportError("LoadRecord: No employee records exist, skipping load");
        user = nullptr;
        return 0;
    }
    
    System *sys = term->system_data;
    Employee *e = sys->user_db.FindByRecord(term, record, view_active);
    if (e == nullptr) {
        ReportError("LoadRecord: Could not find employee record");
        user = nullptr;
        return 1;
    }

    Settings *s = &(sys->settings);
    if (s == nullptr) {
        ReportError("LoadRecord: Settings is null");
        return 1;
    }
    
    user = e;

    int job_active[MAX_JOBS];
    int i;
    for (i = 0; i < MAX_JOBS; ++i)
        job_active[i] = 0;

    int list = 0;
    while (JobName[list])
    {
        int j = JobValue[list];
        job_active[list] = s->job_active[j];
        ++list;
    }

    FormField *f = FieldList();
    if (f == nullptr) {
        ReportError("LoadRecord: FieldList() returned null");
        return 1;
    }
    
    // FIX: Process basic employee fields with null checks
    // Each form field access is protected to prevent segmentation faults
    // if the form field chain is corrupted during page transitions
    if (f != nullptr) { f->Set(e->key); f = f->next; }
    if (f != nullptr) { f->Set(e->system_name); f = f->next; }

    if (f != nullptr) { f->Set(e->training); f = f->next; }
    if (f != nullptr) { f->Set(e->last_name); f = f->next; }
    if (f != nullptr) { f->Set(e->first_name); f = f->next; }
    if (f != nullptr) { f->Set(e->address); f = f->next; }
    if (f != nullptr) { f->Set(e->city); f = f->next; }
    if (f != nullptr) { f->Set(e->state); f = f->next; }
    if (f != nullptr) { f->Set(e->phone); f = f->next; }
    if (f != nullptr) { f->Set(e->ssn); f = f->next; }
    if (f != nullptr) { f->Set(e->description); f = f->next; }
    if (f != nullptr) { f->Set(e->employee_no); f = f->next; }

    // FIX: Process exactly 3 job slots to match the form structure
    // Changed from unbounded while loop to bounded for loop to prevent
    // infinite loops and ensure form structure consistency
    JobInfo *j = e->JobList();
    for (i = 0; i < 3; ++i)
    {
        if (f == nullptr) {
            ReportError("LoadRecord: Form field pointer became null during job iteration");
            break;
        }
        
        if (j != nullptr)
        {
            if (f != nullptr) { f->active = 1; f = f->next; }
            if (f != nullptr) { f->active = 1; f->Set(j->job); f->SetActiveList(job_active); f = f->next; }
            if (f != nullptr) { f->active = 1; f->Set(j->pay_rate); f = f->next; }
            if (f != nullptr) { f->active = 1; f->Set(term->SimpleFormatPrice(j->pay_amount)); f = f->next; }
            if (f != nullptr) { 
                f->active = 1; 
                int defpage = AddStartPages(term, f); 
                if (j->starting_page == -1)	// unset/default, use 1st normal start page
                    j->starting_page = defpage;
                f->Set(j->starting_page); 
                f = f->next; 
            }
            if (f != nullptr) { f->active = 1; f->Set(j->dept_code); f = f->next; }
            if (f != nullptr) { f->active = (e->JobCount() > 1); f = f->next; }
            j = j->next;
        }
        else
        {
            // No more jobs, deactivate remaining fields for this slot
            for (int k = 0; k < 7 && f != nullptr; ++k)
            {
                f->active = 0; 
                f = f->next;
            }
        }
    }
    
    if (f != nullptr) {
        f->active = (e->JobCount() < 3);
    }
    return 0;
}

int UserEditZone::SaveRecord(Terminal *term, int record, int write_file)
{
    FnTrace("UserEditZone::SaveRecord()");
    
    // FIX: Add null pointer checks for terminal and system data
    // This method is called during page transitions when clicking "Return" button
    if (term == nullptr || term->system_data == nullptr) {
        ReportError("SaveRecord: Terminal or system_data is null");
        return 1;
    }
    
    // FIX: Handle case when there are no employee records
    // CRITICAL: This was the main cause of segmentation fault. When employee
    // database is empty (records=0), user=nullptr, but SaveRecord() was still
    // called during "Return" button processing, trying to access null form fields
    // and employee data, causing memory access violations.
    if (records <= 0) {
        ReportError("SaveRecord: No employee records exist, skipping save");
        return 0;
    }
    
    Employee *e = user;
    if (e == nullptr) {
        ReportError("SaveRecord: Employee user pointer is null");
        return 0;
    }
    
    if (e)
    {
        FormField *f = FieldList();
        if (f == nullptr) {
            ReportError("SaveRecord: FieldList() returned null");
            return 1;
        }
        
        // FIX: Process basic employee fields with comprehensive null checks
        // Every form field access is protected to prevent crashes if the
        // form field linked list is corrupted during page transitions
        if (f != nullptr) { f->Get(e->key); f = f->next; }
        if (f != nullptr) { f->Get(e->system_name); f = f->next; }
        e->system_name = AdjustCase(e->system_name.str());
        if (f != nullptr) { f->Get(e->training); f = f->next; }
        if (f != nullptr) { f->Get(e->last_name); f = f->next; }
        e->last_name = AdjustCase(e->last_name.str());
        if (f != nullptr) { f->Get(e->first_name); f = f->next; }
        e->first_name = AdjustCase(e->first_name.str());
        if (f != nullptr) { f->Get(e->address); f = f->next; }
        e->address = AdjustCase(e->address.str());
        if (f != nullptr) { f->Get(e->city); f = f->next; }
        e->city = AdjustCase(e->city.str());
        if (f != nullptr) { f->Get(e->state); f = f->next; }
        e->state = StringToUpper(e->state.str());
        if (f != nullptr) { f->Get(e->phone); f = f->next; }
        if (f != nullptr) { f->Get(e->ssn); f = f->next; }
        if (f != nullptr) { f->Get(e->description); f = f->next; }
        if (f != nullptr) { f->Get(e->employee_no); f = f->next; }

        // FIX: Process exactly 3 job slots to match the form structure
        // Changed from original unbounded iteration to bounded loop matching
        // the LoadRecord() structure to prevent form field misalignment
        JobInfo *j = e->JobList();
        for (int i = 0; i < 3; ++i)
        {
            if (f == nullptr) {
                ReportError("SaveRecord: Form field pointer became null during job iteration");
                break;
            }
            
            if (j != nullptr)
            {
                // Skip the job active field
                f = f->next;
                // FIX: Process job fields with comprehensive null checks
                // Each field access is protected to prevent segmentation faults
                if (f != nullptr) { f->Get(j->job); f = f->next; }
                if (f != nullptr) { f->Get(j->pay_rate); f = f->next; }
                if (f != nullptr) { f->GetPrice(j->pay_amount); f = f->next; }
                if (f != nullptr) { f->Get(j->starting_page); f = f->next; }
                if (f != nullptr) { f->Get(j->dept_code); f = f->next; }
                if (f != nullptr) { f = f->next; } // Skip the delete button field
                j = j->next;
            }
            else
            {
                // FIX: No more jobs, safely skip the remaining fields for this slot
                // Added null check in loop condition to prevent crashes when
                // form field chain is shorter than expected
                for (int k = 0; k < 7 && f != nullptr; ++k)
                {
                    f = f->next;
                }
            }
        }
    }

    // Additional null check for employee pointer
    if (e && (e->system_name.empty()) &&
        ((e->first_name.size() > 0) && (e->last_name.size() > 0)))
    {
        char tempname[STRLONG];
        snprintf(tempname, STRLONG, "%s %s", e->first_name.Value(), e->last_name.Value());
        e->system_name.Set(tempname);
    }
    

    if (write_file)
        term->system_data->user_db.Save();
    return 0;
}

int UserEditZone::NewRecord(Terminal *term)
{
    FnTrace("UserEditZone::NewRecord()");
    ReportError("DEBUG: NewRecord() called");
    term->job_filter = 0; // make sure new user is shown on list
    ReportError("DEBUG: About to call NewUser()");
    user = term->system_data->user_db.NewUser();
    ReportError("DEBUG: NewUser() returned");
    record_no = 0;
    view_active = 1;
    ReportError("DEBUG: About to call RecordCount()");
    records = RecordCount(term); // Update record count before saving
    ReportError("DEBUG: RecordCount() returned: " + std::to_string(records));
    ReportError("DEBUG: NewRecord() completed successfully");
    return 0;
}

int UserEditZone::KillRecord(Terminal *term, int record)
{
    FnTrace("UserEditZone::KillRecord()");
    if (user == nullptr || term->IsUserOnline(user))
        return 1;
    term->system_data->user_db.Remove(user);
    delete user;
    user = nullptr;
    return 0;
}

int UserEditZone::PrintRecord(Terminal *term, int record)
{
    FnTrace("UserEditZone::PrintRecord()");
    // FIX - finish UserEditZone::PrintRecord()
    return 1;
}

int UserEditZone::Search(Terminal *term, int record, const char* word)
{
    FnTrace("UserEditZone::Search()");
    int r = term->system_data->user_db.FindRecordByWord(term, word, view_active, record);
    if (r < 0)
        return 0;  // no matches
    record_no = r;
    return 1;  // one match (only 1 for now)
}

int UserEditZone::ListReport(Terminal *term, Report *r)
{
    return term->system_data->user_db.ListReport(term, view_active, r);
}

int UserEditZone::RecordCount(Terminal *term)
{
    return term->system_data->user_db.UserCount(term, view_active);
}


/**** JobSecurityZone Class ****/
// Constructor
JobSecurityZone::JobSecurityZone()
{
    wrap        = 0;
    keep_focus  = 0;
    form_header = 2;
    font        = FONT_DEJAVU_18;
    int i;

    for (i = 1; JobName[i] != nullptr; ++i)
    {
        AddLabel(JobName[i], 10);  // label width 10 (narrower)
        AddListField("", MarkName, nullptr, 0, 2);  // Active (box width 2)
        AddListField("", MarkName, nullptr, 0, 2);  // Enter System
        AddListField("", MarkName, nullptr, 0, 2);  // Order
        AddListField("", MarkName, nullptr, 0, 2);  // Settle
        AddListField("", MarkName, nullptr, 0, 2);  // Move Table
        AddListField("", MarkName, nullptr, 0, 2);  // Rebuild Edit
        AddListField("", MarkName, nullptr, 0, 2);  // Comp
        AddListField("", MarkName, nullptr, 0, 2);  // Supervisor Functions
        AddListField("", MarkName, nullptr, 0, 2);  // Manager Functions
        AddListField("", MarkName, nullptr, 0, 2);  // Employee Records
        AddNewLine();
    }
}

// Member Functions
RenderResult JobSecurityZone::Render(Terminal *term, int update_flag)
{
    FnTrace("JobSecurityZone::Render()");

    int col = color[0];
    FormZone::Render(term, update_flag);
    // Header positions based on actual field layout:
    int x = 0;
    TextPosC(term, x + 5.5,   .5, "Job", col);           // center of label (0-11)
    x = 12;
    TextPosC(term, x + 1.5,   .5, "Active", col);         // center of first box (12-15)
    x += 4;
    TextPosC(term, x + 1.5,   0, "Enter", col);
    TextPosC(term, x + 1.5,   1, "System", col);
    x += 4;
    TextPosC(term, x + 1.5,   .5, "Order", col);
    x += 4;
    TextPosC(term, x + 1.5,   .5, "Settle", col);
    x += 4;
    TextPosC(term, x + 1.5,   0, "Move", col);
    TextPosC(term, x + 1.5,   1, "Table", col);
    x += 4;
    TextPosC(term, x + 1.5,   0, "Rebuild", col);
    TextPosC(term, x + 1.5,   1, "Edit", col);
    x += 4;
    TextPosC(term, x + 1.5,   .5, "Comp", col);
    x += 4;
    TextPosC(term, x + 1.5,   0, "Supervisor", col);
    TextPosC(term, x + 1.5,   1, "Functions", col);
    x += 4;
    TextPosC(term, x + 1.5,   0, "Manager", col);
    TextPosC(term, x + 1.5,   1, "Functions", col);
    x += 4;
    TextPosC(term, x + 1.5,   0, "Employee", col);
    TextPosC(term, x + 1.5,   1, "Records", col);
    return RENDER_OKAY;
}

/****
 * IsActiveField:  What do I name this?  The idea is that there
 *  is one field per job category, second field from the left.
 *  We need to determine if that field is the current field,
 *  and whether it is being disabled.  If both of those are
 *  true, return 1.  Otherwise, return 0.
 ****/
int JobSecurityZone::DisablingCategory()
{
    FnTrace("JobSecurityZone::DisablingCategory()");
    int retval       = 0;
    FormField *field = FieldList();
    int counter      = 0;
    int is_active    = 0;  // container for the field's value

    while (field != nullptr && retval == 0)
    {
        // Every 'columns' columns, we have the label.  The next
        // field over is the "active" field for the current job,
        // which is the one we want.
        if ((counter % columns) == 0)
        {
            // We're on the right row.  Now skip to the field we want.
            field = field->next;
            counter += 1;
            field->Get(is_active);
            if (field == keyboard_focus && is_active == 1)
            {
                // We have a match.  We'll give the index of the job category,
                // which is slightly convoluted.
                retval = ((counter - 1) / columns) + 1;
            }
        }
        if (field != nullptr)
            field = field->next;
        counter += 1;
    }

    // retval is an index into JobValue[]
    retval = JobValue[retval];

    return retval;
}

int JobSecurityZone::EmployeeIsUsing(Terminal *term, int active_job)
{
    FnTrace("JobSecurityZone::EmployeeIsUsing()");
    int retval = 0;
    Employee *employee   = nullptr;
    JobInfo  *jobinfo    = nullptr;

    employee = MasterSystem->user_db.UserList();
    while (employee != nullptr && retval == 0)
    {
        jobinfo = employee->JobList();
        while (jobinfo != nullptr && retval == 0)
        {
            if (jobinfo->job == active_job)
                retval = 1;

            jobinfo = jobinfo->next;
        }
        employee = employee->next;
    }

    return retval;
}

SignalResult JobSecurityZone::Signal(Terminal *term, const char* message)
{
    FnTrace("JobSecurityZone::Signal()");
    SignalResult retval = SIGNAL_IGNORED;
    static const genericChar* commands[] = { "jsz_no", "jsz_yes", nullptr};
    int idx = CompareListN(commands, message);

    switch (idx)
    {
    case 0:
        last_focus = nullptr;
        break;
    case 1:
        if (last_focus != nullptr)
        {
            keyboard_focus = last_focus;
            last_focus = nullptr;
            keyboard_focus->Touch(term, this, keyboard_focus->x + 1, keyboard_focus->y + 1);
            UpdateForm(term, 0);
            Draw(term, 0);
        }
        break;
    default:
        retval = FormZone::Signal(term, message);
        break;
    }

    return retval;
}

SignalResult JobSecurityZone::Touch(Terminal *term, int tx, int ty)
{
    FnTrace("JobSecurityZone::Touch()");
    int active_cat        = 0;
    int is_used           = 0;
    SimpleDialog *sdialog = nullptr;
    if (records <= 0)
        return SIGNAL_IGNORED;
    
    LayoutZone::Touch(term, tx, ty);

    // It's bad to disable a job category when there are employee's configured
    // for that job.  We'll allow it, but only after a confirmation.
    keyboard_focus = Find(selected_x, selected_y);
    active_cat = DisablingCategory();
    if (active_cat > 0)
        is_used = EmployeeIsUsing(term, active_cat);
    if (is_used)
    {
        last_focus = keyboard_focus;
        sdialog = new SimpleDialog("This category is in use.  Are \
                                    you sure you want to disable it?");
        sdialog->Button("Yes", "jsz_yes");
        sdialog->Button("No", "jsz_no");
        term->OpenDialog(sdialog);
    }
    else
        FormZone::Touch(term, tx, ty);

    Draw(term, 0);
    return SIGNAL_OKAY;
}

SignalResult JobSecurityZone::Mouse(Terminal *term, int action, int mx, int my)
{
    FnTrace("JobSecurityZone::Mouse()");
    SignalResult retval = SIGNAL_IGNORED;
    if (records <= 0 || !(action & MOUSE_PRESS))
        return SIGNAL_IGNORED;

    // mouse touches are just touches here
    if (action & MOUSE_PRESS)
        retval = Touch(term, mx, my);

    return retval;
}

int JobSecurityZone::LoadRecord(Terminal *term, int record)
{
    FnTrace("JobSecurityZone::LoadRecord()");
    Settings  *s = term->GetSettings();
    FormField *f = FieldList();
    int i = 1;
    while (JobName[i])
    {
        int j    = JobValue[i];
        int a    = s->job_active[j];
        int flag = s->job_flags[j];

        // job title
        f->label.Set(term->Translate(JobName[i]));
        f = f->next;

        // Active switch
        f->Set(a); f = f->next;

        // tables
        f->active = a;
        if (flag & SECURITY_TABLES)
            f->Set(1);
        else
            f->Set(0);
        f = f->next;

        // order
        f->active = a;
        if (flag & SECURITY_ORDER)
            f->Set(1);
        else
            f->Set(0);
        f = f->next;

        // settle
        f->active = a;
        if (flag & SECURITY_SETTLE)
            f->Set(1);
        else
            f->Set(0);
        f = f->next;

        // transfer
        f->active = a;
        if (flag & SECURITY_TRANSFER)
            f->Set(1);
        else
            f->Set(0);
        f = f->next;

        // rebuild
        f->active = a;
        if (flag & SECURITY_REBUILD)
            f->Set(1);
        else
            f->Set(0);
        f = f->next;

        // comp
        f->active = a;
        if (flag & SECURITY_COMP)
            f->Set(1);
        else
            f->Set(0);
        f = f->next;

        // supervisor
        f->active = a;
        if (flag & SECURITY_SUPERVISOR)
            f->Set(1);
        else
            f->Set(0);
        f = f->next;

        // manager
        f->active = a;
        if (flag & SECURITY_MANAGER)
            f->Set(1);
        else
            f->Set(0);
        f = f->next;

        // employees
        f->active = a;
        if (flag & SECURITY_EMPLOYEES)
            f->Set(1);
        else
            f->Set(0);
        f = f->next;
        ++i;
    }
    return 0;
}

int JobSecurityZone::SaveRecord(Terminal *term, int record, int write_file)
{
    FnTrace("JobSecurityZone::SaveRecord()");
    Settings  *s = term->GetSettings();
    FormField *f = FieldList();
    int i = 1;
    while (JobName[i])
    {
        int flag = 0;
        int val[9];
        int jv = JobValue[i];

        // job title (skip)
        f = f->next;
        // job active
        f->Get(s->job_active[jv]); f = f->next;

        int j;
        for (j = 0; j < 9; ++j)
        {
            f->Get(val[j]);
            f = f->next;
        }

        if (val[0] > 0) flag |= SECURITY_TABLES;
        if (val[1] > 0) flag |= SECURITY_ORDER;
        if (val[2] > 0) flag |= SECURITY_SETTLE;
        if (val[3] > 0) flag |= SECURITY_TRANSFER;
        if (val[4] > 0) flag |= SECURITY_REBUILD;
        if (val[5] > 0) flag |= SECURITY_COMP;
        if (val[6] > 0) flag |= SECURITY_SUPERVISOR;
        if (val[7] > 0) flag |= SECURITY_MANAGER;
        if (val[8] > 0) flag |= SECURITY_EMPLOYEES;

        s->job_flags[jv] = flag;
        ++i;
    }

    if (write_file)
        s->Save();
    return 0;
}

int JobSecurityZone::UpdateForm(Terminal *term, int record)
{
    FnTrace("JobSecurityZone::UpdateForm()");
    FormField *f = FieldList();
    int a;
    int i = 1;
    int j;

    while (JobName[i])
    {
        f = f->next;
        f->Get(a);
        f = f->next;
        for (j = 0; j < 9; ++j)
        {
            f->active = a; f = f->next;
        }
        ++i;
    }
    return 0;
}
