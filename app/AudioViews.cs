using System.Globalization;
using System.Windows;
using System.Windows.Media;
namespace SpatialASIO;
public abstract class AudioView : FrameworkElement {
    public IReadOnlyList<Channel> Channels {get;set;}=[];
    protected static SolidColorBrush Brush(string color)=>new((Color)ColorConverter.ConvertFromString(color));
    protected static readonly Brush Ink=Brush("#E6E8EB"),Muted=Brush("#A5ABB2"),Accent=Brush("#A7BACB"),Line=Brush("#44484D"),Inset=Brush("#202225"),Panel=Brush("#2D3034"),Red=Brush("#D28888");
    protected void Text(DrawingContext dc,string text,double x,double y,double size=11,Brush? brush=null,bool center=false){
        var face=new Typeface((FontFamily)Application.Current.FindResource("InterfaceFont"),FontStyles.Normal,FontWeights.Normal,FontStretches.Normal);
        var f=new FormattedText(text,CultureInfo.GetCultureInfo("zh-CN"),FlowDirection.LeftToRight,face,size,brush??Muted,VisualTreeHelper.GetDpi(this).PixelsPerDip);
        dc.DrawText(f,new Point(center?x-f.Width/2:x,y));
    }
    public void Refresh()=>InvalidateVisual();
}
public sealed class StageView : AudioView {
    internal static double Azimuth(string name,bool rearSurround)=>name switch {
        "FL"=>-30,"FR"=>30,"FC"=>0,"FWL"=>-60,"FWR"=>60,"SL"=>rearSurround?-110:-90,"SR"=>rearSurround?110:90,
        "BL"=>-150,"BR"=>150,"TFL"=>-45,"TFR"=>45,"TML"=>-90,"TMR"=>90,"TBL"=>-135,"TBR"=>135,_=>0};
    internal static (Point Center,double Radius) Geometry(double w,double h)=>
        (new Point(w/2,(60+h-42)/2),Math.Max(1,Math.Min((w-180)/2,(h-100)/2)));
    protected override void OnRender(DrawingContext dc){
        base.OnRender(dc);var w=ActualWidth;var h=ActualHeight;if(w<1||h<1)return;
        dc.DrawRoundedRectangle(Inset,new Pen(Line,1),new Rect(0,0,w,h),4,4);
        Text(dc,"扬声器位置",20,15,14,Ink);Text(dc,"实线 地面  /  虚线 高度",w-203,18,11);
        var (center,r)=Geometry(w,h);var cx=center.X;var cy=center.Y;
        dc.DrawEllipse(null,new Pen(Line,1),center,r,r);
        var dash=new Pen(Line,1){DashStyle=DashStyles.Dash};
        if(Channels.Any(c=>c.Name.StartsWith('T')))dc.DrawEllipse(null,dash,center,r*.57,r*.57);
        dc.DrawLine(new Pen(Line,.6),new Point(cx-r-18,cy),new Point(cx+r+18,cy));dc.DrawLine(new Pen(Line,.6),new Point(cx,cy-r-16),new Point(cx,cy+r+16));
        Text(dc,"前方",cx,cy-r-39,11,Muted,true);
        dc.DrawEllipse(Panel,new Pen(Accent,1.5),center,15,15);
        dc.DrawLine(new Pen(Accent,1.5),new Point(cx,cy+5),new Point(cx,cy-7));
        dc.DrawLine(new Pen(Accent,1.5),new Point(cx-4,cy-3),new Point(cx,cy-7));
        dc.DrawLine(new Pen(Accent,1.5),new Point(cx+4,cy-3),new Point(cx,cy-7));
        var rearSurround=!Channels.Any(c=>c.Name=="BL");
        foreach(var c in Channels){
            var angle=Azimuth(c.Name,rearSurround)*Math.PI/180;var radius=c.Name.StartsWith('T')?r*.57:r;
            var x=c.Name=="LFE"?cx+r+53:cx+Math.Sin(angle)*radius;
            var y=c.Name=="LFE"?cy-r*.72:cy-Math.Cos(angle)*radius;
            var active=c.Peak>.0001;var color=c.Peak>=1?Red:active?Accent:Line;
            var pen=new Pen(color,1.2);if(c.Name.StartsWith('T'))pen.DashStyle=DashStyles.Dash;
            var labelWidth=c.Name.Length*7+16;
            dc.DrawRoundedRectangle(active?Panel:Inset,pen,new Rect(x-labelWidth/2.0,y-13,labelWidth,26),3,3);Text(dc,c.Name,x,y-10,12,active?Ink:Muted,true);
        }
        Text(dc,"圆形俯视示意 · 高度层内缩显示 · 输出可在声道路由中配置",w/2,h-28,11,Muted,true);
    }
}
public sealed class MeterView : AudioView {
    protected override void OnRender(DrawingContext dc){
        base.OnRender(dc);var w=ActualWidth;var h=ActualHeight;if(w<1||h<45||Channels.Count==0)return;
        const double left=29;var cell=(w-left)/Channels.Count;var bar=Math.Clamp(cell*.37,7,19);var top=4.0;var bottom=h-42;var height=bottom-top;
        foreach(var db in new[]{0,-12,-24,-36,-48,-60}){
            var y=top+(-db/60.0)*height;Text(dc,db.ToString(),0,y-7,9);dc.DrawLine(new Pen(Line,.5),new Point(left,y),new Point(w,y));
        }
        for(int i=0;i<Channels.Count;i++){
            var c=Channels[i];var x=left+cell*(i+.5);var rms=Math.Clamp((20*Math.Log10(Math.Max(c.Rms,1e-9))+60)/60,0,1);var peak=Math.Clamp((20*Math.Log10(Math.Max(c.Peak,1e-9))+60)/60,0,1);
            dc.DrawRectangle(Panel,null,new Rect(x-bar/2,top,bar,height));
            if(rms>0)dc.DrawRectangle(c.Peak>=1?Red:Accent,null,new Rect(x-bar/2,bottom-rms*height,bar,rms*height));
            if(peak>0)dc.DrawLine(new Pen(c.Peak>=1?Red:Ink,2),new Point(x-bar/2,bottom-peak*height),new Point(x+bar/2,bottom-peak*height));
            Text(dc,c.Name,x,bottom+6,10,c.Peak>=1?Red:Ink,true);Text(dc,c.Peak>=1?"过载":$"{c.Physical:00}",x,bottom+23,9,c.Peak>=1?Red:Muted,true);
        }
    }
}
