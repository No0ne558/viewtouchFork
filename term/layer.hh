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
 * layer.hh - revision 16 (8/10/98)
 * Pixmap graphic buffer objects
 */

#pragma once

#include "term_view.hh"
#include "list_utility.hh"


/**** Types ****/
class Layer;
class LayerList;
class LayerObject;
class LayerObjectList;

class LayerObject : public RegionInfo
{
public:
    LayerObject *next, *fore;
    int id, hilight, select;

    // Constructor
    LayerObject();
    // Destructor
    virtual ~LayerObject() { }

    // Member Functions
    int UpdateAll(LayerList *ll, Layer *l);
    virtual int Render(Layer *l) = 0;
    virtual int Layout(Layer *l) { return 0; }
    virtual int MouseEnter(LayerList *ll, Layer *l);
    virtual int MouseExit(LayerList *ll, Layer *);
    virtual int MouseAction(LayerList *ll, Layer *l, int mouse_x, int mouse_y, int code) { return 0; }
};

class LayerObjectList
{
    DList<LayerObject> list;

public:
    // Constructor
    LayerObjectList();

    // Member Functions
    int Add(LayerObject *lo);
    int Remove(LayerObject *lo);
    int Purge();
    LayerObject *FindByID(int id);
    LayerObject *FindByPoint(int x, int y);

    int Render(Layer *l);
    int Layout(Layer *l);
    int MouseAction(LayerList *ll, Layer *l, int x, int y, int code);
};

class Layer : public RegionInfo
{
public:
    Layer *next;
    Layer *fore;
    int id;
    int offset_x;
    int offset_y;
    int window_frame;
    Str window_title;
    Pixmap pix;
    Display *dis;
    Window win;
    GC gfx;
    int update;
    int cursor;
    int page_x;
    int page_y;
    int page_w;
    int page_h;
    int page_split;
    int split_opt;
    int bg_texture;
    int frame_width;
    int title_color;
    int title_height;
    int title_mode;
    RegionInfo max;
    RegionInfo clip;
    int use_clip;
    Str page_title;
    LayerObjectList buttons;

    // Constructor
    Layer(Display *d, GC g, Window dw, int lw, int lh);
    // Destructor
    virtual ~Layer();

    // Member Functions
    int DrawArea(int dx, int dy, int dw, int dh);
    int DrawAll();
    int BlankPage(int mode, int texture, int title_color, int size, int split,
                  int split_opt, const genericChar* title, const genericChar* time);
    int Background(int x, int y, int w, int h);
    int TitleBar();
    int Text(const char* string, int len, int tx, int ty, int color, int font,
             int align, int max_pixel_width = 0);
    int ZoneText(const char* str, int x, int y, int w, int h,
                 int color, int font, int align = ALIGN_CENTER);
    int Rectangle(int rx, int ry, int rw, int rh, int image);
    int SolidRectangle(int rx, int ry, int rw, int rh, int pixel_id);
    int Circle(int cx, int cy, int cw, int ch, int image);
    int Diamond(int dx, int dy, int dw, int dh, int image);
    int Zone(int x, int y, int w, int h, int frame, int texture,
             int shape = SHAPE_RECTANGLE);
    int Shadow(int x, int y, int w, int h, int s, int shape = SHAPE_RECTANGLE);
    int Ghost(int gx, int gy, int gw, int gh);
    int HLine(int x, int y, int len, int lw, int color);
    int VLine(int x, int y, int len, int lw, int color);
    int Shape(int sx, int sy, int sw, int sh, int image, int shape);
    int Edge(int ex, int ey, int ew, int eh, int thick, int image);
    int Edge(int ex, int ey, int ew, int eh, int thick, int image, int shape);
    int Frame(int fx, int fy, int fw, int fh, int thick, int flags = 0);
    int FilledFrame(int x, int y, int w, int h, int fw, int texture,
                    int flags = 0);
    int StatusBar(int x, int y, int w, int h, int bar_color,
                  const genericChar* text, int font, int text_color);
    int EditCursor(int x, int y, int w, int h);
    int FramedWindow(int x, int y, int w, int h, int color);
    int VGrip(int x, int y, int w, int h);
    int HGrip(int x, int y, int w, int h);
    int SetClip(int x, int y, int w, int h);
    int ClearClip();

    int MouseEnter(LayerList *ll);
    int MouseExit(LayerList *ll);
    int MouseAction(LayerList *ll, int x, int y, int code);
    int Touch(LayerList *ll, int x, int y);
    int Keyboard(LayerList *ll, genericChar key, int code, int state);
};

class LayerList
{
    DList<Layer> list;
    DList<Layer> inactive;

public:
    Display *dis;
    Window   win;
    GC       gfx;
    int select_on;
    int select_x1;
    int select_y1;
    int select_x2;
    int select_y2;
    int mouse_x;
    int mouse_y;
    int drag_x;
    int drag_y;
    int screen_blanked;
    int screen_image;
    int active_frame_color;
    int inactive_frame_color;
    Layer *last_layer;
    Layer *drag;
    LayerObject *last_object;

    // Constructor
    LayerList();
    // Destructor
    ~LayerList() { Purge(); }

    // Member Functions
    int XWindowInit(Display *d, GC g, Window w);
    int Add(Layer *l, int update = 1);
    int AddInactive(Layer *l);
    int Remove(Layer *l, int update = 1);
    int Purge();
    Layer *FindByPoint(int x, int y);
    Layer *FindByID(int id);

    int SetScreenBlanker(int set);
    int SetScreenImage(int set);
    int UpdateAll(int select_all = 1);
    // redraws all layers (only layers with update flag if select_all = 0)
    int UpdateArea(int x, int y, int w, int h);
    // redraws all layers in region
    int OptimalUpdateArea(int x, int y, int w, int h, Layer *end = nullptr);
    // redraws all layers with update flag set in region
    int RubberBandOff();
    int RubberBandUpdate(int x, int y);
    int MouseAction(int x, int y, int code);
    int DragLayer(int x, int y);
    int Touch(int x, int y);
    int Keyboard(char key, int code, int state);
    int HideCursor();
    int SetCursor(Layer *l, int type);
};

class LO_PushButton : public LayerObject
{
public:
    Str text;
    int color[2];
    int font;

    // Constructor
    LO_PushButton(const char* str, int normal_color, int active_color);

    // Member Functions
    int Render(Layer *l);
    int MouseAction(LayerList *ll, Layer *l, int mouse_x, int mouse_y, int code);

    virtual int Command(Layer *l);
};

class LO_ScrollBar : public LayerObject
{
public:
    RegionInfo bar;
    int bar_x, bar_y, press_x, press_y;

    // Constructor
    LO_ScrollBar();

    // Member Functions
    int Render(Layer *l);
    int MouseAction(LayerList *ll, Layer *l, int mouse_x, int mouse_y, int code);
};

class LO_ItemList : public LayerObject
{
public:
    // Constructor
    LO_ItemList();

    // Member Functions
    int Render(Layer *l);
    int MouseAction(LayerList *ll, Layer *l, int x, int y, int code);
};

class LO_ItemMenu : public LayerObject
{
public:
    // Constructor
    LO_ItemMenu();

    // Member Functions
    int Render(Layer *l);
    int MouseAction(LayerList *ll, Layer *l, int x, int y, int code);
};

class LO_TextEntry : public LayerObject
{
public:
    // Constructor
    LO_TextEntry();

    // Member Functions
    int Render(Layer *l);
    int MouseAction(LayerList *ll, Layer *l, int x, int y, int code);
};
