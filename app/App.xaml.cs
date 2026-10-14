using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
namespace SpatialASIO;
public partial class App : Application {
    public static bool Preview { get; private set; }
    private Mutex? singleton;
    protected override void OnStartup(StartupEventArgs e) {
        base.OnStartup(e);
        if(e.Args.Length>0 && e.Args[0]=="--self-test") { Shutdown(AppChecks.Run());return; }
        Preview=e.Args.Length>1 && e.Args[0]=="--render";
        if(!Preview) {
            singleton=new Mutex(true,"Local\\SpatialASIO.Desktop",out var created);
            if(!created){MessageBox.Show("Spatial ASIO 已经打开。","Spatial ASIO");Shutdown();return;}
        }
        var window=new MainWindow();MainWindow=window;
        if(Preview) {
            window.Width=1280;window.Height=840;
            var content=(FrameworkElement)window.Content;
            content.Measure(new Size(1280,840));content.Arrange(new Rect(0,0,1280,840));content.UpdateLayout();
            var bitmap=new RenderTargetBitmap(1280,840,96,96,PixelFormats.Pbgra32);bitmap.Render(content);
            var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using(var file=File.Create(e.Args[1]))encoder.Save(file);
            Shutdown();
        } else window.Show();
    }
    protected override void OnExit(ExitEventArgs e){singleton?.Dispose();base.OnExit(e);}
}
