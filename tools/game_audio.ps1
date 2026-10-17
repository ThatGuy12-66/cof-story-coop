# The Windows audio sessions of a game copy's processes on every active output device.
# Used by tools/live_loop.py so an automated game run makes no sound (-nosound stops the engine's own sound, but
# Cry of Fear still opens a session of its own).
#   -Action list   -Root <copy>              list them
#   -Action guard  -Root <copy> -Stop <file> from now until <file> exists: mute every session of a process started
#                                            from <copy> the moment it appears (polled every 100 ms); then unmute the
#                                            ones it muted and exit. Windows remembers a session's mute for the
#                                            program, so the guard is stopped while the game still runs.
param([ValidateSet('list', 'guard')][string]$Action = 'list', [string]$Root = '', [string]$Stop = '')

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom {}

[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator {
    int EnumAudioEndpoints(int dataFlow, int stateMask, out IMMDeviceCollection devices);
}

[Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceCollection {
    int GetCount(out int count);
    int Item(int index, out IMMDevice device);
}

[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice {
    int Activate(ref Guid iid, int clsCtx, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
}

[Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionManager2 {
    int GetAudioSessionControl(IntPtr sessionGuid, int flags, out IntPtr control);
    int GetSimpleAudioVolume(IntPtr sessionGuid, int flags, out IntPtr volume);
    int GetSessionEnumerator(out IAudioSessionEnumerator sessions);
}

[Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionEnumerator {
    int GetCount(out int count);
    int GetSession(int index, out IAudioSessionControl2 session);
}

[Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionControl2 {
    int GetState(out int state);
    int GetDisplayName([MarshalAs(UnmanagedType.LPWStr)] out string name);
    int SetDisplayName(string name, ref Guid ctx);
    int GetIconPath([MarshalAs(UnmanagedType.LPWStr)] out string path);
    int SetIconPath(string path, ref Guid ctx);
    int GetGroupingParam(out Guid param);
    int SetGroupingParam(ref Guid param, ref Guid ctx);
    int RegisterAudioSessionNotification(IntPtr client);
    int UnregisterAudioSessionNotification(IntPtr client);
    int GetSessionIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
    int GetSessionInstanceIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
    int GetProcessId(out uint pid);
}

[Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface ISimpleAudioVolume {
    int SetMasterVolume(float level, ref Guid ctx);
    int GetMasterVolume(out float level);
    int SetMute(bool mute, ref Guid ctx);
    int GetMute(out bool mute);
}

public static class GameAudio {
    static Dictionary<uint, string> paths = new Dictionary<uint, string>();
    static HashSet<string> muted = new HashSet<string>();

    static string PathOf(uint pid) {
        string path;
        if (paths.TryGetValue(pid, out path)) return path;
        try { path = System.Diagnostics.Process.GetProcessById((int)pid).MainModule.FileName; } catch { path = ""; }
        paths[pid] = path;
        return path;
    }

    // action: 0 list, 1 mute new sessions (guard), 2 unmute the ones the guard muted
    public static List<string> Pass(string root, int action) {
        var lines = new List<string>();
        string prefix = root.TrimEnd('\\') + "\\";
        var en = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
        IMMDeviceCollection devs; en.EnumAudioEndpoints(0, 1, out devs);
        int n; devs.GetCount(out n);
        for (int d = 0; d < n; d++) {
            IMMDevice dev; devs.Item(d, out dev);
            object o;
            Guid smIid = typeof(IAudioSessionManager2).GUID;
            if (dev.Activate(ref smIid, 23, IntPtr.Zero, out o) != 0) continue;
            var sm = (IAudioSessionManager2)o;
            IAudioSessionEnumerator se; sm.GetSessionEnumerator(out se);
            int sc; se.GetCount(out sc);
            for (int s = 0; s < sc; s++) {
                IAudioSessionControl2 c; se.GetSession(s, out c);
                uint pid; c.GetProcessId(out pid);
                if (pid == 0) continue;
                string path = PathOf(pid);
                if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) continue;
                string id; c.GetSessionInstanceIdentifier(out id);
                var vol = (ISimpleAudioVolume)c;
                Guid ctx = Guid.Empty;
                bool m; vol.GetMute(out m);
                if (action == 1 && !m) { vol.SetMute(true, ref ctx); muted.Add(id); lines.Add("muted pid=" + pid + " " + path); }
                if (action == 2 && muted.Contains(id)) { vol.SetMute(false, ref ctx); lines.Add("unmuted pid=" + pid); }
                if (action == 0) { int state; c.GetState(out state); lines.Add(string.Format("session pid={0} {1} state={2} muted={3}", pid, path, state, m)); }
            }
        }
        return lines;
    }
}
'@

if ($Action -eq 'list') {
    [GameAudio]::Pass($Root, 0) | ForEach-Object { $_ }
    exit 0
}
[GameAudio]::Pass($Root, 1) | ForEach-Object { $_ }
Write-Output 'guard ready'
while (-not (Test-Path -LiteralPath $Stop)) {
    try { [GameAudio]::Pass($Root, 1) | ForEach-Object { Write-Output $_ } } catch { Write-Output "error: $_" }
    Start-Sleep -Milliseconds 100
}
[GameAudio]::Pass($Root, 2) | ForEach-Object { $_ }
