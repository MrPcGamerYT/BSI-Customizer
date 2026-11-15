// Offline companion backend for the native VOID Optimizer QML panel.
// No injection, game-memory edits, network, service/task changes or RAM-clean loop.
// Native timer requests and idle-sleep prevention are optional, held by THIS helper process/thread only.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Threading;
using System.Windows.Forms;
using System.Xml;
using System.Xml.Serialization;
using Microsoft.Win32;

namespace VoidOptimizer {
    public sealed class Options {
        public string Profile = "balanced";
        public bool Power, Capture, KeepAwake;
        public bool AutoRestore = true;
        public int Timer100ns;
        public static Options Parse(string value) {
            Uri uri;
            if (!Uri.TryCreate(value, UriKind.Absolute, out uri) || (uri.Scheme != "voidoptimizer" && uri.Scheme != "voidoptimizer-v2" && uri.Scheme != "voidoptimizer-v3") ||
                !String.IsNullOrEmpty(uri.UserInfo) || !uri.IsDefaultPort || !String.IsNullOrEmpty(uri.Fragment) ||
                (uri.AbsolutePath != "" && uri.AbsolutePath != "/")) throw new InvalidOperationException("Invalid optimizer request.");
            if (uri.Host == "open" && uri.Query == "") return new Options();
            if (uri.Host != "review") throw new InvalidOperationException("Only open/review requests are allowed.");
            Options result = new Options(); HashSet<string> keys = new HashSet<string>();
            foreach (string item in uri.Query.TrimStart('?').Split('&')) {
                string[] pair = item.Split('=');
                if (pair.Length != 2 || !keys.Add(pair[0])) throw new InvalidOperationException("Malformed or duplicate option.");
                if (pair[0] == "profile" && (pair[1] == "balanced" || pair[1] == "performance" || pair[1] == "eco")) result.Profile = pair[1];
                else if (pair[0] == "power" && (pair[1] == "0" || pair[1] == "1")) result.Power = pair[1] == "1";
                else if (pair[0] == "capture" && (pair[1] == "0" || pair[1] == "1")) result.Capture = pair[1] == "1";
                else if (pair[0] == "timer" && (pair[1] == "0" || pair[1] == "5000" || pair[1] == "10000")) result.Timer100ns = Int32.Parse(pair[1]);
                else if (pair[0] == "awake" && (pair[1] == "0" || pair[1] == "1")) result.KeepAwake = pair[1] == "1";
                else if (pair[0] == "autorestore" && (pair[1] == "0" || pair[1] == "1")) result.AutoRestore = pair[1] == "1";
                else throw new InvalidOperationException("Unsupported helper v3 option. Nothing applied; rebuild and register the matching helper from the UI pack.");
            }
            if (!keys.Contains("profile") || !keys.Contains("power") || !keys.Contains("capture")) throw new InvalidOperationException("A review requires profile, power and capture explicitly.");
            return result;
        }
        public string Priority { get { return Profile == "performance" ? "AboveNormal" : Profile == "eco" ? "BelowNormal" : "Normal"; } }
    }
    public sealed class Identity {
        public int Pid;
        public long Start;
        public string Image;
        public override string ToString() { return "PID " + Pid + "  |  " + Path.GetDirectoryName(Image); }
    }
    public sealed class CaptureValue {
        public int Index;
        public bool Existed;
        public int Before;
        public bool Change;
    }
    public sealed class Record {
        public int Format = 1;
        public Identity Target;
        public string BeforePriority, AfterPriority, BeforePower;
        public bool Priority, Power;
        public CaptureValue[] Capture = new CaptureValue[0];
        public void Validate() {
            Guid guid;
            ProcessPriorityClass priority;
            if (Format != 1 || Target == null || Target.Pid <= 0 || Target.Start <= 0 || String.IsNullOrEmpty(Target.Image) ||
                !Path.IsPathRooted(Target.Image) || !String.Equals(Path.GetFileName(Target.Image), "HD-Player.exe", StringComparison.OrdinalIgnoreCase) ||
                !Enum.TryParse<ProcessPriorityClass>(BeforePriority, out priority) || !Enum.IsDefined(typeof(ProcessPriorityClass), priority) ||
                (AfterPriority != "Normal" && AfterPriority != "AboveNormal" && AfterPriority != "BelowNormal") ||
                (Power && !Guid.TryParse(BeforePower, out guid)) || Capture == null || Capture.Length > 2)
                throw new InvalidOperationException("Invalid recovery record; no changes authorized.");
            HashSet<int> seen = new HashSet<int>();
            foreach (CaptureValue entry in Capture) if (entry == null || entry.Index < 0 || entry.Index > 1 || !seen.Add(entry.Index)) throw new InvalidOperationException("Invalid capture recovery entry.");
        }
    }
    public interface IPlatform {
        string Priority(Identity target);
        void Priority(Identity target, string value);
        string Power();
        void Power(string value);
        // Process-local effects: never replay a stale recovery file as an acquire operation.
        void TimerResolution(int period100ns);
        string TimerStatus();
        void KeepAwake(bool enabled);
        CaptureValue Capture(int index);
        void Capture(int index, bool exists, int value);
    }
    public interface IStore { bool Exists { get; } Record Read(); void Write(Record record); void Clear(); }

    public sealed class Session {
        public const string HighPower = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
        readonly IPlatform os; readonly IStore store;
        bool timerHeld, awakeHeld;
        public bool Active { get { return store.Exists || timerHeld || awakeHeld; } }
        public string TimerStatus() { return os.TimerStatus(); }
        public bool TargetExited() {
            if (!store.Exists) return false;
            Record record = store.Read(); record.Validate();
            try { os.Priority(record.Target); return false; } catch (TargetGone) { return true; }
        }
        public Session(IPlatform platform, IStore recovery) { os = platform; store = recovery; }
        public void Apply(Identity target, Options options) {
            if (Active) throw new InvalidOperationException("Restore the previous session before starting a new one.");
            if (options.Timer100ns != 0 && options.Timer100ns != 5000 && options.Timer100ns != 10000) throw new InvalidOperationException("Unsupported timer request.");
            // Snapshot and validate BEFORE touching the OS. The exact identity is rechecked on every process access.
            Record record = new Record { Target = target, BeforePriority = os.Priority(target), AfterPriority = options.Priority,
                BeforePower = options.Power ? os.Power() : null, Power = options.Power };
            record.Priority = record.BeforePriority != record.AfterPriority;
            if (options.Capture) {
                record.Capture = new CaptureValue[] { os.Capture(0), os.Capture(1) };
                foreach (CaptureValue value in record.Capture) value.Change = !value.Existed || value.Before != 0;
            }
            record.Validate(); store.Write(record); // Durable write-ahead recovery, including a partial apply.
            try {
                if (options.Timer100ns != 0) { os.TimerResolution(options.Timer100ns); timerHeld = true; }
                if (options.KeepAwake) { os.KeepAwake(true); awakeHeld = true; }
                if (record.Priority) {
                    if (os.Priority(target) != record.BeforePriority) throw new InvalidOperationException("Process priority changed during preview; retry.");
                    os.Priority(target, record.AfterPriority);
                    if (os.Priority(target) != record.AfterPriority) throw new InvalidOperationException("Priority read-back failed.");
                }
                if (record.Power) {
                    if (os.Power() != record.BeforePower) throw new InvalidOperationException("Power plan changed during preview; retry.");
                    os.Power(HighPower);
                    if (os.Power() != HighPower) throw new InvalidOperationException("Power plan read-back failed.");
                }
                foreach (CaptureValue value in record.Capture) if (value.Change) {
                    CaptureValue now = os.Capture(value.Index);
                    if (now.Existed != value.Existed || (now.Existed && now.Before != value.Before)) throw new InvalidOperationException("Game-capture preference changed during preview; retry.");
                    os.Capture(value.Index, true, 0);
                    now = os.Capture(value.Index);
                    if (!now.Existed || now.Before != 0) throw new InvalidOperationException("Game-capture read-back failed.");
                }
            } catch (Exception failure) {
                string rollback;
                try { rollback = Restore(); } catch (Exception error) { rollback = "RECOVERY STILL REQUIRED: " + error.Message; }
                throw new InvalidOperationException("Session did not complete: " + failure.Message + "\n" + rollback, failure);
            }
        }
        public string Restore() {
            List<string> notes = new List<string>(), failures = new List<string>();
            // Always release our process-local requests, including when a disk journal is damaged.
            if (timerHeld) try { os.TimerResolution(0); timerHeld = false; } catch (Exception error) { failures.Add("Timer release: " + error.Message); }
            if (awakeHeld) try { os.KeepAwake(false); awakeHeld = false; } catch (Exception error) { failures.Add("Sleep prevention release: " + error.Message); }
            if (!store.Exists) {
                if (failures.Count != 0) throw new InvalidOperationException(String.Join(" | ", failures.ToArray()));
                return "No active recovery record. Process-local requests released.";
            }
            Record record = store.Read(); record.Validate();
            // Restore every independent setting even if a preceding restore fails.
            foreach (CaptureValue value in record.Capture) if (value.Change) try {
                CaptureValue now = os.Capture(value.Index);
                if (now.Existed && now.Before == 0) os.Capture(value.Index, value.Existed, value.Before);
                else notes.Add("Kept externally changed capture preference " + value.Index + ".");
            } catch (Exception error) { failures.Add("Capture " + value.Index + ": " + error.Message); }
            if (record.Power) try {
                if (os.Power() == HighPower) os.Power(record.BeforePower);
                else notes.Add("Kept externally changed power plan.");
            } catch (Exception error) { failures.Add("Power plan: " + error.Message); }
            if (record.Priority) try {
                string now = os.Priority(record.Target);
                if (now == record.AfterPriority) os.Priority(record.Target, record.BeforePriority);
                else notes.Add("Kept externally changed process priority.");
            } catch (TargetGone) { notes.Add("Original process exited or its PID was reused; no other process touched."); }
              catch (Exception error) { failures.Add("Process: " + error.Message); }
            if (failures.Count != 0) throw new InvalidOperationException("Recovery record retained. " + String.Join(" | ", failures.ToArray()));
            store.Clear();
            return "Session restored. " + String.Join(" ", notes.ToArray());
        }
    }
    public sealed class TargetGone : Exception { public TargetGone() : base("Original process no longer matches.") {} }
    public sealed class WindowsPlatform : IPlatform {
        static readonly string[] Keys = { @"Software\Microsoft\Windows\CurrentVersion\GameDVR", @"System\GameConfigStore" };
        static readonly string[] Names = { "AppCaptureEnabled", "GameDVR_Enabled" };
        [DllImport("powrprof.dll")] static extern uint PowerGetActiveScheme(IntPtr root, out IntPtr guid);
        [DllImport("powrprof.dll")] static extern uint PowerSetActiveScheme(IntPtr root, ref Guid guid);
        [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
        [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
        static Process Match(Identity target) {
            Process process;
            try { process = Process.GetProcessById(target.Pid); } catch (ArgumentException) { throw new TargetGone(); }
            try {
                if (process.HasExited || process.StartTime.ToUniversalTime().Ticks != target.Start ||
                    !String.Equals(process.MainModule.FileName, target.Image, StringComparison.OrdinalIgnoreCase)) throw new TargetGone();
                IntPtr token;
                if (!OpenProcessToken(process.Handle, 8, out token)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                try { using (WindowsIdentity owner = new WindowsIdentity(token)) using (WindowsIdentity user = WindowsIdentity.GetCurrent())
                    if (owner.User == null || !owner.User.Equals(user.User)) throw new InvalidOperationException("Only this Windows user's emulator is allowed.");
                } finally { CloseHandle(token); }
                return process;
            } catch { process.Dispose(); throw; }
        }
        public string Priority(Identity target) { using (Process p = Match(target)) return p.PriorityClass.ToString(); }
        public void Priority(Identity target, string value) { using (Process p = Match(target)) p.PriorityClass = (ProcessPriorityClass)Enum.Parse(typeof(ProcessPriorityClass), value); }
        public string Power() {
            IntPtr ptr; uint error = PowerGetActiveScheme(IntPtr.Zero, out ptr);
            if (error != 0) throw new System.ComponentModel.Win32Exception((int)error);
            try { return ((Guid)Marshal.PtrToStructure(ptr, typeof(Guid))).ToString(); } finally { LocalFree(ptr); }
        }
        public void Power(string value) {
            Guid guid = new Guid(value); uint error = PowerSetActiveScheme(IntPtr.Zero, ref guid);
            if (error != 0) throw new System.ComponentModel.Win32Exception((int)error, "Power plan unavailable or not permitted.");
        }
        [DllImport("ntdll.dll", ExactSpelling=true)] static extern int NtQueryTimerResolution(out uint minimum, out uint maximum, out uint current);
        [DllImport("ntdll.dll", ExactSpelling=true)] static extern int NtSetTimerResolution(uint desired, [MarshalAs(UnmanagedType.U1)] bool enable, out uint current);
        [DllImport("kernel32.dll")] static extern uint SetThreadExecutionState(uint flags);
        uint timerPeriod, priorExecution;
        bool awake;
        static void NativeOK(int status) { if (status < 0) throw new InvalidOperationException("Native timer API refused: NTSTATUS 0x" + status.ToString("X8")); }
        public string TimerStatus() {
            uint min,max,current; NativeOK(NtQueryTimerResolution(out min,out max,out current));
            return String.Format(System.Globalization.CultureInfo.InvariantCulture,
                "Native query: current {0:0.###} ms; supported {1:0.###}–{2:0.###} ms. Our request: {3}. Not an emulator latency measurement.",
                current/10000.0, Math.Min(min,max)/10000.0, Math.Max(min,max)/10000.0,
                timerPeriod == 0 ? "none" : (timerPeriod/10000.0).ToString("0.###",System.Globalization.CultureInfo.InvariantCulture)+" ms");
        }
        public void TimerResolution(int period100ns) {
            uint current;
            if (period100ns == 0) {
                if (timerPeriod != 0) { NativeOK(NtSetTimerResolution(timerPeriod,false,out current)); timerPeriod=0; }
                return;
            }
            if (period100ns != 5000 && period100ns != 10000) throw new InvalidOperationException("Only 0.5/1 ms timer requests are allowed.");
            if (timerPeriod != 0) throw new InvalidOperationException("A native timer request is already held.");
            uint min,max; NativeOK(NtQueryTimerResolution(out min,out max,out current));
            uint requested=(uint)period100ns;
            if (requested < Math.Min(min,max) || requested > Math.Max(min,max)) throw new InvalidOperationException("Requested timer resolution is outside this Windows system's reported range.");
            NativeOK(NtSetTimerResolution(requested,true,out current)); timerPeriod=requested;
            // 'current' is an aggregate observation, not proof the emulator received this resolution.
        }
        public void KeepAwake(bool enabled) {
            if (enabled == awake) return;
            uint previous=SetThreadExecutionState(enabled ? 0x80000001u : priorExecution);
            if (previous == 0) throw new InvalidOperationException("Windows refused the thread idle-sleep request.");
            if (enabled) priorExecution=previous;
            awake=enabled;
        }
        public CaptureValue Capture(int index) {
            using (RegistryKey key = Registry.CurrentUser.OpenSubKey(Keys[index], false)) {
                object value = key == null ? null : key.GetValue(Names[index], null, RegistryValueOptions.DoNotExpandEnvironmentNames);
                if (value != null && key.GetValueKind(Names[index]) != RegistryValueKind.DWord) throw new InvalidOperationException("Unsupported existing game-capture value type; refusing overwrite.");
                return new CaptureValue { Index = index, Existed = value != null, Before = value == null ? 0 : (int)value };
            }
        }
        public void Capture(int index, bool exists, int value) {
            // If a baseline value was absent, remove only our value; never delete the whole key.
            using (RegistryKey key = exists ? Registry.CurrentUser.CreateSubKey(Keys[index]) : Registry.CurrentUser.OpenSubKey(Keys[index], true)) {
                if (key == null) return;
                if (exists) key.SetValue(Names[index], value, RegistryValueKind.DWord); else key.DeleteValue(Names[index], false);
                key.Flush();
            }
        }
    }
    public sealed class FileStore : IStore {
        readonly string file;
        public FileStore(string path) { file = path; }
        public bool Exists { get { return File.Exists(file); } }
        void Check() {
            string folder = Path.GetDirectoryName(file);
            if ((Directory.Exists(folder) && (File.GetAttributes(folder) & FileAttributes.ReparsePoint) != 0) ||
                (Exists && (File.GetAttributes(file) & FileAttributes.ReparsePoint) != 0)) throw new InvalidOperationException("Recovery path must not be a link.");
        }
        public Record Read() {
            Check(); if (new FileInfo(file).Length > 65536) throw new InvalidOperationException("Oversized recovery record.");
            XmlReaderSettings settings = new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = 65536 };
            using (XmlReader reader = XmlReader.Create(file, settings)) return (Record)new XmlSerializer(typeof(Record)).Deserialize(reader);
        }
        public void Write(Record value) {
            value.Validate(); Check(); Directory.CreateDirectory(Path.GetDirectoryName(file));
            string temp = file + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try {
                using (FileStream stream = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                    new XmlSerializer(typeof(Record)).Serialize(stream, value); stream.Flush(true);
                }
                if (Exists) File.Replace(temp, file, null); else File.Move(temp, file);
            } finally { if (File.Exists(temp)) File.Delete(temp); }
        }
        public void Clear() { Check(); File.Delete(file); }
    }
    public static class Protocol {
        const string Key = @"Software\Classes\voidoptimizer-v3";
        static string Command { get { return "\"" + Application.ExecutablePath + "\" --request \"%1\""; } }
        public static void Register(bool remove) {
            using (RegistryKey existing = Registry.CurrentUser.OpenSubKey(Key)) {
                if (existing != null) using (RegistryKey command = existing.OpenSubKey(@"shell\open\command")) {
                    if (command == null || !String.Equals(command.GetValue("") as string, Command, StringComparison.Ordinal))
                        throw new InvalidOperationException("A different voidoptimizer-v3 handler exists. It was not overwritten or removed.");
                }
                if (remove && existing == null) return;
            }
            if (remove) { Registry.CurrentUser.DeleteSubKeyTree(Key); return; }
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(Key)) {
                key.SetValue("", "URL:VOID Optimizer v3"); key.SetValue("URL Protocol", "");
                using (RegistryKey command = key.CreateSubKey(@"shell\open\command")) command.SetValue("", Command);
            }
        }
    }
    public sealed class OptimizerForm : Form {
        public const string WindowTitle = "VOID Optimizer v3 — session helper";
        public const int ShowMessage = 0x8000 + 73;
        readonly ComboBox targets = new ComboBox(), profile = new ComboBox();
        readonly CheckBox power = new CheckBox(), capture = new CheckBox(), awake = new CheckBox(), autoRestore = new CheckBox();
        readonly ComboBox timerResolution = new ComboBox();
        readonly System.Windows.Forms.Timer monitor = new System.Windows.Forms.Timer { Interval = 2000 };
        bool monitoring;
        readonly TextBox log = new TextBox();
        readonly Button apply = new Button(), restore = new Button(), refresh = new Button();
        readonly IStore store; readonly Session session;
        public OptimizerForm(Options options, bool smoke) {
            Text = WindowTitle; BackColor = Color.FromArgb(11,11,14); ForeColor = Color.WhiteSmoke;
            Font = new Font("Segoe UI", 10); ClientSize = new Size(760,780); MinimumSize = new Size(760,780);
            StartPosition = FormStartPosition.CenterScreen;
            store = smoke ? (IStore)new MemoryStore() : new FileStore(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "VOID-Optimizer", "session.xml"));
            session = new Session(smoke ? (IPlatform)new FakePlatform() : new WindowsPlatform(), store);
            Label title = new Label { Text = "𝓥𝓞𝓘𝓓  /  PERFORMANCE SESSION", Location = new Point(24,20), AutoSize = true, Font = new Font("Segoe UI", 17, FontStyle.Bold) }; Controls.Add(title);
            Label note = new Label { Text = "Developed By NOVA XIS  ·  No changes until you confirm. Minimize during play; close restores.", Location = new Point(26,65), AutoSize = true }; Controls.Add(note);
            targets.SetBounds(26,106,570,30); targets.DropDownStyle = ComboBoxStyle.DropDownList; Controls.Add(targets);
            refresh.Text = "Refresh"; refresh.SetBounds(610,105,120,32); refresh.Click += delegate { RefreshTargets(); }; Controls.Add(refresh);
            profile.SetBounds(26,154,570,30); profile.DropDownStyle = ComboBoxStyle.DropDownList;
            profile.Items.AddRange(new object[] { "Balanced — Normal priority", "Performance — AboveNormal priority", "Eco — BelowNormal priority (may reduce FPS)" });
            profile.SelectedIndex = options.Profile == "performance" ? 1 : options.Profile == "eco" ? 2 : 0; Controls.Add(profile);
            power.Text = "Use existing High performance power plan (more heat/battery use)"; power.SetBounds(26,202,700,32); power.Checked = options.Power; Controls.Add(power);
            capture.Text = "Disable this Windows user's Game DVR capture (game clips may stop)"; capture.SetBounds(26,240,700,32); capture.Checked = options.Capture; Controls.Add(capture);
            Label timerLabel=new Label {Text="Native timer request (helper process; no FPS guarantee)",Location=new Point(26,284),AutoSize=true}; Controls.Add(timerLabel);
            timerResolution.SetBounds(26,312,470,30); timerResolution.DropDownStyle=ComboBoxStyle.DropDownList;
            timerResolution.Items.AddRange(new object[]{"System default — no request", "1.0 ms — if supported", "0.5 ms — if supported (more power use)"});
            timerResolution.SelectedIndex=options.Timer100ns == 5000 ? 2 : options.Timer100ns == 10000 ? 1 : 0; Controls.Add(timerResolution);
            Button query=new Button { Text="Query timer",Location=new Point(514,310),Size=new Size(216,32) }; Controls.Add(query);
            query.Click+=delegate { try { log.Text=session.TimerStatus(); } catch(Exception error) { log.Text="QUERY FAILED: "+error.Message; } };
            awake.Text="Prevent idle system sleep during this session (display may still turn off)"; awake.SetBounds(26,354,700,30); awake.Checked=options.KeepAwake; Controls.Add(awake);
            autoRestore.Text="Automatically restore when the selected emulator instance exits"; autoRestore.SetBounds(26,392,700,30); autoRestore.Checked=options.AutoRestore; Controls.Add(autoRestore);
            Label limits=new Label {Text="Windows may scope/ignore timer requests, especially when minimized. No player injection, HPET or boot edits.",Location=new Point(26,432),Size=new Size(704,38)}; Controls.Add(limits);
            apply.Text = "Review & start session"; apply.SetBounds(26,482,220,42); apply.Click += delegate { Apply(); }; Controls.Add(apply);
            restore.Text = "Restore session"; restore.SetBounds(264,482,180,42); restore.Click += delegate { Restore(); }; Controls.Add(restore);
            log.SetBounds(26,548,704,208); log.Multiline = true; log.ReadOnly = true; log.ScrollBars = ScrollBars.Vertical; log.BackColor = Color.FromArgb(20,20,25); log.ForeColor = Color.Gainsboro; log.BorderStyle = BorderStyle.FixedSingle; log.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right; Controls.Add(log);
            foreach (Button button in new Button[] { apply, restore, refresh }) { button.FlatStyle = FlatStyle.Flat; button.BackColor = Color.FromArgb(35,35,42); button.ForeColor = Color.White; }
            RefreshTargets(); UpdateState();
            if (store.Exists) log.Text = "Recovery record found. Restore it before starting a new session. No changes applied on startup.";
            else log.Text = "Ready. Select an HD-Player instance explicitly. No High/Realtime, RAM trim, VSync, FSR/RTX, service or game-memory changes.";
            monitor.Tick += delegate {
                if (!monitoring || !autoRestore.Checked || !store.Exists) return;
                try { if (session.TargetExited()) { monitoring=false; Restore(); } }
                catch (Exception error) { monitoring=false; log.Text="Instance monitor stopped (no identity bypass): "+error.Message; }
            };
            monitor.Start();
            FormClosed += delegate { monitor.Stop(); monitor.Dispose(); };
            FormClosing += delegate(object sender, FormClosingEventArgs e) {
                if (session.Active && !Restore()) e.Cancel = MessageBox.Show(this,
                    "Some settings could not be restored. The recovery record is retained. Exit anyway and retry Restore later?",
                    "Pending recovery", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes;
            };
        }
        protected override void WndProc(ref Message message) {
            if (message.Msg == ShowMessage) { if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal; Show(); Activate(); }
            base.WndProc(ref message);
        }
        void RefreshTargets() {
            targets.Items.Clear(); targets.SelectedIndex = -1;
            foreach (Process process in Process.GetProcessesByName("HD-Player")) using (process) try {
                Identity identity = new Identity { Pid = process.Id, Start = process.StartTime.ToUniversalTime().Ticks, Image = process.MainModule.FileName };
                if (String.Equals(Path.GetFileName(identity.Image), "HD-Player.exe", StringComparison.OrdinalIgnoreCase)) targets.Items.Add(identity);
            } catch { /* Inaccessible instances are not silently selected or modified. */ }
        }
        void UpdateState() { apply.Enabled = !session.Active; targets.Enabled = profile.Enabled = power.Enabled = capture.Enabled = timerResolution.Enabled = awake.Enabled = autoRestore.Enabled = !session.Active; restore.Enabled = session.Active; }
        void Apply() {
            Identity target = targets.SelectedItem as Identity;
            if (target == null) { log.Text = "Select a running HD-Player instance. Nothing changed."; return; }
            Options options = new Options { Profile = profile.SelectedIndex == 1 ? "performance" : profile.SelectedIndex == 2 ? "eco" : "balanced", Power = power.Checked, Capture = capture.Checked, Timer100ns=timerResolution.SelectedIndex == 2 ? 5000 : timerResolution.SelectedIndex == 1 ? 10000 : 0, KeepAwake=awake.Checked, AutoRestore=autoRestore.Checked };
            string preview = target + "\n" + target.Image + "\n\nPriority: " + options.Priority + "\nHigh performance power plan: " + options.Power + "\nDisable Windows game capture: " + options.Capture + "\nNative timer request: " + (options.Timer100ns/10000.0) + " ms (0 = none)\nPrevent idle system sleep: " + options.KeepAwake + "\nAuto-restore on instance exit: " + options.AutoRestore + "\n\nTimer request belongs to this helper; Windows may not apply it to the emulator. More battery/heat is possible. No FPS gain is guaranteed. Normal helper close restores. Continue?";
            if (MessageBox.Show(this, preview, "Confirm exact changes", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes) return;
            try { session.Apply(target, options); monitoring=true; log.Text = "ACTIVE — priority/power/capture read back where selected; native requests accepted, NOT proof of emulator timer/latency changes. Keep the helper open during play. Close or Restore to undo."; }
            catch (Exception error) { log.Text = "STOPPED: " + error.Message; }
            UpdateState();
        }
        bool Restore() {
            try { log.Text = session.Restore(); monitoring=false; UpdateState(); return true; }
            catch (Exception error) { log.Text = "RESTORE FAILED: " + error.Message + "\r\nRecovery record is retained. Retry Restore, or confirm exit and recover later."; UpdateState(); return false; }
        }
    }
    // Fake adapters deliberately keep CI tests away from the host's power plan/registry/process scheduling.
    sealed class MemoryStore : IStore {
        public Record Value; public bool FailWrite;
        public bool Exists { get { return Value != null; } }
        public Record Read() { return Value; }
        public void Write(Record record) { if (FailWrite) throw new IOException("simulated journal failure"); Value = record; }
        public void Clear() { Value = null; }
    }
    sealed class FakePlatform : IPlatform {
        public string P = "Normal", G = "381b4222-f694-41f0-9685-ff5bb260df2e";
        public bool Gone, FailPower, FailRestore, FailTimer=false, FailAwake=false, FailTimerRelease=false;
        public int TimerPeriod; public bool Awake;
        public bool[] Exists = { true, false }; public int[] Values = { 1, 0 };
        public string Priority(Identity target) { if (Gone) throw new TargetGone(); return P; }
        public void Priority(Identity target, string value) { if (Gone) throw new TargetGone(); P = value; }
        public string Power() { return G; }
        public void Power(string value) { if ((FailPower && value == Session.HighPower) || (FailRestore && value != Session.HighPower)) throw new IOException("simulated power failure"); G = value; }
        public void TimerResolution(int period100ns) { if ((FailTimer && period100ns!=0) || (FailTimerRelease && period100ns==0)) throw new IOException("simulated timer failure"); TimerPeriod=period100ns; }
        public string TimerStatus() { return "Fake timer; no OS call"; }
        public void KeepAwake(bool enabled) { if (FailAwake && enabled) throw new IOException("simulated sleep request failure"); Awake=enabled; }
        public CaptureValue Capture(int index) { return new CaptureValue { Index = index, Existed = Exists[index], Before = Values[index] }; }
        public void Capture(int index, bool exists, int value) { Exists[index] = exists; Values[index] = value; }
    }
    public static class Tests {
        static void Check(bool value, string text) { if (!value) throw new Exception("SELF TEST: " + text); }
        public static void Run() {
            Options screenshot = Options.Parse("voidoptimizer-v3://review?profile=performance&power=1&capture=1&timer=5000&awake=0&autorestore=1");
            Check(screenshot.Profile=="performance" && screenshot.Power && screenshot.Capture && screenshot.Timer100ns==5000 && !screenshot.KeepAwake && screenshot.AutoRestore, "screenshot review is accepted by matching v3 helper without applying effects");
            Options full = Options.Parse("voidoptimizer://review?profile=performance&power=1&capture=1");
            Check(full.Priority == "AboveNormal" && full.Power && full.Capture, "URI options");
            foreach (string bad in new string[] { "https://review?profile=performance&power=1&capture=1", "voidoptimizer://review?profile=Realtime&power=1&capture=1", "voidoptimizer://review?profile=eco&power=0&capture=0&cmd=calc", "voidoptimizer://review?profile=eco&power=0&capture=0&power=1", "voidoptimizer://open?capture=1", "voidoptimizer://review?profile=eco&power=2&capture=0", "voidoptimizer://review?profile=eco%26power=1&power=0&capture=0", "voidoptimizer://open#fragment", "voidoptimizer://review?profile=eco", "voidoptimizer://open/extra" }) {
                bool refused = false; try { Options.Parse(bad); } catch { refused = true; } Check(refused, "URI rejected: " + bad);
            }
            Identity target = new Identity { Pid = 123, Start = 456, Image = @"C:\Fake Player\HD-Player.exe" };
            FakePlatform os = new FakePlatform(); string original = os.G; MemoryStore store = new MemoryStore(); Session session = new Session(os, store);
            session.Apply(target, full); Check(store.Exists && os.P == "AboveNormal" && os.G == Session.HighPower && os.Exists[1] && os.Values[0] == 0, "apply");
            bool second = false; try { session.Apply(target, full); } catch { second = true; } Check(second, "no nested session");
            session.Restore(); Check(!store.Exists && os.P == "Normal" && os.G == original && os.Values[0] == 1 && !os.Exists[1], "restore exact values/absence");
            os.FailPower = true; bool failed = false; try { session.Apply(target, full); } catch { failed = true; }
            Check(failed && !store.Exists && os.P == "Normal" && os.G == original && os.Values[0] == 1, "partial-apply rollback"); os.FailPower = false;
            store.FailWrite = true; failed = false; try { session.Apply(target, full); } catch { failed = true; }
            Check(failed && os.P == "Normal" && os.G == original, "journal failure before mutations"); store.FailWrite = false;
            session.Apply(target, full); os.P = "BelowNormal"; os.G = original; os.Values[0] = 5; session.Restore();
            Check(os.P == "BelowNormal" && os.G == original && os.Values[0] == 5 && !store.Exists, "preserve external edits");
            os.P = "Normal"; os.Values[0] = 1; session.Apply(target, full); os.Gone = true; session.Restore(); Check(!store.Exists, "exited/reused PID not changed"); os.Gone = false;
            os.P = "Normal"; session.Apply(target, full); os.FailRestore = true; failed = false; try { session.Restore(); } catch { failed = true; }
            Check(failed && store.Exists && os.P == "Normal" && os.Values[0] == 1, "restore errors retain journal but restore independent settings"); os.FailRestore = false; session.Restore();
            // Match every URI combination emitted by the new QML, including
            // the screenshot's 0.5ms/awake/no-autorestore review. Parsing has no effects.
            foreach (string preset in new string[] { "balanced", "performance", "eco" })
            foreach (int period in new int[] { 0, 5000, 10000 })
            for (int bits=0;bits<16;bits++) {
                string request="voidoptimizer-v3://review?profile="+preset+"&power="+(bits&1)+"&capture="+((bits>>1)&1)+"&timer="+period+"&awake="+((bits>>2)&1)+"&autorestore="+((bits>>3)&1);
                Options parsed=Options.Parse(request);
                Check(parsed.Profile==preset && parsed.Timer100ns==period && parsed.KeepAwake==((bits&4)!=0) && parsed.AutoRestore==((bits&8)!=0),"v3 QML protocol combination");
            }
            Options effects=Options.Parse("voidoptimizer-v2://review?profile=balanced&power=0&capture=0&timer=10000&awake=1&autorestore=1");
            Check(effects.Timer100ns==10000 && effects.KeepAwake && effects.AutoRestore,"explicit effects URI");
            os.P="Normal"; session.Apply(target,effects); Check(os.TimerPeriod==10000 && os.Awake && session.Active,"native effects held");
            os.Gone=true; Check(session.TargetExited(),"instance exit detected"); session.Restore(); os.Gone=false;
            Check(os.TimerPeriod==0 && !os.Awake && !session.Active,"effects released on exit restore");
            os.FailTimer=true; failed=false; try{session.Apply(target,effects);}catch{failed=true;} os.FailTimer=false;
            Check(failed && !session.Active && os.TimerPeriod==0 && !os.Awake,"timer failure rolls back");
            os.FailAwake=true; failed=false; try{session.Apply(target,effects);}catch{failed=true;} os.FailAwake=false;
            Check(failed && !session.Active && os.TimerPeriod==0,"later effect failure releases timer");
            store.FailWrite=true; failed=false; try{session.Apply(target,effects);}catch{failed=true;} store.FailWrite=false;
            Check(failed && os.TimerPeriod==0 && !os.Awake,"journal failure acquires no effects");
            session.Apply(target,effects); os.FailTimerRelease=true; failed=false; try{session.Restore();}catch{failed=true;}
            Check(failed && session.Active && !os.Awake && store.Exists,"release failure retains retry state and releases independent effects");
            os.FailTimerRelease=false; session.Restore(); Check(!session.Active && os.TimerPeriod==0,"release retry");
            foreach(string badTimer in new string[]{"1","-1","9999","500","Infinity"}) {
                failed=false; try{Options.Parse("voidoptimizer://review?profile=balanced&power=0&capture=0&timer="+badTimer);}catch{failed=true;}
                Check(failed,"unsafe timer request rejected");
            }
            string folder = Path.Combine(Path.GetTempPath(), "VOID optimizer self test " + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(folder);
            try {
                FileStore disk = new FileStore(Path.Combine(folder, "session.xml"));
                Record record = new Record { Target = target, BeforePriority = "Normal", AfterPriority = "AboveNormal" };
                disk.Write(record); disk.Read().Validate(); Check(disk.Exists, "durable XML roundtrip"); disk.Clear();
                File.WriteAllText(Path.Combine(folder, "session.xml"), "<!DOCTYPE x [<!ENTITY a SYSTEM 'file:///C:/Windows/win.ini'>]><Record>&a;</Record>");
                failed = false; try { disk.Read(); } catch { failed = true; } Check(failed, "DTD rejected");
            } finally { Directory.Delete(folder, true); }
            Console.WriteLine("PASS: strict requests, explicit presets, exact restore, absence, partial rollback, durable journal, concurrent edits, PID reuse, recovery retry, DTD rejection, native timer/sleep effects acquire/release/failure retry and instance-exit detection; host tuning untouched.");
        }
    }
    static class Program {
        [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string cls, string title);
        [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window, int message, IntPtr a, IntPtr b);
        [STAThread] static int Main(string[] args) {
            try {
                Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                if (args.Length == 1 && args[0] == "--self-test") { Tests.Run(); return 0; }
                bool smoke = args.Length == 1 && args[0] == "--ui-smoke-test";
                Options options = new Options();
                if (args.Length == 2 && args[0] == "--request") options = Options.Parse(args[1]);
                else if (!smoke && args.Length != 0 && !(args.Length == 1 && (args[0] == "--register" || args[0] == "--unregister"))) throw new InvalidOperationException("Unsupported command line; nothing applied.");
                string sid; using (WindowsIdentity identity = WindowsIdentity.GetCurrent()) sid = identity.User.Value;
                bool first;
                using (Mutex mutex = new Mutex(true, @"Local\VOIDOptimizer-" + sid, out first)) {
                    if (!first) {
                        if (args.Length == 1 && (args[0] == "--register" || args[0] == "--unregister")) { MessageBox.Show("Close the running helper first. The protocol link was not changed.", "VOID Optimizer"); return 1; }
                        IntPtr window = FindWindow(null, OptimizerForm.WindowTitle);
                        if (window != IntPtr.Zero) PostMessage(window, OptimizerForm.ShowMessage, IntPtr.Zero, IntPtr.Zero);
                        else MessageBox.Show("Another VOID helper is starting. Retry shortly.", "VOID Optimizer");
                        return 0; // Never apply a new URI over an existing session.
                    }
                    if (args.Length == 1 && (args[0] == "--register" || args[0] == "--unregister")) {
                        Protocol.Register(args[0] == "--unregister");
                        MessageBox.Show(args[0] == "--register" ? "Per-user helper link registered. No performance changes applied." : "This helper's protocol link removed. Native sidebar/assets were not changed.", "VOID Optimizer");
                        return 0;
                    }
                    using (OptimizerForm form = new OptimizerForm(options, smoke)) {
                        if (smoke) {
                            System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer { Interval = 300 };
                            timer.Tick += delegate { timer.Stop(); form.Close(); timer.Dispose(); }; timer.Start();
                        }
                        Application.Run(form);
                    }
                }
                return 0;
            } catch (Exception error) {
                Console.Error.WriteLine(error);
                if (args.Length == 1 && args[0] == "--self-test") File.WriteAllText(Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "self-test-error.txt"), error.ToString());
                if (args.Length == 0 || args[0] != "--self-test") MessageBox.Show("STOPPED: " + error.Message, "VOID Optimizer", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }
    }
}
