using System.Text.RegularExpressions;

namespace Sukut;

internal sealed partial class PhoneTools
{
    private async Task<int> CurrentUser(PhoneDevice device)
    {
        string text = (await Run(["-s", device.Serial, "shell", "am", "get-current-user"])).Trim();
        return ParseUser(text);
    }
    internal static int ParseUser(string text) => int.TryParse(text, out int user) && user >= 0 && user <= 21474
        ? user : throw new IOException("Could not identify the current Android user.");
    internal static bool WorkerIsReady(string state, long version) => version >= 49
        && Regex.IsMatch(state, @"\bstate=READY(?:[,}\s]|$)")
        && Regex.IsMatch(state, @"\bprotocol=1(?:[,}\s]|$)")
        && Regex.IsMatch(state, @"\bversion=" + version + @"(?:[,}\s]|$)")
        && Regex.IsMatch(state, @"\bpid=[1-9]\d*(?:[,}\s]|$)");
    private Task<string> WorkerState(PhoneDevice device, int user) => Run(["-s", device.Serial, "shell", "content", "call", "--user", user.ToString(),
        "--uri", "content://com.altarafiyah.sukut.worker", "--method", "status"], 15, true);
    public async Task<string> ActivateSukut(PhoneDevice device)
    {
        await EnsureAuthorized(device);
        var installation = await SukutStatus(device);
        if (!installation.IsCurrent) await InstallSukut(device, installation.InstalledCode != null);
        long version = (await SukutStatus(device)).InstalledCode ?? 0;
        if (version < 49) throw new IOException("Install Sukut beta.12 or later before activation.");
        int user = await CurrentUser(device);
        if (WorkerIsReady(await WorkerState(device,user),version)) return "Sukut audio worker is already ready. Press Start on the phone.";
        await OpenSukut(device);
        string script = Path.Combine(AppContext.BaseDirectory, "Assets", "Phone", "activate-worker.sh");
        string expected = (await File.ReadAllTextAsync(script + ".sha256")).Trim();
        if (!Regex.IsMatch(expected, "^[A-Fa-f0-9]{64}$") || !string.Equals(await Hash(script), expected, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Sukut activation script verification failed. Use the complete Sukut folder.");
        await Run(["-s", device.Serial, "push", script, "/data/local/tmp/sukut-activate-worker.sh"]);
        await ActivateWorkerCore((args, seconds, allowFailure) => Run(["-s", device.Serial, ..args], seconds, allowFailure),user,version);
        return "Sukut audio worker is ready. You can unplug USB, then press Start on the phone. Repeat activation after a phone restart.";
    }
    internal static async Task ActivateWorkerCore(Func<string[],int,bool,Task<string>> run,int user,long version,Func<int,Task>? delay=null)
    {
        ParseUser(user.ToString());
        if(version<49)throw new IOException("Install Sukut beta.12 or later before activation.");
        delay??=ms=>Task.Delay(ms);
        async Task<bool> Ready()=>WorkerIsReady(await run(["shell","content","call","--user",user.ToString(),"--uri","content://com.altarafiyah.sukut.worker","--method","status"],15,true),version);
        if(await Ready())return;
        for(int launch=0;launch<2;launch++) {
            string output=await run(["shell","sh","/data/local/tmp/sukut-activate-worker.sh",user.ToString()],20,false);
            if(!output.Contains("SUKUT_WORKER_LAUNCHED",StringComparison.Ordinal))throw new IOException("Could not launch Sukut worker: "+output.Trim());
            for(int attempt=0;attempt<12;attempt++) {
                if(await Ready())return;
                await delay(300);
            }
        }
        string log=await run(["shell","tail","-c","6000","/data/local/tmp/sukut-altarafiyah-worker-u"+user+".log"],10,true);
        throw new IOException("Sukut worker did not connect. Keep the phone unlocked and open Sukut, then retry. "+log.Trim());
    }
    public async Task<string> DeactivateSukut(PhoneDevice device)
    {
        await EnsureAuthorized(device);
        int user=await CurrentUser(device);
        string result=await Run(["-s",device.Serial,"shell","content","call","--user",user.ToString(),"--uri","content://com.altarafiyah.sukut.worker","--method","deactivate"]);
        if(!Regex.IsMatch(result,@"\bstate=INACTIVE(?:[,}\s]|$)"))throw new IOException("Could not deactivate Sukut. "+result.Trim());
        return "Sukut audio worker deactivated. Activate it again before using Start.";
    }
}
