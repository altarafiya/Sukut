using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using Sukut;
using Xunit;

public sealed class ContractTests
{
 [Theory]
 [InlineData(6,1.9952623)] [InlineData(-20,.1)] [InlineData(-80,0)] [InlineData(-100,0)]
 public void Gains(float db, double expected) => Assert.InRange(Math.Abs(AudioModePreset.DbToLinear(db)-expected),0,.00001);

 [Theory] [InlineData(0,0,0,0,false,false)] [InlineData(1,-80,0,0,true,false)] [InlineData(2,-100,0,0,true,false)] [InlineData(3,-80,-80,0,true,true)] [InlineData(42,0,0,0,false,false)]
 public void Presets(int mode,float music,float background,float voice,bool muteMusic,bool muteBackground)
 {
  var p=AudioModePreset.ForMode(mode);
  Assert.Equal(mode is 1 or 2 or 3 ? mode : 0,p.Mode);
  Assert.Equal(music,p.MusicDb); Assert.Equal(background,p.BackgroundDb); Assert.Equal(voice,p.VoiceDb);
  Assert.Equal(muteMusic,p.MuteMusic); Assert.Equal(muteBackground,p.MuteBackground);
 }
 [Theory] [InlineData("Headset", "\uE95B")] [InlineData("HyperX", "\uE7F6")] [InlineData("AirPods", "\uE7F6")] [InlineData("HDMI", "\uE7F4")] [InlineData("Speakers", "\uE767")]
 public void Glyphs(string name,string expected)
 {
  var e=new Endpoint{Name=name}; Assert.Equal(expected,e.Glyph); Assert.Same(e.Glyph,e.Glyph);
  e.Name="Unknown"; Assert.Equal("\uE767",e.Glyph);
 }
 [Theory] [InlineData(-100,-20)] [InlineData(-24,-20)] [InlineData(-20,-20)] [InlineData(0,0)] [InlineData(10,10)] [InlineData(100,10)]
 public void OptimizedMutesMusicAndRetainsIndependentBackground(int db,int expected)
 {
  var optimized=AudioModePreset.ForMode(2,db);
  Assert.True(optimized.MuteMusic); Assert.Equal(0,optimized.MusicLinear);
  Assert.Equal(expected,optimized.BackgroundDb); Assert.False(optimized.MuteBackground);
  Assert.Equal(0,optimized.VoiceDb); Assert.Equal(0,AudioModePreset.ForMode(1,db).BackgroundDb);
  var preferences=new UiPreferences{OptimizedBackgroundDb=db};
  var restored=JsonSerializer.Deserialize(JsonSerializer.Serialize(preferences.Copy(),SukutJsonContext.Default.UiPreferences),SukutJsonContext.Default.UiPreferences)!;
  Assert.Equal(expected,restored.OptimizedBackgroundDb);
 }
 [Fact] public void AutomaticRestartWaitsAndStopsAfterThreeAttempts()
 {
  var retry=new AudioRestartPolicy(); long now=100;
  foreach(int delay in new[]{1000,2000,4000})
  {
   Assert.True(retry.Request(now));
   Assert.Equal(now+delay,retry.DueAt);
   Assert.True(retry.Request(now+10)); Assert.Equal(now+delay,retry.DueAt);
   Assert.False(retry.TryBegin(now+delay-1)); Assert.True(retry.TryBegin(now+delay));
   Assert.False(retry.Pending); now+=delay;
  }
  Assert.Equal(3,retry.Attempts);Assert.False(retry.Request(now+1));Assert.False(retry.Pending);
 }
 [Fact] public void ShortLivedHealthySessionsDoNotReplenishRestartBudget()
 {
  var retry=new AudioRestartPolicy();
  Assert.True(retry.Request(0));Assert.True(retry.TryBegin(1000));
  retry.Healthy(2000);retry.Healthy(61999);Assert.Equal(1,retry.Attempts);
  Assert.True(retry.Request(62000));Assert.True(retry.TryBegin(64000));
  retry.Healthy(65000);retry.Healthy(124999);Assert.Equal(2,retry.Attempts);
  retry.Healthy(125000);Assert.Equal(0,retry.Attempts);
 }
 [Fact] public void HealthyPlaybackCancelsAnUnneededRestartAndManualResetClearsIt()
 {
  var retry=new AudioRestartPolicy();Assert.True(retry.Request(0));
  retry.Healthy(500);Assert.False(retry.Pending);Assert.False(retry.TryBegin(1000));
  Assert.True(retry.Request(1100));Assert.True(retry.TryBegin(2100));
  Assert.True(retry.Request(2200));retry.Reset();Assert.Equal(0,retry.Attempts);Assert.False(retry.Pending);
 }
 [Fact] public void JsonSupportsLegacyAndUnknownFields()
 {
  var p=JsonSerializer.Deserialize("{\"Language\":\"ar\",\"future\":true}",SukutJsonContext.Default.UiPreferences)!;
  Assert.True(p.CloseToTray); Assert.False(p.SetupCompleted); Assert.Equal("system",p.Theme);
  p.CloseToTray=false;
  Assert.False(JsonSerializer.Deserialize(JsonSerializer.Serialize(p,SukutJsonContext.Default.UiPreferences),SukutJsonContext.Default.UiPreferences)!.CloseToTray);
  var state=JsonSerializer.Deserialize("{\"initialized\":1,\"endpoints\":[{\"id\":\"x\",\"name\":\"سماعات\",\"flow\":1}],\"future\":0}",SukutJsonContext.Default.EngineState)!;
  Assert.Equal(1,state.Initialized); Assert.Equal("سماعات",state.Endpoints[0].Name);
  var round=JsonSerializer.Deserialize(JsonSerializer.Serialize(state,SukutJsonContext.Default.EngineState),SukutJsonContext.Default.EngineState)!;
  Assert.Equal(state.Endpoints[0].Id,round.Endpoints[0].Id);
 }
 [Theory] [InlineData(0)] [InlineData(1)] [InlineData(2)] [InlineData(3)]
 public void PresetFileContainsUpdatedFourModeContract(int mode)
 {
  string root=Path.Combine(Path.GetTempPath(),"sukut-test-"+Guid.NewGuid());
  try
  {
   var engine=(Engine)RuntimeHelpers.GetUninitializedObject(typeof(Engine));
   typeof(Engine).GetField("_root",BindingFlags.NonPublic|BindingFlags.Instance)!.SetValue(engine,root);
   typeof(Engine).GetMethod("WritePresetConfig",BindingFlags.NonPublic|BindingFlags.Instance)!.Invoke(engine,[AudioModePreset.ForMode(mode)]);
   using var json=JsonDocument.Parse(File.ReadAllBytes(Path.Combine(root,"runtime","sukut-presets.json")));
   var data=json.RootElement;
   Assert.Equal(mode,data.GetProperty("activeMode").GetInt32());
   var table=data.GetProperty("presets");
   Assert.Equal(4,table.EnumerateObject().Count());
   Assert.Equal(0,table.GetProperty("halal").GetProperty("BackgroundDb").GetSingle());
   Assert.False(table.GetProperty("halal").GetProperty("MuteBackground").GetBoolean());
   Assert.True(table.GetProperty("strict").GetProperty("MuteBackground").GetBoolean());
   Assert.Equal(0,table.GetProperty("strict").GetProperty("VoiceDb").GetSingle());
   Assert.Equal(mode,data.GetProperty("activePreset").GetProperty("Mode").GetInt32());
  }
  finally { if(Directory.Exists(root)) Directory.Delete(root,true); }
 }
 [Fact] public void BufferReusesAndGrows()
 {
  var buffer=new NativeReplyBuffer(); byte[]? initial=null; int calls=0;
  var data=Encoding.UTF8.GetBytes(new string('a',20000)+"\0");
  var result=buffer.Read((bytes,size)=>{calls++; initial??=bytes; if(size>=data.Length)data.CopyTo(bytes,0); return data.Length;},(_,_)=>1).ToArray();
  Assert.Equal(20000,result.Length); Assert.Equal(2,calls); Assert.Equal(32768,buffer.Capacity);
  byte[]? retained=null;
  buffer.Read((b,_)=>{retained=b;b[0]=0;return 1;},(_,_)=>1);
  buffer.Read((b,_)=>{Assert.Same(retained,b); b[0]=0;return 1;},(_,_)=>1);
  Assert.NotSame(initial,retained);
 }
 [Fact] public void NestedErrorIsDecodedOnceAndDoesNotRecurse()
 {
  var b=new NativeReplyBuffer();
  var error=Assert.Throws<InvalidOperationException>(()=>b.Read((_,_)=>-1,(bytes,_)=>{Encoding.UTF8.GetBytes("failure\0").CopyTo(bytes,0);return 8;}));
  Assert.Equal("failure",error.Message);
  Assert.Throws<InvalidOperationException>(()=>b.Read((_,_)=>-1,(_,_)=>-1));
  Assert.Throws<InvalidOperationException>(()=>b.Read((_,_)=>0,(_,_)=>1));
 }
 [Fact] public async Task SaveIsDebouncedAtomicAndFlushesLatestSnapshot()
 {
  string dir=Path.Combine(Path.GetTempPath(),"sukut-store-"+Guid.NewGuid()),file=Path.Combine(dir,"ui.json");
  try
  {
   await using var store=new SettingsStore(file); var p=new UiPreferences();
   for(int i=0;i<100;i++){p.Output=i.ToString();store.Schedule(p);}
   await store.Flush(p); Assert.Equal("99",JsonSerializer.Deserialize(File.ReadAllBytes(file),SukutJsonContext.Default.UiPreferences)!.Output);
   Assert.False(File.Exists(file+".tmp"));
   var written=File.GetLastWriteTimeUtc(file); await Task.Delay(350); store.Schedule(p); await Task.Delay(400);
   Assert.Equal(written,File.GetLastWriteTimeUtc(file));
   p.Language="ar";store.Schedule(p);p.Language="en";await Task.Delay(400);
   Assert.Equal("ar",JsonSerializer.Deserialize(File.ReadAllBytes(file),SukutJsonContext.Default.UiPreferences)!.Language);
  }
  finally { if(Directory.Exists(dir))Directory.Delete(dir,true); }
 }
}
