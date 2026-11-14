// Manual Windows 7 integration regression; .NET Framework 4, x86 or x64.
// Queries package metadata only. No credential acquisition or network traffic.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

internal static class SspiPackages {
    [StructLayout(LayoutKind.Sequential)]
    private struct PackageInfo {
        public uint Capabilities;
        public ushort Version, RpcId;
        public uint MaxToken;
        public IntPtr Name, Comment;
    }
    [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet=CharSet.Unicode)]
    private delegate int QueryPackage(string package, out IntPtr info);
    [DllImport("secur32.dll", ExactSpelling=true, CharSet=CharSet.Unicode)]
    private static extern int QuerySecurityPackageInfoW(string package, out IntPtr info);
    [DllImport("secur32.dll", ExactSpelling=true)]
    private static extern int EnumerateSecurityPackagesW(out uint count, out IntPtr packages);
    [DllImport("secur32.dll", ExactSpelling=true)]
    private static extern int FreeContextBuffer(IntPtr buffer);
    [DllImport("kernel32.dll", ExactSpelling=true, CharSet=CharSet.Unicode)]
    private static extern IntPtr GetModuleHandleW(string name);
    [DllImport("kernel32.dll", ExactSpelling=true, CharSet=CharSet.Ansi)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32.dll", ExactSpelling=true, CharSet=CharSet.Unicode)]
    private static extern bool GetModuleHandleExW(uint flags, IntPtr address, out IntPtr module);
    [DllImport("kernel32.dll", ExactSpelling=true, CharSet=CharSet.Unicode)]
    private static extern uint GetModuleFileNameW(IntPtr module, StringBuilder path, int capacity);

    private static int failures;
    private static readonly string[] Names = {
        "Microsoft Unified Security Protocol Provider", "Schannel", "KxSChanl"
    };
    private static void Check(bool condition, string description) {
        Console.WriteLine((condition ? "PASS " : "FAIL ") + description);
        if (!condition) ++failures;
    }
    private static string Hex(int status) {
        return "0x" + unchecked((uint)status).ToString("x8");
    }
    private static string OwnerName(IntPtr address) {
        IntPtr module;
        StringBuilder path = new StringBuilder(1024);
        if (address == IntPtr.Zero || !GetModuleHandleExW(6, address, out module)) return "unresolved";
        uint count = GetModuleFileNameW(module, path, path.Capacity);
        return count == 0 || count >= path.Capacity ? "unresolved" : Path.GetFileName(path.ToString());
    }
    private static string NameAt(IntPtr info) {
        PackageInfo value = (PackageInfo)Marshal.PtrToStructure(info, typeof(PackageInfo));
        return value.Name == IntPtr.Zero ? null : Marshal.PtrToStringUni(value.Name);
    }
    private static void QueryAll(QueryPackage query, string route, bool extended) {
        for (int index = 0; index < Names.Length; ++index) {
            IntPtr buffer = IntPtr.Zero;
            int status = query(Names[index], out buffer);
            string returnedName = null;
            if (status == 0 && buffer != IntPtr.Zero) {
                try { returnedName = NameAt(buffer); }
                finally { Check(FreeContextBuffer(buffer) == 0, route + " frees package metadata"); }
            }
            Console.WriteLine("QUERY {0}: {1} => {2}", route, Names[index], Hex(status));
            if (index < 2 || extended)
                Check(status == 0 && String.Equals(returnedName, Names[index], StringComparison.OrdinalIgnoreCase),
                      route + " returns the requested " + Names[index] + " package");
        }
    }
    private static void Enumerate(bool extended) {
        uint count;
        IntPtr buffer = IntPtr.Zero;
        int status = EnumerateSecurityPackagesW(out count, out buffer);
        Check(status == 0 && buffer != IntPtr.Zero && count <= 256, "bounded package enumeration succeeds");
        HashSet<string> names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if (status == 0 && buffer != IntPtr.Zero) {
            try {
                if (count <= 256) {
                    int size = Marshal.SizeOf(typeof(PackageInfo));
                    for (uint index = 0; index < count; ++index)
                        names.Add(NameAt(new IntPtr(checked(buffer.ToInt64() + index * size))));
                }
            } finally { Check(FreeContextBuffer(buffer) == 0, "enumeration metadata is freed"); }
        }
        foreach (string name in names) Console.WriteLine("PACKAGE " + name);
        Check(names.Contains(Names[0]) && names.Contains(Names[1]), "enumeration retains both native Schannel names");
        if (extended) Check(names.Contains(Names[2]), "enumeration also retains KxSChanl");
    }
    private static void NativeProvider() {
        IntPtr schannel = IntPtr.Zero;
        bool windowsLocation = false;
        string windows = Environment.GetEnvironmentVariable("SystemRoot");
        if (windows == null) throw new InvalidOperationException("SystemRoot is unavailable");
        string system32 = Path.Combine(windows, "System32");
        string wow64 = Path.Combine(windows, "SysWOW64");
        using (Process self = Process.GetCurrentProcess()) {
            foreach (ProcessModule module in self.Modules) {
                if (!String.Equals(module.ModuleName, "schannel.dll", StringComparison.OrdinalIgnoreCase)) continue;
                schannel = module.BaseAddress;
                string folder = Path.GetDirectoryName(module.FileName);
                windowsLocation = String.Equals(folder, system32, StringComparison.OrdinalIgnoreCase)
                    || String.Equals(folder, wow64, StringComparison.OrdinalIgnoreCase);
                break;
            }
        }
        Check(schannel != IntPtr.Zero && windowsLocation, "native Windows Schannel is loaded");
        Check(schannel != IntPtr.Zero && GetProcAddress(schannel, "SpUserModeInitialize") != IntPtr.Zero,
              "native provider exposes SpUserModeInitialize");
    }
    public static int Main(string[] args) {
        if (args.Length != 1 || (args[0] != "--native" && args[0] != "--vxkex")) {
            Console.Error.WriteLine("Usage: SspiPackages.exe --native | --vxkex");
            return 2;
        }
        bool extended = args[0] == "--vxkex";
        Console.WriteLine("Pointer bits: {0}; CLR: {1}", IntPtr.Size * 8, Environment.Version);
        try {
            bool loaded = GetModuleHandleW("KexDll.dll") != IntPtr.Zero;
            Check(loaded == extended, "actual NEXT activation matches requested mode");
            if (loaded != extended) return 1;
            QueryAll(QuerySecurityPackageInfoW, "CLR PInvoke", extended);
            Enumerate(extended);
            // Use the real loaded secur32 handle, rather than asking an extended
            // GetModuleHandle wrapper to choose an equivalent namespace.
            IntPtr secur32 = IntPtr.Zero;
            using (Process self = Process.GetCurrentProcess())
                foreach (ProcessModule module in self.Modules)
                    if (String.Equals(module.ModuleName, "secur32.dll", StringComparison.OrdinalIgnoreCase))
                        secur32 = module.BaseAddress;
            IntPtr query = secur32 == IntPtr.Zero ? IntPtr.Zero : GetProcAddress(secur32, "QuerySecurityPackageInfoW");
            Check(query != IntPtr.Zero, "native secur32 package query resolves");
            Console.WriteLine("Native query export owner: " + OwnerName(query));
            if (query != IntPtr.Zero)
                QueryAll((QueryPackage)Marshal.GetDelegateForFunctionPointer(query, typeof(QueryPackage)),
                         "secur32 GPA delegate", extended);
            NativeProvider();
        } catch (Exception error) {
            ++failures;
            Console.Error.WriteLine(error.GetType().Name + ": " + error.Message);
        }
        Console.WriteLine("Failures: " + failures);
        Console.WriteLine("Package queries only; no credentials, handshake or configuration changes.");
        return failures == 0 ? 0 : 1;
    }
}
