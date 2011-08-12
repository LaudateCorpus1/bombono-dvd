//
// mgui/editor/actions.cpp
// This file is part of Bombono DVD project.
//
// Copyright (c) 2008-2010 Ilya Murav'jov
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
// 

#include <mgui/_pc_.h>

#include "actions.h"
#include "render.h"
#include "toolbar.h"

#include <mgui/project/menu-actions.h>
#include <mgui/project/dnd.h>

#include <mlib/sdk/logger.h>

void AddStandardEvents(Gtk::Widget& wdg)
{
    wdg.add_events(
        Gdk::EXPOSURE_MASK  |   // прорисовка
        //
        // для сигнала "configure-event"; но он предназначен, в оригинале, только для окон
        // верхнего уровня (GtkWindow, ...) + искусственно посылается в случае GtkDrawingArea 
        // (вне зависимости от маски); поэтому, в случае других виджетов (GtkLayout) пользуемся
        // сигналом "size-allocate" (который обычно и является суммарным эффектом от "configure-event" для
        // окна верхнего уровня)
        // 
        // Gdk::STRUCTURE_MASK |   
        Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |      // кнопки мыши
        Gdk::POINTER_MOTION_HINT_MASK|Gdk::POINTER_MOTION_MASK   // движения мыши (*HINT* - для получений
        // сообщений по требованию - 
        // вызов gdk_window_get_pointer())
        //Gdk::ENTER_NOTIFY_MASK |                               // когда зашли-вышли из окна - не нужно
        );

    // удобный способ (при разработке) визуально наблюдать, 
    // какая часть окна перерисовывается 
    //wdg.get_window()->set_debug_updates();
}

Point DisplayAspectOrDef(MenuRegion* m_rgn)
{
    return m_rgn ? m_rgn->GetParams().DisplayAspect() : MenuParams(true).DisplayAspect();
}

Point DisplaySizeOrDef(MenuRegion* m_rgn)
{
    return m_rgn ? m_rgn->GetParams().Size() : MenuParams(true).Size();
}

void RenderForRegion(MEditorArea& edt_area, RectListRgn& rct_lst)
{
    MenuRegion& m_rgn = edt_area.CurMenuRegion();
    if( ReDivideRects(rct_lst, m_rgn) )
    {
        if( edt_area.StandAlone() )
        {
            RenderEditor(edt_area, rct_lst);

            edt_area.DrawUpdate(rct_lst);
        }
        else
            RenderMenuSystem(GetOwnerMenu(&m_rgn), rct_lst);
    }
}

void RenderForRegion(MEditorArea& edt_area, const Rect& rel_rct)
{
    RectListRgn rct_lst;
    rct_lst.push_back(rel_rct);
    RenderForRegion(edt_area, rct_lst);
}

int GetObjectAtPos(MEditorArea& edt_area, const Point& lct)
{
    SelVis sel_vis(lct.x, lct.y);
    edt_area.CurMenuRegion().Accept(sel_vis);
    return sel_vis.selPos;
}

inline Gdk::ModifierType GetKeyboardState()
{
    Gdk::ModifierType mask;
    gdk_window_get_pointer(0, 0, 0, (GdkModifierType*)&mask);
    return mask;
}

inline bool IsControlPressed()
{
    return GetKeyboardState() & Gdk::CONTROL_MASK;
}

inline bool IsAltPressed()
{
    return GetKeyboardState() & Gdk::MOD1_MASK;
}

void Editor::Kit::on_drag_data_received(const RefPtr<Gdk::DragContext>& context, int x, int y, 
                                        const Gtk::SelectionData& selection_data, guint info, guint time)
{
    CheckSelFormat(selection_data);
    if(  selection_data.get_target() == Project::MediaItemDnDTVType() )
    {
        typedef Gtkmm2ext::SerializedObjectPointers<Project::MediaItem> SOPType;
        SOPType& dat = GetSOP<Project::MediaItem>(selection_data);

        if( dat.data.size() == 1 ) // только если выделен один объект
        {
            Project::MediaItem mi = *dat.data.begin();
            if( IsControlPressed() )
            {
                if( !IsMenu(mi) )
                    SetBackgroundLink(mi); // меняем фон
            }
            else
            {
                int pos = GetObjectAtPos(*this, Point(x, y));
                if( pos == -1 )
                {
                    // добавляем новый 
                    const Planed::Transition tr = Transition();
                    Point center = tr.RelToAbs(tr.DevToRel(Point(x, y)));
                    Editor::AddFTOItem(*this, center, mi);
                }
                else
                {
                    // меняем ссылку/постер
                    SetLinkForObject(*this, mi, pos, IsAltPressed());
                }
            }
        }
    }

    return MyParent::on_drag_data_received(context, x, y, selection_data, info, time);
}

static void DrawDndFrame(RGBA::Drawer* drw, const Rect& dnd_rct)
{
    Rect rct(dnd_rct);
    // обводим внутри
    rct.rgt -= 1;
    rct.btm -= 1;
    if( !rct.IsValid() )
        return;

    drw->SetForegroundColor(BLUE_CLR);
    drw->MoveTo(rct.lft, rct.top);
    drw->FrameRectTo(rct.rgt, rct.btm);
}

static void DrawRect(RGBA::Drawer* drw, const Rect& rct, const int clr)
{
    drw->SetForegroundColor(clr);
    drw->MoveTo(rct.lft, rct.top);
    drw->FrameRectTo(rct.rgt, rct.btm);
}

void DrawSafeArea(RGBA::Drawer* drw, MEditorArea& edt_area)
{
    Rect frm_rct = edt_area.FrameRect();
    // с каждой стороны убираем 4,7%
    const double safe_size = 1 - 0.047*2;

    Rect rct = frm_rct;
    rct.SetWidth(Round(frm_rct.Width()*safe_size));
    rct.SetHeight(Round(frm_rct.Height()*safe_size));

    rct = CenterRect(rct, frm_rct, true, true);

    const int SA_BLUE = 0x3465a4ff; // цвет как у иконки :)
    DrawRect(drw, rct+Point(2, 2), SA_BLUE);
    DrawRect(drw, rct, WHITE_CLR);
}

typedef Gtk::ToggleButton Editor::Toolbar::* ToolbarBoolAttr;

static bool IsBAEnabled(MEditorArea& edt_area, ToolbarBoolAttr tba)
{
    Gtk::ToggleButton& tb = edt_area.Toolbar().*tba;
    return tb.get_active();
}

Point MenuSize(MenuRegion& mr)
{
    return mr.GetParams().Size();
}

void
ge_hsb_from_color(const CR::Color *color, 
                  gdouble *hue,
                  gdouble *saturation,
                  gdouble *brightness);
void
ge_color_from_hsb(gdouble hue, 
                  gdouble saturation,
                  gdouble brightness, 
                  CR::Color *color);

static void DrawInvertPixel(RGBA::Drawer* drw, RefPtr<Gdk::Pixbuf> canv_pix, const Point& p)
{
    if( canv_pix && PixbufBounds(canv_pix).Contains(p) )
    {
        double h,s,b;
        RGBA::Pixel pxl = RGBA::GetPixel(canv_pix, p);
        //clr.red   = RGBA::Pixel::MaxClr - clr.red;
        //clr.green = RGBA::Pixel::MaxClr - clr.green;
        //clr.blue  = RGBA::Pixel::MaxClr - clr.blue;
        CR::Color clr(pxl);
        ge_hsb_from_color(&clr, &h, &s, &b);
        h = h < 180 ? h + 180 : h - 180 ;
        // лучше других вариантов (s, 1-s), потому что наибольший контраст +
        // любой цвет поменяется (если изначальный был с макс. контрастом (это не серый/белый/черный) и b=0.5, то
        // будет хорошо видна разница по h=цвету)
        s = 1.0;
        b = 1.0 - b;
        ge_color_from_hsb(h, s, b, &clr);
        drw->SetForegroundColor(ColorToPixel(clr));
    }
    drw->MoveTo(p.x, p.y);
    drw->LineTo(p.x+1, p.y);
}

void DrawGrid(RGBA::Drawer* drw, MEditorArea& edt_area)
{
    // :TRICKY: понимаем, что решетка будет не квадратной, тем более для 16:9,
    // но в исходных координатах считать правильнее (а у нас они - 720xfull)
    MenuRegion& m_rgn = edt_area.CurMenuRegion();
    Point sz(MenuSize(m_rgn));
    const Planed::Transition& trans = m_rgn.Transition();
    
    // :KLUDGE: при каждой отрисовке вся сетка пересчитывается, неоптимально
    //drw->SetForegroundColor(WHITE_CLR);
    if( RGBA::RgnPixelDrawer* r_drw = dynamic_cast<RGBA::RgnPixelDrawer*>(drw) )
    {    
        RefPtr<Gdk::Pixbuf> canv_pix = r_drw->Canvas();
        for( int x = 0; x <= sz.x; x += GRID_STEP )
            for( int y = 0; y <= sz.y; y += GRID_STEP )
            {
                Point p = trans.AbsToRel(Point(x, y));
                //drw->MoveTo(p.x-1, p.y-1);
                //drw->RectTo(p.x+1, p.y+1);
                // каждый пиксел надо отдельно рассчитывать (цвет), потому что иначе
                // частичная перерисовка вернет для не p оригинальный цвет
                DrawInvertPixel(drw, canv_pix, Point(p.x-1, p.y-1));
                DrawInvertPixel(drw, canv_pix, Point(p.x, p.y-1));
                DrawInvertPixel(drw, canv_pix, Point(p.x-1, p.y));
                DrawInvertPixel(drw, canv_pix, p);
            }
    }
    else
    {    
        // оптимизация - лучше все перерисовать
        drw->MoveTo(0, 0);
        Point edt_sz(edt_area.Size());
        drw->RectTo(edt_sz.x, edt_sz.y);
    }
}

bool IsSnapToGrid(MEditorArea& edt_area)
{
    return IsBAEnabled(edt_area, &Editor::Toolbar::gridBtn);
}

void RenderEditor(MEditorArea& edt_area, RectListRgn& rct_lst)
{
    RenderVis r_vis(edt_area.SelArr(), rct_lst);
    edt_area.CurMenuRegion().Accept(r_vis);

    RGBA::Drawer* drw = &r_vis.GetDrawer(); 
    DrawDndFrame(drw, edt_area.DndSelFrame());

    if( IsBAEnabled(edt_area, &Editor::Toolbar::frmBtn) )
        DrawSafeArea(drw, edt_area);
    if( IsSnapToGrid(edt_area) )
        DrawGrid(drw, edt_area);
}

void CalcRgnForRedraw(MEditorArea& edt_area, const DrawerFnr& fnr)
{
    RectListRgn rct_lst;
    RGBA::RectListDrawer lst_drawer(rct_lst);

    fnr(&lst_drawer, edt_area);

    RenderForRegion(edt_area, rct_lst);
}

void Editor::Kit::RecalcForDndFrame(RGBA::Drawer* lst_drawer, const Rect& dnd_rct)
{
    DrawDndFrame(lst_drawer, dndSelFrame);
    DrawDndFrame(lst_drawer, dnd_rct);

    // *
    dndSelFrame = dnd_rct;
}

void Editor::Kit::SetDndFrame(const Rect& dnd_rct)
{
    if( dndSelFrame != dnd_rct ) // нужна перерисовка
        CalcRgnForRedraw(*this, bb::bind(&Editor::Kit::RecalcForDndFrame, this, _1, dnd_rct));
}

void Editor::Kit::on_drag_leave(const RefPtr<Gdk::DragContext>& context, guint time)
{
    SetDndFrame(Rect());
    return MyParent::on_drag_leave(context, time);
}

bool Editor::Kit::on_drag_motion(const RefPtr<Gdk::DragContext>& context, int x, int y, guint time)
{
    // :TODO: исправить ошибку с вызовом Widget::drag_get_data()
    // и перенести код в Editor::Kit::on_drag_data_received()

    Rect frame_rct = FramePlacement();
    if( !frame_rct.Contains(Point(x, y)) )
    {
        SetDndFrame(Rect());

        // возвращения одного false не хватает почему-то
        context->drag_status((Gdk::DragAction)0, time);
        return false;
    }

    Rect dnd_rct;
    if( IsControlPressed() )
        dnd_rct = Rect0Sz(frame_rct.Size());
    else
    {
        int pos = GetObjectAtPos(*this, Point(x, y));
        if( pos != -1 )
            if( Comp::MediaObj* obj = dynamic_cast<Comp::MediaObj*>(CurMenuRegion().List()[pos]) )
                dnd_rct = Planed::AbsToRel(Transition(), obj->Placement());
    }
    SetDndFrame(dnd_rct);

    return MyParent::on_drag_motion(context, x, y, time);
}

