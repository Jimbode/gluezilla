md idl
md include
md include\widget
md include\docshell
md include\dom
md lib
copy %1\idl\*.idl idl
copy %1\include\*.h include
copy %1\lib\* lib
copy idl_extras\* idl
xcopy  /E include_extras\* include


xpidl.exe -I %1\idl\ -m header idl\nsIAppShell.idl
move nsIAppShell.h include\widget\
xpidl.exe -I %1\idl\ -m header idl\nsIBaseWindow.idl
move nsIBaseWindow.h include\widget\
xpidl.exe -I %1\idl\ -m header idl\nsIDocShellTreeItem.idl
move nsIDocShellTreeItem.h include\docshell\
xpidl.exe -I %1\idl\ -m header idl\nsIDOMKeyEvent.idl
move nsIDOMKeyEvent.h include\dom\
xpidl.exe -I %1\idl\ -m header idl\nsIWebNavigation.idl
move nsIWebNavigation.h include\docshell\
