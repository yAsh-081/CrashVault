' CrashVault — silent Windows launcher (double-click this file).
' Starts the WSL/WSLg app with no visible console. For diagnostics, use CrashVault.bat.
Option Explicit

Dim shell, fso, winDir, wslDir, cmd
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

winDir = fso.GetParentFolderName(WScript.ScriptFullName)
' C:\Users\... → /mnt/c/Users/...
wslDir = "/mnt/" & LCase(Left(winDir, 1)) & Replace(Mid(winDir, 3), "\", "/")

cmd = "wsl.exe -e bash -lc ""chmod +x '" & wslDir & "/launch.sh' 2>/dev/null; exec '" & wslDir & "/launch.sh'"""
' WindowStyle 0 = hidden; False = do not wait (launcher exits; CrashVault keeps running)
shell.Run cmd, 0, False
