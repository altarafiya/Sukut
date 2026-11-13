using System.Diagnostics;
using System.IO.Compression;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace Sukut;

internal sealed record PhoneDevice(string Serial, string State, string Model)
{
    public string Label => Model+" · "+(Serial.Length>4?Serial[^4..]:Serial);
}
internal sealed record PhoneConnectionInfo(string Name, string Android, string? SukutVersion, long? SukutCode, bool WorkerReady);
internal sealed record PhoneTransfer(string Item, long Received, long? Total);
internal sealed record SukutInstallation(long? InstalledCode, long BundledCode, string BundledVersion)
{
    public bool CanUpdate => InstalledCode is long installed && installed < BundledCode;
    public bool IsCurrent => InstalledCode is long installed && installed >= BundledCode;
}
internal sealed partial class PhoneTools
{
    internal const string SukutPackage = "com.altarafiyah.sukut";
    internal const string ToolsHash = "45F4D63113E895EBDE0C90F194099A4676B6AC653BD28D54314A9E022BBC1A99";
    private readonly string _dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Sukut", "PhoneTools");
    private string Adb => Path.Combine(_dir, "platform-tools", "adb.exe");
    public bool AdbReady => File.Exists(Adb)
        && File.Exists(Path.Combine(_dir, "platform-tools", "AdbWinApi.dll"))
        && File.Exists(Path.Combine(_dir, "platform-tools", "AdbWinUsbApi.dll"));
    public async Task Prepare(IProgress<PhoneTransfer>? progress = null)
    {
        Directory.CreateDirectory(_dir);
        if (AdbReady) return;
        string archive = Path.Combine(_dir, "platform-tools.zip");
        await Download("https://dl.google.com/android/repository/platform-tools_r37.0.1-win.zip", archive, ToolsHash, "USB tools", progress);
        using var zip = ZipFile.OpenRead(archive);
        foreach (string name in new[] { "adb.exe", "AdbWinApi.dll", "AdbWinUsbApi.dll", "NOTICE.txt", "source.properties" })
        {
            var entry = zip.GetEntry("platform-tools/" + name);
            if (entry == null) { if (name.EndsWith(".exe") || name.EndsWith(".dll")) throw new IOException("Incomplete platform tools archive."); continue; }
            if (entry.Length > 40_000_000) throw new IOException("Unexpected platform tools file size.");
            string target = Path.Combine(_dir, "platform-tools", name);
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            entry.ExtractToFile(target, overwrite: true);
        }
        zip.Dispose(); File.Delete(archive);
    }
    private static async Task Download(string url, string target, string expected, string item, IProgress<PhoneTransfer>? progress)
    {
        if (File.Exists(target) && await Hash(target) == expected) return;
        string temporary = target + ".download";
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(3) };
            using var response = await http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
            response.EnsureSuccessStatusCode();
            long? length = response.Content.Headers.ContentLength;
            if (length > 40_000_000) throw new IOException("Unexpected download size.");
            progress?.Report(new(item, 0, length));
            await using (var input = await response.Content.ReadAsStreamAsync())
            await using (var output = File.Create(temporary))
            {
                byte[] buffer = new byte[81920]; int count; long total = 0;
                using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(3));
                while ((count = await input.ReadAsync(buffer, timeout.Token)) != 0)
                {
                    total += count; if (total > 40_000_000) throw new IOException("Unexpected download size.");
                    await output.WriteAsync(buffer.AsMemory(0, count), timeout.Token);
                    progress?.Report(new(item, total, length));
                }
            }
            if (await Hash(temporary) != expected) throw new IOException("Download verification failed. Please retry.");
            File.Move(temporary, target, overwrite: true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
    private static async Task<string> Hash(string file)
    {
        await using var stream = File.OpenRead(file);
        return Convert.ToHexString(await SHA256.HashDataAsync(stream));
    }
    internal static List<PhoneDevice> ParseDevices(string text)
    {
        var devices = new List<PhoneDevice>();
        foreach (string line in text.Split('\n'))
        {
            var match = Regex.Match(line.Trim(), @"^(\S+)\s+(device|unauthorized|offline)\b(.*)$");
            if (!match.Success) continue;
            string serial = match.Groups[1].Value;
            if (!Regex.IsMatch(serial, @"^[A-Za-z0-9._:\-\[\]]{1,200}$")) continue;
            string model = Regex.Match(match.Groups[3].Value, @"\bmodel:(\S+)").Groups[1].Value.Replace('_', ' ');
            devices.Add(new(serial, match.Groups[2].Value, model.Length > 0 ? model : "Android"));
        }
        return devices;
    }
    // ADB can report an offline transport briefly after reconnect/server startup.
    // Re-read it without killing the shared server or changing authorization.
    internal static async Task<List<PhoneDevice>> DiscoverDevices(Func<Task<string>> read, Func<int, Task>? delay = null)
    {
        delay ??= milliseconds => Task.Delay(milliseconds);
        List<PhoneDevice> devices = [];
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            devices = ParseDevices(await read());
            if (devices.Any(x => x.State is "device" or "unauthorized")) break;
            if (attempt < 2) await delay(400 * (attempt + 1));
        }
        return devices;
    }
    internal static PhoneDevice? SelectDevice(List<PhoneDevice> devices, string? previousSerial)
    {
        var previous = devices.Find(x => x.Serial == previousSerial);
        if (previous?.State == "device") return previous;
        var online = devices.Where(x => x.State == "device").ToList();
        if (online.Count == 1) return online[0];
        return previous ?? (devices.Count == 1 ? devices[0] : null);
    }
    public Task<List<PhoneDevice>> Devices() => DiscoverDevices(() => Run(["devices", "-l"]));
    internal static string AndroidProperty(string properties,string key)
        =>Regex.Match(properties,@"(?m)^\["+Regex.Escape(key)+@"\]: \[([^\r\n]*)\]\s*$").Groups[1].Value;
    internal static PhoneConnectionInfo ParsePhoneInfo(PhoneDevice device,string properties,string sukut, bool ready = false)
    {
        string name=AndroidProperty(properties,"ro.product.marketname");
        if(name.Length==0)name=AndroidProperty(properties,"ro.product.vendor.marketname");
        if(name.Length==0){name=AndroidProperty(properties,"ro.product.model");if(name.Length==0)name=device.Model;}
        string android=AndroidProperty(properties,"ro.build.version.release");
        string version=Regex.Match(sukut,@"\bversionName=(\d+\.\d+(?:\.\d+)?(?:-beta\.\d+)?)").Groups[1].Value;
        return new(name,android,version.Length==0?null:version,ParseVersionCode(sukut),ready);
    }
    public async Task<PhoneConnectionInfo> InspectPhone(PhoneDevice device)
    {
        await EnsureAuthorized(device);
        string properties=await Run(["-s",device.Serial,"shell","getprop"]);
        string sukut=await Run(["-s",device.Serial,"shell","dumpsys","package",SukutPackage]);
        int user = await CurrentUser(device);
        string state = await WorkerState(device, user);
        return ParsePhoneInfo(device,properties,sukut,WorkerIsReady(state,ParseVersionCode(sukut) ?? 0));
    }
    internal static long? ParseVersionCode(string output)
    {
        var match = Regex.Match(output, @"\bversionCode=(\d+)");
        return match.Success && long.TryParse(match.Groups[1].Value, out long code) ? code : null;
    }
    internal static (string Version, long Code) ParseSukutBundleVersion(string text)
    {
        string[] lines = text.Split('\n', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        if (lines.Length != 2 || !Regex.IsMatch(lines[0], @"^\d+\.\d+(?:\.\d+)?(?:-beta\.\d+)?$") || !long.TryParse(lines[1], out long code) || code <= 0)
            throw new IOException("Sukut APK version information is missing or invalid. Use the complete Sukut folder.");
        return (lines[0], code);
    }
    public async Task<SukutInstallation> SukutStatus(PhoneDevice device)
    {
        await EnsureAuthorized(device);
        var installed = await Run(["-s", device.Serial, "shell", "dumpsys", "package", SukutPackage]);
        var bundle = ParseSukutBundleVersion(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Assets", "Phone", "Sukut.apk.version")));
        return new(ParseVersionCode(installed), bundle.Code, bundle.Version);
    }
    public async Task<string> InstallSukut(PhoneDevice device, bool update = false)
    {
        var status = await SukutStatus(device);
        if (update && status.InstalledCode == null) throw new IOException("Sukut is not installed. Choose Install Sukut first.");
        if (status.InstalledCode != null && !update) return "Sukut is already installed. Your settings are kept.";
        if (status.IsCurrent) return "Sukut is already up to date. Your settings are kept.";
        string apk = Path.Combine(AppContext.BaseDirectory, "Assets", "Phone", "Sukut.apk");
        string checksum = apk + ".sha256";
        if (!File.Exists(apk) || !File.Exists(checksum)) throw new IOException("Sukut APK is missing. Use the complete Sukut folder.");
        string expected = (await File.ReadAllTextAsync(checksum)).Trim();
        if (!Regex.IsMatch(expected, "^[A-Fa-f0-9]{64}$") || !string.Equals(await Hash(apk), expected, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Sukut APK verification failed. Use the complete Sukut folder.");
        string result = await Run(status.InstalledCode != null ? ["-s", device.Serial, "install", "-r", apk] : ["-s", device.Serial, "install", apk], 120);
        if (!result.Contains("Success", StringComparison.Ordinal)) throw new IOException(result.Trim());
        var verified = await SukutStatus(device);
        if (verified.InstalledCode != status.BundledCode) throw new IOException("Sukut APK installation did not report the expected version. Check the phone and retry.");
        return status.InstalledCode != null ? "Sukut updated. Your settings are kept." : "Sukut installed. Continue to open it on the phone.";
    }
    public async Task<string> OpenSukut(PhoneDevice device)
    {
        await EnsureAuthorized(device);
        string result = await Run(["-s", device.Serial, "shell", "am", "start", "-W", "-a", "android.intent.action.MAIN",
            "-c", "android.intent.category.LAUNCHER", "-f", "0x10200000", "-n", SukutPackage + "/com.osama.cleanwave.MainActivity"]);
        if (result.Contains("Error:", StringComparison.OrdinalIgnoreCase)) throw new IOException(result.Trim());
        return "Sukut opened. Activate its audio worker, then press Start on the phone.";
    }
    private async Task EnsureAuthorized(PhoneDevice device)
    {
        if (!(await Devices()).Exists(x => x.Serial == device.Serial && x.State == "device"))
            throw new IOException("Unlock the phone, allow USB debugging, then refresh devices.");
    }
    public Task<string> SetupSukut(PhoneDevice device) => SetupSukutCore(()=>SukutStatus(device),
        update=>InstallSukut(device,update),()=>OpenSukut(device));

    internal static async Task<string> SetupSukutCore(Func<Task<SukutInstallation>> readStatus,
        Func<bool,Task<string>> install,Func<Task<string>> open)
    {
        var status=await readStatus();
        if(!status.IsCurrent)
        {
            await install(status.InstalledCode!=null);
            var actual=await readStatus();
            if(actual.InstalledCode!=status.BundledCode) throw new IOException("Sukut installation did not report the expected version. Check the phone and retry.");
        }
        await open();
        return "Sukut is installed and ready for activation.";
    }
    private async Task<string> Run(string[] args, int seconds = 20, bool allowFailure = false)
    {
        var info = new ProcessStartInfo(Adb) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string arg in args) info.ArgumentList.Add(arg);
        using var process = Process.Start(info) ?? throw new IOException("Could not start ADB.");
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(seconds));
        async Task<string> ReadBounded(StreamReader reader)
        {
            char[] buffer = new char[4096]; var output = new System.Text.StringBuilder(); int count;
            while ((count = await reader.ReadAsync(buffer.AsMemory(), timeout.Token)) > 0)
            { if (output.Length + count > 262144) throw new IOException("ADB returned too much output."); output.Append(buffer, 0, count); }
            return output.ToString();
        }
        var stdout = ReadBounded(process.StandardOutput); var stderr = ReadBounded(process.StandardError);
        try
        {
            await process.WaitForExitAsync(timeout.Token);
            string result = await stdout + await stderr;
            if (process.ExitCode != 0 && !allowFailure) throw new IOException(result.Trim());
            return result;
        }
        catch { if (!process.HasExited) process.Kill(entireProcessTree: true); throw; }
    }
}
