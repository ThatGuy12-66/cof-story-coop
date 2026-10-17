# Mute every active output device for an automated game run, and put each one back as it was afterwards.
# Used by tools/live_loop.py: -nosound stops the engine's own sound, but Cry of Fear still plays through an audio
# session of its own, and a per-program mute is remembered by Windows (the next person to start the game would hear
# nothing). Muting the devices themselves, from before the game starts until after it has exited, lets nothing
# through and leaves no trace once restored.
#   -Action mute    -State <file>   write each active output device's id and mute state to <file>, then mute them all
#   -Action restore -State <file>   put each device in <file> back to the mute state recorded there
#   -Action list                    each active output device: id, muted, volume
param([ValidateSet('list', 'mute', 'restore')][string]$Action = 'list', [string]$State = '')

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom {}

[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator {
    [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IMMDeviceCollection devices);
    [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice endpoint);
    [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice device);
}

[Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceCollection {
    [PreserveSig] int GetCount(out int count);
    [PreserveSig] int Item(int index, out IMMDevice device);
}

[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice {
    [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
    [PreserveSig] int OpenPropertyStore(int access, out IntPtr store);
    [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
}

[Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioEndpointVolume {
    [PreserveSig] int RegisterControlChangeNotify(IntPtr notify);
    [PreserveSig] int UnregisterControlChangeNotify(IntPtr notify);
    [PreserveSig] int GetChannelCount(out int count);
    [PreserveSig] int SetMasterVolumeLevel(float db, ref Guid ctx);
    [PreserveSig] int SetMasterVolumeLevelScalar(float level, ref Guid ctx);
    [PreserveSig] int GetMasterVolumeLevel(out float db);
    [PreserveSig] int GetMasterVolumeLevelScalar(out float level);
    [PreserveSig] int SetChannelVolumeLevel(uint ch, float db, ref Guid ctx);
    [PreserveSig] int SetChannelVolumeLevelScalar(uint ch, float level, ref Guid ctx);
    [PreserveSig] int GetChannelVolumeLevel(uint ch, out float db);
    [PreserveSig] int GetChannelVolumeLevelScalar(uint ch, out float level);
    // a Win32 BOOL (4 bytes): the default COM bool marshalling is VARIANT_BOOL, which some drivers refuse
    [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid ctx);
    [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
}

public static class Devices {
    static IAudioEndpointVolume Volume(IMMDevice dev) {
        object o;
        Guid iid = typeof(IAudioEndpointVolume).GUID;
        if (dev.Activate(ref iid, 23, IntPtr.Zero, out o) != 0) return null;
        return (IAudioEndpointVolume)o;
    }

    // every active output device: "id<TAB>muted<TAB>volume" (the volume exactly, to be put back as it was)
    public static List<string> List() {
        var lines = new List<string>();
        var en = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
        IMMDeviceCollection devs;
        if (en.EnumAudioEndpoints(0, 1, out devs) != 0) throw new Exception("no output devices listed");
        int n; devs.GetCount(out n);
        for (int d = 0; d < n; d++) {
            IMMDevice dev; devs.Item(d, out dev);
            string id; dev.GetId(out id);
            var v = Volume(dev);
            if (v == null) continue;
            bool m; float level; v.GetMute(out m); v.GetMasterVolumeLevelScalar(out level);
            lines.Add(id + "\t" + (m ? "1" : "0") + "\t" + level.ToString("R", System.Globalization.CultureInfo.InvariantCulture));
        }
        return lines;
    }

    static IAudioEndpointVolume Device(string id) {
        var en = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
        IMMDevice dev;
        if (en.GetDevice(id, out dev) != 0 || dev == null) return null;
        return Volume(dev);
    }

    // Silent: muted, or (a device that will not mute) its volume at 0. "muted", "volume 0" or "" (failed).
    public static string Silence(string id) {
        var v = Device(id);
        if (v == null) return "";
        Guid ctx = Guid.Empty;
        bool m;
        v.SetMute(true, ref ctx);
        if (v.GetMute(out m) == 0 && m) return "muted";
        float level;
        v.SetMasterVolumeLevelScalar(0.0f, ref ctx);
        if (v.GetMasterVolumeLevelScalar(out level) == 0 && level <= 0.0001f) return "volume 0";
        return "";
    }

    public static bool Put(string id, bool mute, float level) {
        var v = Device(id);
        if (v == null) return false;
        Guid ctx = Guid.Empty;
        bool m; float now;
        v.SetMasterVolumeLevelScalar(level, ref ctx);
        v.SetMute(mute, ref ctx);
        return v.GetMute(out m) == 0 && m == mute && v.GetMasterVolumeLevelScalar(out now) == 0 && Math.Abs(now - level) < 0.001f;
    }}
'@

switch ($Action) {
    'list' { [Devices]::List() | ForEach-Object { $_ } }
    'mute' {
        $before = @([Devices]::List())
        # recorded before anything changes: a run killed halfway is put back from this file
        Set-Content -LiteralPath $State -Value $before -Encoding UTF8
        $done = @()
        foreach ($line in $before) {
            $id = ($line -split "`t")[0]
            $how = [Devices]::Silence($id)
            if (-not $how) { Write-Output "could not silence $id"; exit 1 }
            $done += $how
        }
        Write-Output "silenced $($before.Count) devices ($(($done | Group-Object | ForEach-Object { "$($_.Count) $($_.Name)" }) -join ', '))"
    }
    'restore' {
        if (-not (Test-Path -LiteralPath $State)) { Write-Output 'nothing to restore'; exit 0 }
        $failed = 0
        foreach ($line in Get-Content -LiteralPath $State -Encoding UTF8) {
            $parts = $line -split "`t"
            if ($parts.Count -lt 3) { continue }
            $level = [float]::Parse($parts[2], [Globalization.CultureInfo]::InvariantCulture)
            if (-not [Devices]::Put($parts[0], $parts[1] -eq '1', $level)) { $failed++; Write-Output "could not restore $($parts[0])" }
        }
        if ($failed) { exit 1 }
        Write-Output 'devices restored'
    }
}