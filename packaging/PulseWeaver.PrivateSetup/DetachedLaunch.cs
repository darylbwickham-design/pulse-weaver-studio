using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace PulseWeaver.Setup;

// Older app launchers can pass their open log handle into setup. Start a clean
// process before maintenance; never close arbitrary inherited handles in place.
internal static class DetachedLaunch
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct StartupInfo {
        public int Size;
        public string? Reserved, Desktop, Title;
        public int X, Y, XSize, YSize, XChars, YChars, Fill, Flags;
        public short Show, ReservedSize;
        public IntPtr ReservedBytes, Input, Output, Error;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct ProcessInfo { public IntPtr Process, Thread; public int Pid, Tid; }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool CreateProcess(string application, StringBuilder command, IntPtr processSecurity,
        IntPtr threadSecurity, [MarshalAs(UnmanagedType.Bool)] bool inheritHandles, uint flags,
        IntPtr environment, string directory, ref StartupInfo startup, out ProcessInfo process);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);

    internal static int Start(string executable, string arguments)
    {
        var startup = new StartupInfo { Size = Marshal.SizeOf<StartupInfo>() };
        if (!CreateProcess(executable, new StringBuilder('"' + executable + "\" " + arguments),
            IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, Path.GetDirectoryName(executable)!,
            ref startup, out var process)) throw new Win32Exception(Marshal.GetLastWin32Error());
        CloseHandle(process.Thread); CloseHandle(process.Process);
        return process.Pid;
    }

    internal static bool Bootstrap(string[] args)
    {
        if (args.Length == 2 && args[0] == "/clean-launch" && int.TryParse(args[1], out var parentId)) {
            try {
                using var parent = Process.GetProcessById(parentId);
                if (!parent.WaitForExit(15000)) throw new IOException("The original setup is still closing. Close both setup windows and reopen the downloaded installer.");
            } catch (ArgumentException) { /* Parent already exited. */ }
            return false;
        }
        // Command-line diagnostics do not perform maintenance on the installed app.
        if (args.Length != 0) return false;
        Start(Environment.ProcessPath!, "/clean-launch " + Environment.ProcessId);
        return true;
    }
}
