using System;
using System.Drawing;
using System.Windows.Forms;

namespace VoidOptimizer {
    // Real WinForms popup/window/message-loop tests, not screenshots alone.
    // This mode never creates a Session, scans a player or registers a protocol.
    static class DropdownTests {
        static void Need(bool value,string message) { if(!value)throw new Exception("DROPDOWN TEST: "+message); }
        static void Pump() { Application.DoEvents(); }
        static ContextMenuStrip Open(StudioChoice choice) {
            choice.PerformClick();Pump();
            ContextMenuStrip menu=choice.DropDownForTests;
            Need(menu.Visible&&!menu.IsDisposed,"popup must open without disposed-object access");
            return menu;
        }
        static void Close(ContextMenuStrip menu,ToolStripDropDownCloseReason reason) {
            menu.Close(reason);Pump();
            Need(!menu.Visible&&!menu.IsDisposed,"closing must hide, not dispose, the owned menu");
        }
        public static void Run() {
            Exception failure=null;
            using(Form host=new Form { Text="VOID dropdown lifecycle tests - no tuning",ClientSize=new Size(600,240),StartPosition=FormStartPosition.CenterScreen }) {
                StudioChoice[] choices=new StudioChoice[3];
                for(int i=0;i<choices.Length;i++) {
                    choices[i]=new StudioChoice { Dock=DockStyle.Top,Height=46 };
                    choices[i].Items.AddRange(new object[]{"First","Second","Third"});host.Controls.Add(choices[i]);
                }
                host.Shown+=delegate {
                    host.BeginInvoke((Action)delegate {
                        try {
                            for(int pass=0;pass<12;pass++)foreach(StudioChoice choice in choices) {
                                ContextMenuStrip menu=Open(choice);
                                ToolStripItem selected=menu.Items[2];selected.PerformClick();
                                if(menu.Visible)menu.Close(ToolStripDropDownCloseReason.ItemClicked);
                                Pump();Need(choice.SelectedIndex==2,"item click must select exactly the requested item");
                                Need(!menu.IsDisposed,"selection must leave reusable menu alive");
                                foreach(ToolStripDropDownCloseReason reason in new ToolStripDropDownCloseReason[]{ToolStripDropDownCloseReason.Keyboard,ToolStripDropDownCloseReason.AppClicked,ToolStripDropDownCloseReason.CloseCalled}) {
                                    Need(Object.ReferenceEquals(menu,Open(choice)),"reopen must reuse the control-owned menu");
                                    Need(selected.IsDisposed,"old menu items must be disposed on the next open");
                                    Close(menu,reason);Need(choice.SelectedIndex==2,"dismissal must not change selection");
                                }
                                Open(choice);choice.PerformClick();Pump();
                                Need(!menu.Visible&&!menu.IsDisposed,"repeated source click must close without disposing");
                                Need(menu.Items.Count==3,"items/handlers must not accumulate across opens");
                            }
                            // A refresh while a popup is open must not select a stale row.
                            StudioChoice target=choices[0];ContextMenuStrip staleMenu=Open(target);
                            ToolStripItem staleItem=staleMenu.Items[0];int before=target.SelectedIndex;
                            target.Items.Clear();target.Items.Add(new object());staleItem.PerformClick();
                            if(staleMenu.Visible)staleMenu.Close();Pump();
                            Need(target.SelectedIndex==before&&target.SelectedItem==null,"stale item must not authorize replacement data");
                            target.Items.Clear();target.PerformClick();Pump();Need(!staleMenu.Visible,"empty choices must not open a popup");
                            // Disposing one owner while its menu is visible must not break others.
                            ContextMenuStrip owned=Open(choices[1]);choices[1].Dispose();Pump();
                            Need(owned.IsDisposed,"owner disposal must release its menu");
                            ContextMenuStrip survivor=Open(choices[2]);Close(survivor,ToolStripDropDownCloseReason.Keyboard);
                            // Close/dispose the window with another dropdown still open.
                            Open(choices[2]);host.Close();
                        } catch(Exception error) { failure=error;host.Close(); }
                    });
                };
                Application.Run(host);
                foreach(StudioChoice choice in choices)Need(choice.DropDownForTests.IsDisposed,"form disposal must release every owned menu");
            }
            if(failure!=null)throw new Exception("Real dropdown lifecycle regression failed",failure);
            using(StudioChoice unshown=new StudioChoice()) { }
            Console.WriteLine("PASS: 3 real dropdowns x 12 select/dismiss/reopen cycles, item/keyboard/outside/programmatic close paths, source toggle, stale/empty data, item cleanup, visible-owner disposal, window close and independent menu ownership; no helper session, registration or tuning.");
        }
    }
}
