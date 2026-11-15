using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace VoidOptimizer {
    static class Skin {
        public static readonly Color Background=Color.FromArgb(9,9,12), Surface=Color.FromArgb(17,17,22), Border=Color.FromArgb(49,49,61), Text=Color.FromArgb(236,236,243), Muted=Color.FromArgb(151,151,167);
        public static GraphicsPath Round(Rectangle box, int radius) {
            int d=Math.Min(radius*2,Math.Min(box.Width,box.Height)); GraphicsPath path=new GraphicsPath();
            path.AddArc(box.X,box.Y,d,d,180,90); path.AddArc(box.Right-d,box.Y,d,d,270,90);
            path.AddArc(box.Right-d,box.Bottom-d,d,d,0,90); path.AddArc(box.X,box.Bottom-d,d,d,90,90); path.CloseFigure(); return path;
        }
        public static void Box(Graphics g,Rectangle box,Color fill,Color border,int radius) {
            g.SmoothingMode=SmoothingMode.AntiAlias;
            using(GraphicsPath path=Round(box,radius)) using(SolidBrush brush=new SolidBrush(fill)) using(Pen pen=new Pen(border)) { g.FillPath(brush,path);g.DrawPath(pen,path); }
        }
    }
    // Native AutoScroll paints a bright system scrollbar on older/classic hosts.
    // Keep the viewport and thumb fully theme-owned; all content remains real controls.
    sealed class StudioScroll : Panel {
        Control body;
        readonly HashSet<Control> watched=new HashSet<Control>();
        int offset,extent,dragStart,dragOffset;
        bool dragging,layingOut;
        public StudioScroll() { DoubleBuffered=true;BackColor=Skin.Background;TabStop=true;SetStyle(ControlStyles.ResizeRedraw,true); }
        public void SetBody(Control value) { body=value;body.Dock=DockStyle.None;body.Anchor=AnchorStyles.Top|AnchorStyles.Left;body.AutoSize=false;Controls.Add(body);Watch(body);PerformLayout(); }
        void Watch(Control control) {
            if(!watched.Add(control))return;
            control.ControlAdded+=delegate(object sender,ControlEventArgs e) { Watch(e.Control); };
            control.Enter+=delegate {
                if(control.Controls.Count!=0||control==body)return;
                Point point=PointToClient(control.PointToScreen(Point.Empty));
                if(point.Y<0)SetOffset(offset+point.Y);
                else if(point.Y+control.Height>ClientSize.Height)SetOffset(offset+point.Y+control.Height-ClientSize.Height);
            };
            foreach(Control child in control.Controls)Watch(child);
        }
        protected override void OnLayout(LayoutEventArgs e) {
            base.OnLayout(e);if(body==null||layingOut)return;layingOut=true;
            try {
                int width=Math.Max(1,ClientSize.Width-14);
                body.Width=width;
                extent=body.GetPreferredSize(new Size(width,0)).Height;
                body.Size=new Size(width,Math.Max(1,extent));
                SetOffset(offset);
            } finally { layingOut=false; }
        }
        void SetOffset(int value) { offset=Math.Max(0,Math.Min(value,Math.Max(0,extent-ClientSize.Height)));if(body!=null)body.Location=new Point(0,-offset);Invalidate(); }
        public void ScrollToBottom() { PerformLayout();SetOffset(extent); }
        protected override void OnMouseWheel(MouseEventArgs e) { SetOffset(offset-e.Delta/120*60);base.OnMouseWheel(e); }
        protected override bool IsInputKey(Keys keyData) { return keyData==Keys.Down||keyData==Keys.Up||keyData==Keys.PageDown||keyData==Keys.PageUp||base.IsInputKey(keyData); }
        protected override void OnKeyDown(KeyEventArgs e) { if(e.KeyCode==Keys.Down)SetOffset(offset+40);else if(e.KeyCode==Keys.Up)SetOffset(offset-40);else if(e.KeyCode==Keys.PageDown)SetOffset(offset+Height-30);else if(e.KeyCode==Keys.PageUp)SetOffset(offset-Height+30);base.OnKeyDown(e); }
        protected override void OnPaint(PaintEventArgs e) {
            base.OnPaint(e);if(extent<=ClientSize.Height||ClientSize.Height<1)return;
            int height=Math.Max(26,ClientSize.Height*ClientSize.Height/extent);
            int y=(int)((long)offset*(ClientSize.Height-height)/Math.Max(1,extent-ClientSize.Height));
            Skin.Box(e.Graphics,new Rectangle(Width-7,y,4,Math.Max(2,height-1)),Skin.Muted,Skin.Muted,2);
        }
        protected override void OnMouseDown(MouseEventArgs e) { if(e.X>=Width-14&&extent>Height) { dragging=true;dragStart=e.Y;dragOffset=offset;Capture=true; }base.OnMouseDown(e); }
        protected override void OnMouseMove(MouseEventArgs e) { if(dragging)SetOffset(dragOffset+(e.Y-dragStart)*extent/Math.Max(1,Height));base.OnMouseMove(e); }
        protected override void OnMouseUp(MouseEventArgs e) { dragging=false;Capture=false;base.OnMouseUp(e); }
    }
    sealed class CardPanel : Panel {
        public CardPanel() { DoubleBuffered=true; BackColor=Skin.Background; Padding=new Padding(16); AutoSize=true; AutoSizeMode=AutoSizeMode.GrowAndShrink; Margin=new Padding(0,0,0,12); Dock=DockStyle.Top; }
        protected override void OnPaintBackground(PaintEventArgs e) { e.Graphics.Clear(BackColor); if(Width>2&&Height>2)Skin.Box(e.Graphics,new Rectangle(0,0,Width-1,Height-1),Skin.Surface,Skin.Border,14); }
    }
    class StudioButton : Button {
        public bool Primary;
        bool hot;
        public StudioButton() {
            SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true);
            FlatStyle=FlatStyle.Flat; FlatAppearance.BorderSize=0; Height=38; Width=138; Cursor=Cursors.Hand; BackColor=Skin.Surface; ForeColor=Skin.Text;
        }
        protected override void OnMouseEnter(EventArgs e) { hot=true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hot=false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
        protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
        protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }
        protected virtual void DrawContent(Graphics g,Color text) { TextRenderer.DrawText(g,Text,Font,new Rectangle(12,0,Width-24,Height),text,TextFormatFlags.HorizontalCenter|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis|TextFormatFlags.SingleLine|TextFormatFlags.NoPrefix); }
        protected override void OnPaint(PaintEventArgs e) {
            if(Width<3||Height<3)return;
            Color fill=!Enabled?Color.FromArgb(24,24,30):Primary?(hot?Color.White:Color.FromArgb(230,230,239)):hot?Color.FromArgb(36,36,46):Skin.Surface;
            Color ink=!Enabled?Color.FromArgb(110,110,125):Primary?Skin.Background:Skin.Text;
            Skin.Box(e.Graphics,new Rectangle(0,0,Width-1,Height-1),fill,Focused&&Enabled?Color.White:Primary&&Enabled?fill:Skin.Border,10);
            DrawContent(e.Graphics,ink);
        }
    }
    sealed class StudioChoice : StudioButton {
        public readonly List<object> Items=new List<object>();
        int index=-1;
        public event EventHandler SelectedIndexChanged;
        public int SelectedIndex { get { return index; } set { index=value>=0&&value<Items.Count?value:-1; Invalidate(); if(SelectedIndexChanged!=null)SelectedIndexChanged(this,EventArgs.Empty); } }
        public object SelectedItem { get { return index>=0&&index<Items.Count?Items[index]:null; } }
        public string Placeholder="Choose an option";
        protected override void DrawContent(Graphics g,Color text) {
            string label=SelectedItem==null?Placeholder:SelectedItem.ToString();
            TextRenderer.DrawText(g,label,Font,new Rectangle(12,0,Width-44,Height),SelectedItem==null?Skin.Muted:text,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis|TextFormatFlags.SingleLine|TextFormatFlags.NoPrefix);
            using(Pen pen=new Pen(text,1.4f))g.DrawLines(pen,new Point[]{new Point(Width-25,Height/2-2),new Point(Width-20,Height/2+3),new Point(Width-15,Height/2-2)});
        }
        protected override void OnClick(EventArgs e) {
            base.OnClick(e); if(Items.Count==0)return;
            ContextMenuStrip menu=new ContextMenuStrip(); menu.BackColor=Skin.Surface; menu.ForeColor=Skin.Text;menu.Font=Font;
            menu.Renderer=new ToolStripProfessionalRenderer(new DarkMenuColors()); menu.ShowImageMargin=false;
            for(int i=0;i<Items.Count;i++) { int choice=i; ToolStripMenuItem item=new ToolStripMenuItem(Items[i].ToString()); item.Checked=i==index; item.Click+=delegate { SelectedIndex=choice; }; menu.Items.Add(item); }
            menu.Closed+=delegate { menu.Dispose(); }; menu.Show(this,new Point(0,Height));
        }
    }
    sealed class DarkMenuColors : ProfessionalColorTable {
        public override Color ToolStripDropDownBackground { get { return Skin.Surface; } }
        public override Color MenuBorder { get { return Skin.Border; } }
        public override Color MenuItemSelected { get { return Color.FromArgb(45,45,57); } }
        public override Color MenuItemBorder { get { return Skin.Muted; } }
        public override Color CheckBackground { get { return Skin.Border; } }
        public override Color CheckSelectedBackground { get { return Skin.Border; } }
        public override Color ImageMarginGradientBegin { get { return Skin.Surface; } }
        public override Color ImageMarginGradientMiddle { get { return Skin.Surface; } }
        public override Color ImageMarginGradientEnd { get { return Skin.Surface; } }
    }
    sealed class StudioToggle : CheckBox {
        public string Detail="";
        public StudioToggle() { BackColor=Color.Transparent;ForeColor=Skin.Text;AutoSize=false; Height=58; Dock=DockStyle.Top; Cursor=Cursors.Hand; SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer|ControlStyles.ResizeRedraw,true); }
        protected override void OnCheckedChanged(EventArgs e) { Invalidate();base.OnCheckedChanged(e); }
        protected override void OnGotFocus(EventArgs e) { Invalidate();base.OnGotFocus(e); }
        protected override void OnLostFocus(EventArgs e) { Invalidate();base.OnLostFocus(e); }
        protected override void OnPaint(PaintEventArgs e) {
            Color ink=Enabled?Skin.Text:Skin.Muted;
            TextRenderer.DrawText(e.Graphics,Text,Font,new Rectangle(0,4,Math.Max(10,Width-60),24),ink,TextFormatFlags.Left|TextFormatFlags.VerticalCenter|TextFormatFlags.EndEllipsis);
            using(Font small=new Font(Font.FontFamily,8.5f))TextRenderer.DrawText(e.Graphics,Detail,small,new Rectangle(0,29,Math.Max(10,Width-60),25),Skin.Muted,TextFormatFlags.Left|TextFormatFlags.WordBreak|TextFormatFlags.EndEllipsis|TextFormatFlags.NoPrefix);
            Rectangle track=new Rectangle(Width-40,19,36,20);
            Skin.Box(e.Graphics,track,Checked?Color.FromArgb(228,228,239):Color.FromArgb(42,42,53),Focused?Color.White:Skin.Border,10);
            e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;
            using(SolidBrush brush=new SolidBrush(Checked?Skin.Background:Skin.Muted))e.Graphics.FillEllipse(brush,track.X+(Checked?19:3),track.Y+3,14,14);
        }
    }
}
