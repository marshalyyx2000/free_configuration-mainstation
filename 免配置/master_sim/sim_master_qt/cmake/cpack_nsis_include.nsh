; SimMasterQt NSIS include (injected through CPACK_NSIS_DEFINES, before the MUI pages).
;
; Goals
;  1. Program Files install: keep "Next" enabled on the directory page for admins.
;  2. In-place upgrade: the previous install's Qt DLLs / exe may still be open
;     (app still running, restart pending, antivirus scan, second setup instance).
;     NSIS then aborts with "无法打开要写入的文件" and the user has to guess.
;     Here we close the app and wait until every file we are about to overwrite
;     can really be opened for writing, and only then copy the payload.
;
; The wait loop is the important part: a bare taskkill returns before Windows has
; released the image sections, so the first files of the payload (Qt6Core.dll)
; could still be mapped by the dying process.

Var smLockedFile
Var smWaitCount

; Close every process that keeps files inside $INSTDIR open.
Function simMasterKillApps
  DetailPrint "Closing running SimMasterQt ..."
  ExecWait '"$WINDIR\System32\taskkill.exe" /F /IM sim_master_qt.exe /T' $0
  ExecWait '"$WINDIR\System32\taskkill.exe" /F /IM mms_master_cli.exe /T' $0
  Sleep 500
FunctionEnd

; Check whether $0 can be opened for writing.
;   out: $1 = "1" when another process holds the file open, "0" otherwise.
; A missing file (fresh install) or a permission problem is NOT reported as a
; lock - NSIS itself reports those with a meaningful message.
Function simMasterCheckLock
  StrCpy $1 "0"
  System::Call 'kernel32::CreateFileW(w "$0", i 0x40000000, i 0, p 0, i 3, i 0x00000080, p 0) p .R1 ?e'
  Pop $R2
  StrCmp $R1 "-1" 0 sm_check_open_ok
  StrCmp $R2 "32" 0 sm_check_done   ; only ERROR_SHARING_VIOLATION means "in use"
    StrCpy $1 "1"
    Goto sm_check_done
  sm_check_open_ok:
    System::Call 'kernel32::CloseHandle(p R1)'
  sm_check_done:
FunctionEnd

; Wait until the files an upgrade must overwrite are no longer locked.
Function simMasterWaitUnlocked
  StrCpy $smWaitCount 0
  sm_wait_again:
    StrCpy $smLockedFile ""

    StrCpy $0 "$INSTDIR\bin\sim_master_qt.exe"
    Call simMasterCheckLock
    StrCmp $1 "1" 0 sm_try_cli
      StrCpy $smLockedFile "$0"
      Goto sm_wait_held

    sm_try_cli:
    StrCpy $0 "$INSTDIR\bin\mms_master_cli.exe"
    Call simMasterCheckLock
    StrCmp $1 "1" 0 sm_try_core
      StrCpy $smLockedFile "$0"
      Goto sm_wait_held

    sm_try_core:
    StrCpy $0 "$INSTDIR\bin\Qt6Core.dll"
    Call simMasterCheckLock
    StrCmp $1 "1" 0 sm_wait_free
      StrCpy $smLockedFile "$0"
      Goto sm_wait_held

    sm_wait_free:
      Return

    sm_wait_held:
      IntOp $smWaitCount $smWaitCount + 1
      IntCmp $smWaitCount 30 sm_wait_timeout sm_wait_sleep sm_wait_sleep

    sm_wait_sleep:
      DetailPrint "Waiting for file to be released ($smWaitCount s): $smLockedFile"
      Sleep 1000
      Goto sm_wait_again

    sm_wait_timeout:
      DetailPrint "Still locked: $smLockedFile"
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
        "无法写入下面的文件：$\r$\n$smLockedFile$\r$\n$\r$\n该文件正被其他程序占用（通常是 SimMasterQt 尚未完全退出，或安全软件正在扫描）。$\r$\n请关闭 SimMasterQt 后点击[重试]；点击[取消]退出安装。" \
        /SD IDCANCEL IDRETRY sm_wait_retry IDCANCEL sm_wait_abort
      Goto sm_wait_free

    sm_wait_retry:
      StrCpy $smWaitCount 0
      Call simMasterKillApps
      Goto sm_wait_again

    sm_wait_abort:
      Quit
FunctionEnd

; Called right before the payload is copied (CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS).
Function simMasterStopRunning
  Call simMasterKillApps
  Call simMasterWaitUnlocked
FunctionEnd

Function .onVerifyInstDir
  StrLen $0 $INSTDIR
  IntCmp $0 0 dir_empty dir_ok dir_ok
dir_empty:
  MessageBox MB_OK|MB_ICONEXCLAMATION "Please choose an install folder."
  Abort
dir_ok:
FunctionEnd
