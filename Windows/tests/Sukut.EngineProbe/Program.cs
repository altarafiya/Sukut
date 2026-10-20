using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using Sukut;

internal static class Native
{
    [DllImport("SukutEngine.dll", CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)]
    internal static extern int Sukut_RestoreRouting(string root,string owner);
    [DllImport("SukutEngine.dll", CallingConvention=CallingConvention.Cdecl)]
    internal static extern int Sukut_Error(byte[] output,int capacity);
}

internal static class Program
{
    static void Check(bool value,string message) { if(!value)throw new Exception(message); }
    static Task RestoreOnSta(string root)
    {
        var done=new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var thread=new Thread(()=>
        {
            try {
                if(Native.Sukut_RestoreRouting(root,"")==0){var bytes=new byte[4096];Native.Sukut_Error(bytes,bytes.Length);throw new Exception(System.Text.Encoding.UTF8.GetString(bytes).TrimEnd('\0'));}
                done.SetResult();
            } catch(Exception error){done.SetException(error);}
        });
        thread.SetApartmentState(ApartmentState.STA);thread.Start();return done.Task;
    }
    static async Task Main(string[] args)
    {
        string root=Path.GetFullPath(args[0]),evidence=Path.GetFullPath(args[1]);
        Directory.CreateDirectory(evidence);
        var p=JsonSerializer.Deserialize(File.ReadAllBytes(Path.Combine(root,"runtime/sukut-ui.json")),SukutJsonContext.Default.UiPreferences)!;
        Check(p.SourcePid==0 && p.CableVolumeDisabledConfirmed,"Probe requires existing user-confirmed All system route; never pre-confirm it.");
        NativeLibrary.SetDllImportResolver(Assembly.GetExecutingAssembly(),(name,assembly,path)=>name=="SukutEngine.dll"?NativeLibrary.Load(Path.Combine(root,"build/sukut-0.2.9/SukutEngine.dll")):IntPtr.Zero);
        await using var engine=new Engine();
        await engine.Initialize(root);
        async Task<EngineState> Until(Func<EngineState,bool> predicate)
        {
            for(int i=0;i<80;i++){var state=await engine.Read();if(predicate(state))return state;await Task.Delay(100);}
            throw new Exception("Engine state deadline exceeded");
        }
        await Until(state=>state.Initialized!=0);
        var records=new List<object>();
        try
        {
            await engine.Select(2,p.Source,p.Capture,p.Output,0,p.Buffering,true,-24);
            var first=await Until(state=>state.State==1 && state.PipelineActive!=0);
            records.Add(new{step="start",first.Session,first.State,first.PipelineActive});
            await engine.Select(2,p.Source,p.Capture,p.Output,0,p.Buffering,true,-18);
            Check((await engine.Read()).Session==first.Session,"Background mix update restarted the route");
            // Exercise external restoration of THIS probe's own journal, like
            // a guard/route change. It must become visible, not remain healthy.
            await RestoreOnSta(root);
            var fault=await Until(state=>!string.IsNullOrWhiteSpace(state.Error) && state.State!=1);
            records.Add(new{step="route_loss_detected",fault.State,fault.Error});
            await engine.Select(2,p.Source,p.Capture,p.Output,0,p.Buffering,true,-18,restart:true);
            var restarted=await Until(state=>state.State==1 && state.PipelineActive!=0);
            Check(restarted.Session>first.Session,"Restart did not recreate audio clients");
            records.Add(new{step="restart",restarted.Session,restarted.State,restarted.PipelineActive});
        }
        finally
        {
            await engine.Select(0,p.Source,p.Capture,p.Output,0,p.Buffering,p.CableVolumeDisabledConfirmed);
        }
        var stopped=await engine.Read();Check(stopped.State==0 && stopped.PipelineActive==0,"OFF left active audio");
        records.Add(new{step="stop",stopped.State,stopped.PipelineActive});
        File.WriteAllText(Path.Combine(evidence,"engine-probe.json"),JsonSerializer.Serialize(records,new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine("PASS live mix without restart; lost route detected; explicit restart recreates clients; OFF restores route");
    }
}
