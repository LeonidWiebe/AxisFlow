; ===========================================================================
;  AxisFlow -- NSIS installer for the prebuilt Windows workbench.
;
;  Per-user install (no admin): drops the runtime files into the user's FreeCAD
;  Mod folder, registers an uninstaller in "Apps & features" under HKCU.
;
;  Qt/Coin/Python DLLs are NOT installed -- they belong to the user's FreeCAD.
;  A compiled .pyd only loads in a FreeCAD whose Python/Qt/Coin ABI matches the
;  one it was built against (target here: FreeCAD 1.1, Windows 64-bit).
;
;  Build it via build_installer.bat, which stages dist\AxisFlow first and then
;  runs makensis WITH /NOCD from the repo root. The switch is load-bearing:
;  makensis otherwise chdirs to the directory holding this .nsi, and every
;  relative path below -- "LICENSE", ${SRCDIR}, OutFile -- would resolve against
;  installer\ instead of the repo root. Running makensis on this file by hand
;  without /NOCD fails on the licence page.
; ===========================================================================

Unicode true

; ---- identity (APPVERSION can be overridden: makensis /DAPPVERSION=x.y.z) ----
!ifndef APPVERSION
  !define APPVERSION "0.2.1"
!endif
!define APPNAME    "AxisFlow"
!define FCTARGET   "FC1.1"
!define PUBLISHER  "Leonid Wiebe"
!define APPURL     "https://github.com/LeonidWiebe/AxisFlow"

!ifndef SRCDIR
  !define SRCDIR "dist\AxisFlow"          ; staged runtime files
!endif
!ifndef OUTFILE
  !define OUTFILE "AxisFlow-${APPVERSION}-win64-${FCTARGET}-setup.exe"
!endif

!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"

Name "${APPNAME} ${APPVERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel user               ; installs into %APPDATA% -- no elevation
SetCompressor /SOLID lzma
InstallDir "$APPDATA\FreeCAD\Mod\${APPNAME}"   ; refined in .onInit
InstallDirRegKey HKCU "Software\${APPNAME}" "InstallDir"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

; ---- branding (optional) ----
; Generated from logotip.png / logotip2.png by installer\make_assets.py and
; committed, so building the installer needs only makensis -- no Python, no
; Pillow. NSIS reads neither PNG nor anything else modern: the page artwork has
; to be a 24-bit BMP at a fixed size and the icon a real .ico, which is what
; that script produces.
;
; Guarded with /FileExists on purpose. A define pointing at a missing image
; aborts the whole compile, exactly the way the LICENSE path did -- and someone
; building from a checkout without the artwork should get the stock NSIS look,
; not a broken build. Paths are relative to the REPO ROOT, which is what /NOCD
; in build_installer.bat guarantees.
!if /FileExists "installer\assets\AxisFlow.ico"
  !define MUI_ICON   "installer\assets\AxisFlow.ico"
  ; Same file for the uninstaller: NSIS is fussy about the two icons matching in
  ; structure, and there is no reason for them to differ.
  !define MUI_UNICON "installer\assets\AxisFlow.ico"
!endif

!if /FileExists "installer\assets\welcome.bmp"
  !define MUI_WELCOMEFINISHPAGE_BITMAP   "installer\assets\welcome.bmp"
  !define MUI_UNWELCOMEFINISHPAGE_BITMAP "installer\assets\welcome.bmp"
!endif

!if /FileExists "installer\assets\header.bmp"
  !define MUI_HEADERIMAGE
  !define MUI_HEADERIMAGE_BITMAP   "installer\assets\header.bmp"
  !define MUI_HEADERIMAGE_UNBITMAP "installer\assets\header.bmp"
!endif

; ---- version info on the setup .exe ----
VIProductVersion "${APPVERSION}.0"
VIAddVersionKey  "ProductName"    "${APPNAME}"
VIAddVersionKey  "ProductVersion" "${APPVERSION}"
VIAddVersionKey  "FileVersion"    "${APPVERSION}.0"
VIAddVersionKey  "CompanyName"    "${PUBLISHER}"
VIAddVersionKey  "LegalCopyright" "LGPL-2.1-or-later"
VIAddVersionKey  "FileDescription" "${APPNAME} FreeCAD workbench installer"

; ---- MUI pages ----
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "${APPNAME} ${APPVERSION} for FreeCAD ${FCTARGET}"
!define MUI_WELCOMEPAGE_TEXT "This installs the ${APPNAME} workbench into your FreeCAD Mod folder.$\r$\n$\r$\nBefore continuing:$\r$\n  - CLOSE FreeCAD (its files are locked while it runs).$\r$\n  - Make sure this build matches your FreeCAD's ABI (target: FreeCAD ${FCTARGET}, Windows 64-bit).$\r$\n$\r$\nQt, Coin and Python are provided by FreeCAD and are not installed."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"

!define MUI_DIRECTORYPAGE_TEXT_TOP "AxisFlow will be installed into the folder below -- your FreeCAD Mod directory, in an 'AxisFlow' subfolder. If your FreeCAD uses a different data directory, adjust it here. Tip: in FreeCAD's Python console, App.getUserAppDataDir() prints the base path (then append \Mod\AxisFlow)."
; Checked on LEAVING the directory page: the earliest moment $INSTDIR is final,
; and still before the install page, so a locked module costs the user a Retry
; rather than a half-written folder.
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE EnsureWritable
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_TITLE "AxisFlow installed"
!define MUI_FINISHPAGE_TEXT "Restart FreeCAD, then pick 'AxisFlow' from the workbench selector.$\r$\n$\r$\nIf it does not appear, this build likely does not match your FreeCAD's ABI -- use a build made for your FreeCAD."
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ---- is the installed module locked? ------------------------------------
; Testing for a LOCKED MODULE rather than for a running FreeCAD, because that
; is the condition that actually matters. FreeCAD running without AxisFlow
; loaded installs perfectly well -- the user just restarts afterwards -- so
; blocking on the process would refuse installs that would have succeeded.
; Conversely a FreeCAD with AxisFlow loaded holds AxisFlow.pyd mapped as an
; image section, and Windows denies write sharing on it. That is exactly what
; would make File fail halfway through, leaving a half-installed folder.
;
; The test is FileOpen in append mode: it asks for write access without
; truncating (mode "w" would empty the file it is testing). No plugin needed --
; nsProcess and FindProcDLL are third-party and would have to be dropped into
; every builder's NSIS installation.
;
; $0 = "1" if we cannot write to it.
!macro ModuleLockedInto DIR
  Push $1
  StrCpy $0 "0"
  ${If} ${FileExists} "${DIR}\AxisFlow.pyd"
    ClearErrors
    FileOpen $1 "${DIR}\AxisFlow.pyd" a
    ${If} ${Errors}
      StrCpy $0 "1"
    ${Else}
      FileClose $1
    ${EndIf}
  ${EndIf}
  Pop $1
!macroend

; Called from two places with two different meanings for Abort, both of them the
; one we want: on a page-leave callback it means "stay on this page", inside a
; section it means "stop the install". Nothing to special-case.
Function EnsureWritable
  retry:
  !insertmacro ModuleLockedInto "$INSTDIR"
  ${If} $0 == "1"
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
      "$INSTDIR\AxisFlow.pyd cannot be written to.$\r$\n$\r$\nAlmost always this means FreeCAD is running with AxisFlow loaded -- Windows locks the module while it is in use. Close FreeCAD and press Retry.$\r$\n$\r$\n(If FreeCAD is already closed, check that the folder is not read-only.)" \
      IDRETRY retry
    Abort
  ${EndIf}
FunctionEnd

Function un.EnsureWritable
  retry:
  !insertmacro ModuleLockedInto "$INSTDIR"
  ${If} $0 == "1"
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
      "$INSTDIR\AxisFlow.pyd cannot be removed -- FreeCAD is most likely running with AxisFlow loaded.$\r$\n$\r$\nClose FreeCAD and press Retry." \
      IDRETRY retry
    Abort
  ${EndIf}
FunctionEnd

; ---- pick a sensible default install dir ----
Function .onInit
  ; No "please close FreeCAD" nag here any more: EnsureWritable checks for real
  ; when the user leaves the directory page, and again before the first file is
  ; written. A prompt nobody can act on yet is just a click to dismiss.

  ; Prior install wins; else prefer the versioned FreeCAD 1.1 data dir; else plain.
  ReadRegStr $0 HKCU "Software\${APPNAME}" "InstallDir"
  ${If} $0 != ""
    StrCpy $INSTDIR "$0"
  ${ElseIf} ${FileExists} "$APPDATA\FreeCAD\v1-1\*.*"
    StrCpy $INSTDIR "$APPDATA\FreeCAD\v1-1\Mod\${APPNAME}"
  ${Else}
    StrCpy $INSTDIR "$APPDATA\FreeCAD\Mod\${APPNAME}"
  ${EndIf}
FunctionEnd

; ---- install ----
Section "AxisFlow (required)" SecMain
  SectionIn RO
  SetOverwrite on

  ; Again: the directory page may have been left minutes ago, and FreeCAD is
  ; exactly the sort of thing a user reopens to check something first.
  Call EnsureWritable

  SetOutPath "$INSTDIR"

  ; Ship the STAGED TREE VERBATIM instead of re-listing its contents here.
  ;
  ; stage_dist.bat is the single source of truth for what ships; it wipes dist\
  ; and rebuilds it from an explicit list, so dist\AxisFlow is exactly the
  ; payload -- .pyd, every axisflow_*.py, package.xml, hotkeys.ini, the docs,
  ; Resources\ and translations\. Enumerating those files a second time here
  ; only created a list that could drift out of sync with the staging script,
  ; and it did: axisflow_transform.py was added to stage_dist.bat but never to
  ; this list, so installer builds shipped Move/Copy toolbar buttons whose
  ; commands died on ModuleNotFoundError. (The stale ${SRCDIR}\man\CHANGELOG.md
  ; entry had rotted the same way, silenced by /nonfatal.)
  ;
  ; The wildcard is "*" and not "*.*": LICENSE has no extension. /r recreates
  ; the sub-directory structure, so Resources\ and translations\ come along.
  ; Deliberately NOT /nonfatal -- if staging did not run, makensis must fail
  ; loudly ("File: no files found") rather than emit an empty installer.
  File /r "${SRCDIR}\*"

  ; uninstaller + registration (all under HKCU -- per-user, no admin)
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\${APPNAME}" "Version"    "${APPVERSION}"

  WriteRegStr   HKCU "${UNINSTKEY}" "DisplayName"     "${APPNAME} (FreeCAD ${FCTARGET} workbench)"
  WriteRegStr   HKCU "${UNINSTKEY}" "DisplayVersion"  "${APPVERSION}"
  WriteRegStr   HKCU "${UNINSTKEY}" "Publisher"       "${PUBLISHER}"
  WriteRegStr   HKCU "${UNINSTKEY}" "URLInfoAbout"    "${APPURL}"
  WriteRegStr   HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr   HKCU "${UNINSTKEY}" "DisplayIcon"     "$INSTDIR\Uninstall.exe"
  WriteRegStr   HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr   HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1

  ; estimated size for Apps & features
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" "$0"
SectionEnd

; ---- uninstall ----
Section "Uninstall"
  ; RMDir /r silently skips what it cannot delete, so without this the
  ; uninstaller would report success and leave the .pyd (and the folder) behind.
  Call un.EnsureWritable

  ; Safety net: only wipe the folder if it really holds our module, so a
  ; mis-chosen $INSTDIR can never take an unrelated folder with it.
  IfFileExists "$INSTDIR\AxisFlow.pyd" 0 +2
    RMDir /r "$INSTDIR"

  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "Software\${APPNAME}"
SectionEnd
