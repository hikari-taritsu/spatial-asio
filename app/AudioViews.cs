using System.Globalization;
using System.Windows;
using System.Windows.Media;
namespace SpatialASIO;
public abstract class AudioView : FrameworkElement {
    public IReadOnlyList<Channel> Channels {get;set;}=[];
    protected static SolidColorBrush Brush(string color)=>new((Color)ColorConverter.ConvertFromString(color));
    protected static readonly Brush Ink=Brush("#E6E8EB"),Muted=Brush("#A5ABB2"),Accent=Brush("#A7BACB"),Line=Brush("#44484D"),Inset=Brush("#202225"),Panel=Brush("#2D3034"),Red=Brush("#D28888");
    protected void Text(DrawingContext dc,string text,double x,double y,double size=11,Brush? brush=null,bool center=false){
        var f=new FormattedText(text,CultureInfo.GetCultureInfo("zh-CN"),FlowDirection.LeftToRight,new Typeface("Microsoft YaHei UI"),size,brush??Muted,VisualTreeHelper.GetDpi(this).PixelsPerDip);
        dc.DrawText(f,new Point(center?x-f.Width/2:x,y));
    }
    public void Refresh()=>InvalidateVisual();
}
public sealed class StageView : AudioView {
    protected override void OnRender(DrawingContext dc){
        base.OnRender(dc);var w=ActualWidth;var h=ActualHeight;if(w<1||h<1)return;
        dc.DrawRoundedRectangle(Inset,new Pen(Line,1),new Rect(0,0,w,h),4,4);
        Text(dc,"扬声器位置",20,15,13,Ink);Text(dc,"实线 地面  /  虚线 高度",w-194,18,10);
        var cx=w/2;var cy=h/2+7;var rx=Math.Min(w*.31,h*.73);var ry=h*.32;
        dc.DrawEllipse(null,new Pen(Line,1),new Point(cx,cy),rx,ry);
        var dash=new Pen(Line,1){DashStyle=DashStyles.Dash};dc.DrawEllipse(null,dash,new Point(cx,cy),rx*.65,ry*.68);
        dc.DrawLine(new Pen(Line,.6),new Point(cx-rx-20,cy),new Point(cx+rx+20,cy));dc.DrawLine(new Pen(Line,.6),new Point(cx,cy-ry-17),new Point(cx,cy+ry+17));
        Text(dc,"前方",cx,43,10,Muted,true);
        dc.DrawEllipse(Panel,new Pen(Accent,1.5),new Point(cx,cy),17,17);Text(dc,"◦",cx,cy-13,20,Ink,true);
        foreach(var c in Channels){
            var (u,v)=c.Name switch {
                "FL"=>(-.72,-.84),"FR"=>(.72,-.84),"FC"=>(0.0,-1.0),"LFE"=>(1.05,-.95),"BL"=>(-.72,.87),"BR"=>(.72,.87),"SL"=>(-1.0,0.0),"SR"=>(1.0,0.0),"FWL"=>(-1.0,-.55),"FWR"=>(1.0,-.55),
                "TFL"=>(-.45,-.55),"TFR"=>(.45,-.55),"TML"=>(-.6,0.0),"TMR"=>(.6,0.0),"TBL"=>(-.45,.56),"TBR"=>(.45,.56),_=>(0.0,0.0)};
            var x=cx+u*rx;var y=cy+v*ry;var active=c.Peak>.0001;var color=c.Peak>=1?Red:active?Accent:Line;
            var pen=new Pen(color,1.2);if(c.Name.StartsWith('T'))pen.DashStyle=DashStyles.Dash;
            dc.DrawRoundedRectangle(active?Panel:Inset,pen,new Rect(x-23,y-15,46,30),3,3);Text(dc,c.Name,x,y-10,11,active?Ink:Muted,true);
        }
        Text(dc,"规范布局示意 · 声道位置固定，物理输出可在路由中配置",w/2,h-25,10,Muted,true);
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
