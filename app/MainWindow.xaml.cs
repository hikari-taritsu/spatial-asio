using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
namespace SpatialASIO;
public partial class MainWindow : Window {
    readonly string root=AppContext.BaseDirectory;
    readonly string logs=Path.Combine(Configuration.DataRoot,"Logs");
    Configuration config=new();Process? engine;Task? monitoring;bool initialized,busy,muted,closing,allowClose;
    readonly List<string> recent=[];string? logFile;
    string EnginePath=>Path.Combine(root,"SpatialASIO.Engine.exe");
    string PythonPath=>Path.Combine(root,"Tools","NativeSpatial","python","python.exe");
    public MainWindow(){
        InitializeComponent();
        string? loadError=null;
        if(!App.Preview)try{config=Configuration.Load();if(config.Channels.Count<2||config.Channels.Count>16)throw new InvalidDataException("声道配置损坏");}catch(Exception e){config=new();loadError=e.Message;}
        LayoutBox.ItemsSource=Configuration.Layouts;LayoutBox.SelectedItem=config.Layout;
        GainSlider.Value=config.GainDb;RateBox.Text=config.SampleRate.ToString(CultureInfo.InvariantCulture);BufferBox.Text=config.BufferFrames.ToString();PrebufferBox.Text=config.PrebufferMs.ToString();
        BindChannels();initialized=true;UpdateGainLabel();
        if(App.Preview){DriverBox.ItemsSource=new[]{new Driver("preview","选择已安装的 ASIO 驱动")};DriverBox.SelectedIndex=0;Footer.Text="界面预览 · 没有打开音频设备，全部电平为零";}
        else Loaded+=async(_,_)=>{Directory.CreateDirectory(logs);if(loadError!=null)Log("配置读取失败，使用默认值："+loadError);await Guard(LoadDrivers);};
        Closing+=OnClosing;
    }
    void BindChannels(){Routes.ItemsSource=config.Channels;Stage.Channels=config.Channels;Meters.Channels=config.Channels;Stage.Refresh();Meters.Refresh();RenderState.Text="OAR · "+config.Layout;LayoutCount.Text=config.Channels.Count+" CHANNELS";}
    void LayoutChanged(object sender,SelectionChangedEventArgs e){if(!initialized||engine!=null)return;if(LayoutBox.SelectedItem is string l){config.Layout=l;config.Channels=Configuration.CreateChannels(l);BindChannels();}}
    void ShowMonitor(object s,RoutedEventArgs e){Stage.Visibility=Visibility.Visible;RoutingPage.Visibility=LogView.Visibility=Visibility.Collapsed;}
    void ShowRouting(object s,RoutedEventArgs e){RoutingPage.Visibility=Visibility.Visible;Stage.Visibility=LogView.Visibility=Visibility.Collapsed;}
    void ShowDiagnostics(object s,RoutedEventArgs e){LogView.Visibility=Visibility.Visible;Stage.Visibility=RoutingPage.Visibility=Visibility.Collapsed;}
    void Log(string text){
        var line=$"{DateTime.Now:HH:mm:ss}  {text}";recent.Add(line);if(recent.Count>160)recent.RemoveRange(0,recent.Count-160);
        LogView.Text=string.Join(Environment.NewLine,recent);LogView.ScrollToEnd();Footer.Text=text;
        try{Directory.CreateDirectory(logs);logFile??=Path.Combine(logs,$"session-{DateTime.Now:yyyyMMdd-HHmmss}.log");File.AppendAllText(logFile,line+Environment.NewLine,Encoding.UTF8);}catch(IOException){}
    }
    async Task<(int Code,string Output,string Error)> RunSmall(string file,IEnumerable<string> args,int timeout=15000){
        var start=new ProcessStartInfo(file){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8,WorkingDirectory=root};
        foreach(var arg in args)start.ArgumentList.Add(arg);
        using var p=Process.Start(start)??throw new IOException("无法启动辅助进程");var output=p.StandardOutput.ReadToEndAsync();var error=p.StandardError.ReadToEndAsync();
        try{using var timer=new CancellationTokenSource(timeout);await p.WaitForExitAsync(timer.Token);}catch(OperationCanceledException){try{p.Kill(true);}catch{}throw new IOException("辅助进程超时，请检查驱动是否被其他程序占用。");}
        return(p.ExitCode,await output,await error);
    }
    void SetBusy(bool value){busy=value;ConfigurationControls.IsEnabled=!value&&engine==null;StartButton.IsEnabled=!value;}
    async Task Guard(Func<Task> task){if(busy)return;SetBusy(true);try{await task();}catch(Exception e){Log(e.Message);}finally{SetBusy(false);}}
    async Task LoadDrivers(){
        var r=await RunSmall(EnginePath,["drivers"]);if(r.Code!=0)throw new IOException(r.Error);
        var drivers=r.Output.Split('\n',StringSplitOptions.RemoveEmptyEntries).Select(s=>JsonSerializer.Deserialize<Driver>(s)!).ToArray();DriverBox.ItemsSource=drivers;
        DriverBox.SelectedItem=drivers.FirstOrDefault(d=>d.id==config.DriverId)??drivers.FirstOrDefault(d=>d.name.Contains("Dante Via",StringComparison.OrdinalIgnoreCase))??drivers.FirstOrDefault();
        Log(drivers.Length==0?"未找到 64 位 ASIO 驱动，请安装音频设备厂商提供的驱动。":$"找到 {drivers.Length} 个 ASIO 驱动；尚未打开硬件。");
    }
    async void RefreshDrivers(object s,RoutedEventArgs e)=>await Guard(LoadDrivers);
    Driver SelectedDriver()=>DriverBox.SelectedItem as Driver??throw new InvalidDataException("请先选择 ASIO 驱动。");
    async void ProbeDriver(object s,RoutedEventArgs e)=>await Guard(async()=>{
        var r=await RunSmall(EnginePath,["probe",SelectedDriver().id]);if(r.Code!=0)throw new IOException(r.Error);
        using var j=JsonDocument.Parse(r.Output);var v=j.RootElement;
        DriverInfo.Text=$"{v.GetProperty("outputChannels")} 个输出 · 当前 {v.GetProperty("sampleRate")} Hz\n缓冲 {v.GetProperty("minimumBuffer")}–{v.GetProperty("maximumBuffer")} 帧，推荐 {v.GetProperty("preferredBuffer")} 帧";
        Log("ASIO 检测完成。打开驱动面板或修改设置时，请先停止其他占用它的音频软件。");
    });
    async void OpenDriverPanel(object s,RoutedEventArgs e)=>await Guard(async()=>{var r=await RunSmall(EnginePath,["panel",SelectedDriver().id],600000);if(r.Code!=0)throw new IOException(r.Error);});
    void ReadConfiguration(){
        if(!Routes.CommitEdit(DataGridEditingUnit.Cell,true)||!Routes.CommitEdit(DataGridEditingUnit.Row,true))throw new InvalidDataException("请修正声道表中的数值。");
        config.DriverId=SelectedDriver().id;config.GainDb=GainSlider.Value;
        config.SampleRate=double.Parse(RateBox.Text,CultureInfo.InvariantCulture);config.BufferFrames=int.Parse(BufferBox.Text,CultureInfo.InvariantCulture);config.PrebufferMs=int.Parse(PrebufferBox.Text,CultureInfo.InvariantCulture);config.Validate();
    }
    void SaveConfiguration(object s,RoutedEventArgs e){try{ReadConfiguration();config.Save();Meters.Refresh();Log("配置已保存。");}catch(Exception ex){Log(ex.Message);}}
    async Task<bool> Preflight(){
        var r=await RunSmall(PythonPath,["-u",Path.Combine(root,"Tools","NativeSpatial","native_host.py"),"--check"],20000);
        if(r.Code!=0){Log(r.Error.Trim());TopStatus.Text="捕获检查未通过";return false;}
        using var j=JsonDocument.Parse(r.Output);Log($"捕获版本已验证 · 音频宿主 PID {j.RootElement.GetProperty("pid")}。仍需游戏主动发送空间对象。");return true;
    }
    async void CheckCompatibility(object s,RoutedEventArgs e)=>await Guard(async()=>{await Preflight();});
    async void ToggleCapture(object s,RoutedEventArgs e){if(engine!=null){await Guard(Stop);return;}await Guard(async()=>{
        ReadConfiguration();config.Save();if(!await Preflight())return;
        var profiles=Path.Combine(Configuration.DataRoot,"Profiles");Directory.CreateDirectory(profiles);var path=Path.Combine(profiles,"active.ini");File.WriteAllText(path,config.ToIni(),Encoding.Unicode);
        var start=new ProcessStartInfo(EnginePath){WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true,RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true,StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8};
        foreach(var a in new[]{"run",path,GainValue().ToString("R",CultureInfo.InvariantCulture),config.PrebufferMs.ToString()})start.ArgumentList.Add(a);
        engine=Process.Start(start)??throw new IOException("无法启动捕获引擎");engine.StandardInput.AutoFlush=true;
        Routes.IsReadOnly=true;StartButton.Content="停止捕获";TopStatus.Text="正在连接";CaptureState.Text="启动捕获器";Log("引擎已启动，等待实际对象数据。");
        monitoring=Monitor(engine);
    });}
    async Task Monitor(Process p){
        try{
            async Task Pump(StreamReader reader,bool telemetry){while(await reader.ReadLineAsync() is string line){if(telemetry){try{using var json=JsonDocument.Parse(line);HandleStatus(json.RootElement);}catch(JsonException){Log(line);}}else Log(line);}}
            await Task.WhenAll(Pump(p.StandardOutput,true),Pump(p.StandardError,false),p.WaitForExitAsync());Log(p.ExitCode==0?"捕获已停止，挂钩与输出已清理。":$"引擎退出，代码 {p.ExitCode}。请查看上方错误。");
        }catch(Exception e){Log("引擎监视失败："+e.Message);}
        finally{
            if(ReferenceEquals(engine,p))engine=null;p.Dispose();Routes.IsReadOnly=false;ConfigurationControls.IsEnabled=!busy;StartButton.Content="开始捕获";TopStatus.Text="已停止";CaptureState.Text="等待捕获";OutputState.Text="驱动已关闭";
            foreach(var c in config.Channels){c.Peak=c.Rms=0;}Stage.Refresh();Meters.Refresh();
        }
    }
    void HandleStatus(JsonElement j){
        if(j.GetProperty("event").GetString()!="status")return;
        bool receiving=j.GetProperty("receiving").GetBoolean(),output=j.GetProperty("asioRunning").GetBoolean();
        CaptureState.Text=receiving?"正在接收对象":"等待对象音频";OutputState.Text=output?$"{j.GetProperty("bufferFrames")} 帧 · 输出已启动":"尚未打开驱动";
        bool audible=false;var peaks=j.GetProperty("peak");var rms=j.GetProperty("rms");
        for(int i=0;i<Math.Min(config.Channels.Count,peaks.GetArrayLength());i++){config.Channels[i].Peak=peaks[i].GetDouble();config.Channels[i].Rms=rms[i].GetDouble();audible|=config.Channels[i].Peak>.000001;}
        TopStatus.Text=receiving&&output?(audible?"信号正在输出":"收到对象 · 当前静音"):"运行中 · 等待空间音频";
        ObjectInfo.Text=$"{j.GetProperty("objects")} 个对象 · {j.GetProperty("packets")} 个数据包";DropInfo.Text=$"欠载 {j.GetProperty("underruns")} 帧 · 丢包 {j.GetProperty("gaps")}";
        Stage.Refresh();Meters.Refresh();
    }
    async Task Stop(){
        var p=engine;if(p==null)return;StartButton.IsEnabled=false;Log("正在停止捕获并恢复音频宿主……");
        try{await p.StandardInput.WriteLineAsync("stop");using var t=new CancellationTokenSource(20000);await p.WaitForExitAsync(t.Token);if(monitoring!=null)await monitoring;}catch(InvalidOperationException){}catch(OperationCanceledException){Log("清理仍在进行，请查看捕获日志；为保留宿主清理机会，没有强行终止。");}
    }
    double GainValue()=>muted?0:Math.Pow(10,GainSlider.Value/20);
    void UpdateGainLabel(){if(GainLabel==null)return;GainLabel.Text=muted?"静音":$"{GainSlider.Value:F1} dB";}
    async Task SendGain(){try{if(engine is {HasExited:false})await engine.StandardInput.WriteLineAsync("gain "+GainValue().ToString("R",CultureInfo.InvariantCulture));}catch(Exception e)when(e is IOException or InvalidOperationException){Log("音量更新失败："+e.Message);}}
    async void GainChanged(object s,RoutedPropertyChangedEventArgs<double> e){UpdateGainLabel();if(initialized)await SendGain();}
    async void ToggleMute(object s,RoutedEventArgs e){muted=!muted;MuteButton.Content=muted?"取消静音":"静音";UpdateGainLabel();await SendGain();}
    void OpenLogs(object s,RoutedEventArgs e){Directory.CreateDirectory(logs);Process.Start(new ProcessStartInfo(logs){UseShellExecute=true});}
    void OpenSoundSettings(object s,RoutedEventArgs e)=>Process.Start(new ProcessStartInfo("ms-settings:sound"){UseShellExecute=true});
    async void OnClosing(object? s,CancelEventArgs e){if(allowClose||engine==null)return;e.Cancel=true;if(closing)return;closing=true;await Stop();closing=false;if(engine==null){allowClose=true;Close();}}
}
