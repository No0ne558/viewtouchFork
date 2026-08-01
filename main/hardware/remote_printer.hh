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
 * remote_printer.hh - revision 2 (8/8/97)
 * Remote Printer link module
 *
 * NOT BUILT, AND NOT REACHABLE.
 *
 * remote_printer.cc appears in no CMake target, so nothing in it is compiled
 * into any binary. It is also not reachable even if it were: NewReportPrinter()
 * below is the module's only entry point and has no callers anywhere in the
 * tree.
 *
 * This matters because several changelog entries describe bug fixes made in
 * remote_printer.cc. Those fixes have never run. Adding the file to a target
 * would not change that either, since nothing calls into it -- making it live
 * would require a call site as well as a build entry.
 *
 * Left in place rather than deleted because the intent behind those recent
 * edits belongs to the maintainer, not to a test-coverage pass. Whether to wire
 * it up properly or remove it is a decision for whoever knows what it was for.
 */

#ifndef _REMOTE_PRINTER_HH
#define _REMOTE_PRINTER_HH

/**** Types ****/
class Printer;

/**** Functions ****/
Printer *NewReportPrinter(const char* host, int port, int model, int no);

#endif

