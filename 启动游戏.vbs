Option Explicit
Dim shell, fs, root, executable, result
Set shell = CreateObject("WScript.Shell")
Set fs = CreateObject("Scripting.FileSystemObject")
root = fs.GetParentFolderName(WScript.ScriptFullName)
shell.CurrentDirectory = root
executable = root & "\build\sword_forge.exe"
If Not fs.FileExists(executable) Then
    result = shell.Run("powershell -NoProfile -ExecutionPolicy Bypass -File """ & root & "\build.ps1""", 0, True)
    If result <> 0 Then
        MsgBox "Build failed. Please run build.bat to see the error.", vbExclamation, "Sword Forge"
        WScript.Quit 1
    End If
End If
shell.Run """" & executable & """", 1, False
