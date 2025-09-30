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
 * cdu_att.hh  Separate file for storing the various serial port attributes
 *  (8N1, et al) for the CDU devices.
 */

#ifndef VT_CDU_ATT_HH

#define CDU_PORT 65529

int EpsonSetAttributes(int fd);
int BA63SetAttributes(int fd);

#define VT_CDU_ATT_HH
#endif
