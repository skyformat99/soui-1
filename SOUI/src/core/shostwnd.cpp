#include "souistd.h"

#include "SApp.h"
#include "core/shostwnd.h"
#include "core/mybuffer.h"
#include "helper/STipCtrl.h"
#include "helper/color.h"

namespace SOUI
{

#define TIMER_CARET    1
#define TIMER_NEXTFRAME 2

//////////////////////////////////////////////////////////////////////////
// SHostWnd
//////////////////////////////////////////////////////////////////////////
SHostWnd::SHostWnd( LPCTSTR pszResName /*= NULL*/ )
: SwndContainerImpl(this)
, m_strXmlLayout(pszResName)
, m_bTrackFlag(FALSE)
, m_bCaretShowing(FALSE)
, m_bCaretActive(FALSE)
, m_bNeedRepaint(FALSE)
, m_bNeedAllRepaint(TRUE)
, m_pTipCtrl(NULL)
, m_dummyWnd(this)
{
    SetContainer(this);
}

SHostWnd::~SHostWnd()
{
}

HWND SHostWnd::Create(HWND hWndParent,DWORD dwStyle,DWORD dwExStyle, int x, int y, int nWidth, int nHeight)
{
    if (NULL != m_hWnd)
        return m_hWnd;

    HWND hWnd = CSimpleWnd::Create(L"HOSTWND",dwStyle,dwExStyle, x,y,nWidth,nHeight,hWndParent,NULL);
    if(!hWnd) return NULL;

    //tooltip
    m_pTipCtrl=new STipCtrl;
    m_pTipCtrl->Create(m_hWnd);

    SetContainer(this);

    if(!m_strXmlLayout.IsEmpty())  SetXml(m_strXmlLayout);

    if(nWidth==0 || nHeight==0) CenterWindow(hWnd);
    return hWnd;
}

HWND SHostWnd::Create(HWND hWndParent,int x,int y,int nWidth,int nHeight)
{
    return Create(hWndParent, WS_POPUP | WS_CLIPCHILDREN | WS_TABSTOP,0,x,y,nWidth,nHeight);
}

BOOL SHostWnd::SetXml(LPCTSTR pszXmlName)
{
    pugi::xml_document xmlDoc;
    if(!LOADXML(xmlDoc,pszXmlName,RT_LAYOUT)) return FALSE;

    return InitFromXml(xmlDoc.child(L"SOUI"));
}

BOOL SHostWnd::SetXml(LPCWSTR lpszXml,int nLen)
{
    pugi::xml_document xmlDoc;
    if(!xmlDoc.load_buffer(lpszXml,nLen,pugi::parse_default,pugi::encoding_utf16)) return FALSE;
 
    return InitFromXml(xmlDoc.child(L"SOUI"));
}

BOOL SHostWnd::InitFromXml(pugi::xml_node xmlNode )
{
    if(!xmlNode) return FALSE;
    
    m_hostAttr.FreeOwnedSkins();

    DWORD dwStyle =CSimpleWnd::GetStyle();
    DWORD dwExStyle  = CSimpleWnd::GetExStyle();
    
    SHostWndAttr hostAttr;
    hostAttr.InitFromXml(xmlNode);
    m_hostAttr=hostAttr;
    m_hostAttr.LoadOwnedSkins();

    if (m_hostAttr.m_bResizable)
    {
        dwStyle |= WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME;
    }
    if(m_hostAttr.m_bAppWnd)
    {
        dwStyle |= WS_SYSMENU;
        dwExStyle |= WS_EX_APPWINDOW;
    }else if(m_hostAttr.m_bToolWnd)
    {
        dwExStyle |= WS_EX_TOOLWINDOW;
    }
    if(m_hostAttr.m_bTranslucent)
    {
        dwExStyle |= WS_EX_LAYERED;
    }
    
    if(m_hostAttr.m_dwStyle!=0) dwStyle=m_hostAttr.m_dwStyle;
    if(m_hostAttr.m_dwExStyle != 0) dwExStyle =m_hostAttr.m_dwExStyle;
    
    ModifyStyle(0,dwStyle);
    ModifyStyleEx(0,dwExStyle);
    CSimpleWnd::SetWindowTextW(m_hostAttr.m_strTitle);
    
    if(m_hostAttr.m_bTranslucent)
    {
        SetWindowLongPtr(GWL_EXSTYLE, GetWindowLongPtr(GWL_EXSTYLE) | WS_EX_LAYERED);
        m_dummyWnd.Create(_T("SOUI_DUMMY_WND"),WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,0,0,10,10,m_hWnd,NULL);
        m_dummyWnd.SetWindowLongPtr(GWL_EXSTYLE,m_dummyWnd.GetWindowLongPtr(GWL_EXSTYLE) | WS_EX_LAYERED);
        ::SetLayeredWindowAttributes(m_dummyWnd.m_hWnd,0,0,LWA_ALPHA);
        m_dummyWnd.ShowWindow(SW_SHOWNOACTIVATE);
    }
    

    SWindow::InitFromXml(xmlNode.child(L"root"));

    CRect rcClient;
    GetClientRect(&rcClient);
    if(rcClient.IsRectEmpty())//APPû��ָ�����ڴ�С��ʹ��XML�е�ֵ
    {
        SetWindowPos(NULL,0,0,m_hostAttr.m_szInit.cx,m_hostAttr.m_szInit.cy,SWP_NOZORDER|SWP_NOMOVE);
    }else
    {
        Move(&rcClient);
        OnWindowPosChanged(NULL);
    }

    _Redraw();

    RedrawRegion(m_memRT,m_rgnInvalidate);

    return TRUE;
}

void SHostWnd::_Redraw()
{
    m_bNeedAllRepaint = TRUE;
    m_bNeedRepaint = TRUE;
    m_rgnInvalidate->Clear();

    if(!m_hostAttr.m_bTranslucent)
        CSimpleWnd::Invalidate(FALSE);
    else if(m_dummyWnd.IsWindow()) 
        m_dummyWnd.Invalidate(FALSE);
}

void SHostWnd::OnPrint(CDCHandle dc, UINT uFlags)
{
    if((m_hostAttr.m_bTranslucent && !uFlags) && !m_bNeedAllRepaint && !m_bNeedRepaint) return;
    if (m_bNeedAllRepaint)
    {
        m_rgnInvalidate->Clear();
        m_bNeedAllRepaint = FALSE;
        m_bNeedRepaint=TRUE;
    }


    if (m_bNeedRepaint)
    {
        SThreadActiveWndMgr::EnterPaintLock();
        CAutoRefPtr<IFont> defFont,oldFont;
        defFont = SFontPool::getSingleton().GetFont(FF_DEFAULTFONT);
        m_memRT->SelectObject(defFont,(IRenderObj**)&oldFont);
        m_memRT->SetTextColor(RGBA(0,0,0,0xFF));

        //m_rgnInvalidate�п�����RedrawRegionʱ���޸ģ���������һ����ʱ���������
        CAutoRefPtr<IRegion> pRgnUpdate=m_rgnInvalidate;
        m_rgnInvalidate=NULL;
        GETRENDERFACTORY->CreateRegion(&m_rgnInvalidate);

        CRect rcInvalid=m_rcWindow;
        if (!pRgnUpdate->IsEmpty())
        {
            m_memRT->PushClipRegion(pRgnUpdate,RGN_COPY);
            pRgnUpdate->GetRgnBox(&rcInvalid);
        }else
        {
            m_memRT->PushClipRect(&rcInvalid,RGN_COPY);
        }
        //���������alphaֵ
        HDC hdc=m_memRT->GetDC();
        HBRUSH hbr=::CreateSolidBrush(0);
        ::FillRect(hdc,&rcInvalid,hbr);
        m_memRT->ReleaseDC(hdc);

        if(m_bCaretActive) DrawCaret(m_ptCaret);//clear old caret 
        RedrawRegion(m_memRT, pRgnUpdate);
        if(m_bCaretActive) DrawCaret(m_ptCaret);//redraw caret 
        
        m_memRT->PopClip();
        
        m_memRT->SelectObject(oldFont);

        m_bNeedRepaint = FALSE;
        SThreadActiveWndMgr::LeavePaintLock();
    }

    CRect rc;
    GetClientRect(&rc);
    UpdateHost(dc,rc);
}

void SHostWnd::OnPaint(CDCHandle dc)
{
    CPaintDC dc1(m_hWnd);
    OnPrint(m_hostAttr.m_bTranslucent?NULL:dc1.m_hDC, 0);
}

BOOL SHostWnd::OnEraseBkgnd(CDCHandle dc)
{
    return TRUE;
}


int SHostWnd::OnCreate( LPCREATESTRUCT lpCreateStruct )
{
    GETRENDERFACTORY->CreateRenderTarget(&m_memRT,0,0);
    GETRENDERFACTORY->CreateRegion(&m_rgnInvalidate);
    return 0;
}

void SHostWnd::OnDestroy()
{
    SWindow::SendSwndMessage(WM_DESTROY);

    if(m_pTipCtrl)
    {
        if (m_pTipCtrl->IsWindow())
            m_pTipCtrl->DestroyWindow();
        delete m_pTipCtrl;
    }
    if(m_hostAttr.m_bTranslucent && m_dummyWnd.IsWindow())
    {
        m_dummyWnd.DestroyWindow();
    }
    m_hostAttr.FreeOwnedSkins();
}

void SHostWnd::OnSize(UINT nType, CSize size)
{
    if(IsIconic()) return;

    if (size.cx==0 || size.cy==0)
        return;
    
    m_memRT->Resize(size);

    CRect rcClient;
    GetClientRect(rcClient);

    Move(rcClient);

    _Redraw();

    SetMsgHandled(FALSE);//����������������������
}

void SHostWnd::OnMouseMove(UINT nFlags, CPoint point)
{
    if (!m_bTrackFlag)
    {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.hwndTrack = m_hWnd;
        tme.dwFlags = TME_LEAVE;
        tme.dwHoverTime = 0;
        m_bTrackFlag = TrackMouseEvent(&tme);
    }

    OnMouseEvent(WM_MOUSEMOVE,nFlags,MAKELPARAM(point.x,point.y));
}

void SHostWnd::OnMouseLeave()
{
    m_bTrackFlag = FALSE;
    DoFrameEvent(WM_MOUSELEAVE,0,0);
}

void SHostWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
    DoFrameEvent(WM_LBUTTONDOWN,nFlags,MAKELPARAM(point.x,point.y));
}

void SHostWnd::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    DoFrameEvent(WM_LBUTTONDBLCLK,nFlags,MAKELPARAM(point.x,point.y));
}

BOOL SHostWnd::OnSetCursor(HWND hwnd, UINT nHitTest, UINT message)
{
    if(hwnd!=m_hWnd) return FALSE;
    if(nHitTest==HTCLIENT)
    {
        CPoint pt;
        GetCursorPos(&pt);
        ScreenToClient(&pt);
        return DoFrameEvent(WM_SETCURSOR,0,MAKELPARAM(pt.x,pt.y))!=0;
    }
    return DefWindowProc()!=0;
}

void SHostWnd::OnTimer(UINT_PTR idEvent)
{
    STimerID sTimerID((DWORD)idEvent);
    if(sTimerID.bSwndTimer)
    {
        SWindow *pSwnd=SWindowMgr::GetWindow((SWND)sTimerID.Swnd);
        if(pSwnd)
        {
            if(pSwnd==this) OnSwndTimer(sTimerID.uTimerID);//����DUIWIN������ATLһ�µ���Ϣӳ���ģʽ�������HOST�в�����DUI����Ϣӳ������ظ��ᵼ��SetMsgHandled����)
            else pSwnd->SendSwndMessage(WM_TIMER,sTimerID.uTimerID,0);
        }
        else
        {
            //�����Ѿ�ɾ�����Զ�����ô��ڵĶ�ʱ��
            ::KillTimer(m_hWnd,idEvent);
        }
    }
    else
    {
        SetMsgHandled(FALSE);
    }
}

void SHostWnd::OnSwndTimer( char cTimerID )
{
    if(cTimerID==TIMER_CARET)
    {
        ASSERT(m_bCaretShowing);
        DrawCaret(m_ptCaret);
        m_bCaretActive=!m_bCaretActive;
    }else if(cTimerID==TIMER_NEXTFRAME)
    {
        OnNextFrame();
    }
}

void SHostWnd::DrawCaret(CPoint pt)
{
    CAutoRefPtr<IRenderTarget> pRTCaret;
    GETRENDERFACTORY->CreateRenderTarget(&pRTCaret,0,0);
    pRTCaret->SelectObject(m_bmpCaret);

    CRect rcCaret(pt,m_szCaret);
    CRect rcShowCaret;
    rcShowCaret.IntersectRect(m_rcValidateCaret,rcCaret);
    
    m_memRT->BitBlt(&rcShowCaret,pRTCaret,rcShowCaret.left-pt.x,rcShowCaret.top-pt.y,DSTINVERT);
    
    if(!m_hostAttr.m_bTranslucent)
    {
        CSimpleWnd::InvalidateRect(rcCaret, FALSE);
    }else
    {
        if(m_dummyWnd.IsWindow()) 
            m_dummyWnd.Invalidate(FALSE);
    }
}

LRESULT SHostWnd::OnMouseEvent(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    DoFrameEvent(uMsg,wParam,lParam);    //�������Ϣת����SWindow����

    if(m_pTipCtrl && m_pTipCtrl->IsWindow())
    {
        SWindow *pHover=SWindowMgr::GetWindow(m_hHover);
        if(!pHover || pHover->IsDisabled(TRUE))
        {
            m_pTipCtrl->ShowTip(FALSE);
        }
        else
        {
            SWND hNewTipHost=0;
            CRect rcTip;
            SStringT strTip;
            BOOL bUpdate=pHover->OnUpdateToolTip(m_pTipCtrl->m_dwHostID,hNewTipHost,rcTip,strTip);
            if(bUpdate)
            {
                m_pTipCtrl->m_dwHostID=hNewTipHost;
                m_pTipCtrl->UpdateTip(rcTip,strTip);
            }
        }
    }

    return 0;
}

LRESULT SHostWnd::OnKeyEvent(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if(uMsg==WM_SYSKEYDOWN || uMsg==WM_SYSKEYUP)
    {
        SWindow *pFocus=SWindowMgr::GetWindow(m_focusMgr.GetFocusedHwnd());
        if(!pFocus  || !(pFocus->OnGetDlgCode()&SC_WANTSYSKEY))
        {
            SetMsgHandled(FALSE);
            return 0;
        }
    }
    LRESULT lRet = DoFrameEvent(uMsg,wParam,lParam);
    SetMsgHandled(SWindow::IsMsgHandled());
    return lRet;
}

BOOL SHostWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    ScreenToClient(&pt);
    return DoFrameEvent(WM_MOUSEWHEEL,MAKEWPARAM(nFlags,zDelta),MAKELPARAM(pt.x,pt.y))!=0;
}


void SHostWnd::OnActivate( UINT nState, BOOL bMinimized, HWND wndOther )
{
    if(nState==WA_ACTIVE)
        ::SetFocus(m_hWnd);
    else
        ::SetFocus(NULL);
}

BOOL SHostWnd::OnFireEvent(EventArgs &evt)
{
    BOOL bRet=FALSE;
    if(evt.GetEventID()>=EVT_INTERNAL_FIRST && evt.GetEventID()<=EVT_INTERNAL_LAST)
    {
        bRet=TRUE;
        switch(evt.GetEventID())
        {
        case EVT_REALWND_CREATE:
            {
                EventRealWndCreate * pEvt = (EventRealWndCreate *)&evt;
                pEvt->hWndCreated = OnRealWndCreate((SRealWnd*)pEvt->sender);
            }
            break;
        case EVT_REALWND_INIT:
            {
                EventRealWndInit * pEvt = (EventRealWndInit *)&evt;
                pEvt->bSetFocus=OnRealWndInit((SRealWnd*)pEvt->sender);
            }
            break;
        case EVT_REALWND_DESTROY:
            OnRealWndDestroy((SRealWnd*)evt.sender);
            break;
        case EVT_REALWND_SIZE:
            OnRealWndSize((SRealWnd*)evt.sender);
            break;
        default:
            bRet=FALSE;
        }
    }else
    {
        bRet=_HandleEvent(&evt);
    }
    return bRet;
}

CRect SHostWnd::GetContainerRect()
{
    return m_rcWindow;
}

HWND SHostWnd::GetHostHwnd()
{
    return m_hWnd;
}

IRenderTarget * SHostWnd::OnGetRenderTarget(const CRect & rc,DWORD gdcFlags)
{
    IRenderTarget *pRT=NULL;
    GETRENDERFACTORY->CreateRenderTarget(&pRT,rc.Width(),rc.Height());
    pRT->OffsetViewportOrg(-rc.left,-rc.top);
    
    pRT->SelectObject(SFontPool::getSingleton().GetFont(FF_DEFAULTFONT));
    pRT->SetTextColor(RGBA(0,0,0,0xFF));

    if(!(gdcFlags & OLEDC_NODRAW))
    {
        if(m_bCaretActive)
        {
            DrawCaret(m_ptCaret);//clear old caret
        }
        pRT->BitBlt(&rc,m_memRT,rc.left,rc.top,SRCCOPY);
    }
    return pRT;
}

void SHostWnd::OnReleaseRenderTarget(IRenderTarget * pRT,const CRect &rc,DWORD gdcFlags)
{
    if(!(gdcFlags & OLEDC_NODRAW))
    {
        m_memRT->BitBlt(&rc,pRT,rc.left,rc.top,SRCCOPY);
        if(m_bCaretActive)
        {
            DrawCaret(m_ptCaret);//clear old caret
        }
        CDCHandle dc=GetDC();
        UpdateHost(dc,rc);
        ReleaseDC(dc);
    }
    pRT->Release();
}

void SHostWnd::UpdateHost(CDCHandle dc, const CRect &rcInvalid )
{
    HDC hdc=m_memRT->GetDC(0);
    if(m_hostAttr.m_bTranslucent)
    {
        CRect rc;
        GetWindowRect(&rc);
        BLENDFUNCTION bf= {AC_SRC_OVER,0,0xFF,AC_SRC_ALPHA};
        CDCHandle hdcSrc=::GetDC(NULL);
        UpdateLayeredWindow(hdcSrc,&rc.TopLeft(),&rc.Size(),hdc,&CPoint(0,0),0,&bf,ULW_ALPHA);
        ::ReleaseDC(NULL,hdcSrc);
    }
    else
    {
        dc.BitBlt(rcInvalid.left,rcInvalid.top,rcInvalid.Width(),rcInvalid.Height(),hdc,rcInvalid.left,rcInvalid.top,SRCCOPY);
    }
    m_memRT->ReleaseDC(hdc);
}

void SHostWnd::OnRedraw(const CRect &rc)
{
    if(!IsWindow()) return;
    
    m_rgnInvalidate->CombineRect(&rc,RGN_OR);
    
    m_bNeedRepaint = TRUE;

    if(!m_hostAttr.m_bTranslucent)
    {
        CSimpleWnd::InvalidateRect(rc, FALSE);
    }else
    {
        if(m_dummyWnd.IsWindow()) 
            m_dummyWnd.Invalidate(FALSE);
    }
}

BOOL SHostWnd::OnReleaseSwndCapture()
{
    if(!__super::OnReleaseSwndCapture()) return FALSE;
    ::ReleaseCapture();
    CPoint pt;
    GetCursorPos(&pt);
    ScreenToClient(&pt);
    PostMessage(WM_MOUSEMOVE,0,MAKELPARAM(pt.x,pt.y));
    return TRUE;
}

SWND SHostWnd::OnSetSwndCapture(SWND swnd)
{
    CSimpleWnd::SetCapture();
    return __super::OnSetSwndCapture(swnd);
}

BOOL SHostWnd::IsTranslucent()
{
    return m_hostAttr.m_bTranslucent;
}


BOOL SHostWnd::SwndCreateCaret( HBITMAP hBmp,int nWidth,int nHeight )
{
    ::CreateCaret(m_hWnd,hBmp,nWidth,nHeight);
    if(m_bmpCaret)
    {
        m_bmpCaret=NULL;
    }
    
    CAutoRefPtr<IRenderTarget> pRT;
    GETRENDERFACTORY->CreateRenderTarget(&pRT,nWidth,nHeight);
    m_bmpCaret = (IBitmap*) pRT->GetCurrentObject(OT_BITMAP);
    m_szCaret.cx=nWidth;
    m_szCaret.cy=nHeight;
        
    if(hBmp)
    {
        //�����췽ʽ����һ�������λͼ
        HDC hdc=pRT->GetDC(0);
        HDC hdc2=CreateCompatibleDC(hdc);
        SelectObject(hdc2,hBmp);
        
        BITMAP bm;
        GetObject(hBmp,sizeof(bm),&bm);
        StretchBlt(hdc,0,0,nWidth,nHeight,hdc2,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);
        DeleteDC(hdc2);
        pRT->ReleaseDC(hdc);
    }
    else
    {
        //����һ����ɫ�������λͼ
        pRT->FillSolidRect(&CRect(0,0,nWidth,nHeight),RGBA(0,0,0,0xFF));
    }
    return TRUE;
}

BOOL SHostWnd::SwndShowCaret( BOOL bShow )
{
    m_bCaretShowing=bShow;

    if(bShow)
    {
        SWindow::SetTimer(TIMER_CARET,GetCaretBlinkTime());
        if(!m_bCaretActive)
        {
            DrawCaret(m_ptCaret);
            m_bCaretActive=TRUE;
        }
    }
    else
    {
        SWindow::KillTimer(TIMER_CARET);
        if(m_bCaretActive)
        {
            DrawCaret(m_ptCaret);
        }
        m_bCaretActive=FALSE;
    }
   return TRUE;
}

BOOL SHostWnd::SwndSetCaretPos( int x,int y )
{
    if(!SetCaretPos(x,y)) return FALSE;
    if(m_bCaretShowing && m_bCaretActive)
    {
        //clear old caret
        DrawCaret(m_ptCaret);
    }
    m_ptCaret=CPoint(x,y);
    if(m_bCaretShowing && m_bCaretActive)
    {
        //draw new caret
        DrawCaret(m_ptCaret);
    }
    return TRUE;
}


BOOL SHostWnd::SwndUpdateWindow()
{
    if(m_hostAttr.m_bTranslucent) UpdateWindow(m_dummyWnd.m_hWnd);
    else UpdateWindow(m_hWnd);
    return TRUE;
}

LRESULT SHostWnd::OnNcCalcSize(BOOL bCalcValidRects, LPARAM lParam)
{
    if (bCalcValidRects && (CSimpleWnd::GetStyle() & WS_POPUP))
    {
        CRect rcWindow;
        GetWindowRect(rcWindow);

        LPNCCALCSIZE_PARAMS pParam = (LPNCCALCSIZE_PARAMS)lParam;

        if (SWP_NOSIZE == (SWP_NOSIZE & pParam->lppos->flags))
            return 0;

        if (0 == (SWP_NOMOVE & pParam->lppos->flags))
        {
            rcWindow.left = pParam->lppos->x;
            rcWindow.top = pParam->lppos->y;
        }

        rcWindow.right = rcWindow.left + pParam->lppos->cx;
        rcWindow.bottom = rcWindow.top + pParam->lppos->cy;
        pParam->rgrc[0] = rcWindow;
    }

    return 0;
}

void SHostWnd::OnGetMinMaxInfo(LPMINMAXINFO lpMMI)
{
    HMONITOR hMonitor = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONULL);

    if (hMonitor)
    {
        MONITORINFO mi = {sizeof(MONITORINFO)};
        ::GetMonitorInfo(hMonitor, &mi);

        CRect rcWork = mi.rcWork, rcMonitor = mi.rcMonitor;
        lpMMI->ptMaxPosition.x = abs(rcWork.left - rcMonitor.left) - 1;
        lpMMI->ptMaxPosition.y = abs(rcWork.top - rcMonitor.top) - 1;
        lpMMI->ptMaxSize.x = abs(rcWork.Width()) + 2;
        lpMMI->ptMaxSize.y = abs(rcWork.Height()) + 2;
        lpMMI->ptMaxTrackSize.x = abs(rcWork.Width()) + 2;
        lpMMI->ptMaxTrackSize.y = abs(rcWork.Height()) + 2;
        lpMMI->ptMinTrackSize = CPoint(m_hostAttr.m_szMin.cx, m_hostAttr.m_szMin.cy);
    }
}

BOOL SHostWnd::OnNcActivate(BOOL bActive)
{
    return TRUE;
}

UINT SHostWnd::OnWndNcHitTest(CPoint point)
{
    if (m_hostAttr.m_bResizable)
    {
        ScreenToClient(&point);
        if (point.x > m_rcWindow.right - m_hostAttr.m_rcMargin.right)
        {
            if (point.y > m_rcWindow.bottom - m_hostAttr.m_rcMargin.bottom)
            {
                return HTBOTTOMRIGHT;
            }
            else if (point.y < m_hostAttr.m_rcMargin.top)
            {
                return HTTOPRIGHT;
            }
            return HTRIGHT;
        }
        else if (point.x < m_hostAttr.m_rcMargin.left)
        {
            if (point.y > m_rcWindow.bottom - m_hostAttr.m_rcMargin.bottom)
            {
                return HTBOTTOMLEFT;
            }
            else if (point.y < m_hostAttr.m_rcMargin.top)
            {
                return HTTOPLEFT;
            }
            return HTLEFT;
        }
        else if (point.y > m_rcWindow.bottom - m_hostAttr.m_rcMargin.bottom)
        {
            return HTBOTTOM;
        }
        else if (point.y < m_hostAttr.m_rcMargin.top)
        {
            return HTTOP;
        }
    }
    return HTCLIENT;
}


//////////////////////////////////////////////////////////////////////////
// IRealWndHandler
HWND SHostWnd::OnRealWndCreate(SRealWnd *pRealWnd)
{
    CRect rcWindow;
    UINT uCmdID=pRealWnd->GetID();
    pRealWnd->GetRect(&rcWindow);

    const SRealWndParam & paramRealWnd=pRealWnd->GetRealWndParam();
    return CreateWindowEx(paramRealWnd.m_dwExStyle,paramRealWnd.m_strClassName,paramRealWnd.m_strWindowName,paramRealWnd.m_dwStyle,
                          rcWindow.left,rcWindow.top,rcWindow.Width(),rcWindow.Height(),
                          m_hWnd,(HMENU)(ULONG_PTR)uCmdID,0,NULL);
}

BOOL SHostWnd::OnRealWndInit( SRealWnd *pRealWnd )
{
    return FALSE;
}

void SHostWnd::OnRealWndDestroy(SRealWnd *pRealWnd)
{
    if(::IsWindow(pRealWnd->GetRealHwnd(FALSE)))
    {
        ::DestroyWindow(pRealWnd->GetRealHwnd(FALSE));
    }
    if(pRealWnd->GetData())
    {
        delete pRealWnd->GetData();
        pRealWnd->SetData(0);
    }
}


void SHostWnd::OnRealWndSize( SRealWnd *pRealWnd )
{
    if(::IsWindow(pRealWnd->GetRealHwnd(FALSE)))
    {
        CRect rcClient;
        pRealWnd->GetClient(&rcClient);
        ::SetWindowPos(pRealWnd->GetRealHwnd(FALSE),0, rcClient.left, rcClient.top, rcClient.Width(), rcClient.Height(), SWP_NOZORDER);
    }
}

void SHostWnd::OnSetFocus( HWND wndOld )
{
    DoFrameEvent(WM_SETFOCUS,0,0);
}

void SHostWnd::OnKillFocus( HWND wndFocus )
{
    DoFrameEvent(WM_KILLFOCUS,0,0);
}

void SHostWnd::UpdateLayerFromRenderTarget(IRenderTarget *pRT,BYTE byAlpha)
{
    ASSERT(IsTranslucent());
    HDC hdc=pRT->GetDC(0);
    CRect rc;
    GetWindowRect(&rc);
    BLENDFUNCTION bf= {AC_SRC_OVER,0,byAlpha,AC_SRC_ALPHA};
    CDCHandle dc=GetDC();
    UpdateLayeredWindow(dc,&rc.TopLeft(),&rc.Size(),hdc,&CPoint(0,0),0,&bf,ULW_ALPHA);
    ReleaseDC(dc);
    pRT->ReleaseDC(hdc);
}
/*
BOOL _BitBlt(IRenderTarget *pRTDst,IRenderTarget * pRTSrc,CRect rcDst,CPoint ptSrc)
{
    return S_OK == pRTDst->BitBlt(&rcDst,pRTSrc,ptSrc.x,ptSrc.y,SRCCOPY);
}
*/
BOOL _BitBlt(IRenderTarget *pRTDst,IRenderTarget * pRTSrc,CRect rcDst,CPoint ptSrc)
{
    HDC dcSrc=pRTSrc->GetDC();
    HDC dcDst=pRTDst->GetDC();
    ::BitBlt(dcDst,rcDst.left,rcDst.top,rcDst.Width(),rcDst.Height(),dcSrc,ptSrc.x,ptSrc.y,SRCCOPY);
    pRTDst->ReleaseDC(dcDst);
    pRTSrc->ReleaseDC(dcSrc);
    return TRUE;
}


BOOL SHostWnd::AnimateHostWindow(DWORD dwTime,DWORD dwFlags)
{
    if(!IsTranslucent())
    {
        return ::AnimateWindow(m_hWnd,dwTime,dwFlags);
    }else
    {
        CRect rcWnd;//���ھ���
        GetClientRect(&rcWnd);
        CRect rcShow(rcWnd);//���������пɼ�����
        
        CAutoRefPtr<IRenderTarget> pRT;
        GETRENDERFACTORY->CreateRenderTarget(&pRT,rcShow.Width(),rcShow.Height());

        int nSteps=dwTime/10;
        if(dwFlags & AW_HIDE)
        {
            if(dwFlags& AW_SLIDE)
            {
                LONG  x1 = rcShow.left;
                LONG  x2 = rcShow.left;
                LONG  y1 = rcShow.top;
                LONG  y2 = rcShow.top;
                LONG * x =&rcShow.left;
                LONG * y =&rcShow.top;

                if(dwFlags & AW_HOR_POSITIVE)
                {//left->right:move left
                    x1=rcShow.left,x2=rcShow.right;
                    x=&rcShow.left;
                }else if(dwFlags & AW_HOR_NEGATIVE)
                {//right->left:move right
                    x1=rcShow.right,x2=rcShow.left;
                    x=&rcShow.right;
                }
                if(dwFlags & AW_VER_POSITIVE)
                {//top->bottom
                    y1=rcShow.top,y2=rcShow.bottom;
                    y=&rcShow.top;
                }else if(dwFlags & AW_VER_NEGATIVE)
                {//bottom->top
                    y1=rcShow.bottom,y2=rcShow.top;
                    y=&rcShow.bottom;
                }
                LONG xStepLen=(x2-x1)/nSteps;
                LONG yStepLen=(y2-y1)/nSteps;

                for(int i=0;i<nSteps;i++)
                {
                    *x+=xStepLen;
                    *y+=yStepLen;
                    pRT->Clear();
                    CPoint ptAnchor;
                    if(dwFlags & AW_VER_NEGATIVE)
                        ptAnchor.y=rcWnd.bottom-rcShow.Height();
                    if(dwFlags & AW_HOR_NEGATIVE)
                        ptAnchor.x=rcWnd.right-rcShow.Width();
                    _BitBlt(pRT,m_memRT,rcShow,ptAnchor);
                    UpdateLayerFromRenderTarget(pRT,0xFF);
                    Sleep(10);
                }
                ShowWindow(SW_HIDE);
                return TRUE;
            }else if(dwFlags&AW_CENTER)
            {
                int xStep=rcShow.Width()/(2*nSteps);
                int yStep=rcShow.Height()/(2*nSteps);
                for(int i=0;i<nSteps;i++)
                {
                    rcShow.DeflateRect(xStep,yStep);
                    pRT->Clear();
                    _BitBlt(pRT,m_memRT,rcShow,rcShow.TopLeft());
                    UpdateLayerFromRenderTarget(pRT,0xFF);
                    Sleep(10);
                }
                ShowWindow(SW_HIDE);
                return TRUE;
            }else if(dwFlags&AW_BLEND)
            {
                BYTE byAlpha=255;
                for(int i=0;i<nSteps;i++)
                {
                    byAlpha-=255/nSteps;
                    UpdateLayerFromRenderTarget(m_memRT,byAlpha);
                    Sleep(10);
                }
                ShowWindow(SW_HIDE);
                return TRUE;
            }
            return FALSE;
        }else
        {
            if(!IsWindowVisible())
            {
                SetWindowPos(0,0,0,0,0,SWP_SHOWWINDOW|SWP_NOMOVE|SWP_NOZORDER|SWP_NOSIZE);
            }
            SetWindowPos(HWND_TOP,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE);
            if(dwFlags& AW_SLIDE)
            {
                LONG  x1 = rcShow.left;
                LONG  x2 = rcShow.left;
                LONG  y1 = rcShow.top;
                LONG  y2 = rcShow.top;
                LONG * x =&rcShow.left;
                LONG * y =&rcShow.top;
                
                if(dwFlags & AW_HOR_POSITIVE)
                {//left->right:move right
                    x1=rcShow.left,x2=rcShow.right;
                    rcShow.right=rcShow.left,x=&rcShow.right;
                }else if(dwFlags & AW_HOR_NEGATIVE)
                {//right->left:move left
                    x1=rcShow.right,x2=rcShow.left;
                    rcShow.left=rcShow.right,x=&rcShow.left;
                }
                if(dwFlags & AW_VER_POSITIVE)
                {//top->bottom
                    y1=rcShow.top,y2=rcShow.bottom;
                    rcShow.bottom=rcShow.top,y=&rcShow.bottom;
                }else if(dwFlags & AW_VER_NEGATIVE)
                {//bottom->top
                    y1=rcShow.bottom,y2=rcShow.top;
                    rcShow.top=rcShow.bottom,y=&rcShow.top;
                }
                LONG xStepLen=(x2-x1)/nSteps;
                LONG yStepLen=(y2-y1)/nSteps;
                
                for(int i=0;i<nSteps;i++)
                {
                    *x+=xStepLen;
                    *y+=yStepLen;
                    pRT->Clear();
                    CPoint ptAnchor;
                    if(dwFlags & AW_VER_POSITIVE)
                        ptAnchor.y=rcWnd.bottom-rcShow.Height();
                    if(dwFlags & AW_HOR_POSITIVE)
                        ptAnchor.x=rcWnd.right-rcShow.Width();
                     _BitBlt(pRT,m_memRT,rcShow,ptAnchor);
                    UpdateLayerFromRenderTarget(pRT,0xFF);
                    Sleep(10);
                }
                UpdateLayerFromRenderTarget(m_memRT,0xFF);
                return TRUE;
            }else if(dwFlags&AW_CENTER)
            {
                int xStep=rcShow.Width()/(2*nSteps);
                int yStep=rcShow.Height()/(2*nSteps);
                rcShow.left=rcShow.right=(rcShow.left+rcShow.right)/2;
                rcShow.top=rcShow.bottom=(rcShow.top+rcShow.bottom)/2;
                for(int i=0;i<nSteps;i++)
                {
                    rcShow.InflateRect(xStep,yStep);
                    pRT->Clear();
                    _BitBlt(pRT,m_memRT,rcShow,rcShow.TopLeft());
                    UpdateLayerFromRenderTarget(pRT,0xFF);
                    Sleep(10);
                }
                UpdateLayerFromRenderTarget(m_memRT,0xFF);
                return TRUE;
            }else if(dwFlags&AW_BLEND)
            {
                BYTE byAlpha=0;
                for(int i=0;i<nSteps;i++)
                {
                    byAlpha+=255/nSteps;
                    UpdateLayerFromRenderTarget(m_memRT,byAlpha);
                    Sleep(10);
                }
                UpdateLayerFromRenderTarget(m_memRT,255);
                return TRUE;
            }
        }
        return FALSE;
    }
}

void SHostWnd::OnSetCaretValidateRect( LPCRECT lpRect )
{
    m_rcValidateCaret=lpRect;
}

BOOL SHostWnd::RegisterTimelineHandler( ITimelineHandler *pHandler )
{
    BOOL bRet = SwndContainerImpl::RegisterTimelineHandler(pHandler);
    if(bRet && m_lstTimelineHandler.GetCount()==1) SWindow::SetTimer(TIMER_NEXTFRAME,10);
    return bRet;
}

BOOL SHostWnd::UnregisterTimelineHandler( ITimelineHandler *pHandler )
{
    BOOL bRet=SwndContainerImpl::UnregisterTimelineHandler(pHandler);
    if(bRet && m_lstTimelineHandler.IsEmpty()) SWindow::KillTimer(TIMER_NEXTFRAME);
    return bRet;
}

//////////////////////////////////////////////////////////////////////////
//    CTranslucentHostWnd
//////////////////////////////////////////////////////////////////////////
void SDummyWnd::OnPaint( CDCHandle dc )
{
    CPaintDC dc1(m_hWnd);
    m_pOwner->OnPrint(NULL,1);
}

}//namespace SOUI