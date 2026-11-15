using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Collections.Generic;
namespace VoidOptimizer {
    static class DiscoveryTests {
        static void Need(bool condition,string message) { if(!condition)throw new Exception("DISCOVERY TEST: "+message); }
        public static void Run() {
            DiscoveryReport fake=PlayerDiscovery.Scan(new int[]{1,2,3,4,1},delegate(int pid) {
                if(pid==2)throw new Win32Exception(5,"Access denied");
                if(pid==3)throw new InvalidOperationException("Different Windows user");
                if(pid==4)throw new TargetGone();
                return new Identity { Pid=pid,Start=1,Image=@"C:\Fixture\HD-Player.exe" };
            });
            Need(fake.Candidates==4&&fake.Players.Count==1&&fake.Issues.Count==3,"denied/foreign/exited candidates are reported, not silently discarded; duplicate PID removed");
            Need(fake.Details.Contains("BLOCKED")&&fake.Details.Contains("Different Windows user")&&fake.Details.Contains("EXITED"),"diagnostic reasons");
            Need(PlayerDiscovery.Scan(new int[0],PlayerAccess.Inspect).Details.Contains("No HD-Player process name found"),"empty scan diagnosis");
            bool refused=false;
            try { PlayerAccess.Inspect(Process.GetCurrentProcess().Id); }catch(InvalidOperationException) { refused=true; }
            Need(refused,"manual PID cannot authorize an unrelated executable");
            List<Process> children=new List<Process>();
            try {
                foreach(string bits in new string[]{"x86","x64"}) {
                    string path=Path.Combine(Path.GetDirectoryName(System.Windows.Forms.Application.ExecutablePath),"fixtures",bits,"HD-Player.exe");
                    Process child=Process.Start(new ProcessStartInfo(path) { UseShellExecute=false,CreateNoWindow=true });children.Add(child);
                    Identity identity=PlayerAccess.Inspect(child.Id);
                    Need(identity.Pid==child.Id&&String.Equals(identity.Image,path,StringComparison.OrdinalIgnoreCase),"query-only "+bits+" image path");
                    Need(identity.Start==child.StartTime.ToUniversalTime().Ticks,"exact creation time "+bits);
                    Need(!String.IsNullOrEmpty(PlayerAccess.Priority(identity)),"query-only priority "+bits);
                    DiscoveryReport observed=PlayerDiscovery.Scan();
                    Need(observed.Players.Exists(delegate(Identity row) { return row.Pid==child.Id; }),"real name enumeration finds "+bits+" child");
                    refused=false;
                    try { PlayerAccess.Priority(new Identity { Pid=identity.Pid,Image=identity.Image,Start=identity.Start+1 }); }catch(TargetGone) { refused=true; }
                    Need(refused,"stale identity refused "+bits);
                    child.Kill();child.WaitForExit();refused=false;
                    try { PlayerAccess.Priority(identity); }catch(TargetGone) { refused=true; }
                    Need(refused,"exited target refused "+bits);
                }
            } finally { foreach(Process child in children)using(child) { if(!child.HasExited) { child.Kill();child.WaitForExit(); } } }
            Console.WriteLine("PASS: real x86/x64 HD-Player-name fixture detection, limited-query image/time/owner reads, manual PID whitelist, query-only priority, stale/exited identity refusal, and deterministic blocked/foreign/empty diagnostics. No registry/power/priority/timer changes.");
        }
    }
}
