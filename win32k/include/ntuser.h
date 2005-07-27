#ifndef _WIN32K_NTUSER_H
#define _WIN32K_NTUSER_H

extern char* _file;
extern DWORD _line;
extern DWORD _locked;

extern FAST_MUTEX UserLock;

#define DECLARE_RETURN(type) type _ret_
#define RETURN(value) { _ret_ = value; goto _cleanup_; }
#define CLEANUP /*unreachable*/ ASSERT(FALSE); _cleanup_
#define END_CLEANUP return _ret_;

#if 0

#define UserEnterShared() { \
 UUserEnterShared(); ASSERT(InterlockedIncrement(&_locked) > 0); \
 }
 #define UserEnterExclusive() {\
  UUserEnterExclusive(); ASSERT(InterlockedIncrement(&_locked) > 0); \
  }

#define UserLeave() { ASSERT(InterlockedDecrement(&_locked) >= 0);  \
 UUserLeave(); }

#endif


VOID FASTCALL UserStackTrace();

#define UserEnterShared() \
{ \
   DPRINT1("try lock, %s, %i (%i)\n",__FILE__,__LINE__, _locked); \
   ASSERT(UserLock.Owner != KeGetCurrentThread()); \
   UUserEnterShared(); \
   ASSERT(InterlockedIncrement(&_locked) == 1 /*> 0*/); \
   DPRINT("got lock, %s, %i (%i)\n",__FILE__,__LINE__, _locked); \
}

#define UserEnterExclusive() \
{ \
  /* DPRINT1("try lock, %s, %i (%i)\n",__FILE__,__LINE__, _locked);*/ \
   ASSERT(UserLock.Owner != KeGetCurrentThread()); \
   UUserEnterExclusive(); \
   ASSERT(InterlockedIncrement(&_locked) == 1 /*> 0*/); \
  /* DPRINT("got lock, %s, %i (%i)\n",__FILE__,__LINE__, _locked);*/ \
}

#define UserLeave() \
{ \
   ASSERT(InterlockedDecrement(&_locked) == 0/*>= 0*/); \
   /*DPRINT("unlock, %s, %i (%i)\n",__FILE__,__LINE__, _locked);*/ \
   ASSERT(UserLock.Owner == KeGetCurrentThread()); \
   UUserLeave(); \
}
 




#define GetWnd(hwnd) IntGetWindowObject(hwnd)




#if 0
#define IntLockUserShared() {if(_locked){ DPRINT1("last %s, %i\n",_file,_line);} \
 IIntLockUserShared(); \
  _locked++; _file = __FILE__; _line = __LINE__; \
  }
  
 #define IntUserEnterExclusive() {if(_locked){ DPRINT1("last %s, %i\n",_file,_line);} \
  IIntUserEnterExclusive(); \
  _locked++; _file = __FILE__; _line = __LINE__; \
  }


#define IntUserLeave() { if(!_locked){ DPRINT1("not locked %s, %i\n",__FILE__,__LINE__);} \
 _locked--; IIntUserLeave(); }
#endif

NTSTATUS FASTCALL InitUserImpl(VOID);
VOID FASTCALL UninitUser(VOID);
VOID FASTCALL UUserEnterShared(VOID);
VOID FASTCALL UUserEnterExclusive(VOID);
VOID FASTCALL UUserLeave(VOID);
BOOL FASTCALL UserIsEntered();




#define FIRST_USER_HANDLE 0x0020  /* first possible value for low word of user handle */
#define LAST_USER_HANDLE  0xffef  /* last possible value for low word of user handle */


typedef struct _USER_HANDLE_ENTRY
{
    void          *ptr;          /* pointer to object */
    unsigned short type;         /* object type (0 if free) */
    unsigned short generation;   /* generation counter */
} USER_HANDLE_ENTRY, * PUSER_HANDLE_ENTRY;



typedef struct _USER_HANDLE_TABLE
{
   PUSER_HANDLE_ENTRY handles;
   PUSER_HANDLE_ENTRY freelist;
   int nb_handles;
   int allocated_handles;
} USER_HANDLE_TABLE, * PUSER_HANDLE_TABLE;

/*
typedef enum {
  otUnknown = 0,
  otClass,
  otWindow,
  otMenu,
  otAcceleratorTable,
  otCursorIcon,
  otHookProc,
  otMonitor
} USER_OBJECT_TYPE;
*/

typedef enum _USER_OBJECT_TYPE
{
  /* 0 = free */
  USER_CLASS = 1,
  USER_WINDOW,
  USER_MENU,
  USER_ACCELERATOR_TABLE,
  USER_CURSOR_ICON,
  USER_HOOK_PROC,
  USER_MONITOR
} USER_OBJECT_TYPE;







#endif /* _WIN32K_NTUSER_H */

/* EOF */
