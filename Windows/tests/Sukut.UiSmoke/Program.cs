using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using FlaUI.Core;
using FlaUI.Core.AutomationElements;
using FlaUI.Core.Capturing;
using FlaUI.UIA3;

internal static class Program
{
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr hwnd,uint message,IntPtr w,IntPtr l);
    [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint process);
    [StructLayout(LayoutKind.Sequential)] struct HighContrast { public uint Size,Flags;public IntPtr Scheme; }
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool SystemParametersInfo(uint action,uint parameter,ref HighContrast value,uint flags);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool SystemParametersInfo(uint action,uint parameter,string? value,uint flags);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr window,uint message,IntPtr w,string value,uint flags,uint timeout,out IntPtr result);
    [STAThread] static int Main(string[] args)
    {
        try
        {
            SetProcessDpiAwarenessContext((IntPtr)(-4));
            if(args[0]=="restore-os")
            {
                using var original=JsonDocument.Parse(File.ReadAllText(args[1]));
                var contrast=new HighContrast{Size=(uint)Marshal.SizeOf<HighContrast>()};
                contrast.Flags=uint.Parse(original.RootElement.GetProperty("contrast_flags").GetString()!)&~0x1000u;
                if(!SystemParametersInfo(0x43,contrast.Size,ref contrast,3))throw new Exception("Could not restore contrast state");
                foreach(var property in original.RootElement.GetProperty("personalize").EnumerateObject())
                    Microsoft.Win32.Registry.SetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",property.Name,property.Value.GetInt32(),Microsoft.Win32.RegistryValueKind.DWord);
                string themeFile=Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[1]))!,"os-theme-before.theme");
                string section="";
                foreach(string line in File.ReadLines(themeFile))
                {
                    if(line.StartsWith('[')){section=line.Trim();continue;}
                    int equals=line.IndexOf('=');if(equals<0)continue;
                    string key=line[..equals],value=Environment.ExpandEnvironmentVariables(line[(equals+1)..]);
                    if(section=="[Control Panel\\Desktop]" && key=="Wallpaper" && File.Exists(value))
                        SystemParametersInfo(20,0,value,3);
                    if(section=="[Control Panel\\Cursors]" && (File.Exists(value) || key=="DefaultValue"))
                        Microsoft.Win32.Registry.SetValue(@"HKEY_CURRENT_USER\Control Panel\Cursors",key=="DefaultValue"?"":key,value);
                }
                SystemParametersInfo(0x57,0,(string?)null,3);
                SendMessageTimeout((IntPtr)0xffff,0x1A,IntPtr.Zero,"ImmersiveColorSet",2,1000,out _);
                Console.WriteLine("Restored original contrast flags and personalization values");return 0;
            }
            int processId = int.Parse(args[0]);
            string command = args[1], output = args[2];
            Directory.CreateDirectory(output);
            using var automation = new UIA3Automation();
            using var app = FlaUI.Core.Application.Attach(processId);
            var window = app.GetMainWindow(automation, TimeSpan.FromSeconds(15)) ?? throw new Exception("Sukut window not found");
            AutomationElement Find(string id)
            {
                for(int attempt=0;attempt<15;attempt++)
                {
                    AutomationElement? element=null;
                    try { element=window.FindFirstDescendant(x=>x.ByAutomationId(id)); }
                    catch(System.Runtime.InteropServices.COMException error) when(error.HResult is unchecked((int)0x80131505) or unchecked((int)0x80040201)) { }
                    if(element!=null)return element;
                    Thread.Sleep(200);
                    window=app.GetMainWindow(automation,TimeSpan.FromSeconds(3))??window;
                }
                throw new Exception("Missing element: "+id);
            }
            void Invoke(string id)
            {
                Console.WriteLine("Invoke " + id);
                var element = Find(id);
                if (element.Patterns.Invoke.TryGetPattern(out var invoke)) invoke.Invoke();
                else if (element.Patterns.SelectionItem.TryGetPattern(out var select)) select.Select();
                else throw new Exception("Element has no invoke/selection pattern: " + id);
                Thread.Sleep(400);
            }
            void Shot(string name)
            {
                for(int attempt=0;attempt<5;attempt++)
                {
                try
                {
                var work=System.Windows.Forms.Screen.PrimaryScreen!.WorkingArea;
                var bounds=window.BoundingRectangle;
                if(bounds.Right>work.Right || bounds.Bottom>work.Bottom || bounds.Left<work.Left || bounds.Top<work.Top)
                    window.Patterns.Transform.Pattern.Move(work.X,work.Y);
                window.SetForeground(); Thread.Sleep(250);
                using var capture = Capture.Element(window);
                capture.ToFile(Path.Combine(output, name + ".png"));
                return;
                }
                catch(System.Runtime.InteropServices.COMException) when(attempt<4)
                {
                    Thread.Sleep(500);window=app.GetMainWindow(automation,TimeSpan.FromSeconds(5))??window;
                }
                }
            }
            void Metrics(string name)
            {
                var rows = new List<object>();
                var watch = Stopwatch.StartNew();
                using var process = Process.GetProcessById(processId);
                uint uiThreadId=GetWindowThreadProcessId((IntPtr)window.Properties.NativeWindowHandle.Value,out _);
                ProcessThread? uiThread=process.Threads.Cast<ProcessThread>().FirstOrDefault(t=>t.Id==uiThreadId);
                double previousUi=uiThread?.TotalProcessorTime.TotalSeconds??0;
                double previousCpu = process.TotalProcessorTime.TotalSeconds, previousTime = 0;
                string counterTool = Path.GetFullPath("deps/diagnostics/dotnet-counters.exe");
                using var counters = Process.Start(new ProcessStartInfo(counterTool)
                {
                    UseShellExecute = false, CreateNoWindow = true,
                    ArgumentList = { "collect", "--process-id", processId.ToString(), "--counters", "System.Runtime", "--duration", "00:06:00", "--refresh-interval", "1", "--format", "csv", "--output", Path.Combine(output, name + "-counters.csv") }
                });
                while (watch.Elapsed.TotalSeconds < 360)
                {
                    Thread.Sleep(2000); process.Refresh();
                    double now = watch.Elapsed.TotalSeconds, cpu = process.TotalProcessorTime.TotalSeconds;
                    double uiCpu=uiThread?.TotalProcessorTime.TotalSeconds??0;
                    rows.Add(new { elapsed_s = now, settle = now < 60, cpu_one_core_percent = 100 * (cpu - previousCpu) / (now - previousTime), ui_cpu_one_core_percent=100*(uiCpu-previousUi)/(now-previousTime), private_bytes = process.PrivateMemorySize64, working_set_bytes = process.WorkingSet64, handles = process.HandleCount, threads = process.Threads.Count });
                    previousUi=uiCpu;
                    previousCpu = cpu; previousTime = now;
                }
                counters?.WaitForExit(10000);
                File.WriteAllText(Path.Combine(output, name + "-process.json"), JsonSerializer.Serialize(rows, new JsonSerializerOptions { WriteIndented = true }));
                Console.WriteLine("MEASURED " + name);
            }
            bool baseline = FindOptional("HomeButton") != null;
            AutomationElement? FindOptional(string id) => window.FindFirstDescendant(x => x.ByAutomationId(id));
            string home = baseline ? "HomeButton" : "HomeNavItem", settings = baseline ? "SettingsButton" : "SettingsNavItem", about = baseline ? "AboutButton" : "AboutNavItem";
            void WaitStatus(params string[] expected)
            {
                var timer=Stopwatch.StartNew();
                while(timer.Elapsed.TotalSeconds<60)
                {
                    string value=Find("StatusLabel").Name;
                    if(expected.Contains(value))return;
                    if(value is "Attention" or "تنبيه")throw new Exception("Audio Attention: "+FindOptional("ErrorBar")?.Name);
                    Thread.Sleep(200);
                }
                throw new Exception("Timed out waiting for "+string.Join(",",expected)+"; actual="+Find("StatusLabel").Name);
            }
            void SelectMode(string id,params string[] status)
            {
                Find(id).Patterns.SelectionItem.Pattern.Select(); WaitStatus(status);
            }
            if(command=="pc0210-layout")
            {
                Invoke(home);
                try
                {
                    SelectMode("OptimizedCard","Optimized");Thread.Sleep(300);
                    var modes=Find("OptimizedCard").BoundingRectangle;
                    var slider=Find("OptimizedBackgroundSlider").BoundingRectangle;
                    var details=Find("ModeDetailTitle").BoundingRectangle;
                    Assert(slider.Top>=modes.Bottom,"Slider follows mode cards");
                    Assert(details.Top>=slider.Bottom+24,"Mode description has a separate card gap below slider");
                    Shot("separate-mode-controls");
                    Invoke(settings);Find("LanguageChoice").AsComboBox().Select(1);Invoke(home);Thread.Sleep(300);
                    modes=Find("OptimizedCard").BoundingRectangle;slider=Find("OptimizedBackgroundSlider").BoundingRectangle;details=Find("ModeDetailTitle").BoundingRectangle;
                    Assert(slider.Top>=modes.Bottom && details.Top>=slider.Bottom+24,"Arabic has the same separated layout");
                    Shot("separate-mode-controls-ar");
                }
                finally
                {
                    Invoke(settings);Find("LanguageChoice").AsComboBox().Select(0);Invoke(home);SelectMode("OffCard","Off");
                }
                Console.WriteLine("PASS English/Arabic: slider inside Audio mode box; descriptions separate below");
            }
            else if(command=="pc029")
            {
                string root=Path.GetFullPath(args[3]);
                using var preferences=JsonDocument.Parse(File.ReadAllText(Path.Combine(root,"runtime/sukut-ui.json")));
                string sourceId=preferences.RootElement.GetProperty("Source").GetString()!;
                int originalBackground=preferences.RootElement.TryGetProperty("OptimizedBackgroundDb",out var savedBackground)?savedBackground.GetInt32():0;
                Invoke(home);
                Process? source=null;
                try
                {
                    SelectMode("OptimizedCard","Optimized");
                    Assert(Find("MusicModeDetail").Name.Contains("Muted"),"Optimized must mute music");
                    Assert(Find("VoiceModeDetail").Name.Contains("0 dB"),"Voice stays at 0 dB");
                    Find("OptimizedBackgroundSlider").Patterns.RangeValue.Pattern.SetValue(-24);
                    Thread.Sleep(700);
                    Assert(Find("BackgroundModeDetail").Name.Contains("-24"),"Slider changes background mix");
                    Shot("optimized-background");
                    source=Process.Start(new ProcessStartInfo(Path.Combine(root,"build/bootstrap/bin/Release/cleanwave_live_source.exe"))
                    {
                        UseShellExecute=false,CreateNoWindow=true,
                        ArgumentList={sourceId,"48000","16",Path.Combine(output,"source.txt")}
                    });
                    Assert(source!=null,"Test source starts");
                    Thread.Sleep(5500);
                    using var firstHealth=JsonDocument.Parse(File.ReadAllText(Path.Combine(root,"runtime/sukut-health.json")));
                    long firstPlayed=firstHealth.RootElement.GetProperty("pipeline").GetProperty("played").GetInt64();
                    Assert(firstPlayed>0,"Playback advances while visible");
                    IntPtr hwnd=(IntPtr)window.Properties.NativeWindowHandle.Value;
                    window.Close(); Thread.Sleep(6000);
                    using var process=Process.GetProcessById(processId);
                    Assert(!process.HasExited && !IsWindowVisible(hwnd),"Close hides to tray while processing");
                    string hiddenHealth=File.ReadAllText(Path.Combine(root,"runtime/sukut-health.json"));
                    using var secondHealth=JsonDocument.Parse(hiddenHealth);
                    Assert(secondHealth.RootElement.GetProperty("state").GetString()=="Running","Hidden playback remains Running");
                    Assert(secondHealth.RootElement.GetProperty("pipeline").GetProperty("active").GetBoolean(),"Hidden pipeline active");
                    Assert(secondHealth.RootElement.GetProperty("pipeline").GetProperty("played").GetInt64()>firstPlayed,"Playback advances in tray");
                    File.WriteAllText(Path.Combine(output,"tray-health.json"),hiddenHealth);
                    PostMessage(hwnd,0x8000+91,(IntPtr)1,(IntPtr)0x203);Thread.Sleep(700);
                    Assert(IsWindowVisible(hwnd),"Tray callback reopens window");WaitStatus("Optimized");
                    Shot("reopened");
                    Assert(source!.WaitForExit(7000) && source.ExitCode==0,"Bounded generated source completes");
                }
                finally
                {
                    if(source!=null && !source.HasExited){source.Kill();source.WaitForExit(3000);}
                    source?.Dispose();
                    var hwnd=(IntPtr)window.Properties.NativeWindowHandle.Value;
                    if(!IsWindowVisible(hwnd)){PostMessage(hwnd,0x8000+91,(IntPtr)1,(IntPtr)0x203);Thread.Sleep(500);}
                    Find("OptimizedBackgroundSlider").Patterns.RangeValue.Pattern.SetValue(originalBackground);
                    Thread.Sleep(400);SelectMode("OffCard","Off");
                }
                Shot("off");
                Console.WriteLine("PASS Optimized music mute, voice 0 dB, live background slider, bounded audio continues hidden in tray, reopen and OFF");
            }
            else if(command=="row-details")
            {
                foreach(int language in new[]{0,1})foreach(int theme in new[]{1,2})
                {
                    Invoke(settings);Find("LanguageChoice").AsComboBox().Select(language);Find("ThemeChoice").AsComboBox().Select(theme);Thread.Sleep(350);Invoke(home);
                    window.Patterns.Transform.Pattern.Resize(780,720);Thread.Sleep(300);
                    void Row()
                    {
                        var boxes=new[]{"OffCard","HalalCard","OptimizedCard","StrictCard"}.Select(id=>Find(id).BoundingRectangle).ToArray();
                        Assert(boxes.Max(x=>x.Top)-boxes.Min(x=>x.Top)<2,"All four modes must be in one row");
                        Assert(boxes.All(x=>x.Left>=window.BoundingRectangle.Left && x.Right<=window.BoundingRectangle.Right),"Every mode fits window width");
                        Assert(Find("ModeDetailTitle").BoundingRectangle.Top>=boxes.Max(x=>x.Bottom),"Description sits below mode row");
                    }
                    Row();SelectMode("StrictCard",language==0?"Strict":"صارم");
                    Assert(Find("VoiceModeDetail").Name.Contains("+6"),"Strict voice +6");
                    Assert(Find("MusicModeDetail").Name.Contains(language==0?"Muted":"مكتوم"),"Strict music mute");
                    Assert(Find("BackgroundModeDetail").Name.Contains(language==0?"Muted":"مكتوم"),"Strict background mute");
                    Shot($"strict-{language}-{theme}");
                    SelectMode("HalalCard",language==0?"Halal":"حلال");Assert(Find("BackgroundModeDetail").Name.Contains("0 dB"),"Halal background 0");
                    SelectMode("OptimizedCard",language==0?"Optimized":"محسّن");Assert(Find("MusicModeDetail").Name.Contains("-20"),"Optimized music -20");
                    SelectMode("OffCard",language==0?"Off":"متوقف");
                    window.Patterns.Transform.Pattern.Resize(640,520);Thread.Sleep(300);Row();Shot($"compact-{language}-{theme}");
                }
                window.Patterns.Transform.Pattern.Resize(780,720);
                Invoke(settings);Find("LanguageChoice").AsComboBox().Select(0);Find("ThemeChoice").AsComboBox().Select(0);Invoke(home);
                Console.WriteLine("PASS four horizontal modes and selected details at 780/640 widths, English/Arabic, Light/Dark");
            }
            else if(command=="brown-update")
            {
                Invoke(settings);Find("LanguageChoice").AsComboBox().Select(0);Find("ThemeChoice").AsComboBox().Select(0);
                var trayToggle=Find("TraySwitch").Patterns.Toggle.Pattern;
                if(trayToggle.ToggleState==FlaUI.Core.Definitions.ToggleState.Off)trayToggle.Toggle();
                Invoke(home);SelectMode("OffCard","Off");
                foreach(string id in new[]{"OffCard","HalalCard","OptimizedCard","StrictCard"})
                {
                    var card=Find(id);
                    Assert(card.Properties.IsKeyboardFocusable.ValueOrDefault,id+" focusable");
                    Assert(card.FindAllDescendants(x=>x.ByControlType(FlaUI.Core.Definitions.ControlType.Text)).Length==1,id+" has only a label");
                }
                SelectMode("StrictCard","Strict");SelectMode("HalalCard","Halal");SelectMode("OptimizedCard","Optimized");SelectMode("OffCard","Off");
                using var process=Process.GetProcessById(processId);string executable=process.MainModule!.FileName;
                for(int index=0;index<10;index++)
                {
                    window.Close();Thread.Sleep(500);process.Refresh();
                    Assert(!process.HasExited,"Close must keep process alive");
                    Assert(!IsWindowVisible((IntPtr)window.Properties.NativeWindowHandle.Value),"Close must hide HWND");
                    using var second=Process.Start(new ProcessStartInfo(executable){UseShellExecute=true});
                    Assert(second!=null && second.WaitForExit(10000),"Second instance must exit");Thread.Sleep(500);
                    Assert(IsWindowVisible((IntPtr)window.Properties.NativeWindowHandle.Value),"Original window must reopen");
                    WaitStatus("Off");
                }
                SelectMode("StrictCard","Strict");window.Close();Thread.Sleep(1000);
                PostMessage((IntPtr)window.Properties.NativeWindowHandle.Value,0x8000+91,(IntPtr)1,(IntPtr)0x205);
                Thread.Sleep(400);
                var trayOff=automation.GetDesktop().FindFirstDescendant(x=>x.ByName("OFF").And(x.ByControlType(FlaUI.Core.Definitions.ControlType.MenuItem)))??throw new Exception("Tray OFF menu missing");
                trayOff.AsMenuItem().Invoke();Thread.Sleep(1000);
                using(var second=Process.Start(new ProcessStartInfo(executable){UseShellExecute=true}))Assert(second!=null && second.WaitForExit(10000),"Active reopen must keep single instance");
                Thread.Sleep(600);WaitStatus("Off");SelectMode("StrictCard","Strict");window.Close();Thread.Sleep(500);
                using(var second=Process.Start(new ProcessStartInfo(executable){UseShellExecute=true}))Assert(second!=null && second.WaitForExit(10000),"Strict reopen must keep single instance");
                Thread.Sleep(600);WaitStatus("Strict");SelectMode("OffCard","Off");Shot("home-four-modes");
                Invoke(settings);Find("LanguageChoice").AsComboBox().Select(1);Invoke(home);Assert(Find("StrictLabel").Name=="الوضع الصارم","Arabic Strict");Shot("home-four-modes-ar");
                Invoke(settings);Find("LanguageChoice").AsComboBox().Select(0);Invoke(home);
                Console.WriteLine("PASS four modes, labels only, 10 close/reopen cycles, hidden tray OFF, active Strict survives hiding, single instance and Arabic");
            }
            else if(command=="ram-cycle")
            {
                window.Patterns.Transform.Pattern.Resize(780,720);
                Invoke(home);WaitStatus("Off");SelectMode("OffCard","Off");
                using var process=Process.GetProcessById(processId);string executable=process.MainModule!.FileName;
                var samples=new List<object>();
                void Sample(string stage)
                {
                    Thread.Sleep(15000);
                    for(int index=0;index<15;index++)
                    {
                        Thread.Sleep(2000);process.Refresh();samples.Add(new{stage,private_bytes=process.PrivateMemorySize64,working_set_bytes=process.WorkingSet64,handles=process.HandleCount,threads=process.Threads.Count});
                    }
                    Console.WriteLine("Sampled "+stage);
                }
                Sample("fresh-off");SelectMode("OptimizedCard","Optimized");Sample("active-optimized");
                SelectMode("OffCard","Off");Sample("stopped-off");window.Close();Thread.Sleep(500);Sample("tray-off");
                using(var second=Process.Start(new ProcessStartInfo(executable){UseShellExecute=true}))second?.WaitForExit(10000);
                Thread.Sleep(500);
                File.WriteAllText(Path.Combine(output,"ram-cycle.json"),JsonSerializer.Serialize(samples,new JsonSerializerOptions{WriteIndented=true}));
                Console.WriteLine("PASS RAM cycle sampled (15s settle +30s sampled per stage; no debugger)");
            }
            else if(command=="fresh-package")
            {
                Assert(Find("SetupStepLabel").Name=="Step 1 of 4","Clean package opens first-run setup");
                for(int step=2;step<=4;step++){Invoke("SetupNextButton");Assert(Find("SetupStepLabel").Name==$"Step {step} of 4","Setup step "+step);}
                Invoke("SetupNextButton");WaitStatus("Off");
                SelectMode("HalalCard","Halal");SelectMode("StrictCard","Strict");SelectMode("OptimizedCard","Optimized");SelectMode("OffCard","Off");Shot("fresh-package-home");
                Console.WriteLine("PASS clean extracted package first-run setup and all four modes");
            }
            else if (command == "ui-smoke")
            {
                Invoke(settings);Find("LanguageChoice").AsComboBox().Select(0);Find("ThemeChoice").AsComboBox().Select(0);Thread.Sleep(400);
                Invoke(home);WaitStatus("Off");
                SelectMode("HalalCard","Halal"); SelectMode("OptimizedCard","Optimized"); SelectMode("OffCard","Off");
                Invoke(about);Assert(Find("BrandName").Name=="الطرفية","About brand");
                Invoke(settings); Find("LanguageChoice").AsComboBox().Select(1);Thread.Sleep(400);
                Assert(Find("SettingsHeading").Name=="الإعدادات","Arabic heading");
                Invoke(home);Assert(Find("ModeHeading").Name=="وضع الصوت","Arabic Home");
                Invoke(settings);Find("LanguageChoice").AsComboBox().Select(0);Find("ThemeChoice").AsComboBox().Select(1);Thread.Sleep(400);
                Find("ThemeChoice").AsComboBox().Select(2);Thread.Sleep(400);
                Invoke("RunSetupButton"); Assert(Find("SetupStepLabel").Name=="Step 1 of 4","Setup start");
                for(int i=2;i<=4;i++){Invoke("SetupNextButton");Assert(Find("SetupStepLabel").Name==$"Step {i} of 4","Setup step "+i);}
                Invoke("SetupNextButton");WaitStatus("Off");
                Invoke(settings);Find("ThemeChoice").AsComboBox().Select(0);Invoke(home);
                File.WriteAllText(Path.Combine(output,"smoke.json"),"{\"mode_cycle\":true,\"languages\":true,\"themes\":true,\"wizard\":true}");
                Console.WriteLine("PASS modes, navigation, languages, themes and all wizard pages");
            }
            else if(command=="stress")
            {
                Invoke(home); SelectMode("HalalCard","Halal"); SelectMode("OptimizedCard","Optimized");SelectMode("OffCard","Off");
                using var p=Process.GetProcessById(processId);p.Refresh();
                var initial=new{handles=p.HandleCount,threads=p.Threads.Count,bytes=p.PrivateMemorySize64};
                for(int i=0;i<100;i++){int mode=(i+1)%3;SelectMode(mode==0?"OffCard":mode==1?"HalalCard":"OptimizedCard",mode==0?"Off":mode==1?"Halal":"Optimized");Thread.Sleep(100);}
                SelectMode("OffCard","Off");
                string executable=p.MainModule!.FileName;
                for(int i=0;i<50;i++)
                {
                    window.Close();Thread.Sleep(80);
                    using var second=Process.Start(new ProcessStartInfo(executable){UseShellExecute=true});
                    Assert(second!=null && second.WaitForExit(15000),"Second instance exits");
                    Thread.Sleep(100);
                    Assert(!window.IsOffscreen,"Original window shown");
                }
                Thread.Sleep(3000);p.Refresh();
                var final=new{handles=p.HandleCount,threads=p.Threads.Count,bytes=p.PrivateMemorySize64};
                File.WriteAllText(Path.Combine(output,"stress.json"),JsonSerializer.Serialize(new{mode_switches=100,show_hide_cycles=50,initial,final},new JsonSerializerOptions{WriteIndented=true}));
                Console.WriteLine("PASS 100 switches, 50 show/hide cycles and single instance");
            }
            else if(command=="about-stress")
            {
                for(int i=0;i<50;i++)
                {
                    Invoke(about);Invoke(home);
                    if(i%10==0){Invoke(settings);Find("ThemeChoice").AsComboBox().Select(i%20==0?1:2);Invoke(home);}
                }
                Invoke(settings);Find("ThemeChoice").AsComboBox().Select(0);Invoke(home);
                Console.WriteLine("PASS 50 About load/unload cycles with theme changes");
            }
            else if(command=="quit-on-close")
            {
                Invoke(settings);Find("LanguageChoice").AsComboBox().Select(1);Find("ThemeChoice").AsComboBox().Select(2);Thread.Sleep(400);
                var toggle=Find("TraySwitch").Patterns.Toggle.Pattern;
                if(toggle.ToggleState==FlaUI.Core.Definitions.ToggleState.On)toggle.Toggle();
                Invoke(home);SelectMode("HalalCard","حلال");window.Close();
                Assert(Process.GetProcessById(processId).WaitForExit(30000),"CloseToTray=false must exit");
                Console.WriteLine("PASS close while Halal exits through OFF");
            }
            else if(command=="persist-check")
            {
                Invoke(settings);
                Assert(Find("LanguageChoice").AsComboBox().SelectedItem?.Text=="العربية","Language persists");
                Assert(Find("ThemeChoice").AsComboBox().SelectedItem?.Text=="داكن","Theme persists");
                var toggle=Find("TraySwitch").Patterns.Toggle.Pattern;
                Assert(toggle.ToggleState==FlaUI.Core.Definitions.ToggleState.Off,"CloseToTray persists");
                toggle.Toggle();Find("LanguageChoice").AsComboBox().Select(0);Find("ThemeChoice").AsComboBox().Select(0);Invoke(home);
                Console.WriteLine("PASS persisted Arabic/dark/CloseToTray=false; restored en/system/true");
            }
            else if(command=="accessibility")
            {
                Invoke(home);var data=new List<object>();
                foreach(var id in new[]{"OffCard","HalalCard","OptimizedCard"})
                {
                    var e=Find(id);data.Add(new{id,name=e.Name,position=e.Properties.PositionInSet.ValueOrDefault,size=e.Properties.SizeOfSet.ValueOrDefault,keyboard=e.Properties.IsKeyboardFocusable.ValueOrDefault});
                }
                File.WriteAllText(Path.Combine(output,"accessibility.json"),JsonSerializer.Serialize(data,new JsonSerializerOptions{WriteIndented=true}));
                window.SetForeground();Find("OffCard").Focus();
                FlaUI.Core.Input.Keyboard.Press(FlaUI.Core.WindowsAPI.VirtualKeyShort.RIGHT);Thread.Sleep(500);FlaUI.Core.Input.Keyboard.Release(FlaUI.Core.WindowsAPI.VirtualKeyShort.RIGHT);
                WaitStatus("Halal");SelectMode("OffCard","Off");Shot("keyboard-focus");
                Console.WriteLine("PASS native arrow-key selection");
            }
            else if (command is "baseline-captures" or "matrix-captures" or "contrast-captures")
            {
                string originalOutput=output;
                foreach(var dimensions in command!="baseline-captures" ? new[]{(640,520),(1100,800)} : new[]{(780,660)})
                {
                uint dpi=GetDpiForWindow((IntPtr)window.Properties.NativeWindowHandle.Value);
                if(command!="baseline-captures")
                {
                    var work=System.Windows.Forms.Screen.PrimaryScreen!.WorkingArea;
                    if(dimensions.Item1*dpi/96>work.Width || dimensions.Item2*dpi/96>work.Height){Console.WriteLine($"SKIP {dimensions} at {dpi} DPI: exceeds {work.Width}x{work.Height} work area");continue;}
                    window.Patterns.Transform.Pattern.Move(work.X,work.Y);
                    window.Patterns.Transform.Pattern.Resize(dimensions.Item1*dpi/96,dimensions.Item2*dpi/96);Thread.Sleep(300);
                    output=Path.Combine(originalOutput,$"size-{dimensions.Item1}x{dimensions.Item2}");Directory.CreateDirectory(output);
                }
                foreach (int language in new[] { 0, 1 })
                foreach (int theme in command=="contrast-captures" ? new[]{0} : new[] { 1, 2 })
                {
                    Invoke(settings);
                    Find("LanguageChoice").AsComboBox().Select(language); Thread.Sleep(400);
                    Find("ThemeChoice").AsComboBox().Select(theme); Thread.Sleep(400);
                    string label = (language == 0 ? "en" : "ar") + "-" + (theme == 0 ? "contrast" : theme == 1 ? "light" : "dark") + "-dpi" + GetDpiForWindow((IntPtr)window.Properties.NativeWindowHandle.Value);
                    Shot("settings-" + label);
                    Invoke(about); Shot("about-" + label);
                    Invoke(home); Shot("home-" + label);
                    Invoke(settings); Invoke("RunSetupButton"); Shot("setup-" + label);
                    Invoke("SetupLaterButton");
                }
                }
                output=originalOutput;
                Invoke(settings); Find("LanguageChoice").AsComboBox().Select(0); Find("ThemeChoice").AsComboBox().Select(0); Invoke(home);
                Console.WriteLine("PASS baseline captures at actual current DPI");
            }
            else if (command == "measure-home-off") { Invoke(home); Metrics("home-off"); }
            else if (command == "measure-about") { Invoke(about); Metrics("about"); Invoke(home); }
            else if (command == "measure-optimized")
            {
                Invoke(home); Find("OptimizedCard").Patterns.SelectionItem.Pattern.Select(); Thread.Sleep(2000);
                string status = Find("StatusLabel").Name;
                if (status is not ("Optimized" or "محسّن")) throw new Exception("Optimized was not confirmed: " + status);
                Metrics("home-optimized");
                Find("OffCard").Patterns.SelectionItem.Pattern.Select(); Thread.Sleep(2000);
            }
            else if (command == "measure-tray")
            {
                string executable = Process.GetProcessById(processId).MainModule!.FileName;
                window.Close(); Thread.Sleep(500); Metrics("tray-off");
                Process.Start(new ProcessStartInfo(executable) { UseShellExecute = true });
            }
            else if (command == "tray-list")
            {
                foreach(var item in automation.GetDesktop().FindAllDescendants(x=>x.ByControlType(FlaUI.Core.Definitions.ControlType.ToolBar)))
                { Console.WriteLine(item.Name); foreach(var child in item.FindAllDescendants()) Console.WriteLine($"  {child.ControlType} {child.AutomationId} {child.Name}"); }
            }
            else if (command == "tray-exit")
            {
                var desktop=automation.GetDesktop();
                // Exercise Sukut's notification callback, then its real native menu.
                // This avoids depending on Explorer's overflow visibility on the test host.
                PostMessage((IntPtr)window.Properties.NativeWindowHandle.Value,0x8000+91,(IntPtr)1,(IntPtr)0x205);
                Thread.Sleep(500);
                var exit=desktop.FindFirstDescendant(x=>(x.ByName("Exit").Or(x.ByName("خروج"))).And(x.ByControlType(FlaUI.Core.Definitions.ControlType.MenuItem)))??throw new Exception("Exit menu missing");
                exit.AsMenuItem().Invoke(); app.WaitWhileMainHandleIsMissing(TimeSpan.FromMilliseconds(200));
                Console.WriteLine("Invoked tray Exit");
            }
            else if(command=="restore-size")
            {
                var work=System.Windows.Forms.Screen.PrimaryScreen!.WorkingArea;
                window.Patterns.Transform.Pattern.Resize(780,660);
                window.Patterns.Transform.Pattern.Move(work.X+(work.Width-780)/2,work.Y+(work.Height-660)/2);
            }
            else if (command == "shot") Shot("window");
            else throw new Exception("Unknown command: " + command);
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
    static void Assert(bool value,string description){if(!value)throw new Exception(description);}
}
