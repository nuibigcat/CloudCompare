; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!

#define MyAppName "CloudCompare"
#define MyAppVersion "2.13.2.preview (05-26-2024)"
#define MyAppPublisher "Daniel Girardeau-Montaut"
#define MyAppURL "http://www.cloudcompare.org/"
#define MyAppExeName "CloudCompare.exe"
#define MyVCRedistPath "E:\These\C++\CloudCompare\vc_redist"
#define MyFaroRedistPath "E:\These\C++\Faro\redist"
#define MyFaroRedistExe "FARO LS 2020.0.7.6378 Setup.exe"
#define MyCCPath "E:\These\C++\CloudCompare\new_bin_x64_msvc_2017\CloudCompare"
#define MyOutputDir "E:\These\C++\CloudCompare\new_bin_x64_msvc_2017"
#define MyCreationDate GetDateTimeString('mm_dd_yyyy', '', '')
#define CertificateSubject "Open Source Developer, Daniel Girardeau-Montaut"

[Setup]
; NOTE: The value of AppId uniquely identifies this application.
; Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppID={{4DE0A2C8-03F9-4B3F-BAFC-1D5F2141464B}
AppName={#MyAppName}                                                                
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={pf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
AllowNoIcons=true
OutputBaseFilename={#MyAppName}_v{#MyAppVersion}_setup_x64
Compression=lzma2/Ultra64
SolidCompression=true
OutputDir={#MyOutputDir}
InternalCompressLevel=Ultra64
; "ArchitecturesAllowed=x64" specifies that Setup cannot run on
; anything but x64.
ArchitecturesAllowed=x64
; "ArchitecturesInstallIn64BitMode=x64" requests that the install be
; done in "64-bit mode" on x64, meaning it should use the native
; 64-bit Program Files directory and the 64-bit view of the registry.
ArchitecturesInstallIn64BitMode=x64
; Installer signing
SignTool=signtool Sign /n "{#CertificateSubject}" /fd sha256 /t http://timestamp.digicert.com $f

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}";
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 0,6.1
; Warning: update the 'Faro support' checkbox index in the NextButtonClick method if you insert another checkbox above this one!!!
Name: "faro_support" ; Description: "Install Faro I/O plugin (to load FWS/FLS files). The will install Faro LS redistributable package as well." ; GroupDescription: "Faro LS support";  Flags: unchecked
; Warning: update the 'Python plugin support' checkbox index in the NextButtonClick method if you insert another checkbox above this one!!!
Name: "python_plugin_support" ; Description: "Install the Python and 3DFin plugins" ; GroupDescription: "Python plugin support";  Flags: unchecked

[Files]
Source: "{#MyCCPath}\CloudCompare.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyCCPath}\*"; Excludes: "*.manifest,QBRGM*.dll,QFARO*.dll,PythonRuntime.dll,plugins-python,Python"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Unknown dependency support:)
Source: "{#MyVCRedistPath}\vcredist_2013_x64.exe"; DestDir: {tmp}; Flags: deleteafterinstall 64bit;
; All other Visual Studio dependencies at once: 2015, 2017 and 2019
Source: "{#MyVCRedistPath}\vcredist_all_x64.exe"; DestDir: {tmp}; Flags: deleteafterinstall 64bit;
; FARO LS support
Source: "{#MyFaroRedistPath}\x64\{#MyFaroRedistExe}"; DestDir: {tmp}; Flags: deleteafterinstall 64bit; Check: WithFaro
Source: "{#MyFaroRedistPath}\x64\{#MyAppExeName}.manifest"; DestDir: "{app}"; Flags: ignoreversion; Check: WithFaro
Source: "{#MyCCPath}\plugins\QFARO*.dll"; DestDir: "{app}\plugins"; Flags: ignoreversion; Check: WithFaro
Source: "{#MyCCPath}\plugins\PythonRuntime.dll"; DestDir: "{app}\plugins"; Flags: ignoreversion; Check: WithPythonPlugin
Source: "{#MyCCPath}\plugins\Python\*"; DestDir: "{app}\plugins\Python"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: WithPythonPlugin
Source: "{#MyCCPath}\plugins-python\*"; DestDir: "{app}\plugins-python"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: WithPythonPlugin

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: quicklaunchicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
Filename: "{tmp}\vcredist_2013_x64.exe"; Parameters: "/install /quiet /norestart"
Filename: "{tmp}\vcredist_all_x64.exe"; Parameters: "/install /quiet /norestart"
Filename: "{tmp}\{#MyFaroRedistExe}"; Check: WithFaro

[Code]
function InitializeUninstall(): Boolean;
  var ErrorCode: Integer;
begin
  ShellExec('open','taskkill.exe','/f /im {#MyAppExeName}','',SW_HIDE,ewNoWait,ErrorCode);
  ShellExec('open','tskill.exe',' {#MyAppName}','',SW_HIDE,ewNoWait,ErrorCode);
  result := True;
end;

var
  WithFaroSupport: Boolean;
  WithPythonPluginSupport: Boolean;

function WithFaro: Boolean;
begin
  Result := WithFaroSupport;
end;

function WithPythonPlugin: Boolean;
begin
  Result := WithPythonPluginSupport;
end;

/////////////////////////////////////////////////////////////////////
function GetUninstallString(): String;
var
  sUnInstPath: String;
  sUnInstallString: String;
begin
  sUnInstPath := ExpandConstant('Software\Microsoft\Windows\CurrentVersion\Uninstall\{#emit SetupSetting("AppId")}_is1');
  sUnInstallString := '';
  if not RegQueryStringValue(HKLM, sUnInstPath, 'UninstallString', sUnInstallString) then
    RegQueryStringValue(HKCU, sUnInstPath, 'UninstallString', sUnInstallString);
  Result := sUnInstallString;
end;


/////////////////////////////////////////////////////////////////////
function IsUpgrade(): Boolean;
begin
  Result := (GetUninstallString() <> '');
end;


/////////////////////////////////////////////////////////////////////
function UnInstallOldVersion(): Integer;
var
  sUnInstallString: String;
  iResultCode: Integer;
begin
// Return Values:
// 1 - uninstall string is empty
// 2 - error executing the UnInstallString
// 3 - successfully executed the UnInstallString

  // default return value
  Result := 0;

  // get the uninstall string of the old app
  sUnInstallString := GetUninstallString();
  if sUnInstallString <> '' then begin
    sUnInstallString := RemoveQuotes(sUnInstallString);
    if Exec(sUnInstallString, '/SILENT /NORESTART /SUPPRESSMSGBOXES','', SW_HIDE, ewWaitUntilTerminated, iResultCode) then
      Result := 3
    else
      Result := 2;
  end else
    Result := 1;
end;

/////////////////////////////////////////////////////////////////////
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep=ssInstall) then
  begin
    if (IsUpgrade()) then
    begin
      UnInstallOldVersion();
    end;
  end;
end;

/////////////////////////////////////////////////////////////////////
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectTasks) then
  begin
    //'Faro support' checkbox is the 2nd one but each checkbox is nested
    //inside a group, and each group conuts as 1!
    WithFaroSupport := WizardForm.TasksList.Checked[3];
    //'Python plugin support' checkbox is the 3rd one but each checkbox is nested
    //inside a group, and each group conuts as 1!
    WithPythonPluginSupport := WizardForm.TasksList.Checked[5];
  end;
end;
