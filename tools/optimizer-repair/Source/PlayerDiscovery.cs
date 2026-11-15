using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;

namespace VoidOptimizer {
    // A single query-only handle supplies image, creation time and token owner.
    // No MainModule/module enumeration, Process.Handle (all-access), debug privilege or injection.
    public static class PlayerAccess {
        const uint QueryLimited = 0x1000, SetInformation = 0x0200;
        [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr handle, uint flags, StringBuilder name, ref int size);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetProcessTimes(IntPtr handle, out long created, out long exited, out long kernel, out long user);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr handle, out uint code);
        [DllImport("kernel32.dll", SetLastError=true)] static extern uint GetPriorityClass(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetPriorityClass(IntPtr handle, uint priority);
        [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr handle, uint access, out IntPtr token);
        static Exception Error(string operation) { int code = Marshal.GetLastWin32Error(); return new Win32Exception(code, operation + " (Windows " + code + "): " + new Win32Exception(code).Message); }
        static Identity Read(IntPtr handle, int pid) {
            uint exitCode;
            if (!GetExitCodeProcess(handle, out exitCode)) throw Error("Read process state");
            if (exitCode != 259) throw new TargetGone();
            StringBuilder name = new StringBuilder(32768); int size = name.Capacity;
            if (!QueryFullProcessImageName(handle, 0, name, ref size)) throw Error("Read process image");
            if (!String.Equals(Path.GetFileName(name.ToString()), "HD-Player.exe", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Not HD-Player.exe. Renamed/unrelated executables are not authorized.");
            long created, exited, kernel, user;
            if (!GetProcessTimes(handle, out created, out exited, out kernel, out user)) throw Error("Read process creation time");
            IntPtr token;
            if (!OpenProcessToken(handle, 8, out token)) throw Error("Read process owner");
            try {
                using (WindowsIdentity owner = new WindowsIdentity(token)) using (WindowsIdentity current = WindowsIdentity.GetCurrent())
                    if (owner.User == null || !owner.User.Equals(current.User)) throw new InvalidOperationException("Different Windows user. Only this user's emulator is allowed.");
            } finally { CloseHandle(token); }
            return new Identity { Pid = pid, Image = name.ToString(), Start = DateTime.FromFileTimeUtc(created).Ticks };
        }
        static IntPtr Open(int pid, bool write) {
            IntPtr handle = OpenProcess(QueryLimited | (write ? SetInformation : 0), false, pid);
            if (handle == IntPtr.Zero) {
                int code = Marshal.GetLastWin32Error();
                if (code == 87) throw new TargetGone();
                throw new Win32Exception(code, "Open PID " + pid + " (Windows " + code + "): " + new Win32Exception(code).Message);
            }
            return handle;
        }
        public static Identity Inspect(int pid) {
            if (pid <= 0) throw new InvalidOperationException("Enter a positive PID from Task Manager > Details.");
            IntPtr handle = Open(pid, false);
            try { return Read(handle, pid); } finally { CloseHandle(handle); }
        }
        static IntPtr Match(Identity target, bool write) {
            if (target == null || target.Pid <= 0 || target.Start <= 0 || String.IsNullOrEmpty(target.Image)) throw new InvalidOperationException("Select a verified player identity first.");
            IntPtr handle = Open(target.Pid, write);
            try {
                Identity observed = Read(handle, target.Pid);
                if (observed.Start != target.Start || !String.Equals(observed.Image, target.Image, StringComparison.OrdinalIgnoreCase)) throw new TargetGone();
                return handle;
            } catch { CloseHandle(handle); throw; }
        }
        public static string Priority(Identity target) {
            IntPtr handle = Match(target, false);
            try { uint value = GetPriorityClass(handle); if (value == 0) throw Error("Read priority"); return ((ProcessPriorityClass)value).ToString(); }
            finally { CloseHandle(handle); }
        }
        public static void Priority(Identity target, string value) {
            // Recovery may restore a pre-existing priority outside the selectable presets.
            ProcessPriorityClass priority;
            if (!Enum.TryParse<ProcessPriorityClass>(value, out priority) || !Enum.IsDefined(typeof(ProcessPriorityClass), priority)) throw new InvalidOperationException("Invalid priority.");
            IntPtr handle = Match(target, true);
            try { if (!SetPriorityClass(handle, (uint)priority)) throw Error("Set priority"); }
            finally { CloseHandle(handle); }
        }
    }
    public sealed class DiscoveryReport {
        public readonly List<Identity> Players = new List<Identity>();
        public readonly List<string> Issues = new List<string>();
        public int Candidates;
        public string Summary { get { return Players.Count + " verified player" + (Players.Count == 1 ? "" : "s") + " / " + Candidates + " candidate" + (Candidates == 1 ? "" : "s") + (Issues.Count == 0 ? "" : " / " + Issues.Count + " unavailable"); } }
        public string Details {
            get {
                StringBuilder text = new StringBuilder(Summary + Environment.NewLine);
                foreach (Identity player in Players) text.AppendLine("READY  PID " + player.Pid + " | " + player.Image);
                foreach (string issue in Issues) text.AppendLine(issue);
                if (Candidates == 0) text.AppendLine("No HD-Player process name found. Check Task Manager > Details while Android is running. Renamed executables are not accepted.");
                text.Append("Access denied? Match the emulator's elevation under the SAME Windows account, or restart the emulator normally. No automatic elevation, debug privilege or ownership bypass. Selection and confirmation are always required.");
                return text.ToString();
            }
        }
    }
    public static class PlayerDiscovery {
        public static DiscoveryReport Scan(IEnumerable<int> pids, Func<int, Identity> inspect) {
            DiscoveryReport result = new DiscoveryReport(); HashSet<int> seen = new HashSet<int>();
            foreach (int pid in pids) {
                if (!seen.Add(pid)) continue;
                result.Candidates++;
                try { result.Players.Add(inspect(pid)); }
                catch (TargetGone) { result.Issues.Add("EXITED  PID " + pid + " | player exited during scan; refresh."); }
                catch (Win32Exception error) { result.Issues.Add("BLOCKED  PID " + pid + " | " + error.Message); }
                catch (InvalidOperationException error) { result.Issues.Add("UNAVAILABLE  PID " + pid + " | " + error.Message); }
            }
            result.Players.Sort(delegate(Identity a, Identity b) { return a.Pid.CompareTo(b.Pid); });
            return result;
        }
        public static DiscoveryReport Scan() {
            List<int> ids = new List<int>();
            foreach (Process process in Process.GetProcessesByName("HD-Player")) using (process) ids.Add(process.Id);
            return Scan(ids, PlayerAccess.Inspect);
        }
    }
}
