/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998, 2025, 2026
 *
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
 * vt_main_entry.cc - Entry point for the vt_main executable.
 *
 * All of the startup logic lives in VtMain() in manager.cc, which is compiled
 * into the vtbusiness library. Keeping main() in its own translation unit is
 * what lets the test target link that library: a library that defined main()
 * would collide with the one Catch2 provides.
 */

#include "manager.hh"

int main(int argc, char *argv[])
{
    return VtMain(argc, argv);
}
