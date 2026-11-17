; OV Game installer (NSIS 3, bouwt ook onder Linux: makensis)
;   makensis -DOUTDIR=<map> -DBINDIR=<map met OVLauncher.exe> ovgame.nsi
;
; Installeert alleen de launcher (per gebruiker, geen beheerdersrechten nodig); de launcher haalt daarna de game
; en de rest van de server. Het serveradres: de server plakt bij het downloaden een regel "OVURL=https://...\r\n"
; (512 bytes, aangevuld met nullen) achter aan deze exe; .onInit leest die. Anders: vorige installatie (register),
; of de speler typt het in.

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user
ManifestDPIAware true

!ifndef OUTDIR
  !define OUTDIR "."
!endif
!ifndef BINDIR
  !define BINDIR "."
!endif
!define APPNAME "OV Game"
!define REGKEY "Software\OVGame"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OVGame"

Name "${APPNAME}"
Caption "${APPNAME} installeren"
BrandingText "OV Game"
OutFile "${OUTDIR}\OVGameSetup.exe"
InstallDir "$LOCALAPPDATA\OVGame"
InstallDirRegKey HKCU "${REGKEY}" "InstallDir"
ShowInstDetails nevershow
ShowUninstDetails nevershow

VIProductVersion "1.0.0.0"
VIAddVersionKey /LANG=1043 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=1043 "FileDescription" "${APPNAME} installatie"
VIAddVersionKey /LANG=1043 "FileVersion" "1.0.0"
VIAddVersionKey /LANG=1043 "ProductVersion" "1.0.0"
VIAddVersionKey /LANG=1043 "LegalCopyright" "OV Game"

!include MUI2.nsh
!include nsDialogs.nsh
!include LogicLib.nsh

!define MUI_ICON "..\launcher\ovgame.ico"
!define MUI_UNICON "..\launcher\ovgame.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "welcome.bmp"
!define MUI_WELCOMEPAGE_TITLE "Welkom bij OV Game"
!define MUI_WELCOMEPAGE_TEXT "Deze installatie zet de OV Game launcher op je computer.$\r$\n$\r$\nDe launcher downloadt daarna de game van de server en houdt hem automatisch bij. De wereld (Eindhoven - 's-Hertogenbosch) komt tijdens het spelen binnen: alleen wat je nodig hebt.$\r$\n$\r$\nJe hebt geen beheerdersrechten nodig.$\r$\n$\r$\nKlik op Volgende om verder te gaan."

Var ServerUrl
Var UrlField

; ---------- pagina's ----------
!insertmacro MUI_PAGE_WELCOME
Page custom ServerPage ServerPageLeave
!define MUI_DIRECTORYPAGE_TEXT_TOP "Kies de map voor OV Game. Hier komen ook de game en de gedownloade wereld (tot een paar GB, afhankelijk van waar je speelt). Kies een map waar je zelf mag schrijven (dus niet 'Program Files')."
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "OV Game is geïnstalleerd"
!define MUI_FINISHPAGE_TEXT "Bij de eerste start downloadt de launcher de game (ongeveer 50 MB)."
!define MUI_FINISHPAGE_RUN "$INSTDIR\OVLauncher.exe"
!define MUI_FINISHPAGE_RUN_TEXT "OV Game nu starten"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Dutch"

; ---------- hulpfuncties ----------
; spaties, CR en LF achteraan weghalen ($R0 in/uit)
Function TrimRight
  Push $R1
  loop:
    StrCpy $R1 $R0 1 -1
    ${If} $R1 == " "
    ${OrIf} $R1 == "$\r"
    ${OrIf} $R1 == "$\n"
    ${OrIf} $R1 == "/"
      StrCpy $R0 $R0 -1
      Goto loop
    ${EndIf}
  Pop $R1
FunctionEnd

; draait de game of de launcher nog? (vensterklassen uit platform.cpp en launcher.cpp)
!macro CHECK_RUNNING
  check_again:
  FindWindow $0 "OVGameWindow" ""
  FindWindow $1 "OVLauncher" ""
  ${If} $0 <> 0
  ${OrIf} $1 <> 0
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "OV Game of de launcher is nog open. Sluit ze en klik op Opnieuw." /SD IDCANCEL IDRETRY check_again
    Abort
  ${EndIf}
!macroend

Function .onInit
  !insertmacro CHECK_RUNNING
  StrCpy $ServerUrl ""
  ; serveradres achter aan de exe (de server zet het er bij het downloaden achter)
  ClearErrors
  FileOpen $0 "$EXEPATH" r
  ${IfNot} ${Errors}
    FileSeek $0 -512 END
    FileRead $0 $1
    FileClose $0
    StrCpy $2 $1 6
    ${If} $2 == "OVURL="
      StrCpy $R0 $1 "" 6
      Call TrimRight
      StrCpy $ServerUrl $R0
    ${EndIf}
  ${EndIf}
  ${If} $ServerUrl == ""
    ReadRegStr $ServerUrl HKCU "${REGKEY}" "Server"
  ${EndIf}
FunctionEnd

; ---------- serveradres ----------
Function ServerPage
  !insertmacro MUI_HEADER_TEXT "Server" "Met welke OV Game server wil je spelen?"
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 36u "Het adres staat al ingevuld als je deze installatie van de website van de server hebt gedownload. De launcher haalt de game en updates bij deze server."
  Pop $0
  ${NSD_CreateLabel} 0 44u 100% 10u "Serveradres:"
  Pop $0
  ${NSD_CreateText} 0 56u 100% 13u "$ServerUrl"
  Pop $UrlField
  ${NSD_SetFocus} $UrlField
  nsDialogs::Show
FunctionEnd

Function ServerPageLeave
  ${NSD_GetText} $UrlField $R0
  Call TrimRight
  StrCpy $ServerUrl $R0
  ${If} $ServerUrl == ""
    MessageBox MB_ICONEXCLAMATION "Vul het adres van de server in, bijvoorbeeld https://ovgame.voorbeeld.nl"
    Abort
  ${EndIf}
  StrCpy $0 $ServerUrl 4
  ${If} $0 != "http"
    StrCpy $ServerUrl "https://$ServerUrl"
  ${EndIf}
FunctionEnd

; ---------- map controleren: moet schrijfbaar zijn (de launcher werkt daar later zelf bij) ----------
Function DirLeave
  ClearErrors
  CreateDirectory "$INSTDIR"
  FileOpen $0 "$INSTDIR\schrijftest.tmp" w
  ${If} ${Errors}
    MessageBox MB_ICONEXCLAMATION "In deze map mag je niet schrijven:$\r$\n$INSTDIR$\r$\n$\r$\nKies een andere map, bijvoorbeeld in je eigen gebruikersmap."
    Abort
  ${EndIf}
  FileClose $0
  Delete "$INSTDIR\schrijftest.tmp"
FunctionEnd

; ---------- installeren ----------
Section "Launcher (vereist)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${BINDIR}\OVLauncher.exe"
  File "..\launcher\ovgame.ico"
  ClearErrors
  FileOpen $0 "$INSTDIR\server.txt" w
  ${IfNot} ${Errors}
    FileWrite $0 "$ServerUrl$\r$\n"
    FileClose $0
  ${EndIf}
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${REGKEY}" "Server" "$ServerUrl"
  ; Apps en onderdelen
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\ovgame.ico"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "OV Game"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "1.0.0"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" 60000
SectionEnd

Section "Startmenu" SecStart
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\OVLauncher.exe" "" "$INSTDIR\ovgame.ico"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME} verwijderen.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Bureaublad" SecDesktop
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\OVLauncher.exe" "" "$INSTDIR\ovgame.ico"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "De launcher: downloadt de game, houdt hem bij en start hem."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStart} "Snelkoppelingen in het startmenu (spelen en verwijderen)."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Een snelkoppeling op je bureaublad."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------- verwijderen ----------
Function un.onInit
  !insertmacro CHECK_RUNNING
FunctionEnd

; Alleen wat de installatie, de launcher en de game zelf maken (nooit de hele map blind weggooien:
; iemand kan een bestaande map gekozen hebben).
Section "Uninstall"
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$DESKTOP\OV Game.lnk"
  RMDir /r "$SMPROGRAMS\${APPNAME}"

  Delete "$INSTDIR\OVLauncher.exe"
  Delete "$INSTDIR\OVLauncher.exe.old"
  Delete "$INSTDIR\OVLauncher.exe.part"
  Delete "$INSTDIR\OVGame.exe"
  Delete "$INSTDIR\OVGame.exe.part"
  Delete "$INSTDIR\installed.idx"
  Delete "$INSTDIR\server.txt"
  Delete "$INSTDIR\shortcut.done"
  Delete "$INSTDIR\ovgame.ico"
  ; game-data: de gedownloade wereld en de bestanden van de launcher altijd; personages en instellingen alleen als je wilt
  RMDir /r "$INSTDIR\data\world"
  RMDir /r "$INSTDIR\data\raw"
  RMDir /r "$INSTDIR\data\bus"
  RMDir /r "$INSTDIR\data\audio"
  Delete "$INSTDIR\data\stream.idx"
  ${If} ${FileExists} "$INSTDIR\data\personages.txt"
  ${OrIf} ${FileExists} "$INSTDIR\data\instellingen.txt"
    MessageBox MB_YESNO|MB_ICONQUESTION "Ook je personages en instellingen verwijderen?" /SD IDNO IDNO keepSettings
    Delete "$INSTDIR\data\personages.txt"
    Delete "$INSTDIR\data\instellingen.txt"
    keepSettings:
  ${EndIf}
  RMDir "$INSTDIR\data"

  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "${REGKEY}"
SectionEnd
