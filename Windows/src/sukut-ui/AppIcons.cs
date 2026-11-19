using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;

namespace Sukut;

// The app icon the user picks: one of five marks on a rounded square of one of six
// colours. Same ids and order as tools/export-branding.py (which writes
// Assets/AppIcons/<mark>-<color>.ico), Android's AppIcon.kt and Linux's AppIcon.h.
// It changes this window and its taskbar button; the tray keeps the س icon, and the
// Start menu shortcut and the EXE keep the default icon.
internal static class AppIcons
{
    internal record Mark(string Id, string English, string Arabic);
    internal record Swatch(string Id, string Hex, string English, string Arabic);
    internal static readonly Mark[] Marks =
    [
        new("soft", "Seen", "حرف السين"), new("wave", "Seen wave", "سين متموّجة"), new("wordmark", "Sukut wordmark", "كلمة سُكوت"),
        new("gold", "Original gold", "الذهبي الأصلي"), new("blue", "White and blue", "أبيض وأزرق"),
    ];
    internal static readonly Swatch[] Colors =
    [
        new("black", "#000000", "Black", "أسود"), new("graphite", "#2B2E33", "Graphite", "رمادي داكن"), new("navy", "#0E2238", "Navy", "كحلي"),
        new("forest", "#0F3B2E", "Forest green", "أخضر داكن"), new("burgundy", "#4A1420", "Burgundy", "عنّابي"), new("plum", "#2E1A47", "Plum", "بنفسجي"),
        new("teal", "#0E9F8E", "Teal", "فيروزي"), new("orange", "#E0662B", "Orange", "برتقالي"), new("raspberry", "#C2185B", "Raspberry", "توتي"),
    ];
    internal const string DefaultMark = "blue", DefaultColor = "black";

    private static string ChoiceFile => Path.Combine(AppLocations.DataRoot, "app-icon.txt");

    internal static (string Mark, string Color) Load()
    {
        try
        {
            string[] parts = File.ReadAllText(ChoiceFile).Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
            if (parts.Length == 2 && Marks.Any(m => m.Id == parts[0]) && Colors.Any(c => c.Id == parts[1])) return (parts[0], parts[1]);
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
        return (DefaultMark, DefaultColor);
    }

    internal static void Save(string mark, string color)
    {
        Directory.CreateDirectory(AppLocations.DataRoot);
        File.WriteAllText(ChoiceFile, mark + " " + color + "\n");
    }

    internal static string IconPath(string mark, string color)
    {
        string path = Path.Combine(AppContext.BaseDirectory, "Assets", "AppIcons", mark + "-" + color + ".ico");
        return File.Exists(path) ? path : Path.Combine(AppContext.BaseDirectory, "Assets", "Sukut.ico");
    }

    internal static string CurrentIconPath() { var (mark, color) = Load(); return IconPath(mark, color); }

    // The picker: five icon buttons in the current colour, then six colour buttons.
    internal static void Render(StackPanel marks, StackPanel colors, bool arabic, Action<string, string> changed)
    {
        var (current, color) = Load();
        marks.Children.Clear(); colors.Children.Clear();
        foreach (var mark in Marks)
        {
            var button = new ToggleButton
            {
                IsChecked = mark.Id == current, Padding = new Thickness(4), CornerRadius = new CornerRadius(14),
                Content = Preview(mark.Id, color),
            };
            AutomationProperties.SetName(button, arabic ? mark.Arabic : mark.English);
            string id = mark.Id;
            button.Click += (_, _) => changed(id, Load().Color);
            marks.Children.Add(button);
        }
        foreach (var swatch in Colors)
        {
            var button = new ToggleButton
            {
                IsChecked = swatch.Id == color, Width = 34, Height = 34, Padding = new Thickness(0), CornerRadius = new CornerRadius(17),
                Background = new SolidColorBrush(Parse(swatch.Hex)),
                BorderThickness = new Thickness(swatch.Id == color ? 3 : 1),
            };
            AutomationProperties.SetName(button, arabic ? swatch.Arabic : swatch.English);
            string id = swatch.Id;
            button.Click += (_, _) => changed(Load().Mark, id);
            colors.Children.Add(button);
        }
    }

    // The tile as the picker shows it: the colour drawn by XAML and the transparent mark
    // (192 px PNG) on top, same proportions as the .ico (corner 22.5 %, mark in 72 %).
    // Loading the .ico into an Image showed its 16 px frame stretched, which looked blurred.
    private static FrameworkElement Preview(string mark, string color)
    {
        const double side = 52;
        string png = Path.Combine(AppContext.BaseDirectory, "Assets", "AppIcons", "mark-" + mark + ".png");
        if (!File.Exists(png))
            return new Image { Width = side, Height = side, Source = new BitmapImage(new Uri(IconPath(mark, color))) { DecodePixelWidth = 256 } };
        string hex = Colors.FirstOrDefault(c => c.Id == color)?.Hex ?? "#000000";
        return new Border
        {
            Width = side, Height = side, CornerRadius = new CornerRadius(side * .225), Background = new SolidColorBrush(Parse(hex)),
            Child = new Image
            {
                Width = side * .72, Height = side * .72, Stretch = Stretch.Uniform,
                HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
                Source = new BitmapImage(new Uri(png)),
            },
        };
    }

    private static Windows.UI.Color Parse(string hex) => Windows.UI.Color.FromArgb(255,
        Convert.ToByte(hex.Substring(1, 2), 16), Convert.ToByte(hex.Substring(3, 2), 16), Convert.ToByte(hex.Substring(5, 2), 16));
}
