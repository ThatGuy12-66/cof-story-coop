# Keep an automated game run silent without touching anything else on the computer: every audio session of one
# program (the copy's test launcher, CoFLaunchApp-autotest.exe) is muted the moment it appears, and stays muted.
# Used by tools/live_loop.py: -nosound stops the engine's own sound, but Cry of Fear still plays through an audio
# session of its own. Windows remembers a program's mute, so after the first run that program's sessions start
# muted; the player's own launcher (CoFLaunchApp.exe) is another program and keeps its sound. The output devices and
# every other program (a browser playing music, say) are never touched.
#   -Action guard -Exe <path> -Stop <file>   from now until <file> exists, mute every new session of <path>
#   -Action list  -Exe <path>                that program's sessions: pid, state, muted
param([ValidateSet('list', 'guard')][string]$Action = 'list', [string]$Exe = '', [string]$Stop = '')

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom {}

[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator {
    [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IMMDeviceCollection devices);
}

[Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceCollection {
    [PreserveSig] int GetCount(out int count);
    [PreserveSig] int Item(int index, out IMMDevice device);
}

[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice {
    [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
}

[Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionManager2 {
    [PreserveSig] int GetAudioSessionControl(IntPtr sessionGuid, int flags, out IntPtr control);
    [PreserveSig] int GetSimpleAudioVolume(IntPtr sessionGuid, int flags, out IntPtr volume);
    [PreserveSig] int GetSessionEnumerator(out IAudioSessionEnumerator sessions);
}

[Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionEnumerator {
    [PreserveSig] int GetCount(out int count);
    [PreserveSig] int GetSession(int index, out IAudioSessionControl2 session);
}

[Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionControl2 {
    [PreserveSig] int GetState(out int state);
    [PreserveSig] int GetDisplayName([MarshalAs(UnmanagedType.LPWStr)] out string name);
    [PreserveSig] int SetDisplayName(string name, ref Guid ctx);
    [PreserveSig] int GetIconPath([MarshalAs(UnmanagedType.LPWStr)] out string path);
    [PreserveSig] int SetIconPath(string path, ref Guid ctx);
    [PreserveSig] int GetGroupingParam(out Guid param);
    [PreserveSig] int SetGroupingParam(ref Guid param, ref Guid ctx);
    [PreserveSig] int RegisterAudioSessionNotification(IntPtr client);
    [PreserveSig] int UnregisterAudioSessionNotification(IntPtr client);
    [PreserveSig] int GetSessionIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
    [PreserveSig] int GetSessionInstanceIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
    [PreserveSig] int GetProcessId(out uint pid);
}

[Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface ISimpleAudioVolume {
    [PreserveSig] int SetMasterVolume(float level, ref Guid ctx);
    [PreserveSig] int GetMasterVolume(out float level);
    // a Win32 BOOL (4 bytes): the default COM bool marshalling is VARIANT_BOOL, which some drivers refuse
    [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid ctx);
    [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
}

public static class GameAudio {
    static Dictionary<uint, string> paths = new Dictionary<uint, string>();
    static HashSet<string> seen = new HashSet<string>();

    static string PathOf(uint pid) {
        string path;
        if (paths.TryGetValue(pid, out path)) return path;
        try { path = System.Diagnostics.Process.GetProcessById((int)pid).MainModule.FileName; } catch { path = ""; }
        paths[pid] = path;
        return path;
    }

    // mute: mute that program's unmuted sessions; else list them
    public static List<string> Pass(string exe, bool mute) {
        var lines = new List<string>();
        var en = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
        IMMDeviceCollection devs;
        if (en.EnumAudioEndpoints(0, 1, out devs) != 0) return lines;
        int n; devs.GetCount(out n);
        for (int d = 0; d < n; d++) {
            IMMDevice dev; devs.Item(d, out dev);
            object o;
            Guid iid = typeof(IAudioSessionManager2).GUID;
            if (dev.Activate(ref iid, 23, IntPtr.Zero, out o) != 0) continue;
            var sm = (IAudioSessionManager2)o;
            IAudioSessionEnumerator se;
            if (sm.GetSessionEnumerator(out se) != 0) continue;
            int sc; se.GetCount(out sc);
            for (int s = 0; s < sc; s++) {
                IAudioSessionControl2 c;
                if (se.GetSession(s, out c) != 0) continue;
                uint pid; c.GetProcessId(out pid);
                if (pid == 0 || !string.Equals(PathOf(pid), exe, StringComparison.OrdinalIgnoreCase)) continue;
                var vol = (ISimpleAudioVolume)c;
                Guid ctx = Guid.Empty;
                bool m; vol.GetMute(out m);
                int state; c.GetState(out state);
                string instance; c.GetSessionInstanceIdentifier(out instance);
                bool first = instance != null && seen.Add(instance);
                if (mute && m && first) lines.Add(string.Format("muted from the start pid={0} device={1}", pid, d));
                if (mute && !m) {
                    vol.SetMute(true, ref ctx);
                    vol.GetMute(out m);
                    lines.Add(string.Format("muted pid={0} device={1} now={2}", pid, d, m ? "muted" : "NOT MUTED"));
                }
                if (!mute) lines.Add(string.Format("session pid={0} device={1} state={2} muted={3}", pid, d, state, m));
            }
        }
        return lines;
    }
}
'@

if ($Action -eq 'list') {
    [GameAudio]::Pass($Exe, $false) | ForEach-Object { $_ }
    exit 0
}
[GameAudio]::Pass($Exe, $true) | ForEach-Object { $_ }
Write-Output 'guard ready'
while (-not (Test-Path -LiteralPath $Stop)) {
    try { [GameAudio]::Pass($Exe, $true) | ForEach-Object { Write-Output $_ } } catch { Write-Output "error: $_" }
    Start-Sleep -Milliseconds 50
}
