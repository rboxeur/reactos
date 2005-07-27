/*
 *  ReactOS W32 Subsystem
 *  Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Windows
 * FILE:             subsys/win32k/ntuser/window.c
 * PROGRAMER:        Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISION HISTORY:
 *       06-06-2001  CSH  Created
 */

/* INCLUDES ******************************************************************/

#include <w32k.h>

//#define NDEBUG
#include <debug.h>

static WndProcHandle *WndProcHandlesArray = 0;
static WORD WndProcHandlesArraySize = 0;
#define WPH_SIZE 0x40 /* the size to add to the WndProcHandle array each time */

/* dialog resources appear to pass this in 16 bits, handle them properly */
#define CW_USEDEFAULT16	(0x8000)

#define POINT_IN_RECT(p, r) (((r.bottom >= p.y) && (r.top <= p.y))&&((r.left <= p.x )&&( r.right >= p.x )))

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * InitWindowImpl
 *
 * Initialize windowing implementation.
 */

NTSTATUS FASTCALL
InitWindowImpl(VOID)
{
   WndProcHandlesArray = ExAllocatePoolWithTag(PagedPool,WPH_SIZE * sizeof(WndProcHandle), TAG_WINPROCLST);
   WndProcHandlesArraySize = WPH_SIZE;
   return STATUS_SUCCESS;
}

/*
 * CleanupWindowImpl
 *
 * Cleanup windowing implementation.
 */

NTSTATUS FASTCALL
CleanupWindowImpl(VOID)
{
   ExFreePool(WndProcHandlesArray);
   WndProcHandlesArray = 0;
   WndProcHandlesArraySize = 0;
   return STATUS_SUCCESS;
}

/* HELPER FUNCTIONS ***********************************************************/

/*
 * IntIsWindow
 *
 * The function determines whether the specified window handle identifies
 * an existing window.
 *
 * Parameters
 *    hWnd
 *       Handle to the window to test.
 *
 * Return Value
 *    If the window handle identifies an existing window, the return value
 *    is TRUE. If the window handle does not identify an existing window,
 *    the return value is FALSE.
 */

BOOL FASTCALL
IntIsWindow(HWND hWnd)
{
   PWINDOW_OBJECT Window;

   if (!(Window = IntGetWindowObject(hWnd)))
      return FALSE;

   return TRUE;
}


PWINDOW_OBJECT FASTCALL IntGetWindowObject(HWND hWnd)
{
   return (PWINDOW_OBJECT)UserGetObject(hWnd, USER_WINDOW );
}


PWINDOW_OBJECT FASTCALL
UserGetParent(PWINDOW_OBJECT Wnd)
{
  HWND hWnd;

  if (Wnd->Style & WS_POPUP)
  {
    hWnd = Wnd->Owner;//FIXME!
    return IntGetWindowObject(hWnd);
  }
  else if (Wnd->Style & WS_CHILD)
  {
    return Wnd->ParentWnd;
  }

  return NULL;
}

PWINDOW_OBJECT FASTCALL
IntGetOwner(PWINDOW_OBJECT Wnd)
{
  HWND hWnd;

  hWnd = Wnd->Owner;

  return IntGetWindowObject(hWnd);
}


/*
 * IntWinListChildren
 *
 * Compile a list of all child window handles from given window.
 *
 * Remarks
 *    This function is similar to Wine WIN_ListChildren. The caller
 *    must free the returned list with ExFreePool.
 */

HWND* FASTCALL
IntWinListChildren(PWINDOW_OBJECT Window)
{
   PWINDOW_OBJECT Child;
   HWND *List;
   UINT Index, NumChildren = 0;


   for (Child = Window->FirstChild; Child; Child = Child->NextSibling)
      ++NumChildren;

   List = ExAllocatePoolWithTag(PagedPool, (NumChildren + 1) * sizeof(HWND), TAG_WINLIST);
   if(!List)
   {
     DPRINT1("Failed to allocate memory for children array\n");
     SetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
     return NULL;
   }
   for (Child = Window->FirstChild, Index = 0;
        Child != NULL;
        Child = Child->NextSibling, ++Index)
      List[Index] = Child->Self;
   List[Index] = NULL;


   return List;
}




/*
 * IntWinListChildren
 *
 * Compile a list of all child window handles from given window.
 *
 * Remarks
 *    This function is similar to Wine WIN_ListChildren. The caller
 *    must free the returned list with ExFreePool.
 */

PWINDOW_OBJECT* FASTCALL
UserListChildWnd(PWINDOW_OBJECT Window)
{
   PWINDOW_OBJECT Child;
   PWINDOW_OBJECT *List;
   UINT Index, NumChildren = 0;


   for (Child = Window->FirstChild; Child; Child = Child->NextSibling)
      ++NumChildren;

   List = ExAllocatePoolWithTag(PagedPool, (NumChildren + 1) * sizeof(PWINDOW_OBJECT), TAG_WINLIST);
   if(!List)
   {
     DPRINT1("Failed to allocate memory for children array\n");
     SetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
     return NULL;
   }
   for (Child = Window->FirstChild, Index = 0;
        Child != NULL;
        Child = Child->NextSibling, ++Index)
      List[Index] = Child;
   List[Index] = NULL;

   return List;
}


/***********************************************************************
 *           IntSendDestroyMsg
 */
static void IntSendDestroyMsg(HWND Wnd)
{

  PWINDOW_OBJECT Window, Owner, Parent;
#if 0 /* FIXME */
  GUITHREADINFO info;

  if (GetGUIThreadInfo(GetCurrentThreadId(), &info))
    {
      if (Wnd == info.hwndCaret)
	{
	  DestroyCaret();
	}
    }
#endif

  Window = IntGetWindowObject(Wnd);
  if (Window) {
    Owner = IntGetOwner(Window);
    if (!Owner) {
      Parent = UserGetParent(Window);
      if (!Parent)
        IntShellHookNotify(HSHELL_WINDOWDESTROYED, (LPARAM) Wnd);
    }

  }

  /* The window could already be destroyed here */

  /*
   * Send the WM_DESTROY to the window.
   */

  IntSendMessage(Wnd, WM_DESTROY, 0, 0);

  /*
   * This WM_DESTROY message can trigger re-entrant calls to DestroyWindow
   * make sure that the window still exists when we come back.
   */
#if 0 /* FIXME */
  if (IsWindow(Wnd))
    {
      HWND* pWndArray;
      int i;

      if (!(pWndArray = WIN_ListChildren( hwnd ))) return;

      /* start from the end (FIXME: is this needed?) */
      for (i = 0; pWndArray[i]; i++) ;

      while (--i >= 0)
	{
	  if (IsWindow( pWndArray[i] )) WIN_SendDestroyMsg( pWndArray[i] );
	}
      HeapFree(GetProcessHeap(), 0, pWndArray);
    }
  else
    {
      DPRINT("destroyed itself while in WM_DESTROY!\n");
    }
#endif
}

/***********************************************************************
 *           IntDestroyWindow
 *
 * Destroy storage associated to a window. "Internals" p.358
 */
static LRESULT IntDestroyWindow(PWINDOW_OBJECT Window,
                                PW32PROCESS ProcessData,
                                PW32THREAD ThreadData,
                                BOOLEAN SendMessages)
{
//  PWINDOW_OBJECT *Children;
//  PWINDOW_OBJECT *Child;
  HWND* List;
  PWINDOW_OBJECT Child;
  PMENU_OBJECT Menu;
  BOOLEAN BelongsToThreadData;

  ASSERT(Window);

  if(Window->Status & WINDOWSTATUS_DESTROYING)
  {
    DPRINT("Tried to call IntDestroyWindow() twice\n");
    return 0;
  }
  Window->Status |= WINDOWSTATUS_DESTROYING;
  Window->Flags &= ~WS_VISIBLE;
  
  /* remove the window already at this point from the thread window list so we
     don't get into trouble when destroying the thread windows while we're still
     in IntDestroyWindow() */
  RemoveEntryList(&Window->ThreadListEntry);

  BelongsToThreadData = IntWndBelongsToThread(Window, ThreadData);

  IntDeRegisterShellHookWindow(Window->Self);

  if(SendMessages)
  {
    /* Send destroy messages */
    IntSendDestroyMsg(Window->Self);
  }

  /* free child windows */
  List = IntWinListChildren(Window);
  if (List)
    {
      int i; 
      for (i=0; List[i]; i++)
        {
          if ((Child = IntGetWindowObject(List[i])))
            {
              if(!IntWndBelongsToThread(Child, ThreadData))
              {
                /* send WM_DESTROY messages to windows not belonging to the same thread */
                IntSendDestroyMsg(Child->Self);
              }
              else
                IntDestroyWindow(Child, ProcessData, ThreadData, SendMessages);

            }
        }
      ExFreePool(List);
    }

  if(SendMessages)
  {
    /*
     * Clear the update region to make sure no WM_PAINT messages will be
     * generated for this window while processing the WM_NCDESTROY.
     */
    UserRedrawWindow(Window, NULL, 0,
                    RDW_VALIDATE | RDW_NOFRAME | RDW_NOERASE |
                    RDW_NOINTERNALPAINT | RDW_NOCHILDREN);
    if(BelongsToThreadData)
      IntSendMessage(Window->Self, WM_NCDESTROY, 0, 0);
  }
  
  

  /* flush the message queue */
  /* BUGBUG: this derefs the queue and possibly frees it right under us! */
//  MsqRemoveWindowMessagesFromQueue(Window);

  /* from now on no messages can be sent to this window anymore */
  //FIXME: this flas isnt used anywhere!
  Window->Status |= WINDOWSTATUS_DESTROYED;
  /* don't remove the WINDOWSTATUS_DESTROYING bit */

  /* reset shell window handles */
  if(ThreadData->Desktop)
  {
    if (Window->Self == ThreadData->Desktop->WindowStation->ShellWindow)
      ThreadData->Desktop->WindowStation->ShellWindow = NULL;

    if (Window->Self == ThreadData->Desktop->WindowStation->ShellListView)
      ThreadData->Desktop->WindowStation->ShellListView = NULL;
  }

  /* Unregister hot keys */
  UnregisterWindowHotKeys (Window);

  /* FIXME: do we need to fake QS_MOUSEMOVE wakebit? */

#if 0 /* FIXME */
  WinPosCheckInternalPos(Window->Self);
  if (Window->Self == GetCapture())
    {
      ReleaseCapture();
    }

  /* free resources associated with the window */
  TIMER_RemoveWindowTimers(Window->Self);
#endif

  if (!(Window->Style & WS_CHILD) && Window->IDMenu
      && (Menu = IntGetMenuObject((HMENU)Window->IDMenu)))
    {
	IntDestroyMenuObject(Menu, TRUE, TRUE);
	Window->IDMenu = 0;
    }

  if(Window->SystemMenu
     && (Menu = IntGetMenuObject(Window->SystemMenu)))
  {
    IntDestroyMenuObject(Menu, TRUE, TRUE);
    Window->SystemMenu = (HMENU)0;
  }

  DceFreeWindowDCE(Window);    /* Always do this to catch orphaned DCs */
#if 0 /* FIXME */
  WINPROC_FreeProc(Window->winproc, WIN_PROC_WINDOW);
  CLASS_RemoveWindow(Window->Class);
#endif

  IntUnlinkWindow(Window);

  //'FIXME IntReferenceWindowObject(Window);
  

  IntDestroyScrollBars(Window);

  /* remove the window from the class object */
  RemoveEntryList(&Window->ClassListEntry);

  /* dereference the class */
  ClassDereferenceClass(Window->Class);
  Window->Class = NULL;

  if(Window->WindowRegion)
  {
    NtGdiDeleteObject(Window->WindowRegion);
  }

  RtlFreeUnicodeString(&Window->WindowName);
  
  /* this must be the last call (i think) Gunnar */
  //FIXME: new name: MsqCleanupQueueWindow()?
  //FIXME: timer cleanup might/should be a part of queue cleanup?
  UserRemoveTimersWindow(Window);
  
  MsqRemoveWindowMessagesFromQueue(Window);

  //ObmCloseHandle(ThreadData->Desktop->WindowStation->HandleTable, Window->Self);
  UserFreeHandle(Window->Self);
  
//  memset(Window, 0x
//memset( void *dest, int c, size_t count );
   RtlZeroMemory(Window, sizeof(WINDOW_OBJECT));

  return 0;
}

VOID FASTCALL
IntGetWindowBorderMeasures(PWINDOW_OBJECT WindowObject, UINT *cx, UINT *cy)
{
  DPRINT1("IntGetWindowBorderMeasures, wnd=0x%x, cx=0x%x, cy=0x%x\n", WindowObject,cx,cy);
  
  if(HAS_DLGFRAME(WindowObject->Style, WindowObject->ExStyle) && !(WindowObject->Style & WS_MINIMIZE))
  {
    *cx = UserGetSystemMetrics(SM_CXDLGFRAME);
    *cy = UserGetSystemMetrics(SM_CYDLGFRAME);
  }
  else
  {
    if(HAS_THICKFRAME(WindowObject->Style, WindowObject->ExStyle)&& !(WindowObject->Style & WS_MINIMIZE))
    {
      *cx = UserGetSystemMetrics(SM_CXFRAME);
      *cy = UserGetSystemMetrics(SM_CYFRAME);
    }
    else if(HAS_THINFRAME(WindowObject->Style, WindowObject->ExStyle))
    {
      *cx = UserGetSystemMetrics(SM_CXBORDER);
      *cy = UserGetSystemMetrics(SM_CYBORDER);
    }
    else
    {
      *cx = *cy = 0;
    }
  }
}

BOOL FASTCALL
IntGetWindowInfo(PWINDOW_OBJECT WindowObject, PWINDOWINFO pwi)
{
  pwi->cbSize = sizeof(WINDOWINFO);
  pwi->rcWindow = WindowObject->WindowRect;
  pwi->rcClient = WindowObject->ClientRect;
  pwi->dwStyle = WindowObject->Style;
  pwi->dwExStyle = WindowObject->ExStyle;
  pwi->dwWindowStatus = (IntGetForegroundWindow() == WindowObject); /* WS_ACTIVECAPTION */
  IntGetWindowBorderMeasures(WindowObject, &pwi->cxWindowBorders, &pwi->cyWindowBorders);
  pwi->atomWindowType = (WindowObject->Class ? WindowObject->Class->Atom : 0);
  pwi->wCreatorVersion = 0x400; /* FIXME - return a real version number */
  return TRUE;
}

static BOOL FASTCALL
IntSetMenu(
   PWINDOW_OBJECT WindowObject,
   HMENU Menu,
   BOOL *Changed)
{
  PMENU_OBJECT OldMenuObject, NewMenuObject = NULL;

  *Changed = (WindowObject->IDMenu != (UINT) Menu);
  if (! *Changed)
    {
      return TRUE;
    }

  if (0 != WindowObject->IDMenu)
    {
      OldMenuObject = IntGetMenuObject((HMENU) WindowObject->IDMenu);
      ASSERT(NULL == OldMenuObject || OldMenuObject->MenuInfo.Wnd == WindowObject->Self);
    }
  else
    {
      OldMenuObject = NULL;
    }

  if (NULL != Menu)
    {
      NewMenuObject = IntGetMenuObject(Menu);
      if (NULL == NewMenuObject)
        {
//          if (NULL != OldMenuObject)
//            {
//              IntReleaseMenuObject(OldMenuObject);
//            }
          SetLastWin32Error(ERROR_INVALID_MENU_HANDLE);
          return FALSE;
        }
      if (NULL != NewMenuObject->MenuInfo.Wnd)
        {
          /* Can't use the same menu for two windows */
//          if (NULL != OldMenuObject)
//            {
//              IntReleaseMenuObject(OldMenuObject);
//            }
          SetLastWin32Error(ERROR_INVALID_MENU_HANDLE);
          return FALSE;
        }

    }

  WindowObject->IDMenu = (UINT) Menu;
  if (NULL != NewMenuObject)
    {
      NewMenuObject->MenuInfo.Wnd = WindowObject->Self;
//      IntReleaseMenuObject(NewMenuObject);
    }
  if (NULL != OldMenuObject)
    {
      OldMenuObject->MenuInfo.Wnd = NULL;
//      IntReleaseMenuObject(OldMenuObject);
    }

  return TRUE;
}


/* INTERNAL ******************************************************************/


VOID FASTCALL
DestroyThreadWindows(struct _ETHREAD *Thread)
{
  PLIST_ENTRY Current;
  PW32PROCESS Win32Process;
  PW32THREAD Win32Thread;
  PWINDOW_OBJECT *List, *pWnd;
  ULONG Cnt = 0;

  Win32Thread = Thread->Tcb.Win32Thread;
  Win32Process = (PW32PROCESS)Thread->ThreadsProcess->Win32Process;

  Current = Win32Thread->WindowListHead.Flink;
  while (Current != &(Win32Thread->WindowListHead))
  {
    Cnt++;
    Current = Current->Flink;
  }

  if(Cnt > 0)
  {
    List = ExAllocatePool(PagedPool, (Cnt + 1) * sizeof(PWINDOW_OBJECT));
    if(!List)
    {
      DPRINT("Not enough memory to allocate window handle list\n");
      return;
    }
    pWnd = List;
    Current = Win32Thread->WindowListHead.Flink;
    while (Current != &(Win32Thread->WindowListHead))
    {
      *pWnd = CONTAINING_RECORD(Current, WINDOW_OBJECT, ThreadListEntry);
//'FIXME      IntReferenceWindowObject(*pWnd);
      pWnd++;
      Current = Current->Flink;
    }
    *pWnd = NULL;

    for(pWnd = List; *pWnd; pWnd++)
    {
      UserDestroyWindow(*pWnd);
    }
    ExFreePool(List);
    return;
  }

}


/*!
 * Internal function.
 * Returns client window rectangle relative to the upper-left corner of client area.
 *
 * \note Does not check the validity of the parameters
*/
VOID FASTCALL
IntGetClientRect(PWINDOW_OBJECT WindowObject, PRECT Rect)
{
  ASSERT( WindowObject );
  ASSERT( Rect );

  Rect->left = Rect->top = 0;
  Rect->right = WindowObject->ClientRect.right - WindowObject->ClientRect.left;
  Rect->bottom = WindowObject->ClientRect.bottom - WindowObject->ClientRect.top;
}


#if 0
HWND FASTCALL
IntGetFocusWindow(VOID)
{
  PUSER_MESSAGE_QUEUE Queue;
  PDESKTOP_OBJECT pdo = UserGetActiveDesktop();

  if( !pdo )
	return NULL;

  Queue = (PUSER_MESSAGE_QUEUE)pdo->ActiveMessageQueue;

  if (Queue == NULL)
      return(NULL);
  else
      return(Queue->FocusWindow);
}
#endif

PMENU_OBJECT FASTCALL
IntGetSystemMenu(PWINDOW_OBJECT WindowObject, BOOL bRevert, BOOL RetMenu)
{
  PMENU_OBJECT MenuObject, NewMenuObject, SysMenuObject, ret = NULL;
  PW32THREAD W32Thread;
  HMENU NewMenu, SysMenu;
  ROSMENUITEMINFO ItemInfo;

  if(bRevert)
  {
    W32Thread = PsGetWin32Thread();

    if(!W32Thread->Desktop)
      return NULL;

    if(WindowObject->SystemMenu)
    {
      MenuObject = IntGetMenuObject(WindowObject->SystemMenu);
      if(MenuObject)
      {
        IntDestroyMenuObject(MenuObject, FALSE, TRUE);
        WindowObject->SystemMenu = (HMENU)0;
      }
    }

    if(W32Thread->Desktop->WindowStation->SystemMenuTemplate)
    {
      /* clone system menu */
      MenuObject = IntGetMenuObject(W32Thread->Desktop->WindowStation->SystemMenuTemplate);
      if(!MenuObject)
        return NULL;

      NewMenuObject = IntCloneMenu(MenuObject);
      if(NewMenuObject)
      {
        WindowObject->SystemMenu = NewMenuObject->MenuInfo.Self;
        NewMenuObject->MenuInfo.Flags |= MF_SYSMENU;
        NewMenuObject->MenuInfo.Wnd = WindowObject->Self;
        ret = NewMenuObject;
      }

    }
    else
    {
      SysMenu = UserCreateMenu(FALSE);
      if (NULL == SysMenu)
      {
        return NULL;
      }
      SysMenuObject = IntGetMenuObject(SysMenu);
      if (NULL == SysMenuObject)
      {
        NtUserDestroyMenu(SysMenu);
        return NULL;
      }
      SysMenuObject->MenuInfo.Flags |= MF_SYSMENU;
      SysMenuObject->MenuInfo.Wnd = WindowObject->Self;
      NewMenu = IntLoadSysMenuTemplate();
      if(!NewMenu)
      {
        NtUserDestroyMenu(SysMenu);
        return NULL;
      }
      MenuObject = IntGetMenuObject(NewMenu);
      if(!MenuObject)
      {
        NtUserDestroyMenu(SysMenu);
        return NULL;
      }

      NewMenuObject = IntCloneMenu(MenuObject);
      if(NewMenuObject)
      {
        NewMenuObject->MenuInfo.Flags |= MF_SYSMENU | MF_POPUP;
        IntSetMenuDefaultItem(NewMenuObject/*->MenuInfo.Self*/, SC_CLOSE, FALSE);

        ItemInfo.cbSize = sizeof(MENUITEMINFOW);
        ItemInfo.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_STATE | MIIM_SUBMENU;
        ItemInfo.fType = MF_POPUP;
        ItemInfo.fState = MFS_ENABLED;
        ItemInfo.dwTypeData = NULL;
        ItemInfo.cch = 0;
        ItemInfo.hSubMenu = NewMenuObject->MenuInfo.Self;
        IntInsertMenuItem(SysMenuObject, (UINT) -1, TRUE, &ItemInfo);

        WindowObject->SystemMenu = SysMenuObject->MenuInfo.Self;

        ret = SysMenuObject;
      }
      IntDestroyMenuObject(MenuObject, FALSE, TRUE);
    }
    if(RetMenu)
      return ret;
    else
      return NULL;
  }
  else
  {
    if(WindowObject->SystemMenu)
      return IntGetMenuObject((HMENU)WindowObject->SystemMenu);
    else
      return NULL;
  }
}


//FIXME: no NtCall lead to this... Duplicate impl. in umode??
BOOL FASTCALL
UserIsChildWindow(PWINDOW_OBJECT Parent, PWINDOW_OBJECT Child)
{
  PWINDOW_OBJECT Window;//, Old;

  //FIXME: HACK!
  if (!Parent || !Child) return FALSE;

  ASSERT(Parent);
  ASSERT(Child);

  Window = Child->ParentWnd;
  while (Window)
  {
    if (Window == Parent)
    {
      return(TRUE);
    }
    //FIXME: wine does not check for this. why should we??
    if(!(Window->Style & WS_CHILD))
    {
      break;
    }
    Window = Window->ParentWnd;
  }

  return(FALSE);
}

HWND FASTCALL
GetHwnd(PWINDOW_OBJECT Wnd)
{
   return Wnd ? Wnd->Self : 0;
}

BOOL FASTCALL
UserIsWindowVisible(PWINDOW_OBJECT Wnd)
{
  ASSERT(Wnd); 

  while(Wnd)
  {
    if(!(Wnd->Style & WS_CHILD))
    {
      break;
    }
    if(!(Wnd->Style & WS_VISIBLE))
    {
      return FALSE;
    }
    Wnd = Wnd->ParentWnd;
  }

  if(Wnd)
  {
    if(Wnd->Style & WS_VISIBLE)
    {
      return TRUE;
    }
  }

  return FALSE;
}


/* link the window into siblings and parent. children are kept in place. */
VOID FASTCALL
IntLinkWindow(
  PWINDOW_OBJECT Wnd,
  PWINDOW_OBJECT WndParent,
  PWINDOW_OBJECT WndPrevSibling /* set to NULL if top sibling */
  )
{
  PWINDOW_OBJECT Parent;

 DPRINT1("Set 0x%x's parent to 0x%x\n",Wnd, WndParent);
  Wnd->ParentWnd = WndParent;//->Self;
  if ((Wnd->PrevSibling = WndPrevSibling))
  {
    /* link after WndPrevSibling */
    if ((Wnd->NextSibling = WndPrevSibling->NextSibling))
      Wnd->NextSibling->PrevSibling = Wnd;
//    else if ((Parent = IntGetWindowObject(Wnd->Parent)))
    else if ((Parent = Wnd->ParentWnd))
    {
      if(Parent->LastChild == WndPrevSibling)
        Parent->LastChild = Wnd;
    }
    Wnd->PrevSibling->NextSibling = Wnd;
  }
  else
  {
    /* link at top */
    Parent = Wnd->ParentWnd;
    if ((Wnd->NextSibling = WndParent->FirstChild))
      Wnd->NextSibling->PrevSibling = Wnd;
    else if (Parent)
    {
      Parent->LastChild = Wnd;
      Parent->FirstChild = Wnd;
      return;
    }
    if(Parent)
    {
      Parent->FirstChild = Wnd;
    }
  }
}

HWND FASTCALL
IntSetOwner(HWND hWnd, HWND hWndNewOwner)
{
  PWINDOW_OBJECT Wnd, WndOldOwner, WndNewOwner;
  HWND ret;

  Wnd = IntGetWindowObject(hWnd);
  if(!Wnd)
    return NULL;

  WndOldOwner = IntGetWindowObject(Wnd->Owner);
  if (WndOldOwner)
  {
     ret = WndOldOwner->Self;
  }
  else
  {
     ret = 0;
  }

  if((WndNewOwner = IntGetWindowObject(hWndNewOwner)))
  {
    Wnd->Owner = hWndNewOwner;
  }
  else
    Wnd->Owner = NULL;

  return ret;
}

PWINDOW_OBJECT FASTCALL
UserSetParent(
   PWINDOW_OBJECT Wnd, 
   PWINDOW_OBJECT WndNewParent OPTIONAL
   )
{
   PWINDOW_OBJECT WndOldParent, Sibling, InsertAfter, Desktop;
   //HWND hWnd;//, hWndNewParent;
   BOOL WasVisible;
   BOOL MenuChanged;

   ASSERT(Wnd);

   Desktop = UserGetDesktopWindow();

   if (Wnd == Desktop)
   {
      SetLastWin32Error(ERROR_ACCESS_DENIED);
      return(NULL);
   }

   if (!WndNewParent)
      WndNewParent = Desktop;

//   hWnd = Wnd->Self;
//   hWndNewParent = WndNewParent->Self;

   /*
    * Windows hides the window first, then shows it again
    * including the WM_SHOWWINDOW messages and all
    */
   WasVisible = WinPosShowWindow(Wnd, SW_HIDE);

   /* Validate that window and parent still exist */
//   if (!IntIsWindow(hWnd) || !IntIsWindow(hWndNewParent))
//      return NULL;

   /* Window must belong to current process */
   if (Wnd->OwnerThread->ThreadsProcess != PsGetCurrentProcess())
      return NULL;

   WndOldParent = Wnd->ParentWnd;

   if (WndNewParent != WndOldParent)
   {
      IntUnlinkWindow(Wnd);
      InsertAfter = NULL;
      if (0 == (Wnd->ExStyle & WS_EX_TOPMOST))
      {
        /* Not a TOPMOST window, put after TOPMOSTs of new parent */
        Sibling = WndNewParent->FirstChild;
        while (NULL != Sibling && 0 != (Sibling->ExStyle & WS_EX_TOPMOST))
        {
          InsertAfter = Sibling;
          Sibling = Sibling->NextSibling;
        }
      }
      if (NULL == InsertAfter)
      {
        IntLinkWindow(Wnd, WndNewParent, InsertAfter /*prev sibling*/);
      }
      else
      {
        IntLinkWindow(Wnd, WndNewParent, InsertAfter /*prev sibling*/);
      }

      if (WndNewParent != UserGetDesktopWindow()) /* a child window */
      {
         if (!(Wnd->Style & WS_CHILD))
         {
            //if ( Wnd->Menu ) DestroyMenu ( Wnd->menu );
            IntSetMenu(Wnd, NULL, &MenuChanged);
         }
      }
   }

   /*
    * SetParent additionally needs to make hwnd the top window
    * in the z-order and send the expected WM_WINDOWPOSCHANGING and
    * WM_WINDOWPOSCHANGED notification messages.
    */
   WinPosSetWindowPos(GetHwnd(Wnd), (0 == (Wnd->ExStyle & WS_EX_TOPMOST) ? HWND_TOP : HWND_TOPMOST),
                      0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE
                                | (WasVisible ? SWP_SHOWWINDOW : 0));

   /*
    * FIXME: a WM_MOVE is also generated (in the DefWindowProc handler
    * for WM_WINDOWPOSCHANGED) in Windows, should probably remove SWP_NOMOVE
    */

   /*
    * Validate that the old parent still exist, since it migth have been
    * destroyed during the last callbacks to user-mode
    */
//   if(WndOldParent)
//   {
//     if(!IntIsWindow(WndOldParent->Self))
//     {
//       return NULL;
//     }

     /* don't dereference the window object here, it must be done by the caller
        of IntSetParent() */
//     return WndOldParent;
//   }
//   return NULL;

   return WndOldParent;
}

BOOL FASTCALL
IntSetSystemMenu(PWINDOW_OBJECT WindowObject, PMENU_OBJECT MenuObject)
{
  PMENU_OBJECT OldMenuObject;
  if(WindowObject->SystemMenu)
  {
    OldMenuObject = IntGetMenuObject(WindowObject->SystemMenu);
    if(OldMenuObject)
    {
      OldMenuObject->MenuInfo.Flags &= ~ MF_SYSMENU;
//      IntReleaseMenuObject(OldMenuObject);
    }
  }

  if(MenuObject)
  {
    /* FIXME check window style, propably return FALSE ? */
    WindowObject->SystemMenu = MenuObject->MenuInfo.Self;
    MenuObject->MenuInfo.Flags |= MF_SYSMENU;
  }
  else
    WindowObject->SystemMenu = (HMENU)0;

  return TRUE;
}


/* unlink the window from siblings and parent. children are kept in place. */
VOID FASTCALL
IntUnlinkWindow(PWINDOW_OBJECT Wnd)
{
  PWINDOW_OBJECT WndParent;

  WndParent = Wnd->ParentWnd;

  if (Wnd->NextSibling) Wnd->NextSibling->PrevSibling = Wnd->PrevSibling;
  else if (WndParent && WndParent->LastChild == Wnd) WndParent->LastChild = Wnd->PrevSibling;

  if (Wnd->PrevSibling) Wnd->PrevSibling->NextSibling = Wnd->NextSibling;
  else if (WndParent && WndParent->FirstChild == Wnd) WndParent->FirstChild = Wnd->NextSibling;

DPRINT1("Set 0x%x's parent to 0x%x\n",Wnd, WndParent);
  Wnd->PrevSibling = Wnd->NextSibling = Wnd->ParentWnd = NULL;
}

BOOL FASTCALL
IntAnyPopup(VOID)
{
  PWINDOW_OBJECT Window, Child;

  if(!(Window = UserGetDesktopWindow()))
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }

  for(Child = Window->FirstChild; Child; Child = Child->NextSibling)
  {
    if(Child->Owner && Child->Style & WS_VISIBLE)
    {
      /*
       * The desktop has a popup window if one of them has
       * an owner window and is visible
       */
      return TRUE;
    }
  }

  return FALSE;
}

BOOL FASTCALL
IntIsWindowInDestroy(PWINDOW_OBJECT Window)
{
  return ((Window->Status & WINDOWSTATUS_DESTROYING) == WINDOWSTATUS_DESTROYING);
}

/* FUNCTIONS *****************************************************************/

/*
 * @unimplemented
 */
DWORD STDCALL
NtUserAlterWindowStyle(DWORD Unknown0,
		       DWORD Unknown1,
		       DWORD Unknown2)
{
  UNIMPLEMENTED

  return(0);
}


/*
 * As best as I can figure, this function is used by EnumWindows,
 * EnumChildWindows, EnumDesktopWindows, & EnumThreadWindows.
 *
 * It's supposed to build a list of HWNDs to return to the caller.
 * We can figure out what kind of list by what parameters are
 * passed to us.
 */
/*
 * @implemented
 */
ULONG
STDCALL
NtUserBuildHwndList(
  HDESK hDesktop,
  HWND hwndParent,
  BOOLEAN bChildren,
  ULONG dwThreadId,
  ULONG lParam,
  HWND* pWnd,
  ULONG nBufSize)
{
  NTSTATUS Status;
  ULONG dwCount = 0;
  DECLARE_RETURN(ULONG);
  /* FIXME handle bChildren */

  DPRINT("Enter NtUserBuildHwndList\n");
  UserEnterExclusive();
  
  if(hwndParent)
  {
    PWINDOW_OBJECT Window, Child;
    if(!(Window = IntGetWindowObject(hwndParent)))
    {
      SetLastWin32Error(ERROR_INVALID_HANDLE);
      RETURN(0);
    }

    for(Child = Window->FirstChild; Child != NULL; Child = Child->NextSibling)
    {
      if(dwCount++ < nBufSize && pWnd)
      {
        Status = MmCopyToCaller(pWnd++, &Child->Self, sizeof(HWND));
        if(!NT_SUCCESS(Status))
        {
          SetLastNtError(Status);
          break;
        }
      }
    }

  }
  else if(dwThreadId)
  {
    PETHREAD Thread;
    PW32THREAD W32Thread;
    PLIST_ENTRY Current;
    PWINDOW_OBJECT Window;

    Status = PsLookupThreadByThreadId((HANDLE)dwThreadId, &Thread);
    if(!NT_SUCCESS(Status))
    {
      SetLastWin32Error(ERROR_INVALID_PARAMETER);
      RETURN(0);
    }
    if(!(W32Thread = Thread->Tcb.Win32Thread))
    {
      ObDereferenceObject(Thread);
      DPRINT("Thread is not a GUI Thread!\n");
      SetLastWin32Error(ERROR_INVALID_PARAMETER);
      RETURN(0);
    }

    Current = W32Thread->WindowListHead.Flink;
    while(Current != &(W32Thread->WindowListHead))
    {
      Window = CONTAINING_RECORD(Current, WINDOW_OBJECT, ThreadListEntry);
      ASSERT(Window);

      if(dwCount < nBufSize && pWnd)
      {
        Status = MmCopyToCaller(pWnd++, &Window->Self, sizeof(HWND));
        if(!NT_SUCCESS(Status))
        {
          SetLastNtError(Status);
          break;
        }
      }
      dwCount++;
      Current = Current->Flink;
    }

    ObDereferenceObject(Thread);
  }
  else
  {
    PDESKTOP_OBJECT Desktop;
    PWINDOW_OBJECT Window, Child;

    if(hDesktop == NULL && !(Desktop = UserGetActiveDesktop()))
    {
      SetLastWin32Error(ERROR_INVALID_HANDLE);
      RETURN(0);
    }

    if(hDesktop)
    {
      Status = IntValidateDesktopHandle(hDesktop,
                                        UserMode,
                                        0,
                                        &Desktop);
      if(!NT_SUCCESS(Status))
      {
        SetLastWin32Error(ERROR_INVALID_HANDLE);
        RETURN(0);
      }
    }
    if(!(Window = IntGetWindowObject(Desktop->DesktopWindow)))
    {
      if(hDesktop)
        ObDereferenceObject(Desktop);
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(0);
    }

    for(Child = Window->FirstChild; Child != NULL; Child = Child->NextSibling)
    {
      if(dwCount++ < nBufSize && pWnd)
      {
        Status = MmCopyToCaller(pWnd++, &Child->Self, sizeof(HWND));
        if(!NT_SUCCESS(Status))
        {
          SetLastNtError(Status);
          break;
        }
      }
    }

    if(hDesktop)
      ObDereferenceObject(Desktop);
  }

  RETURN(dwCount);
  
CLEANUP:
  DPRINT("Leave NtUserBuildHwndList, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}


/*
 * @implemented
 */
HWND STDCALL
NtUserChildWindowFromPointEx(HWND hwndParent,
			     LONG x,
			     LONG y,
			     UINT uiFlags)
{
  PWINDOW_OBJECT Parent;
  POINTL Pt;
  HWND Ret;
  HWND *List;
  DECLARE_RETURN(HWND);
  
  DPRINT("Enter NtUserChildWindowFromPointEx\n");
  UserEnterExclusive();
  
  if(!(Parent = IntGetWindowObject(hwndParent)))
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    RETURN(NULL);
  }

  Pt.x = x;
  Pt.y = y;

  if(Parent != UserGetDesktopWindow())
  {
    Pt.x += Parent->ClientRect.left;
    Pt.y += Parent->ClientRect.top;
  }

  if(!IntPtInWindow(Parent, Pt.x, Pt.y))
  {
    RETURN(NULL);
  }

  Ret = Parent->Self;
  if((List = IntWinListChildren(Parent)))
  {
    int i; 
    for(i=0; List[i]; i++)
    {
      PWINDOW_OBJECT Child;
      if((Child = IntGetWindowObject(List[i])))
      {
        if(!(Child->Style & WS_VISIBLE) && (uiFlags & CWP_SKIPINVISIBLE))
        {
          continue;
        }
        if((Child->Style & WS_DISABLED) && (uiFlags & CWP_SKIPDISABLED))
        {
          continue;
        }
        if((Child->ExStyle & WS_EX_TRANSPARENT) && (uiFlags & CWP_SKIPTRANSPARENT))
        {
          continue;
        }
        if(IntPtInWindow(Child, Pt.x, Pt.y))
        {
          Ret = Child->Self;
          break;
        }
      }
    }
    ExFreePool(List);
  }

  RETURN(Ret);
  
CLEANUP:
  DPRINT("Leave NtUserChildWindowFromPointEx, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}


/*
 * calculates the default position of a window
 */
BOOL FASTCALL
IntCalcDefPosSize(PWINDOW_OBJECT Parent, PWINDOW_OBJECT WindowObject, RECT *rc, BOOL IncPos)
{
  SIZE Sz;
  POINT Pos = {0, 0};

  if(Parent != NULL)
  {
    IntGdiIntersectRect(rc, rc, &Parent->ClientRect);

    if(IncPos)
    {
      Pos.x = Parent->TiledCounter * (UserGetSystemMetrics(SM_CXSIZE) + UserGetSystemMetrics(SM_CXFRAME));
      Pos.y = Parent->TiledCounter * (UserGetSystemMetrics(SM_CYSIZE) + UserGetSystemMetrics(SM_CYFRAME));
      if(Pos.x > ((rc->right - rc->left) / 4) ||
         Pos.y > ((rc->bottom - rc->top) / 4))
      {
        /* reset counter and position */
        Pos.x = 0;
        Pos.y = 0;
        Parent->TiledCounter = 0;
      }
      Parent->TiledCounter++;
    }
    Pos.x += rc->left;
    Pos.y += rc->top;
  }
  else
  {
    Pos.x = rc->left;
    Pos.y = rc->top;
  }

  Sz.cx = EngMulDiv(rc->right - rc->left, 3, 4);
  Sz.cy = EngMulDiv(rc->bottom - rc->top, 3, 4);

  rc->left = Pos.x;
  rc->top = Pos.y;
  rc->right = rc->left + Sz.cx;
  rc->bottom = rc->top + Sz.cy;
  return TRUE;
}



PWINDOW_OBJECT FASTCALL UserCreateWindowObject(HWND* h, ULONG bytes)
{
   PVOID mem;
   
   mem = ExAllocatePool(PagedPool, bytes);
   if (!mem) return NULL;
   RtlZeroMemory(mem, bytes);
   *h = UserAllocHandle(mem, USER_WINDOW);
   if (!*h){
      ExFreePool(mem);
      return NULL;
   }
   return mem;
}


/*
 * @implemented
 */
HWND STDCALL
IntCreateWindowEx(DWORD dwExStyle,
		  PUNICODE_STRING ClassName,
		  PUNICODE_STRING WindowName,
		  DWORD dwStyle,
		  LONG x,
		  LONG y,
		  LONG nWidth,
		  LONG nHeight,
		  HWND hWndParent,
		  HMENU hMenu,
		  HINSTANCE hInstance,
		  LPVOID lpParam,
		  DWORD dwShowMode,
		  BOOL bUnicodeWindow)
{
  PWINSTATION_OBJECT WinStaObject;
  PWNDCLASS_OBJECT ClassObject;
  PWINDOW_OBJECT WindowObject;
  PWINDOW_OBJECT ParentWindow, OwnerWindow;
  HWND ParentWindowHandle;
  HWND OwnerWindowHandle;
  PMENU_OBJECT SystemMenu;
  HWND Handle;
  POINT Pos;
  SIZE Size;
#if 0
  POINT MaxSize, MaxPos, MinTrack, MaxTrack;
#else
  POINT MaxPos;
#endif
  CREATESTRUCTW Cs;
  CBT_CREATEWNDW CbtCreate;
  LRESULT Result;
  BOOL MenuChanged;
  BOOL ClassFound;

  BOOL HasOwner;

  ParentWindowHandle = PsGetWin32Thread()->Desktop->DesktopWindow;
  OwnerWindowHandle = NULL;

  if (hWndParent == HWND_MESSAGE)
    {
      /*
       * native ole32.OleInitialize uses HWND_MESSAGE to create the
       * message window (style: WS_POPUP|WS_DISABLED)
       */
      DPRINT1("FIXME - Parent is HWND_MESSAGE\n");
    }
  else if (hWndParent)
    {
      if ((dwStyle & (WS_CHILD | WS_POPUP)) == WS_CHILD)
        ParentWindowHandle = hWndParent;
      else
        OwnerWindowHandle = GetHwnd(UserGetAncestor(GetWnd(hWndParent), GA_ROOT));
    }
  else if ((dwStyle & (WS_CHILD | WS_POPUP)) == WS_CHILD)
    {
      return (HWND)0;  /* WS_CHILD needs a parent, but WS_POPUP doesn't */
    }

  if (NULL != ParentWindowHandle)
    {
      ParentWindow = IntGetWindowObject(ParentWindowHandle);
    }
  else
    {
      ParentWindow = NULL;
    }

  /* FIXME: parent must belong to the current process */

  /* Check the class. */
  ClassFound = ClassReferenceClassByNameOrAtom(&ClassObject, ClassName->Buffer, hInstance);
  if (!ClassFound)
  {
     if (IS_ATOM(ClassName->Buffer))
       {
         DPRINT1("Class 0x%x not found\n", (DWORD_PTR) ClassName->Buffer);
       }
     else
       {
         DPRINT1("Class %wZ not found\n", ClassName);
       }
     SetLastWin32Error(ERROR_CANNOT_FIND_WND_CLASS);
     return((HWND)0);
  }

  /* Check the window station. */
  if (PsGetWin32Thread()->Desktop == NULL)
    {
      ClassDereferenceClass(ClassObject);
      DPRINT("Thread is not attached to a desktop! Cannot create window!\n");
      return (HWND)0;
    }
  WinStaObject = PsGetWin32Thread()->Desktop->WindowStation;
  ObReferenceObjectByPointer(WinStaObject, KernelMode, ExWindowStationObjectType, 0);

  /* Create the window object. */
//  WindowObject = (PWINDOW_OBJECT)
//    ObmCreateObject(PsGetWin32Thread()->Desktop->WindowStation->HandleTable, &Handle,
//        otWindow, sizeof(WINDOW_OBJECT) + ClassObject->cbWndExtra
//        );

   WindowObject = UserCreateWindowObject(&Handle, sizeof(WINDOW_OBJECT) + ClassObject->cbWndExtra);

  DPRINT("Created object with handle %X\n", Handle);
  if (!WindowObject)
    {
      ObDereferenceObject(WinStaObject);
      ClassDereferenceClass(ClassObject);
      SetLastNtError(STATUS_INSUFFICIENT_RESOURCES);
      return (HWND)0;
    }
  ObDereferenceObject(WinStaObject);

  if (NULL == PsGetWin32Thread()->Desktop->DesktopWindow)
    {
      /* If there is no desktop window yet, we must be creating it */
      PsGetWin32Thread()->Desktop->DesktopWindow = Handle;
    }

  /*
   * Fill out the structure describing it.
   */
  WindowObject->Class = ClassObject;
  InsertTailList(&ClassObject->ClassWindowsListHead, &WindowObject->ClassListEntry);

  WindowObject->ExStyle = dwExStyle;
  WindowObject->Style = dwStyle & ~WS_VISIBLE;
  DPRINT("1: Style is now %lx\n", WindowObject->Style);

  WindowObject->SystemMenu = (HMENU)0;
  WindowObject->ContextHelpId = 0;
  WindowObject->IDMenu = 0;
  WindowObject->Instance = hInstance;
  WindowObject->Self = Handle;
  if (0 != (dwStyle & WS_CHILD))
    {
      WindowObject->IDMenu = (UINT) hMenu;
    }
  else
    {
      IntSetMenu(WindowObject, hMenu, &MenuChanged);
    }
    
  WindowObject->MessageQueue = UserGetCurrentQueue();
  
  DPRINT1("Set 0x%x's parent to 0x%x\n",WindowObject, ParentWindow);
  WindowObject->ParentWnd = ParentWindow;
  if((OwnerWindow = IntGetWindowObject(OwnerWindowHandle)))
  {
    WindowObject->Owner = OwnerWindowHandle;
    HasOwner = TRUE;
  } else {
    WindowObject->Owner = NULL;
    HasOwner = FALSE;
  }
  WindowObject->UserData = 0;
  if ((((DWORD)ClassObject->lpfnWndProcA & 0xFFFF0000) != 0xFFFF0000)
      && (((DWORD)ClassObject->lpfnWndProcW & 0xFFFF0000) != 0xFFFF0000))
    {
      WindowObject->Unicode = bUnicodeWindow;
    }
  else
    {
      WindowObject->Unicode = ClassObject->Unicode;
    }
  WindowObject->WndProcA = ClassObject->lpfnWndProcA;
  WindowObject->WndProcW = ClassObject->lpfnWndProcW;
  WindowObject->OwnerThread = PsGetCurrentThread();
  WindowObject->FirstChild = NULL;
  WindowObject->LastChild = NULL;
  WindowObject->PrevSibling = NULL;
  WindowObject->NextSibling = NULL;

  /* extra window data */
  if (ClassObject->cbWndExtra != 0)
    {
      WindowObject->ExtraData = (PCHAR)(WindowObject + 1);
      WindowObject->ExtraDataSize = ClassObject->cbWndExtra;
      RtlZeroMemory(WindowObject->ExtraData, WindowObject->ExtraDataSize);
    }
  else
    {
      WindowObject->ExtraData = NULL;
      WindowObject->ExtraDataSize = 0;
    }

  InitializeListHead(&WindowObject->PropListHead);
  ExInitializeFastMutex(&WindowObject->UpdateLock);
  InitializeListHead(&WindowObject->WndObjListHead);

  if (NULL != WindowName->Buffer)
    {
      WindowObject->WindowName.MaximumLength = WindowName->MaximumLength;
      WindowObject->WindowName.Length = WindowName->Length;
      WindowObject->WindowName.Buffer = ExAllocatePoolWithTag(PagedPool, WindowName->MaximumLength,
                                                              TAG_STRING);
      if (NULL == WindowObject->WindowName.Buffer)
        {
          ClassDereferenceClass(ClassObject);
          DPRINT1("Failed to allocate mem for window name\n");
          SetLastWin32Error(ERROR_NOT_ENOUGH_MEMORY);
          return NULL;
        }
      RtlCopyMemory(WindowObject->WindowName.Buffer, WindowName->Buffer, WindowName->MaximumLength);
    }
  else
    {
      RtlInitUnicodeString(&WindowObject->WindowName, NULL);
    }


  /*
   * This has been tested for WS_CHILD | WS_VISIBLE.  It has not been
   * tested for WS_POPUP
   */
  if ((dwExStyle & WS_EX_DLGMODALFRAME) ||
      ((!(dwExStyle & WS_EX_STATICEDGE)) &&
      (dwStyle & (WS_DLGFRAME | WS_THICKFRAME))))
    dwExStyle |= WS_EX_WINDOWEDGE;
  else
    dwExStyle &= ~WS_EX_WINDOWEDGE;

  /* Correct the window style. */
  if (!(dwStyle & WS_CHILD))
    {
      WindowObject->Style |= WS_CLIPSIBLINGS;
      DPRINT("3: Style is now %lx\n", WindowObject->Style);
      if (!(dwStyle & WS_POPUP))
	{
	  WindowObject->Style |= WS_CAPTION;
          WindowObject->Flags |= WINDOWOBJECT_NEED_SIZE;
          DPRINT("4: Style is now %lx\n", WindowObject->Style);
	}
    }

  /* create system menu */
  if((WindowObject->Style & WS_SYSMENU) &&
     (WindowObject->Style & WS_CAPTION) == WS_CAPTION)
  {
    SystemMenu = IntGetSystemMenu(WindowObject, TRUE, TRUE);
    if(SystemMenu)
    {
      WindowObject->SystemMenu = SystemMenu->MenuInfo.Self;
//      IntReleaseMenuObject(SystemMenu);
    }
  }
CHECKPOINT1;
  /* Insert the window into the thread's window list. */
  InsertTailList (&PsGetWin32Thread()->WindowListHead, &WindowObject->ThreadListEntry);

  /* Allocate a DCE for this window. */
  if (dwStyle & CS_OWNDC)
    {
       CHECKPOINT1;
      WindowObject->Dce = UserDceAllocDCE(WindowObject->Self, DCE_WINDOW_DC);
    }
  /* FIXME:  Handle "CS_CLASSDC" */
CHECKPOINT1;
  Pos.x = x;
  Pos.y = y;
  Size.cx = nWidth;
  Size.cy = nHeight;

  /* call hook */
  Cs.lpCreateParams = lpParam;
  Cs.hInstance = hInstance;
  Cs.hMenu = hMenu;
  Cs.hwndParent = ParentWindowHandle;
  Cs.cx = Size.cx;
  Cs.cy = Size.cy;
  Cs.x = Pos.x;
  Cs.y = Pos.y;
  Cs.style = dwStyle;
  Cs.lpszName = (LPCWSTR) WindowName;
  Cs.lpszClass = (LPCWSTR) ClassName;
  Cs.dwExStyle = dwExStyle;
  CbtCreate.lpcs = &Cs;
  CbtCreate.hwndInsertAfter = HWND_TOP;
  CHECKPOINT1;
  if (HOOK_CallHooks(WH_CBT, HCBT_CREATEWND, (WPARAM) Handle, (LPARAM) &CbtCreate))
    {
CHECKPOINT1;
      /* FIXME - Delete window object and remove it from the thread windows list */
      /* FIXME - delete allocated DCE */

      ClassDereferenceClass(ClassObject);
      DPRINT1("CBT-hook returned !0\n");
      return (HWND) NULL;
    }
CHECKPOINT1;
  x = Cs.x;
  y = Cs.y;
  nWidth = Cs.cx;
  nHeight = Cs.cy;
CHECKPOINT1;
  /* default positioning for overlapped windows */
  if(!(WindowObject->Style & (WS_POPUP | WS_CHILD)))
  {
    RECT rc, WorkArea;
    PRTL_USER_PROCESS_PARAMETERS ProcessParams;
    BOOL CalculatedDefPosSize = FALSE;
CHECKPOINT1;
    IntGetDesktopWorkArea(WindowObject->OwnerThread->Tcb.Win32Thread->Desktop, &WorkArea);

    rc = WorkArea;
    ProcessParams = PsGetCurrentProcess()->Peb->ProcessParameters;

    if(x == CW_USEDEFAULT || x == CW_USEDEFAULT16)
    {
       CHECKPOINT1;
      CalculatedDefPosSize = IntCalcDefPosSize(ParentWindow, WindowObject, &rc, TRUE);

      if(ProcessParams->WindowFlags & STARTF_USEPOSITION)
      {
        ProcessParams->WindowFlags &= ~STARTF_USEPOSITION;
        Pos.x = WorkArea.left + ProcessParams->StartingX;
        Pos.y = WorkArea.top + ProcessParams->StartingY;
      }
      else
      {
        Pos.x = rc.left;
        Pos.y = rc.top;
      }

      /* According to wine, the ShowMode is set to y if x == CW_USEDEFAULT(16) and
         y is something else */
      if(y != CW_USEDEFAULT && y != CW_USEDEFAULT16)
      {
        dwShowMode = y;
      }
    }
    if(nWidth == CW_USEDEFAULT || nWidth == CW_USEDEFAULT16)
    {
      if(!CalculatedDefPosSize)
      {
         CHECKPOINT1;
        IntCalcDefPosSize(ParentWindow, WindowObject, &rc, FALSE);
      }
      if(ProcessParams->WindowFlags & STARTF_USESIZE)
      {
        ProcessParams->WindowFlags &= ~STARTF_USESIZE;
        Size.cx = ProcessParams->CountX;
        Size.cy = ProcessParams->CountY;
      }
      else
      {
        Size.cx = rc.right - rc.left;
        Size.cy = rc.bottom - rc.top;
      }

      /* move the window if necessary */
      if(Pos.x > rc.left)
        Pos.x = max(rc.left, 0);
      if(Pos.y > rc.top)
        Pos.y = max(rc.top, 0);
    }
  }
  else
  {
     CHECKPOINT1;
    /* if CW_USEDEFAULT(16) is set for non-overlapped windows, both values are set to zero) */
    if(x == CW_USEDEFAULT || x == CW_USEDEFAULT16)
    {
      Pos.x = 0;
      Pos.y = 0;
    }
    if(nWidth == CW_USEDEFAULT || nWidth == CW_USEDEFAULT16)
    {
      Size.cx = 0;
      Size.cy = 0;
    }
  }
CHECKPOINT1;
  /* Initialize the window dimensions. */
  WindowObject->WindowRect.left = Pos.x;
  WindowObject->WindowRect.top = Pos.y;
  WindowObject->WindowRect.right = Pos.x + Size.cx;
  WindowObject->WindowRect.bottom = Pos.y + Size.cy;
  if (0 != (WindowObject->Style & WS_CHILD) && ParentWindow)
    {CHECKPOINT1;
      IntGdiOffsetRect(&(WindowObject->WindowRect), ParentWindow->ClientRect.left,
                      ParentWindow->ClientRect.top);
                      CHECKPOINT1;
    }
  WindowObject->ClientRect = WindowObject->WindowRect;

  /*
   * Get the size and position of the window.
   */
  if ((dwStyle & WS_THICKFRAME) || !(dwStyle & (WS_POPUP | WS_CHILD)))
    {
      POINT MaxSize, MaxPos, MinTrack, MaxTrack;
CHECKPOINT1;
      /* WinPosGetMinMaxInfo sends the WM_GETMINMAXINFO message */
      WinPosGetMinMaxInfo(WindowObject, &MaxSize, &MaxPos, &MinTrack,
			  &MaxTrack);
           CHECKPOINT1;
      if (MaxSize.x < nWidth) nWidth = MaxSize.x;
      if (MaxSize.y < nHeight) nHeight = MaxSize.y;
      if (nWidth < MinTrack.x ) nWidth = MinTrack.x;
      if (nHeight < MinTrack.y ) nHeight = MinTrack.y;
      if (nWidth < 0) nWidth = 0;
      if (nHeight < 0) nHeight = 0;
    }
CHECKPOINT1;
  WindowObject->WindowRect.left = Pos.x;
  WindowObject->WindowRect.top = Pos.y;
  WindowObject->WindowRect.right = Pos.x + Size.cx;
  WindowObject->WindowRect.bottom = Pos.y + Size.cy;
  if (0 != (WindowObject->Style & WS_CHILD) && ParentWindow)
    {CHECKPOINT1;
      IntGdiOffsetRect(&(WindowObject->WindowRect), ParentWindow->ClientRect.left,
                      ParentWindow->ClientRect.top);
    }
  WindowObject->ClientRect = WindowObject->WindowRect;
CHECKPOINT1;
  /* FIXME: Initialize the window menu. */

  /* Send a NCCREATE message. */
  Cs.cx = Size.cx;
  Cs.cy = Size.cy;
  Cs.x = Pos.x;
  Cs.y = Pos.y;

  DPRINT("[win32k.window] IntCreateWindowEx style %d, exstyle %d, parent %d\n", Cs.style, Cs.dwExStyle, Cs.hwndParent);
  DPRINT("IntCreateWindowEx(): (%d,%d-%d,%d)\n", x, y, nWidth, nHeight);
  DPRINT("IntCreateWindowEx(): About to send NCCREATE message.\n");
  Result = IntSendMessage(WindowObject->Self, WM_NCCREATE, 0, (LPARAM) &Cs);
  if (!Result)
    {
      /* FIXME: Cleanup. */
      DPRINT("IntCreateWindowEx(): NCCREATE message failed.\n");
      return((HWND)0);
    }

  /* Calculate the non-client size. */
  MaxPos.x = WindowObject->WindowRect.left;
  MaxPos.y = WindowObject->WindowRect.top;
  DPRINT("IntCreateWindowEx(): About to get non-client size.\n");
  /* WinPosGetNonClientSize SENDS THE WM_NCCALCSIZE message */
  Result = WinPosGetNonClientSize(WindowObject,
				  &WindowObject->WindowRect,
				  &WindowObject->ClientRect);
  IntGdiOffsetRect(&WindowObject->WindowRect,
		 MaxPos.x - WindowObject->WindowRect.left,
		 MaxPos.y - WindowObject->WindowRect.top);

  if (NULL != ParentWindow)
    {
      /* link the window into the parent's child list */
      if ((dwStyle & (WS_CHILD|WS_MAXIMIZE)) == WS_CHILD)
        {
          PWINDOW_OBJECT PrevSibling;

          PrevSibling = ParentWindow->LastChild;

          /* link window as bottom sibling */
          IntLinkWindow(WindowObject, ParentWindow, PrevSibling /*prev sibling*/);
        }
      else
        {
          /* link window as top sibling (but after topmost siblings) */
          PWINDOW_OBJECT InsertAfter, Sibling;
          if (0 == (dwExStyle & WS_EX_TOPMOST))
            {
              InsertAfter = NULL;
              Sibling = ParentWindow->FirstChild;
              while (NULL != Sibling && 0 != (Sibling->ExStyle & WS_EX_TOPMOST))
                {
                  InsertAfter = Sibling;
                  Sibling = Sibling->NextSibling;
                }
            }
          else
            {
              InsertAfter = NULL;
            }

          IntLinkWindow(WindowObject, ParentWindow, InsertAfter /* prev sibling */);
        }
    }

  /* Send the WM_CREATE message. */
  DPRINT("IntCreateWindowEx(): about to send CREATE message.\n");
  Result = IntSendMessage(WindowObject->Self, WM_CREATE, 0, (LPARAM) &Cs);
  if (Result == (LRESULT)-1)
    {
      /* FIXME: Cleanup. */
      ClassDereferenceClass(ClassObject);
      DPRINT("IntCreateWindowEx(): send CREATE message failed.\n");
      return((HWND)0);
    }

  /* Send move and size messages. */
  if (!(WindowObject->Flags & WINDOWOBJECT_NEED_SIZE))
    {
      LONG lParam;

      DPRINT("IntCreateWindow(): About to send WM_SIZE\n");

      if ((WindowObject->ClientRect.right - WindowObject->ClientRect.left) < 0 ||
          (WindowObject->ClientRect.bottom - WindowObject->ClientRect.top) < 0)
      {
         DPRINT("Sending bogus WM_SIZE\n");
      }

      lParam = MAKE_LONG(WindowObject->ClientRect.right -
		  WindowObject->ClientRect.left,
		  WindowObject->ClientRect.bottom -
		  WindowObject->ClientRect.top);
      IntSendMessage(WindowObject->Self, WM_SIZE, SIZE_RESTORED,
          lParam);

      DPRINT("IntCreateWindow(): About to send WM_MOVE\n");

      if (0 != (WindowObject->Style & WS_CHILD) && ParentWindow)
	{
	  lParam = MAKE_LONG(WindowObject->ClientRect.left - ParentWindow->ClientRect.left,
	      WindowObject->ClientRect.top - ParentWindow->ClientRect.top);
	}
      else
	{
	  lParam = MAKE_LONG(WindowObject->ClientRect.left,
	      WindowObject->ClientRect.top);
	}
      IntSendMessage(WindowObject->Self, WM_MOVE, 0, lParam);

      /* Call WNDOBJ change procs */
      IntEngWindowChanged(WindowObject, WOC_RGN_CLIENT);
    }

  /* Show or maybe minimize or maximize the window. */
  if (WindowObject->Style & (WS_MINIMIZE | WS_MAXIMIZE))
    {
      RECT NewPos;
      UINT16 SwFlag;

      SwFlag = (WindowObject->Style & WS_MINIMIZE) ? SW_MINIMIZE :
	SW_MAXIMIZE;
      WinPosMinMaximize(WindowObject, SwFlag, &NewPos);
      SwFlag =
	((WindowObject->Style & WS_CHILD) || UserGetActiveWindow()) ?
	SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED :
	SWP_NOZORDER | SWP_FRAMECHANGED;
      DPRINT("IntCreateWindow(): About to minimize/maximize\n");
      DPRINT("%d,%d %dx%d\n", NewPos.left, NewPos.top, NewPos.right, NewPos.bottom);
      WinPosSetWindowPos(WindowObject->Self, 0, NewPos.left, NewPos.top,
			 NewPos.right, NewPos.bottom, SwFlag);
    }

  /* Notify the parent window of a new child. */
  if ((WindowObject->Style & WS_CHILD) &&
      (!(WindowObject->ExStyle & WS_EX_NOPARENTNOTIFY)) && ParentWindow)
    {
      DPRINT("IntCreateWindow(): About to notify parent\n");
      IntSendMessage(ParentWindow->Self,
                     WM_PARENTNOTIFY,
                     MAKEWPARAM(WM_CREATE, WindowObject->IDMenu),
                     (LPARAM)WindowObject->Self);
    }

  if ((!hWndParent) && (!HasOwner)) {
      DPRINT("Sending CREATED notify\n");
      IntShellHookNotify(HSHELL_WINDOWCREATED, (LPARAM)Handle);
  } else {
      DPRINT("Not sending CREATED notify, %x %d\n", ParentWindow, HasOwner);
  }

  /* Initialize and show the window's scrollbars */
  if (WindowObject->Style & WS_VSCROLL)
  {
     UserShowScrollBar(WindowObject, SB_VERT, TRUE);
  }
  if (WindowObject->Style & WS_HSCROLL)
  {
     UserShowScrollBar(WindowObject, SB_HORZ, TRUE);
  }

  if (dwStyle & WS_VISIBLE)
    {
      DPRINT("IntCreateWindow(): About to show window\n");
      WinPosShowWindow(WindowObject, dwShowMode);
    }

  DPRINT("IntCreateWindow(): = %X\n", Handle);
  DPRINT("WindowObject->SystemMenu = 0x%x\n", WindowObject->SystemMenu);
  return((HWND)Handle);
}

HWND STDCALL
NtUserCreateWindowEx(DWORD dwExStyle,
		     PUNICODE_STRING UnsafeClassName,
		     PUNICODE_STRING UnsafeWindowName,
		     DWORD dwStyle,
		     LONG x,
		     LONG y,
		     LONG nWidth,
		     LONG nHeight,
		     HWND hWndParent,
		     HMENU hMenu,
		     HINSTANCE hInstance,
		     LPVOID lpParam,
		     DWORD dwShowMode,
		     BOOL bUnicodeWindow)
{
  NTSTATUS Status;
  UNICODE_STRING WindowName;
  UNICODE_STRING ClassName;
  HWND NewWindow;
  DECLARE_RETURN(HWND);

  DPRINT("Enter NtUserCreateWindowEx(): (%d,%d-%d,%d)\n", x, y, nWidth, nHeight);
  UserEnterExclusive();

  /* Get the class name (string or atom) */
  Status = MmCopyFromCaller(&ClassName, UnsafeClassName, sizeof(UNICODE_STRING));
  if (! NT_SUCCESS(Status))
    {
      SetLastNtError(Status);
      RETURN(NULL);
    }
  if (! IS_ATOM(ClassName.Buffer))
    {
      Status = IntSafeCopyUnicodeStringTerminateNULL(&ClassName, UnsafeClassName);
      if (! NT_SUCCESS(Status))
        {
          SetLastNtError(Status);
          RETURN(NULL);
        }
    }

  /* safely copy the window name */
  if (NULL != UnsafeWindowName)
    {
      Status = IntSafeCopyUnicodeString(&WindowName, UnsafeWindowName);
      if (! NT_SUCCESS(Status))
        {
          if (! IS_ATOM(ClassName.Buffer))
            {
              RtlFreeUnicodeString(&ClassName);
            }
          SetLastNtError(Status);
          RETURN(NULL);
        }
    }
  else
    {
      RtlInitUnicodeString(&WindowName, NULL);
    }

  NewWindow = IntCreateWindowEx(dwExStyle, &ClassName, &WindowName, dwStyle, x, y, nWidth, nHeight,
		                hWndParent, hMenu, hInstance, lpParam, dwShowMode, bUnicodeWindow);

  RtlFreeUnicodeString(&WindowName);
  if (! IS_ATOM(ClassName.Buffer))
    {
      RtlFreeUnicodeString(&ClassName);
    }

  RETURN(NewWindow);
  
CLEANUP:
  DPRINT("Leave NtUserCreateWindowEx, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}

/*
 * @unimplemented
 */
HDWP STDCALL
NtUserDeferWindowPos(HDWP WinPosInfo,
		     HWND Wnd,
		     HWND WndInsertAfter,
		     int x,
         int y,
         int cx,
         int cy,
		     UINT Flags)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @implemented
 */
BOOLEAN STDCALL
NtUserDestroyWindow(HWND hWnd)
{
  PWINDOW_OBJECT Wnd;
  DECLARE_RETURN(BOOLEAN);

  DPRINT("Enter NtUserDestroyWindow\n");
  UserEnterExclusive();
  
  Wnd = IntGetWindowObject(hWnd);
  if (Wnd == NULL)
    {
       //FIX: last error
      RETURN(FALSE);
    }

  RETURN(UserDestroyWindow(Wnd)); 
  
CLEANUP:
  DPRINT("Leave NtUserDestroyWindow, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}





BOOLEAN FASTCALL
UserDestroyWindow(PWINDOW_OBJECT Wnd)
{
  BOOLEAN isChild;

  ASSERT(Wnd); 

  /* Check for owner thread and desktop window */
  //FIXME: move this inot NtUserDestroyWindow??
  
  if ((Wnd->OwnerThread != PsGetCurrentThread()) || IntIsDesktopWindow(Wnd))
    {
      SetLastWin32Error(ERROR_ACCESS_DENIED);
      return(FALSE);
    }

  /* Look whether the focus is within the tree of windows we will
   * be destroying.
   */
  if (!WinPosShowWindow(Wnd, SW_HIDE))
    {
      if (UserGetActiveWindow() == Wnd)
        {
          WinPosActivateOtherWindow(Wnd);
        }
    }

  if (Wnd->MessageQueue->ActiveWindow == Wnd->Self)
    Wnd->MessageQueue->ActiveWindow = NULL;
    
  if (Wnd->MessageQueue->FocusWindow == Wnd->Self)
    Wnd->MessageQueue->FocusWindow = NULL;
    
  if (Wnd->MessageQueue->CaptureWindow == Wnd->Self)
    Wnd->MessageQueue->CaptureWindow = NULL;


  /* Call hooks */
#if 0 /* FIXME */
  if (HOOK_CallHooks(WH_CBT, HCBT_DESTROYWND, (WPARAM) hwnd, 0, TRUE))
    {
    return(FALSE);
    }
#endif

  IntEngWindowChanged(Wnd, WOC_DELETE);
  isChild = (0 != (Wnd->Style & WS_CHILD));

#if 0 /* FIXME */
  if (isChild)
    {
      if (! USER_IsExitingThread(GetCurrentThreadId()))
   {
     send_parent_notify(hwnd, WM_DESTROY);
   }
    }
  else if (NULL != GetWindow(hWnd, GW_OWNER))
    {
      HOOK_CallHooks( WH_SHELL, HSHELL_WINDOWDESTROYED, (WPARAM)hwnd, 0L, TRUE );
      /* FIXME: clean up palette - see "Internals" p.352 */
    }
#endif

  if (!IntIsWindow(Wnd->Self))
    {
    return(TRUE);
    }

  /* Recursively destroy owned windows */
  if (! isChild)
    {
      for (;;)
   {
     BOOL GotOne = FALSE;
     HWND *List;
//   HWND *ChildHandle;
     PWINDOW_OBJECT Child, Desktop;

     Desktop = UserGetDesktopWindow();
     List = IntWinListChildren(Desktop);

     if (List)
       {
         int i; 
         for (i=0; List[i]; i++)
      {
        Child = IntGetWindowObject(List[i]);
        if (Child == NULL)
          continue;
        if (Child->Owner != Wnd->Self)
          {
            continue;
          }

        if (IntWndBelongsToThread(Child, PsGetWin32Thread()))
          {
            UserDestroyWindow(GetWnd(List[i]));
            GotOne = TRUE;
            continue;
          }

        if (Child->Owner != NULL)
          {
            Child->Owner = NULL;
          }

      }
         ExFreePool(List);
       }
     if (! GotOne)
       {
         break;
       }
   }
    }

  if (!IntIsWindow(Wnd->Self))
    {
      return(TRUE);
    }

  /* Destroy the window storage */
  IntDestroyWindow(Wnd, PsGetWin32Process(), PsGetWin32Thread(), TRUE);

  return(TRUE);
}




/*
 * @unimplemented
 */
DWORD
STDCALL
NtUserDrawMenuBarTemp(
  HWND hWnd,
  HDC hDC,
  PRECT hRect,
  HMENU hMenu,
  HFONT hFont)
{
  /* we'll use this function just for caching the menu bar */
  UNIMPLEMENTED
  return 0;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserEndDeferWindowPosEx(DWORD Unknown0,
			  DWORD Unknown1)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserFillWindow(DWORD Unknown0,
		 DWORD Unknown1,
		 DWORD Unknown2,
		 DWORD Unknown3)
{
  UNIMPLEMENTED

  return 0;
}


HWND FASTCALL
IntFindWindow(PWINDOW_OBJECT Parent,
              PWINDOW_OBJECT ChildAfter,
              RTL_ATOM ClassAtom,
              PUNICODE_STRING WindowName)
{
  BOOL CheckWindowName;
  HWND *List, *pWnd;
  HWND Ret = NULL;

  ASSERT(Parent);

  CheckWindowName = (WindowName && (WindowName->Length > 0));

  if((List = IntWinListChildren(Parent)))
  {
    pWnd = List;
    if(ChildAfter)
    {
      /* skip handles before and including ChildAfter */
      while(*pWnd && (*(pWnd++) != ChildAfter->Self));
    }

    /* search children */
    while(*pWnd)
    {
      PWINDOW_OBJECT Child;// = *pWnd;
      if(!(Child = IntGetWindowObject(*(pWnd++))))
      {
        continue;
      }

      /* Do not send WM_GETTEXT messages in the kernel mode version!
         The user mode version however calls GetWindowText() which will
         send WM_GETTEXT messages to windows belonging to its processes */
      if((!CheckWindowName || !RtlCompareUnicodeString(WindowName, &(Child->WindowName), FALSE)) &&
         (!ClassAtom || Child->Class->Atom == ClassAtom))
      {
        Ret = Child->Self;
        break;
      }

    }
    ExFreePool(List);
  }

  return Ret;
}

/*
 * FUNCTION:
 *   Searches a window's children for a window with the specified
 *   class and name
 * ARGUMENTS:
 *   hwndParent	    = The window whose childs are to be searched.
 *					  NULL = desktop
 *					  HWND_MESSAGE = message-only windows
 *
 *   hwndChildAfter = Search starts after this child window.
 *					  NULL = start from beginning
 *
 *   ucClassName    = Class name to search for
 *					  Reguired parameter.
 *
 *   ucWindowName   = Window name
 *					  ->Buffer == NULL = don't care
 *
 * RETURNS:
 *   The HWND of the window if it was found, otherwise NULL
 */
/*
 * @implemented
 */
HWND STDCALL
NtUserFindWindowEx(HWND hwndParent,
		   HWND hwndChildAfter,
		   PUNICODE_STRING ucClassName,
		   PUNICODE_STRING ucWindowName)
{
  PWINDOW_OBJECT Parent, ChildAfter;
  UNICODE_STRING ClassName, WindowName;
  NTSTATUS Status;
  HWND Desktop, Ret = NULL;
  RTL_ATOM ClassAtom;
  DECLARE_RETURN(HWND);

  DPRINT("Enter NtUserFindWindowEx\n");
  UserEnterExclusive();

  Desktop = IntGetCurrentThreadDesktopWindow();

  if(hwndParent == NULL)
    hwndParent = Desktop;
  /* FIXME
  else if(hwndParent == HWND_MESSAGE)
  {
    hwndParent = IntGetMessageWindow();
  }
  */

  if(!(Parent = IntGetWindowObject(hwndParent)))
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    RETURN( NULL);
  }

  ChildAfter = NULL;
  if(hwndChildAfter && !(ChildAfter = IntGetWindowObject(hwndChildAfter)))
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    RETURN( NULL);
  }

  /* copy the window name */
  Status = IntSafeCopyUnicodeString(&WindowName, ucWindowName);
  if(!NT_SUCCESS(Status))
  {
    SetLastNtError(Status);
    goto Cleanup3;
  }

  /* safely copy the class name */
  Status = MmCopyFromCaller(&ClassName, ucClassName, sizeof(UNICODE_STRING));
  if(!NT_SUCCESS(Status))
  {
    SetLastNtError(Status);
    goto Cleanup2;
  }
  if(ClassName.Length > 0 && ClassName.Buffer)
  {
    WCHAR *buf;
    /* safely copy the class name string (NULL terminated because class-lookup
       depends on it... */
    buf = ExAllocatePoolWithTag(PagedPool, ClassName.Length + sizeof(WCHAR), TAG_STRING);
    if(!buf)
    {
      SetLastWin32Error(STATUS_INSUFFICIENT_RESOURCES);
      goto Cleanup2;
    }
    Status = MmCopyFromCaller(buf, ClassName.Buffer, ClassName.Length);
    if(!NT_SUCCESS(Status))
    {
      ExFreePool(buf);
      SetLastNtError(Status);
      goto Cleanup2;
    }
    ClassName.Buffer = buf;
    /* make sure the string is null-terminated */
    buf += ClassName.Length / sizeof(WCHAR);
    *buf = L'\0';
  }

  /* find the class object */
  if(ClassName.Buffer)
    {
      PWINSTATION_OBJECT WinStaObject;

      if (PsGetWin32Thread()->Desktop == NULL)
        {
          SetLastWin32Error(ERROR_INVALID_HANDLE);
          goto Cleanup;
        }

      WinStaObject = PsGetWin32Thread()->Desktop->WindowStation;

      Status = RtlLookupAtomInAtomTable(
         WinStaObject->AtomTable,
         ClassName.Buffer,
         &ClassAtom);

      if (!NT_SUCCESS(Status))
        {
          DPRINT1("Failed to lookup class atom!\n");
          SetLastWin32Error(ERROR_CLASS_DOES_NOT_EXIST);
          goto Cleanup;
        }
  }

  if(Parent->Self == Desktop)
  {
    HWND *List, *pWnd;
    PWINDOW_OBJECT TopLevelWindow;
    BOOLEAN CheckWindowName;
    BOOLEAN CheckClassName;
    BOOLEAN WindowMatches;
    BOOLEAN ClassMatches;

    /* windows searches through all top-level windows if the parent is the desktop
       window */

    if((List = IntWinListChildren(Parent)))
    {
      pWnd = List;

      if(ChildAfter)
      {
        /* skip handles before and including ChildAfter */
        while(*pWnd && (*(pWnd++) != ChildAfter->Self));
      }

      CheckWindowName = WindowName.Length > 0;
      CheckClassName = ClassName.Buffer != NULL;

      /* search children */
      while(*pWnd)
      {
        if(!(TopLevelWindow = IntGetWindowObject(*(pWnd++))))
        {
          continue;
        }
//        TopLevelWindow = *pWnd;

        /* Do not send WM_GETTEXT messages in the kernel mode version!
           The user mode version however calls GetWindowText() which will
           send WM_GETTEXT messages to windows belonging to its processes */
        WindowMatches = !CheckWindowName || !RtlCompareUnicodeString(
                        &WindowName, &TopLevelWindow->WindowName, FALSE);
        ClassMatches = !CheckClassName ||
                       ClassAtom == TopLevelWindow->Class->Atom;

        if (WindowMatches && ClassMatches)
        {
          Ret = TopLevelWindow->Self;
          break;
        }

        if (IntFindWindow(TopLevelWindow, NULL, ClassAtom, &WindowName))
        {
          /* window returns the handle of the top-level window, in case it found
             the child window */
          Ret = TopLevelWindow->Self;
          break;
        }

      }
      ExFreePool(List);
    }
  }
  else
    Ret = IntFindWindow(Parent, ChildAfter, ClassAtom, &WindowName);

#if 0
  if(Ret == NULL && hwndParent == NULL && hwndChildAfter == NULL)
  {
    /* FIXME - if both hwndParent and hwndChildAfter are NULL, we also should
               search the message-only windows. Should this also be done if
               Parent is the desktop window??? */
    PWINDOW_OBJECT MsgWindows;

    if((MsgWindows = IntGetWindowObject(IntGetMessageWindow())))
    {
      Ret = IntFindWindow(MsgWindows, ChildAfter, ClassAtom, &WindowName);
    }
  }
#endif

  Cleanup:
  if(ClassName.Length > 0 && ClassName.Buffer)
    ExFreePool(ClassName.Buffer);

  Cleanup2:
  RtlFreeUnicodeString(&WindowName);

  Cleanup3:

  RETURN( Ret);
  
CLEANUP:
  DPRINT("Leave NtUserFindWindowEx, ret %i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserFlashWindowEx(DWORD Unknown0)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @implemented
 */
HWND STDCALL
NtUserGetAncestor(HWND hWnd, UINT Type)
{
   PWINDOW_OBJECT Wnd;
   DECLARE_RETURN(HWND);
   
   DPRINT("Enter NtUserGetAncestor\n");
   UserEnterExclusive();
  
   if (!(Wnd = IntGetWindowObject(hWnd)))
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(NULL);
   }
  
   RETURN(GetHwnd(UserGetAncestor(Wnd, Type)));
   
CLEANUP:
   DPRINT("Leave NtUserGetAncestor, ret=%i\n",_ret_);
   UserLeave();
   END_CLEANUP;
}


/*
 * @implemented
 */
PWINDOW_OBJECT FASTCALL
UserGetAncestor(PWINDOW_OBJECT Wnd, UINT Type)
{
   PWINDOW_OBJECT WndAncestor = NULL, Parent;
   
   if (Wnd == UserGetDesktopWindow())
   //if (IntIsDesktopWindow(Wnd)) ???
   {
      return NULL;
   }

   switch (Type)
   {
      case GA_PARENT:
      {
         WndAncestor = Wnd->ParentWnd;
         break;
      }

      case GA_ROOT:
      {
         PWINDOW_OBJECT tmp;
         WndAncestor = Wnd;
         Parent = NULL;

         for(;;)
         {
           tmp = Parent;
           if(!(Parent = WndAncestor->ParentWnd))
           {
             break;
           }
           if(IntIsDesktopWindow(Parent))
           {
             break;
           }
           WndAncestor = Parent;
         }
         break;
      }

      case GA_ROOTOWNER:
      {
         WndAncestor = Wnd;
         for (;;)
         {
            PWINDOW_OBJECT Old;
            Old = WndAncestor;
            Parent = UserGetParent(WndAncestor);
            if (!Parent)
            {
              break;
            }
            WndAncestor = Parent;
         }
         break;
      }

      default:
      {
         return NULL;
      }
   }

   return WndAncestor;
}



/*!
 * Returns client window rectangle relative to the upper-left corner of client area.
 *
 * \param	hWnd	window handle.
 * \param	Rect	pointer to the buffer where the coordinates are returned.
 *
*/
/*
 * @implemented
 */
BOOL STDCALL
NtUserGetClientRect(HWND hWnd, LPRECT Rect)
{
  PWINDOW_OBJECT WindowObject;
  RECT SafeRect;
  DECLARE_RETURN(BOOL);
  
  DPRINT("Enter NtUserGetClientRect\n");
  UserEnterExclusive();
  
  if(!(WindowObject = IntGetWindowObject(hWnd)))
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    RETURN(FALSE);
  }

   //FIXME: check retval?
  IntGetClientRect(WindowObject, &SafeRect);

  if(!NT_SUCCESS(MmCopyToCaller(Rect, &SafeRect, sizeof(RECT))))
  {
    RETURN(FALSE);
  }
  RETURN(TRUE);
  
CLEANUP:
  DPRINT("Leave NtUserGetClientRect, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}


/*
 * @implemented
 */
HWND STDCALL
NtUserGetDesktopWindow()
{  
   DECLARE_RETURN(HWND);
   
   DPRINT("Enter NtUserGetDesktopWindow\n");
   UserEnterExclusive();   
   
   RETURN(GetHwnd(UserGetDesktopWindow()));
   
CLEANUP:
   DPRINT("Leave NtUserGetDesktopWindow, ret=%i\n",_ret_);
   UserLeave();
   END_CLEANUP;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserGetInternalWindowPos(DWORD Unknown0,
			   DWORD Unknown1,
			   DWORD Unknown2)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @unimplemented
 */
HWND STDCALL
NtUserGetLastActivePopup(HWND hWnd)
{
/*
 * This code can't work, because hWndLastPopup member of WINDOW_OBJECT is
 * not changed anywhere.
 * -- Filip, 01/nov/2003
 */
#if 0
  PWINDOW_OBJECT Wnd;
  HWND hWndLastPopup;

  IntAcquireWinLockShared();

  if (!(Wnd = IntGetWindowObject(hWnd)))
  {
    IntReleaseWinLock();
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }

  hWndLastPopup = Wnd->hWndLastPopup;

  IntReleaseWinLock();

  return hWndLastPopup;
#else
   return NULL;
#endif
}

/*
 * NtUserGetParent
 *
 * The NtUserGetParent function retrieves a handle to the specified window's
 * parent or owner.
 *
 * Remarks
 *    Note that, despite its name, this function can return an owner window
 *    instead of a parent window.
 *
 * Status
 *    @implemented
 */

HWND STDCALL
NtUserGetParent(HWND hWnd)
{
   PWINDOW_OBJECT Wnd;
   DECLARE_RETURN(HWND);
   
   DPRINT("Enter NtUserGetParent\n");
   UserEnterExclusive();
   
   if (!(Wnd = IntGetWindowObject(hWnd)))
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(NULL);
   }

   RETURN(GetHwnd(UserGetParent(Wnd)));
   
CLEANUP:
  DPRINT("Leave NtUserGetParent, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}

/*
 * NtUserSetParent
 *
 * The NtUserSetParent function changes the parent window of the specified
 * child window.
 *
 * Remarks
 *    The new parent window and the child window must belong to the same
 *    application. If the window identified by the hWndChild parameter is
 *    visible, the system performs the appropriate redrawing and repainting.
 *    For compatibility reasons, NtUserSetParent does not modify the WS_CHILD
 *    or WS_POPUP window styles of the window whose parent is being changed.
 *
 * Status
 *    @implemented
 */

HWND STDCALL
NtUserSetParent(HWND hWndChild, HWND hWndNewParent)
{
   PWINDOW_OBJECT Wnd = NULL, WndNewParent = NULL;
   DECLARE_RETURN(HWND);

   DPRINT("Enter NtUserSetParent\n");
   UserEnterExclusive();
  
   if (IntIsBroadcastHwnd(hWndChild) || IntIsBroadcastHwnd(hWndNewParent))
   {
      SetLastWin32Error(ERROR_INVALID_PARAMETER);
      RETURN(NULL);
   }

   if (hWndNewParent)
   {
      if (!(WndNewParent = IntGetWindowObject(hWndNewParent)))
      {
         SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
         RETURN(NULL);
      }
   }

   if (!(Wnd = IntGetWindowObject(hWndChild)))
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(NULL);
   }

   RETURN(GetHwnd(UserSetParent(Wnd, WndNewParent)));

CLEANUP:
  DPRINT("Leave NtUserSetParent, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}

/*
 * NtUserGetShellWindow
 *
 * Returns a handle to shell window that was set by NtUserSetShellWindowEx.
 *
 * Status
 *    @implemented
 */

HWND STDCALL
NtUserGetShellWindow()
{
  DECLARE_RETURN(HWND);
  
  DPRINT("Enter NtUserGetShellWindow\n");
  UserEnterExclusive();
  
  RETURN(GetHwnd(UserGetShellWindow()));
  
CLEANUP:
  DPRINT("Leave NtUserGetShellWindow, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}


PWINDOW_OBJECT FASTCALL UserGetShellWindow()
{
  PWINSTATION_OBJECT WinStaObject;
  HWND Ret;
 
  NTSTATUS Status = IntValidateWindowStationHandle(PsGetCurrentProcess()->Win32WindowStation,
                   KernelMode,
                   0,
                   &WinStaObject);

  if (!NT_SUCCESS(Status))
  {
    SetLastNtError(Status);
    return(NULL);
  }

  Ret = (HWND)WinStaObject->ShellWindow;
  ObDereferenceObject(WinStaObject);
  return(GetWnd(Ret));
}

/*
 * NtUserSetShellWindowEx
 *
 * This is undocumented function to set global shell window. The global
 * shell window has special handling of window position.
 *
 * Status
 *    @implemented
 */

BOOL STDCALL
NtUserSetShellWindowEx(HWND hwndShell, HWND hwndListView)
{
  PWINSTATION_OBJECT WinStaObject;
  DECLARE_RETURN(BOOL);
  
  DPRINT("Enter NtUserSetShellWindowEx\n");
  UserEnterExclusive();
  
  NTSTATUS Status = IntValidateWindowStationHandle(PsGetCurrentProcess()->Win32WindowStation,
				       KernelMode,
				       0,
				       &WinStaObject);

  if (!NT_SUCCESS(Status))
  {
    SetLastNtError(Status);
    RETURN(FALSE);
  }

   /*
    * Test if we are permitted to change the shell window.
    */
   if (WinStaObject->ShellWindow)
   {
      ObDereferenceObject(WinStaObject);
      RETURN(FALSE);
   }

   /*
    * Move shell window into background.
    */
   if (hwndListView && hwndListView != hwndShell)
   {
/*
 * Disabled for now to get Explorer working.
 * -- Filip, 01/nov/2003
 */
#if 0
       WinPosSetWindowPos(hwndListView, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
#endif

      if (UserGetWindowLong(GetWnd(hwndListView), GWL_EXSTYLE, FALSE) & WS_EX_TOPMOST)
      {
         ObDereferenceObject(WinStaObject);
         RETURN(FALSE);
      }
   }

   if (UserGetWindowLong(GetWnd(hwndShell), GWL_EXSTYLE, FALSE) & WS_EX_TOPMOST)
   {
      ObDereferenceObject(WinStaObject);
      RETURN(FALSE);
   }

   WinPosSetWindowPos(hwndShell, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

   WinStaObject->ShellWindow = hwndShell;
   WinStaObject->ShellListView = hwndListView;

   ObDereferenceObject(WinStaObject);
   RETURN(TRUE);
   
CLEANUP:
   DPRINT("Leave NtUserSetShellWindowEx, ret=%i\n",_ret_);
   UserLeave();
   END_CLEANUP;
}

/*
 * NtUserGetSystemMenu
 *
 * The NtUserGetSystemMenu function allows the application to access the
 * window menu (also known as the system menu or the control menu) for
 * copying and modifying.
 *
 * Parameters
 *    hWnd
 *       Handle to the window that will own a copy of the window menu.
 *    bRevert
 *       Specifies the action to be taken. If this parameter is FALSE,
 *       NtUserGetSystemMenu returns a handle to the copy of the window menu
 *       currently in use. The copy is initially identical to the window menu
 *       but it can be modified.
 *       If this parameter is TRUE, GetSystemMenu resets the window menu back
 *       to the default state. The previous window menu, if any, is destroyed.
 *
 * Return Value
 *    If the bRevert parameter is FALSE, the return value is a handle to a
 *    copy of the window menu. If the bRevert parameter is TRUE, the return
 *    value is NULL.
 *
 * Status
 *    @implemented
 */

HMENU STDCALL
NtUserGetSystemMenu(HWND hWnd, BOOL bRevert)
{
   HMENU Result = 0;
   PWINDOW_OBJECT WindowObject;
   PMENU_OBJECT MenuObject;
   DECLARE_RETURN(HMENU);
   
   DPRINT("Enter NtUserGetSystemMenu\n");
   UserEnterExclusive();
  
   WindowObject = IntGetWindowObject((HWND)hWnd);
   if (WindowObject == NULL)
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(0);
   }

   MenuObject = IntGetSystemMenu(WindowObject, bRevert, FALSE);
   if (MenuObject)
   {
      Result = MenuObject->MenuInfo.Self;
   }

   RETURN(Result);
   
CLEANUP:
   DPRINT("Leave NtUserGetSystemMenu, ret=%i\n",_ret_);
   UserLeave();
   END_CLEANUP;
}

/*
 * NtUserSetSystemMenu
 *
 * Status
 *    @implemented
 */

BOOL STDCALL
NtUserSetSystemMenu(HWND hWnd, HMENU hMenu)
{
   BOOL Result = FALSE;
   PWINDOW_OBJECT WindowObject;
   PMENU_OBJECT MenuObject;
   DECLARE_RETURN(BOOL);
   
   DPRINT("Enter NtUserSetSystemMenu\n");
   UserEnterExclusive();
  
   WindowObject = IntGetWindowObject(hWnd);
   if (!WindowObject)
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(FALSE);
   }

   if (hMenu)
   {
      /*
       * Assign new menu handle.
       */
      MenuObject = IntGetMenuObject(hMenu);
      if (!MenuObject)
      {
         SetLastWin32Error(ERROR_INVALID_MENU_HANDLE);
         RETURN(FALSE);
      }

      Result = IntSetSystemMenu(WindowObject, MenuObject);
   }

   RETURN(Result);
   
CLEANUP:
   DPRINT("Leave NtUserSetSystemMenu, ret=%i\n",_ret_);
   UserLeave();
   END_CLEANUP;
}

/*
 * NtUserGetWindow
 *
 * The NtUserGetWindow function retrieves a handle to a window that has the
 * specified relationship (Z order or owner) to the specified window.
 *
 * Status
 *    @implemented
 */

HWND STDCALL
NtUserGetWindow(HWND hWnd, UINT Relationship)
{
   PWINDOW_OBJECT Wnd;
   DECLARE_RETURN(HWND);
   
   DPRINT("Enter NtUserGetWindow\n");
   UserEnterExclusive();
  
   if (!(Wnd = IntGetWindowObject(hWnd)))
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(NULL);
   }

   RETURN(GetHwnd(UserGetWindow(Wnd, Relationship)));
   
CLEANUP:
   DPRINT("Leave NtUserGetWindow, ret=%i\n",_ret_);
   UserLeave();
   END_CLEANUP;
}




PWINDOW_OBJECT FASTCALL
UserGetWindow(PWINDOW_OBJECT Wnd, UINT Relationship)
{
   PWINDOW_OBJECT WndResult = NULL, Parent;

   ASSERT(Wnd);
  
   switch (Relationship)
   {
      case GW_HWNDFIRST:
         if((Parent = Wnd->ParentWnd))
         {
           if (Parent->FirstChild)
              WndResult = Parent->FirstChild;
         }
         break;

      case GW_HWNDLAST:
         if((Parent = Wnd->ParentWnd))
         {
           if (Parent->LastChild)
              WndResult = Parent->LastChild;
         }
         break;

      case GW_HWNDNEXT:
         if (Wnd->NextSibling)
            WndResult = Wnd->NextSibling;
         break;

      case GW_HWNDPREV:
         if (Wnd->PrevSibling)
            WndResult = Wnd->PrevSibling;
         break;

      case GW_OWNER:
         if((Parent = IntGetWindowObject(Wnd->Owner)))
         {
           WndResult = Parent;
         }
         break;
      case GW_CHILD:
         if (Wnd->FirstChild)
            WndResult = Wnd->FirstChild;
         break;
   }

   return WndResult;
}



/*
 * NtUserGetWindowLong
 *
 * The NtUserGetWindowLong function retrieves information about the specified
 * window. The function also retrieves the 32-bit (long) value at the
 * specified offset into the extra window memory.
 *
 * Status
 *    @implemented
 */

LONG STDCALL
NtUserGetWindowLong(HWND hWnd, DWORD Index, BOOL Ansi)
{
   PWINDOW_OBJECT WindowObject;
   DECLARE_RETURN(LONG);
   
   DPRINT("Enter NtUserGetWindowLong(%x,%d,%d)\n", hWnd, (INT)Index, Ansi);
   UserEnterExclusive();

   WindowObject = IntGetWindowObject(hWnd);
   if (WindowObject == NULL)
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(0);
   }

   RETURN(UserGetWindowLong(WindowObject, Index, Ansi));
   
CLEANUP:
   DPRINT("Leave NtUserGetWindowLong, ret=%i\n",_ret_);
   UserLeave();
   END_CLEANUP;   
}





LONG FASTCALL
UserGetWindowLong(PWINDOW_OBJECT Wnd, DWORD Index, BOOL Ansi)
{
   PWINDOW_OBJECT Parent;
   LONG Result = 0;
   
   ASSERT(Wnd);
   
   /*
    * WndProc is only available to the owner process
    */
   if (GWL_WNDPROC == Index
       && Wnd->OwnerThread->ThreadsProcess != PsGetCurrentProcess())
   {
      SetLastWin32Error(ERROR_ACCESS_DENIED);
      return(0);
   }

   if ((INT)Index >= 0)
   {
      if ((Index + sizeof(LONG)) > Wnd->ExtraDataSize)
      {
         SetLastWin32Error(ERROR_INVALID_PARAMETER);
         return(0);
      }
      Result = *((LONG *)(Wnd->ExtraData + Index));
   }
   else
   {
      switch (Index)
      {
         case GWL_EXSTYLE:
            Result = Wnd->ExStyle;
            break;

         case GWL_STYLE:
            Result = Wnd->Style;
            break;

         case GWL_WNDPROC:
            if (Ansi)
               Result = (LONG) Wnd->WndProcA;
            else
               Result = (LONG) Wnd->WndProcW;
            break;

         case GWL_HINSTANCE:
            Result = (LONG) Wnd->Instance;
            break;

         case GWL_HWNDPARENT:
            Parent = Wnd->ParentWnd;
            if(Parent)
            {
              if (Parent && Parent == UserGetDesktopWindow())
                 Result = (LONG) GetHwnd(UserGetWindow(Wnd, GW_OWNER));
              else
                 Result = (LONG) Parent->Self;
            }
            break;

         case GWL_ID:
            Result = (LONG) Wnd->IDMenu;
            break;

         case GWL_USERDATA:
            Result = Wnd->UserData;
            break;

         default:
            DPRINT1("NtUserGetWindowLong(): Unsupported index %d\n", Index);
            SetLastWin32Error(ERROR_INVALID_PARAMETER);
            Result = 0;
            break;
      }
   }

   return(Result);
}




/*
 * NtUserSetWindowLong
 *
 * The NtUserSetWindowLong function changes an attribute of the specified
 * window. The function also sets the 32-bit (long) value at the specified
 * offset into the extra window memory.
 *
 * Status
 *    @implemented
 */

LONG STDCALL
NtUserSetWindowLong(HWND hWnd, DWORD Index, LONG NewValue, BOOL Ansi)
{
   PWINDOW_OBJECT Wnd;
   DECLARE_RETURN(LONG);
   
   DPRINT("Enter NtUserSetWindowLong\n");
   UserEnterExclusive();
  
   Wnd = IntGetWindowObject(hWnd);
   if (Wnd == NULL)
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(0);
   }

   RETURN(UserSetWindowLong(Wnd, Index, NewValue, Ansi));
   
CLEANUP:
   DPRINT("Leave NtUserSetWindowLong, ret=%i\n",_ret_);
   UserLeave();
   END_CLEANUP;
}




/*
 * NtUserSetWindowLong
 *
 * The NtUserSetWindowLong function changes an attribute of the specified
 * window. The function also sets the 32-bit (long) value at the specified
 * offset into the extra window memory.
 *
 * Status
 *    @implemented
 */

LONG FASTCALL
UserSetWindowLong(PWINDOW_OBJECT Wnd, DWORD Index, LONG NewValue, BOOL Ansi)
{
   PWINDOW_OBJECT WindowObject, Parent;
   PWINSTATION_OBJECT WindowStation;
   LONG OldValue;
   STYLESTRUCT Style;
   
   if (Wnd == UserGetDesktopWindow())
   {
      SetLastWin32Error(STATUS_ACCESS_DENIED);
      return(0);
   }

   if ((INT)Index >= 0)
   {
      if ((Index + sizeof(LONG)) > Wnd->ExtraDataSize)
      {
         SetLastWin32Error(ERROR_INVALID_PARAMETER);
         return(0);
      }
      OldValue = *((LONG *)(Wnd->ExtraData + Index));
      *((LONG *)(Wnd->ExtraData + Index)) = NewValue;
   }
   else
   {
      switch (Index)
      {
         case GWL_EXSTYLE:
            OldValue = (LONG) Wnd->ExStyle;
            Style.styleOld = OldValue;
            Style.styleNew = NewValue;

            /*
             * Remove extended window style bit WS_EX_TOPMOST for shell windows.
             */
            WindowStation = Wnd->OwnerThread->Tcb.Win32Thread->Desktop->WindowStation;
            if(WindowStation)
            {
              if (GetHwnd(Wnd) == WindowStation->ShellWindow || GetHwnd(Wnd) == WindowStation->ShellListView)
                 Style.styleNew &= ~WS_EX_TOPMOST;
            }

            IntSendMessage(GetHwnd(Wnd), WM_STYLECHANGING, GWL_EXSTYLE, (LPARAM) &Style);
            Wnd->ExStyle = (DWORD)Style.styleNew;
            IntSendMessage(GetHwnd(Wnd), WM_STYLECHANGED, GWL_EXSTYLE, (LPARAM) &Style);
            break;

         case GWL_STYLE:
            OldValue = (LONG) Wnd->Style;
            Style.styleOld = OldValue;
            Style.styleNew = NewValue;
            IntSendMessage(GetHwnd(Wnd), WM_STYLECHANGING, GWL_STYLE, (LPARAM) &Style);
            Wnd->Style = (DWORD)Style.styleNew;
            IntSendMessage(GetHwnd(Wnd), WM_STYLECHANGED, GWL_STYLE, (LPARAM) &Style);
            break;

         case GWL_WNDPROC:
            /* FIXME: should check if window belongs to current process */
            if (Ansi)
            {
               OldValue = (LONG) Wnd->WndProcA;
               Wnd->WndProcA = (WNDPROC) NewValue;
               Wnd->WndProcW = (WNDPROC) IntAddWndProcHandle((WNDPROC)NewValue,FALSE);
               Wnd->Unicode = FALSE;
            }
            else
            {
               OldValue = (LONG) Wnd->WndProcW;
               Wnd->WndProcW = (WNDPROC) NewValue;
               Wnd->WndProcA = (WNDPROC) IntAddWndProcHandle((WNDPROC)NewValue,TRUE);
               Wnd->Unicode = TRUE;
            }
            break;

         case GWL_HINSTANCE:
            OldValue = (LONG) Wnd->Instance;
            Wnd->Instance = (HINSTANCE) NewValue;
            break;

         case GWL_HWNDPARENT:
            Parent = Wnd->ParentWnd;
            if (Parent && (Parent == UserGetDesktopWindow()))
               OldValue = (LONG) IntSetOwner(Wnd->Self, (HWND) NewValue);
            else
               OldValue = (LONG) NtUserSetParent(Wnd->Self, (HWND) NewValue);//FIXME
            break;

         case GWL_ID:
            OldValue = (LONG) Wnd->IDMenu;
            WindowObject->IDMenu = (UINT) NewValue;
            break;

         case GWL_USERDATA:
            OldValue = Wnd->UserData;
            Wnd->UserData = NewValue;
            break;

         default:
            DPRINT1("NtUserSetWindowLong(): Unsupported index %d\n", Index);
            SetLastWin32Error(ERROR_INVALID_PARAMETER);
            OldValue = 0;
            break;
      }
   }

   return(OldValue);
   
}



/*
 * NtUserSetWindowWord
 *
 * Legacy function similar to NtUserSetWindowLong.
 *
 * Status
 *    @implemented
 */

WORD STDCALL
NtUserSetWindowWord(HWND hWnd, INT Index, WORD NewValue)
{
   PWINDOW_OBJECT WindowObject;
   WORD OldValue;
   DECLARE_RETURN(WORD);
   
   DPRINT("Enter NtUserSetWindowWord\n");
   UserEnterExclusive();   
   
   switch (Index)
   {
      case GWL_ID:
      case GWL_HINSTANCE:
      case GWL_HWNDPARENT:
         RETURN(UserSetWindowLong(GetWnd(hWnd), Index, (UINT)NewValue, TRUE));
      default:
         if (Index < 0)
         {
            SetLastWin32Error(ERROR_INVALID_INDEX);
            RETURN(0);
         }
   }

   WindowObject = IntGetWindowObject(hWnd);
   if (WindowObject == NULL)
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(0);
   }

   if (Index > WindowObject->ExtraDataSize - sizeof(WORD))
   {
      SetLastWin32Error(ERROR_INVALID_PARAMETER);
      RETURN(0);
   }

   OldValue = *((WORD *)(WindowObject->ExtraData + Index));
   *((WORD *)(WindowObject->ExtraData + Index)) = NewValue;

   RETURN(OldValue);
   
CLEANUP:
  DPRINT("Leave NtUserSetWindowWord, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}

/*
 * @implemented
 */
BOOL STDCALL
NtUserGetWindowPlacement(HWND hWnd,
			 WINDOWPLACEMENT *lpwndpl)
{
  PWINDOW_OBJECT WindowObject;
  PINTERNALPOS InternalPos;
  POINT Size;
  WINDOWPLACEMENT Safepl;
  NTSTATUS Status;
  DECLARE_RETURN(BOOL); 

  DPRINT("Enter NtUserGetWindowPlacement\n");
  UserEnterExclusive();

  WindowObject = IntGetWindowObject(hWnd);
  if (WindowObject == NULL)
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    RETURN(FALSE);
  }

  Status = MmCopyFromCaller(&Safepl, lpwndpl, sizeof(WINDOWPLACEMENT));
  if(!NT_SUCCESS(Status))
  {
    SetLastNtError(Status);
    RETURN(FALSE);
  }
  if(Safepl.length != sizeof(WINDOWPLACEMENT))
  {
    RETURN(FALSE);
  }

  Safepl.flags = 0;
  Safepl.showCmd = ((WindowObject->Flags & WINDOWOBJECT_RESTOREMAX) ? SW_MAXIMIZE : SW_SHOWNORMAL);

  Size.x = WindowObject->WindowRect.left;
  Size.y = WindowObject->WindowRect.top;
  InternalPos = WinPosInitInternalPos(WindowObject, &Size,
				      &WindowObject->WindowRect);
  if (InternalPos)
  {
    Safepl.rcNormalPosition = InternalPos->NormalRect;
    Safepl.ptMinPosition = InternalPos->IconPos;
    Safepl.ptMaxPosition = InternalPos->MaxPos;
  }
  else
  {
    RETURN(FALSE);
  }

  Status = MmCopyToCaller(lpwndpl, &Safepl, sizeof(WINDOWPLACEMENT));
  if(!NT_SUCCESS(Status))
  {
    SetLastNtError(Status);
    RETURN(FALSE);
  }

  RETURN(TRUE);
  
CLEANUP:
  DPRINT("Leave NtUserGetWindowPlacement, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}


/*!
 * Return the dimension of the window in the screen coordinates.
 * \param	hWnd	window handle.
 * \param	Rect	pointer to the buffer where the coordinates are returned.
*/
/*
 * @implemented
 */
BOOL STDCALL
NtUserGetWindowRect(HWND hWnd, LPRECT Rect)
{
  PWINDOW_OBJECT Wnd;
  NTSTATUS Status;
  DECLARE_RETURN(BOOL);
  
  DPRINT("Enter NtUserGetWindowRect\n");
  UserEnterExclusive();
  
  if (!(Wnd = IntGetWindowObject(hWnd)))
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    RETURN(FALSE);
  }
  Status = MmCopyToCaller(Rect, &Wnd->WindowRect, sizeof(RECT));
  if (!NT_SUCCESS(Status))
  {
    SetLastNtError(Status);
    RETURN(FALSE);
  }
  RETURN(TRUE);
  
CLEANUP:
  DPRINT("Leave NtUserGetWindowRect, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}


/*
 * @implemented
 */
DWORD STDCALL
NtUserGetWindowThreadProcessId(HWND hWnd, LPDWORD UnsafePid)
{
   PWINDOW_OBJECT Wnd;
   DWORD tid, pid;
   DECLARE_RETURN(DWORD);
   
   DPRINT("Enter NtUserGetWindowThreadProcessId\n");
   UserEnterExclusive();
   
   if (!(Wnd = IntGetWindowObject(hWnd)))
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(0);
   }

   tid = (DWORD)IntGetWndThreadId(Wnd);
   pid = (DWORD)IntGetWndProcessId(Wnd);

   if (UnsafePid) MmCopyToCaller(UnsafePid, &pid, sizeof(DWORD));

   RETURN(tid);
   
CLEANUP:
  DPRINT("Leave NtUserGetWindowThreadProcessId, ret=%i\n",_ret_);
  UserLeave(); 
  END_CLEANUP;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserLockWindowUpdate(DWORD Unknown0)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @implemented
 */
BOOL STDCALL
NtUserMoveWindow(
    HWND hWnd,
    int X,
    int Y,
    int nWidth,
    int nHeight,
    BOOL bRepaint)
{
	return NtUserSetWindowPos(hWnd, 0, X, Y, nWidth, nHeight,
                              (bRepaint ? SWP_NOZORDER | SWP_NOACTIVATE :
                               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW));
}

/*
	QueryWindow based on KJK::Hyperion and James Tabor.

	0 = QWUniqueProcessId
	1 = QWUniqueThreadId
	4 = QWIsHung            Implements IsHungAppWindow found
                                by KJK::Hyperion.

        9 = QWKillWindow        When I called this with hWnd ==
                                DesktopWindow, it shutdown the system
                                and rebooted.
*/
/*
 * @implemented
 */
DWORD STDCALL
NtUserQueryWindow(HWND hWnd, DWORD Index)
{
   PWINDOW_OBJECT Window;
   DWORD Result;
   DECLARE_RETURN(DWORD);
   
   DPRINT("Enter NtUserQueryWindow\n");
   UserEnterExclusive();

   Window = IntGetWindowObject(hWnd);
   if (Window == NULL)
   {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(0);
   }

   switch(Index)
   {
      case QUERY_WINDOW_UNIQUE_PROCESS_ID:
         Result = (DWORD)IntGetWndProcessId(Window);
         break;

      case QUERY_WINDOW_UNIQUE_THREAD_ID:
         Result = (DWORD)IntGetWndThreadId(Window);
         break;

      case QUERY_WINDOW_ISHUNG:
         Result = (DWORD)MsqIsHung(Window->MessageQueue);
         break;

      default:
         Result = (DWORD)NULL;
         break;
   }

   RETURN(Result);

CLEANUP:
  DPRINT("Leave NtUserQueryWindow, ret=%i\n",_ret_);
  UserLeave(); 
  END_CLEANUP;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserRealChildWindowFromPoint(DWORD Unknown0,
			       DWORD Unknown1,
			       DWORD Unknown2)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @implemented
 */
UINT STDCALL
NtUserRegisterWindowMessage(PUNICODE_STRING MessageNameUnsafe)
{
  UNICODE_STRING SafeMessageName;
  NTSTATUS Status;
  UINT Ret;
  DECLARE_RETURN(UINT);

  DPRINT("Enter NtUserRegisterWindowMessage\n");
  UserEnterExclusive();

  if(MessageNameUnsafe == NULL)
  {
    SetLastWin32Error(ERROR_INVALID_PARAMETER);
    RETURN(0);
  }

  Status = IntSafeCopyUnicodeStringTerminateNULL(&SafeMessageName, MessageNameUnsafe);
  if(!NT_SUCCESS(Status))
  {
    SetLastNtError(Status);
    RETURN(0);
  }

  Ret = (UINT)IntAddAtom(SafeMessageName.Buffer);
  RtlFreeUnicodeString(&SafeMessageName);
  RETURN(Ret);
  
CLEANUP:
  DPRINT("Leave NtUserRegisterWindowMessage, ret=%i\n",_ret_);
  UserLeave();
  END_CLEANUP;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserSetImeOwnerWindow(DWORD Unknown0,
			DWORD Unknown1)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserSetInternalWindowPos(DWORD Unknown0,
			   DWORD Unknown1,
			   DWORD Unknown2,
			   DWORD Unknown3)
{
  UNIMPLEMENTED

  return 0;

}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserSetLayeredWindowAttributes(DWORD Unknown0,
				 DWORD Unknown1,
				 DWORD Unknown2,
				 DWORD Unknown3)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserSetLogonNotifyWindow(DWORD Unknown0)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @implemented
 */
BOOL STDCALL
NtUserSetMenu(
   HWND hWnd,
   HMENU Menu,
   BOOL Repaint)
{
  PWINDOW_OBJECT WindowObject;
  BOOL Changed;
  DECLARE_RETURN(BOOL);
  
  DPRINT("Enter NtUserSetMenu\n");
  UserEnterExclusive();

  WindowObject = IntGetWindowObject(hWnd);
  if (NULL == WindowObject)
    {
      SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
      RETURN(FALSE);
    }

  if (! IntSetMenu(WindowObject, Menu, &Changed))
    {
      RETURN(FALSE);
    }

  if (Changed && Repaint)
    {
      WinPosSetWindowPos(hWnd, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE |
                         SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }

  RETURN(TRUE);
  
CLEANUP:
  DPRINT("Leave NtUserSetMenu, ret=%i\n",_ret_);
  UserLeave(); 
  END_CLEANUP;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserSetWindowFNID(DWORD Unknown0,
		    DWORD Unknown1)
{
  UNIMPLEMENTED

  return 0;
}



/*
 * @implemented
 */
BOOL STDCALL
NtUserSetWindowPlacement(HWND hWnd,
			 WINDOWPLACEMENT *lpwndpl)
{
  PWINDOW_OBJECT WindowObject;
  WINDOWPLACEMENT Safepl;
  NTSTATUS Status;
  DECLARE_RETURN(BOOL);
  
  DPRINT("Enter NtUserSetWindowPlacement\n");
  UserEnterExclusive();
  
  WindowObject = IntGetWindowObject(hWnd);
  if (WindowObject == NULL)
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    RETURN(FALSE);
  }
  Status = MmCopyFromCaller(&Safepl, lpwndpl, sizeof(WINDOWPLACEMENT));
  if(!NT_SUCCESS(Status))
  {
    SetLastNtError(Status);
    RETURN(FALSE);
  }
  if(Safepl.length != sizeof(WINDOWPLACEMENT))
  {
    RETURN(FALSE);
  }

  if ((WindowObject->Style & (WS_MAXIMIZE | WS_MINIMIZE)) == 0)
  {
     WinPosSetWindowPos(WindowObject->Self, NULL,
        Safepl.rcNormalPosition.left, Safepl.rcNormalPosition.top,
        Safepl.rcNormalPosition.right - Safepl.rcNormalPosition.left,
        Safepl.rcNormalPosition.bottom - Safepl.rcNormalPosition.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
  }

  /* FIXME - change window status */
  WinPosShowWindow(WindowObject, Safepl.showCmd);

  if (WindowObject->InternalPos == NULL)
     WindowObject->InternalPos = ExAllocatePoolWithTag(PagedPool, sizeof(INTERNALPOS), TAG_WININTLIST);
  WindowObject->InternalPos->NormalRect = Safepl.rcNormalPosition;
  WindowObject->InternalPos->IconPos = Safepl.ptMinPosition;
  WindowObject->InternalPos->MaxPos = Safepl.ptMaxPosition;

  RETURN(TRUE);
  
CLEANUP:
  DPRINT("Leave NtUserSetWindowPlacement, ret=%i\n",_ret_);
  UserLeave(); 
  END_CLEANUP;
}


/*
 * @implemented
 */
BOOL STDCALL
NtUserSetWindowPos(
    HWND hWnd,
    HWND hWndInsertAfter,
    int X,
    int Y,
    int cx,
    int cy,
    UINT uFlags)
{
  DECLARE_RETURN(BOOL);
  
  DPRINT("Enter NtUserSetWindowPos\n");
  UserEnterExclusive();

  RETURN(WinPosSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags));
   
CLEANUP:
  DPRINT("Leave NtUserSetWindowPos, ret=%i\n",_ret_);
  UserLeave(); 
  END_CLEANUP;
}


INT FASTCALL
IntGetWindowRgn(HWND hWnd, HRGN hRgn)
{
  INT Ret;
  PWINDOW_OBJECT WindowObject;
  HRGN VisRgn;
  ROSRGNDATA *pRgn;

  if(!(WindowObject = IntGetWindowObject(hWnd)))
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    return ERROR;
  }
  if(!hRgn)
  {
    return ERROR;
  }

  /* Create a new window region using the window rectangle */
  VisRgn = UnsafeIntCreateRectRgnIndirect(&WindowObject->WindowRect);
  NtGdiOffsetRgn(VisRgn, -WindowObject->WindowRect.left, -WindowObject->WindowRect.top);
  /* if there's a region assigned to the window, combine them both */
  if(WindowObject->WindowRegion && !(WindowObject->Style & WS_MINIMIZE))
    NtGdiCombineRgn(VisRgn, VisRgn, WindowObject->WindowRegion, RGN_AND);
  /* Copy the region into hRgn */
  NtGdiCombineRgn(hRgn, VisRgn, NULL, RGN_COPY);

  if((pRgn = RGNDATA_LockRgn(hRgn)))
  {
    Ret = pRgn->rdh.iType;
    RGNDATA_UnlockRgn(pRgn);
  }
  else
    Ret = ERROR;

  NtGdiDeleteObject(VisRgn);

  return Ret;
}

INT FASTCALL
IntGetWindowRgnBox(HWND hWnd, RECT *Rect)
{
  INT Ret;
  PWINDOW_OBJECT WindowObject;
  HRGN VisRgn;
  ROSRGNDATA *pRgn;

  if(!(WindowObject = IntGetWindowObject(hWnd)))
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    return ERROR;
  }
  if(!Rect)
  {
    return ERROR;
  }

  /* Create a new window region using the window rectangle */
  VisRgn = UnsafeIntCreateRectRgnIndirect(&WindowObject->WindowRect);
  NtGdiOffsetRgn(VisRgn, -WindowObject->WindowRect.left, -WindowObject->WindowRect.top);
  /* if there's a region assigned to the window, combine them both */
  if(WindowObject->WindowRegion && !(WindowObject->Style & WS_MINIMIZE))
    NtGdiCombineRgn(VisRgn, VisRgn, WindowObject->WindowRegion, RGN_AND);

  if((pRgn = RGNDATA_LockRgn(VisRgn)))
  {
    Ret = pRgn->rdh.iType;
    *Rect = pRgn->rdh.rcBound;
    RGNDATA_UnlockRgn(pRgn);
  }
  else
    Ret = ERROR;

  NtGdiDeleteObject(VisRgn);

  return Ret;
}


/*
 * @implemented
 */
INT STDCALL
NtUserSetWindowRgn(
  HWND hWnd,
  HRGN hRgn,
  BOOL bRedraw)
{
  PWINDOW_OBJECT WindowObject;
  DECLARE_RETURN(INT);
  
  DPRINT("Enter NtUserSetWindowRgn\n");
  UserEnterExclusive();
  
  WindowObject = IntGetWindowObject(hWnd);
  if (WindowObject == NULL)
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    RETURN(0);
  }

  /* FIXME - Verify if hRgn is a valid handle!!!!
             Propably make this operation thread-safe, but maybe it's not necessary */

  if(WindowObject->WindowRegion)
  {
    /* Delete no longer needed region handle */
    NtGdiDeleteObject(WindowObject->WindowRegion);
  }
  WindowObject->WindowRegion = hRgn;

  /* FIXME - send WM_WINDOWPOSCHANGING and WM_WINDOWPOSCHANGED messages to the window */

  if(bRedraw)
  {
    UserRedrawWindow(WindowObject, NULL, NULL, RDW_INVALIDATE);
  }

  RETURN((INT)hRgn);
  
CLEANUP:
  DPRINT("Leave NtUserSetWindowRgn, ret=%i\n",_ret_);
  UserLeave(); 
  END_CLEANUP;
}


/*
 * @implemented
 */
BOOL STDCALL
NtUserShowWindow(HWND hWnd,
		 LONG nCmdShow)
{
   DECLARE_RETURN(BOOL);
   PWINDOW_OBJECT Wnd;
   
   DPRINT("Enter NtUserShowWindow\n");
   UserEnterExclusive();
   
   if (!(Wnd = IntGetWindowObject(hWnd)))
   {
      //FIXME: last error
      RETURN (FALSE);
   }
   
   RETURN(WinPosShowWindow(Wnd, nCmdShow));
   
CLEANUP:
   DPRINT("Leave NtUserShowWindow, ret=%i\n",_ret_);
   UserLeave(); 
   END_CLEANUP;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserShowWindowAsync(DWORD Unknown0,
		      DWORD Unknown1)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @unimplemented
 */
DWORD STDCALL
NtUserUpdateLayeredWindow(DWORD Unknown0,
			  DWORD Unknown1,
			  DWORD Unknown2,
			  DWORD Unknown3,
			  DWORD Unknown4,
			  DWORD Unknown5,
			  DWORD Unknown6,
			  DWORD Unknown7,
			  DWORD Unknown8)
{
  UNIMPLEMENTED

  return 0;
}


/*
 * @implemented
 */
VOID STDCALL
NtUserValidateRect(HWND hWnd, const RECT* Rect)
{
  return (VOID)NtUserRedrawWindow(hWnd, Rect, 0, RDW_VALIDATE | RDW_NOCHILDREN);
}


/*
 *    @implemented
 */
HWND STDCALL
NtUserWindowFromPoint(LONG X, LONG Y)
{
   POINT pt;
   PWINDOW_OBJECT DesktopWindow, Window = NULL;
   DECLARE_RETURN(HWND);

   DPRINT("Enter NtUserWindowFromPoint\n");
   UserEnterExclusive();

   if ((DesktopWindow = UserGetDesktopWindow()))
   {
      USHORT Hit;

      pt.x = X;
      pt.y = Y;

      Hit = WinPosWindowFromPoint(DesktopWindow, PsGetWin32Thread()->MessageQueue, &pt, &Window);

      if(Window)
      {
        RETURN(Window->Self);
      }

   }
  
  RETURN(NULL);
  
CLEANUP:
  DPRINT("Leave NtUserWindowFromPoint, ret=%i\n",_ret_);
  UserLeave(); 
  END_CLEANUP;
}


/*
 * NtUserDefSetText
 *
 * Undocumented function that is called from DefWindowProc to set
 * window text.
 *
 * Status
 *    @implemented
 */

BOOL STDCALL
NtUserDefSetText(HWND WindowHandle, PUNICODE_STRING WindowText)
{
  PWINDOW_OBJECT WindowObject, Parent, Owner;
  UNICODE_STRING SafeText;
  NTSTATUS Status;
  DECLARE_RETURN(BOOL);

  DPRINT("Enter NtUserDefSetText\n");
  UserEnterExclusive();

  WindowObject = IntGetWindowObject(WindowHandle);
  if(!WindowObject)
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    RETURN(FALSE);
  }

  if(WindowText)
  {
    Status = IntSafeCopyUnicodeString(&SafeText, WindowText);
    if(!NT_SUCCESS(Status))
    {
      SetLastNtError(Status);
      RETURN(FALSE);
    }
  }
  else
  {
    RtlInitUnicodeString(&SafeText, NULL);
  }

  /* FIXME - do this thread-safe! otherwise one could crash here! */
  RtlFreeUnicodeString(&WindowObject->WindowName);

  WindowObject->WindowName = SafeText;

  /* Send shell notifications */

  Owner = IntGetOwner(WindowObject);
  Parent = UserGetParent(WindowObject);

  if ((!Owner) && (!Parent))
  {
    IntShellHookNotify(HSHELL_REDRAW, (LPARAM) WindowHandle);
  }

  RETURN(TRUE);
  
CLEANUP:
  DPRINT("Leave NtUserDefSetText, ret=%i\n",_ret_);
  UserLeave(); 
  END_CLEANUP;
}

/*
 * NtUserInternalGetWindowText
 *
 * Status
 *    @implemented
 */

INT STDCALL
NtUserInternalGetWindowText(HWND hWnd, LPWSTR lpString, INT nMaxCount)
{
  PWINDOW_OBJECT WindowObject;
  NTSTATUS Status;
  INT Result;
  DECLARE_RETURN(INT);
  
  DPRINT("Enter NtUserInternalGetWindowText\n");
  UserEnterExclusive();
  
  if(lpString && (nMaxCount <= 1))
  {
    SetLastWin32Error(ERROR_INVALID_PARAMETER);
    RETURN(0);
  }

  WindowObject = IntGetWindowObject(hWnd);
  if(!WindowObject)
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    RETURN(0);
  }

  /* FIXME - do this thread-safe! otherwise one could crash here! */
  Result = WindowObject->WindowName.Length / sizeof(WCHAR);
  if(lpString)
  {
    const WCHAR Terminator = L'\0';
    INT Copy;
    WCHAR *Buffer = (WCHAR*)lpString;

    Copy = min(nMaxCount - 1, Result);
    if(Copy > 0)
    {
      Status = MmCopyToCaller(Buffer, WindowObject->WindowName.Buffer, Copy * sizeof(WCHAR));
      if(!NT_SUCCESS(Status))
      {
        SetLastNtError(Status);
        RETURN(0);
      }
      Buffer += Copy;
    }

    Status = MmCopyToCaller(Buffer, &Terminator, sizeof(WCHAR));
    if(!NT_SUCCESS(Status))
    {
      SetLastNtError(Status);
      RETURN(0);
    }

    Result = Copy;
  }

  RETURN(Result);
  
CLEANUP:
  DPRINT("Leave NtUserInternalGetWindowText, ret=%i\n",_ret_);
  UserLeave(); 
  END_CLEANUP;
}


DWORD STDCALL
NtUserDereferenceWndProcHandle(WNDPROC wpHandle, WndProcHandle *Data)
{
   DECLARE_RETURN(DWORD);
  
   DPRINT("Enter NtUserDereferenceWndProcHandle\n");
   UserEnterExclusive();
   
	WndProcHandle Entry;
	if (((DWORD)wpHandle & 0xFFFF0000) == 0xFFFF0000)
	{
		Entry = WndProcHandlesArray[(DWORD)wpHandle & 0x0000FFFF];
		Data->WindowProc = Entry.WindowProc;
		Data->IsUnicode = Entry.IsUnicode;
		Data->ProcessID = Entry.ProcessID;
      RETURN(TRUE);
	} else {
      RETURN(FALSE);
	}

   RETURN(FALSE);
   
CLEANUP:
  DPRINT("Leave NtUserDereferenceWndProcHandle, ret=%i\n",_ret_);
  UserLeave(); 
  END_CLEANUP;
}

DWORD
IntAddWndProcHandle(WNDPROC WindowProc, BOOL IsUnicode)
{
	WORD i;
	WORD FreeSpot = 0;
	BOOL found;
	WndProcHandle *OldArray;
	WORD OldArraySize;
	found = FALSE;
	for (i = 0;i < WndProcHandlesArraySize;i++)
	{
		if (WndProcHandlesArray[i].WindowProc == NULL)
		{
			FreeSpot = i;
			found = TRUE;
		}
	}
	if (!found)
	{
		OldArray = WndProcHandlesArray;
		OldArraySize = WndProcHandlesArraySize;
        WndProcHandlesArray = ExAllocatePoolWithTag(PagedPool,(OldArraySize + WPH_SIZE) * sizeof(WndProcHandle), TAG_WINPROCLST);
		WndProcHandlesArraySize = OldArraySize + WPH_SIZE;
		RtlCopyMemory(WndProcHandlesArray,OldArray,OldArraySize * sizeof(WndProcHandle));
		ExFreePool(OldArray);
		FreeSpot = OldArraySize + 1;
	}
	WndProcHandlesArray[FreeSpot].WindowProc = WindowProc;
	WndProcHandlesArray[FreeSpot].IsUnicode = IsUnicode;
	WndProcHandlesArray[FreeSpot].ProcessID = PsGetCurrentProcessId();
	return FreeSpot + 0xFFFF0000;
}

DWORD
IntRemoveWndProcHandle(WNDPROC Handle)
{
	WORD position;
	position = (DWORD)Handle & 0x0000FFFF;
	if (position > WndProcHandlesArraySize)
	{
		return FALSE;
	}
	WndProcHandlesArray[position].WindowProc = NULL;
	WndProcHandlesArray[position].IsUnicode = FALSE;
	WndProcHandlesArray[position].ProcessID = NULL;
	return TRUE;
}

DWORD
IntRemoveProcessWndProcHandles(HANDLE ProcessID)
{
	WORD i;
	for (i = 0;i < WndProcHandlesArraySize;i++)
	{
		if (WndProcHandlesArray[i].ProcessID == ProcessID)
		{
			WndProcHandlesArray[i].WindowProc = NULL;
			WndProcHandlesArray[i].IsUnicode = FALSE;
			WndProcHandlesArray[i].ProcessID = NULL;
		}
	}
	return TRUE;
}

#define WIN_NEEDS_SHOW_OWNEDPOPUP (0x00000040)

BOOL
FASTCALL
IntShowOwnedPopups( HWND owner, BOOL fShow )
{
  int count = 0;
  PWINDOW_OBJECT Window, pWnd;
  HWND *win_array;

  if(!(Window = IntGetWindowObject(owner)))
  {
    SetLastWin32Error(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }

  win_array = IntWinListChildren( Window);

  if (!win_array) return TRUE;

  //woot?!?! it this intended? (walking the list from back to forth) Gunnar
  while (win_array[count]) count++;
  while (--count >= 0)
    {
        if (GetHwnd(UserGetWindow( GetWnd(win_array[count]), GW_OWNER )) != owner) continue;
        if (!(pWnd = IntGetWindowObject( win_array[count] ))) continue;
//        if (pWnd == WND_OTHER_PROCESS) continue;
        
//        pWnd = win_array[count];

        if (fShow)
        {
            if (pWnd->Flags & WIN_NEEDS_SHOW_OWNEDPOPUP)
             {
                /* In Windows, ShowOwnedPopups(TRUE) generates
                 * WM_SHOWWINDOW messages with SW_PARENTOPENING,
                 * regardless of the state of the owner
                 */
                IntSendMessage(win_array[count], WM_SHOWWINDOW, SW_SHOWNORMAL, SW_PARENTOPENING);
                continue;
            }
        }
        else
        {
            if (pWnd->Style & WS_VISIBLE)
            {
                /* In Windows, ShowOwnedPopups(FALSE) generates
                 * WM_SHOWWINDOW messages with SW_PARENTCLOSING,
                 * regardless of the state of the owner
                 */
                IntSendMessage(win_array[count], WM_SHOWWINDOW, SW_HIDE, SW_PARENTCLOSING);
                continue;
            }
        }

    }
    ExFreePool( win_array );
    return TRUE;
}


/* EOF */
