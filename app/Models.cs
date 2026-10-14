using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;
namespace SpatialASIO;
public sealed record Driver(string id,string name) { public override string ToString()=>name; }
public sealed class Channel : INotifyPropertyChanged {
    public string Name {get;set;}="FL";
    public int Physical {get;set;}
    public double Trim {get;set;}
    double peak,rms;
    public double Peak {get=>peak;set{peak=value;PropertyChanged?.Invoke(this,new(nameof(Peak)));}}
    public double Rms {get=>rms;set{rms=value;PropertyChanged?.Invoke(this,new(nameof(Rms)));}}
    public event PropertyChangedEventHandler? PropertyChanged;
}
public sealed class Configuration {
    public string DriverId {get;set;}="";
    public string Layout {get;set;}="9.1.6";
    public double GainDb {get;set;}=-12;
    public int BufferFrames {get;set;}
    public int PrebufferMs {get;set;}=60;
    public double SampleRate {get;set;}=48000;
    public List<Channel> Channels {get;set;}=CreateChannels("9.1.6");
    public static readonly string[] Layouts=["9.1.6","7.1.4","7.1.2","5.1.4","5.1.2","5.1","2.0"];
    public static List<Channel> CreateChannels(string layout) {
        string names=layout switch {
            "9.1.6"=>"FL FR FC LFE BL BR SL SR FWL FWR TFL TFR TML TMR TBL TBR",
            "7.1.4"=>"FL FR FC LFE BL BR SL SR TFL TFR TBL TBR",
            "7.1.2"=>"FL FR FC LFE BL BR SL SR TML TMR",
            "5.1.4"=>"FL FR FC LFE SL SR TFL TFR TBL TBR",
            "5.1.2"=>"FL FR FC LFE SL SR TML TMR",
            "5.1"=>"FL FR FC LFE SL SR", "2.0"=>"FL FR",_=>throw new InvalidDataException("未知扬声器布局")};
        return names.Split(' ').Select((n,i)=>new Channel{Name=n,Physical=i+1}).ToList();
    }
    public void Validate() {
        if(!DriverId.StartsWith("asio:",StringComparison.OrdinalIgnoreCase) || !Guid.TryParse(DriverId[5..],out _))throw new InvalidDataException("请选择已安装的 64 位 ASIO 驱动。");
        if(!Layouts.Contains(Layout)||!Channels.Select(c=>c.Name).SequenceEqual(CreateChannels(Layout).Select(c=>c.Name)))throw new InvalidDataException("布局与声道列表不一致。");
        if(!double.IsFinite(GainDb)||GainDb < -60 || GainDb>0 || BufferFrames<0 || BufferFrames>65536 || PrebufferMs<20 || PrebufferMs>500 || !double.IsFinite(SampleRate)||SampleRate<8000||SampleRate>384000)throw new InvalidDataException("音量、采样率或缓冲参数超出范围。");
        if(Channels.Any(c=>c.Physical<1||c.Physical>256||!double.IsFinite(c.Trim)||c.Trim < -60||c.Trim>12)||Channels.Select(c=>c.Physical).Distinct().Count()!=Channels.Count)throw new InvalidDataException("ASIO 通道必须互不重复（1–256），微调范围为 −60 至 +12 dB。");
    }
    public string ToIni() {
        Validate();var s=new StringBuilder();
        s.AppendLine($"[profile]\nlayout={Layout}\n[layout]\nspeakers={string.Join(',',Channels.Select(c=>c.Name))}\n[asio]\ndriver={DriverId}\nbuffer_frames={BufferFrames}\nsample_rate={SampleRate.ToString(CultureInfo.InvariantCulture)}");
        foreach(var c in Channels)s.AppendLine($"[speaker.{c.Name}]\nchannel={c.Physical}\ntrim_db={c.Trim.ToString(CultureInfo.InvariantCulture)}");
        return s.ToString();
    }
    public static string DataRoot=>Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"SpatialASIO");
    public static string SettingsFile=>Path.Combine(DataRoot,"settings.json");
    public static Configuration Load()=>File.Exists(SettingsFile)?JsonSerializer.Deserialize<Configuration>(File.ReadAllText(SettingsFile))??new():new();
    public void Save(){Validate();Directory.CreateDirectory(DataRoot);var temp=SettingsFile+".tmp";File.WriteAllText(temp,JsonSerializer.Serialize(this,new JsonSerializerOptions{WriteIndented=true}));File.Move(temp,SettingsFile,true);}
}
internal static class AppChecks {
    public static int Run(){try{
        var c=new Configuration{DriverId="asio:{00000000-0000-0000-0000-000000000001}"};c.Validate();
        if(!c.ToIni().Contains("TMR")||c.Channels.Count!=16)throw new Exception("9.1.6 profile");
        foreach(var l in Configuration.Layouts){c.Layout=l;c.Channels=Configuration.CreateChannels(l);c.Validate();}
        c.Channels[1].Physical=c.Channels[0].Physical;bool failed=false;try{c.Validate();}catch(InvalidDataException){failed=true;}if(!failed)throw new Exception("Duplicate routing accepted");
        File.WriteAllText(Path.Combine(AppContext.BaseDirectory,"app-checks.txt"),"PASS: presets, configuration serialization, ASIO identity, duplicate-route rejection");return 0;
    }catch(Exception e){File.WriteAllText(Path.Combine(AppContext.BaseDirectory,"app-checks.txt"),e.ToString());return 1;}}
}
