using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace VoidOptimizer {
    public sealed class OptimizerForm : Form {
        public const string WindowTitle="VOID Optimizer v3 — session helper";
        public const int ShowMessage=0x8000+73;
        readonly StudioChoice targets=new StudioChoice(),profile=new StudioChoice(),timerResolution=new StudioChoice();
        readonly StudioToggle power=new StudioToggle(),capture=new StudioToggle(),awake=new StudioToggle(),autoRestore=new StudioToggle();
        readonly StudioButton apply=new StudioButton(),restore=new StudioButton(),refresh=new StudioButton(),inspect=new StudioButton();
        readonly StudioScroll scroll=new StudioScroll();
        readonly TextBox pidInput=new TextBox(),log=new TextBox();
        readonly Label scanStatus=new Label();
        readonly System.Windows.Forms.Timer monitor=new System.Windows.Forms.Timer { Interval=2000 };
        readonly IStore store; readonly Session session; readonly bool smokeMode;
        bool monitoring;
        string discoveryDetails="Not scanned yet.";
        [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr handle,int attribute,ref int value,int size);
        [DllImport("uxtheme.dll", CharSet=CharSet.Unicode)] static extern int SetWindowTheme(IntPtr handle,string subApp,string subId);
        public OptimizerForm(Options options,bool smoke) {
            smokeMode=smoke; Text=WindowTitle; BackColor=Skin.Background; ForeColor=Skin.Text;
            Font=new Font("Segoe UI",9.5f); AutoScaleMode=AutoScaleMode.Dpi; AutoScaleDimensions=new SizeF(96,96);
            StartPosition=FormStartPosition.CenterScreen; MinimumSize=new Size(680,560);
            ClientSize=new Size(800,Math.Max(520,Math.Min(850,Screen.PrimaryScreen.WorkingArea.Height-90)));
            store=smoke?(IStore)new MemoryStore():new FileStore(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"VOID-Optimizer","session.xml"));
            session=new Session(smoke?(IPlatform)new FakePlatform():new WindowsPlatform(),store);
            TableLayoutPanel frame=new TableLayoutPanel { Dock=DockStyle.Fill,ColumnCount=1,RowCount=3,Padding=new Padding(20,12,20,12),BackColor=Skin.Background };
            frame.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
            frame.RowStyles.Add(new RowStyle(SizeType.Absolute,85)); frame.RowStyles.Add(new RowStyle(SizeType.Percent,100)); frame.RowStyles.Add(new RowStyle(SizeType.Absolute,148));
            Controls.Add(frame);
            TableLayoutPanel header=Stack();
            Add(header,Words("𝓥𝓞𝓘𝓓   /   LOCAL SESSION CONTROL                                       HELPER 06",8.5f,Skin.Muted));
            Add(header,Words("Session helper",23,Skin.Text));
            Add(header,Words("Developed By NOVA XIS  ·  Review first. Keep open during play; close restores.",9,Skin.Muted));
            frame.Controls.Add(header,0,0);
            scroll.Dock=DockStyle.Fill;scroll.Margin=new Padding(0);
            TableLayoutPanel content=Stack();content.Padding=new Padding(0,0,2,0);scroll.SetBody(content); frame.Controls.Add(scroll,0,1);
            TableLayoutPanel player=Section(content,"01   /   PLAYER INSTANCE");
            TableLayoutPanel targetRow=Row(116); targets.Dock=DockStyle.Fill;targets.Placeholder="Select a verified HD-Player instance";targets.AccessibleName="Player instance";
            refresh.Text="Refresh";refresh.Dock=DockStyle.Fill;refresh.Click+=delegate { RefreshTargets(); };
            targetRow.Controls.Add(targets,0,0);targetRow.Controls.Add(refresh,1,0);Add(player,targetRow);
            scanStatus.AutoSize=true;scanStatus.ForeColor=Skin.Muted;scanStatus.Margin=new Padding(0,6,0,6);Add(player,scanStatus);
            FlowLayoutPanel manual=new FlowLayoutPanel { AutoSize=true,Dock=DockStyle.Top,WrapContents=false,Margin=new Padding(0),Padding=new Padding(0) };
            Label pidLabel=Words("Missing? Inspect PID",9,Skin.Muted);pidLabel.Margin=new Padding(0,9,10,0);manual.Controls.Add(pidLabel);
            pidInput.Width=90;pidInput.BackColor=Color.FromArgb(29,29,38);pidInput.ForeColor=Skin.Text;pidInput.BorderStyle=BorderStyle.FixedSingle;pidInput.AccessibleName="Player PID from Task Manager Details";pidInput.Margin=new Padding(0,7,10,0);manual.Controls.Add(pidInput);
            inspect.Text="Inspect PID";inspect.Width=110;inspect.Height=32;inspect.Click+=delegate { InspectPid(); };manual.Controls.Add(inspect);
            Add(player,manual);
            TableLayoutPanel tuning=Section(content,"02   /   PROCESS & WINDOWS");
            profile.Dock=DockStyle.Top;profile.AccessibleName="Process priority profile";
            profile.Items.AddRange(new object[]{"Balanced  /  Normal priority","Performance  /  Above normal priority","Background  /  Below normal priority (may reduce FPS)"});
            profile.SelectedIndex=options.Profile=="performance"?1:options.Profile=="eco"?2:0;Add(tuning,profile);
            Toggle(tuning,power,"High performance power plan","Use an existing plan. May increase heat and battery use.",options.Power);
            Toggle(tuning,capture,"Pause Windows game capture","Change this user's Game DVR preferences; Windows clips may stop.",options.Capture);
            TableLayoutPanel timing=Section(content,"03   /   TIMING & RECOVERY");
            TableLayoutPanel timingRow=Row(116);timerResolution.Dock=DockStyle.Fill;timerResolution.AccessibleName="Native timer request";
            timerResolution.Items.AddRange(new object[]{"Default timing  /  no request","1.0 ms request  /  if supported","0.5 ms request  /  more power use"});
            timerResolution.SelectedIndex=options.Timer100ns==5000?2:options.Timer100ns==10000?1:0;
            StudioButton query=new StudioButton { Text="Query timer",Dock=DockStyle.Fill };
            query.Click+=delegate { try { log.Text=session.TimerStatus(); } catch(Exception error) { log.Text="QUERY FAILED: "+error.Message; } };
            timingRow.Controls.Add(timerResolution,0,0);timingRow.Controls.Add(query,1,0);Add(timing,timingRow);
            Toggle(timing,awake,"Keep the system awake","Session idle-sleep prevention. Display policy stays unchanged.",options.KeepAwake);
            Toggle(timing,autoRestore,"Restore when the selected instance exits","Checks PID, creation time, full image path and Windows owner.",options.AutoRestore);
            Label limits=Words("Timer requests belong to this helper, not the player. Windows may scope or ignore them. No FPS guarantee, injection, services, RAM cleaning or boot edits.",8.5f,Skin.Muted);
            limits.Dock=DockStyle.Top;limits.MaximumSize=new Size(700,0);Add(content,limits);
            CardPanel footer=new CardPanel { Dock=DockStyle.Fill,AutoSize=false,Margin=new Padding(0,8,0,0),Padding=new Padding(12) };
            frame.Controls.Add(footer,0,2);
            TableLayoutPanel footerBody=new TableLayoutPanel { Dock=DockStyle.Fill,ColumnCount=1,RowCount=2,Margin=new Padding(0),Padding=new Padding(0),BackColor=Color.Transparent };
            footerBody.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));
            footerBody.RowStyles.Add(new RowStyle(SizeType.Absolute,44));footerBody.RowStyles.Add(new RowStyle(SizeType.Percent,100));footer.Controls.Add(footerBody);
            FlowLayoutPanel actions=new FlowLayoutPanel { Dock=DockStyle.Top,AutoSize=true,WrapContents=false,Margin=new Padding(0) };
            apply.Text="Review & start session";apply.Primary=true;apply.Width=184;apply.Click+=delegate { Apply(); };actions.Controls.Add(apply);
            restore.Text="Restore session";restore.Width=140;restore.Click+=delegate { Restore(); };actions.Controls.Add(restore);
            StudioButton diagnostics=new StudioButton { Text="Diagnostics",Width=120 };diagnostics.Click+=delegate { ShowDiagnostics(); };actions.Controls.Add(diagnostics);
            actions.Dock=DockStyle.Fill;actions.AutoSize=false;footerBody.Controls.Add(actions,0,0);
            log.Multiline=true;log.ReadOnly=true;log.Height=55;log.Dock=DockStyle.Top;log.BorderStyle=BorderStyle.None;log.BackColor=Skin.Surface;log.ForeColor=Skin.Muted;log.Font=new Font("Segoe UI",9);log.Margin=new Padding(0,8,0,0);log.AccessibleName="Session result and status";log.Dock=DockStyle.Fill;footerBody.Controls.Add(log,0,1);
            targets.SelectedIndexChanged+=delegate { UpdateState(); };
            RefreshTargets();UpdateState();
            log.Text=store.Exists?"RECOVERY PENDING. Restore the previous session before starting another. Nothing applied on startup.":"NOT APPLIED. Select a verified player, review your actions, then confirm the exact changes.";
            monitor.Tick+=delegate {
                if(!monitoring||!autoRestore.Checked||!store.Exists)return;
                try { if(session.TargetExited()) { monitoring=false;Restore(); } }
                catch(Exception error) { monitoring=false;log.Text="Monitor stopped (no identity bypass): "+error.Message; }
            };
            monitor.Start();FormClosed+=delegate { monitor.Stop();monitor.Dispose(); };
            FormClosing+=delegate(object sender,FormClosingEventArgs e) {
                if(session.Active&&!Restore())e.Cancel=MessageBox.Show(this,"Some settings could not be restored. Recovery is retained. Exit anyway and retry Restore later?","Pending recovery",MessageBoxButtons.YesNo,MessageBoxIcon.Warning,MessageBoxDefaultButton.Button2)!=DialogResult.Yes;
            };
        }
        static TableLayoutPanel Stack() { TableLayoutPanel table=new TableLayoutPanel { ColumnCount=1,AutoSize=true,AutoSizeMode=AutoSizeMode.GrowAndShrink,Dock=DockStyle.Top,Margin=new Padding(0),Padding=new Padding(0),BackColor=Color.Transparent };table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));return table; }
        static void Add(TableLayoutPanel table,Control child) { int row=table.RowCount++;table.RowStyles.Add(new RowStyle(SizeType.AutoSize));table.Controls.Add(child,0,row); }
        static Label Words(string text,float size,Color color) { return new Label { Text=text,UseMnemonic=false,AutoSize=true,Font=new Font("Segoe UI",size),ForeColor=color,BackColor=Color.Transparent,Margin=new Padding(0,0,0,6) }; }
        static TableLayoutPanel Row(int rightWidth) { TableLayoutPanel table=new TableLayoutPanel { ColumnCount=2,RowCount=1,Dock=DockStyle.Top,Height=44,Margin=new Padding(0) };table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));table.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,rightWidth));return table; }
        static TableLayoutPanel Section(TableLayoutPanel parent,string title) { CardPanel card=new CardPanel();TableLayoutPanel body=Stack();card.Controls.Add(body);Add(body,Words(title,9,Skin.Muted));Add(parent,card);return body; }
        static void Toggle(TableLayoutPanel parent,StudioToggle control,string title,string detail,bool value) { control.Text=title;control.Detail=detail;control.Checked=value;control.AccessibleName=title;Add(parent,control); }
        protected override void OnHandleCreated(EventArgs e) { base.OnHandleCreated(e);try { int dark=1;DwmSetWindowAttribute(Handle,20,ref dark,4);int corners=2;DwmSetWindowAttribute(Handle,33,ref corners,4);SetWindowTheme(Handle,"DarkMode_Explorer",null); } catch(DllNotFoundException) { } catch(EntryPointNotFoundException) { } }
        protected override void WndProc(ref Message message) { if(message.Msg==ShowMessage) { if(WindowState==FormWindowState.Minimized)WindowState=FormWindowState.Normal;Show();Activate(); }base.WndProc(ref message); }
        void RefreshTargets() {
            if(session.Active) { log.Text="Restore the active session before changing instances.";return; }
            targets.Items.Clear();targets.SelectedIndex=-1;
            try {
                DiscoveryReport report=smokeMode?new DiscoveryReport():PlayerDiscovery.Scan();
                if(smokeMode) { report.Candidates=2;report.Players.Add(new Identity { Pid=4242,Start=1,Image=@"C:\Preview only\HD-Player.exe" });report.Issues.Add("PREVIEW ONLY: simulated candidate. No real process access or tuning."); }
                foreach(Identity player in report.Players)targets.Items.Add(player);
                scanStatus.Text=report.Summary+"  ·  Select explicitly; no automatic tuning.";discoveryDetails=report.Details;
                targets.Placeholder=report.Players.Count==0?"No verified player — open Diagnostics":"Choose a player instance";
                log.Text=report.Players.Count==0?report.Details:"Scan complete. Select a player explicitly; nothing applied.";
            } catch(Exception error) { scanStatus.Text="Scan failed — open Diagnostics";discoveryDetails=error.ToString();log.Text="SCAN FAILED: "+error.Message; }
            targets.Invalidate();scroll.PerformLayout();UpdateState();
        }
        void InspectPid() {
            if(session.Active)return;
            targets.SelectedIndex=-1;
            int pid;
            if(!Int32.TryParse(pidInput.Text.Trim(),out pid)||pid<=0) { log.Text="Enter the PID from Task Manager > Details, not a filename. Nothing changed.";return; }
            try {
                if(smokeMode)throw new InvalidOperationException("PID inspection is disabled in UI preview mode.");
                Identity identity=PlayerAccess.Inspect(pid);targets.Items.Clear();targets.Items.Add(identity);targets.SelectedIndex=-1;
                discoveryDetails="MANUAL PID VERIFIED (query only): "+identity.Image+" | PID "+pid+". Select it explicitly in the player menu.";
                scanStatus.Text="PID verified. Open the player menu and select it.";log.Text=discoveryDetails;targets.Invalidate();
            } catch(Exception error) { discoveryDetails="PID "+pid+": "+error.Message;log.Text="NOT ATTACHED: "+discoveryDetails; }
            UpdateState();
        }
        void ShowDiagnostics() {
            using(Form dialog=new Form { Text="VOID / detection diagnostics",BackColor=Skin.Background,ForeColor=Skin.Text,Font=Font,StartPosition=FormStartPosition.CenterParent,Size=new Size(700,440),MinimizeBox=false }) {
                TextBox details=new TextBox { Multiline=true,ReadOnly=true,Dock=DockStyle.Fill,BorderStyle=BorderStyle.None,BackColor=Skin.Surface,ForeColor=Skin.Text,ScrollBars=ScrollBars.Vertical,Text=discoveryDetails+Environment.NewLine+Environment.NewLine+"SESSION STATUS"+Environment.NewLine+log.Text+Environment.NewLine+"No changes are applied by scanning, PID inspection or this dialog." };
                dialog.Padding=new Padding(16);dialog.Controls.Add(details);dialog.ShowDialog(this);
            }
        }
        void UpdateState() {
            bool idle=!session.Active;
            targets.Enabled=profile.Enabled=power.Enabled=capture.Enabled=timerResolution.Enabled=awake.Enabled=autoRestore.Enabled=refresh.Enabled=inspect.Enabled=pidInput.Enabled=idle;
            apply.Enabled=idle&&targets.SelectedItem is Identity;restore.Enabled=!idle;
        }
        void Apply() {
            Identity target=targets.SelectedItem as Identity;
            if(target==null) { log.Text="Select a verified HD-Player instance. Nothing changed.";return; }
            Options options=new Options { Profile=profile.SelectedIndex==1?"performance":profile.SelectedIndex==2?"eco":"balanced",Power=power.Checked,Capture=capture.Checked,Timer100ns=timerResolution.SelectedIndex==2?5000:timerResolution.SelectedIndex==1?10000:0,KeepAwake=awake.Checked,AutoRestore=autoRestore.Checked };
            string preview=target+"\n"+target.Image+"\n\nPriority: "+options.Priority+"\nHigh performance plan: "+options.Power+"\nDisable game capture: "+options.Capture+"\nNative timer request: "+(options.Timer100ns/10000.0)+" ms (0 = none)\nPrevent idle sleep: "+options.KeepAwake+"\nAuto-restore on exit: "+options.AutoRestore+"\n\nThe timer request belongs to this helper, not the player. More heat/battery use is possible. No FPS guarantee. Close/Restore undoes the session. Continue?";
            if(MessageBox.Show(this,preview,"Confirm exact changes",MessageBoxButtons.YesNo,MessageBoxIcon.Warning,MessageBoxDefaultButton.Button2)!=DialogResult.Yes)return;
            try { session.Apply(target,options);monitoring=true;log.Text="ACTIVE. Selected priority/power/capture read back; native requests accepted, not proof of emulator latency changes. Keep the helper open. Close or Restore to undo."; }
            catch(Exception error) { log.Text="STOPPED: "+error.Message; }
            UpdateState();
        }
        bool Restore() { try { log.Text=session.Restore();monitoring=false;UpdateState();return true; } catch(Exception error) { log.Text="RESTORE FAILED: "+error.Message+" Recovery retained; retry Restore.";UpdateState();return false; } }
        public void SavePreview(string path) {
            LayoutAssertions();
            bool compact=path.Contains("compact");
            if(compact)scroll.ScrollToBottom();
            Control visible=compact?(Control)autoRestore:targets;
            Point point=scroll.PointToClient(visible.PointToScreen(Point.Empty));
            if(point.Y<0||point.Y+visible.Height>scroll.Height)throw new Exception("Scrollable controls not reachable: "+visible.AccessibleName+" "+point+" in "+scroll.ClientSize);
            targets.SelectedIndex=targets.Items.Count>0?0:-1;UpdateState();
            using(Bitmap image=new Bitmap(Width,Height)) { DrawToBitmap(image,new Rectangle(0,0,Width,Height));image.Save(path,System.Drawing.Imaging.ImageFormat.Png); }
        }
        public void UiAssertions() {
            if(!smokeMode)throw new InvalidOperationException("UI assertions require fake mode.");
            if(apply.Enabled)throw new Exception("No implicit player selection allowed.");
            targets.SelectedIndex=0;if(!apply.Enabled)throw new Exception("Explicit player selection should enable review.");
            if(profile.SelectedIndex!=0||timerResolution.SelectedIndex!=0)throw new Exception("Default review values lost.");
            targets.SelectedIndex=-1;
            if(apply.Enabled)throw new Exception("Deselection must disable start.");
            LayoutAssertions();
        }
        void LayoutAssertions() {
            Point location=PointToClient(apply.PointToScreen(Point.Empty));
            if(apply.FindForm()!=this||!apply.Visible||location.Y<0||location.Y+apply.Height>ClientSize.Height||apply.Right>apply.Parent.ClientSize.Width||log.Width<100)throw new Exception("Footer layout clipped. Form="+ClientSize+" button="+apply.Bounds+" actions="+apply.Parent.ClientSize+" log="+log.Bounds+" body="+log.Parent.ClientSize);
        }
    }
}
