using System.Reflection;
using System.Runtime.CompilerServices;
using Sukut;

string target = Path.GetFullPath(args[0]);
Directory.CreateDirectory(target);
// Execute the original serializer without starting its STA thread/native bridge.
var original = (Engine)RuntimeHelpers.GetUninitializedObject(typeof(Engine));
typeof(Engine).GetField("_root", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(original, target);
var write = typeof(Engine).GetMethod("WritePresetConfig", BindingFlags.Instance | BindingFlags.NonPublic)!;
for (int mode = 0; mode < 3; mode++)
{
    write.Invoke(original, [AudioModePreset.ForMode(mode)]);
    File.Copy(Path.Combine(target, "runtime", "sukut-presets.json"), Path.Combine(target, $"mode-{mode}.json"), true);
}
Console.WriteLine("Saved original serializer output for all three modes; no native calls made.");
