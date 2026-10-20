using System.Runtime.CompilerServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Sukut;

/// <summary>Shared text metrics for pages, deferred templates and popup roots.</summary>
internal static class ArabicTypography
{
    private sealed record Original(FontFamily Face, double Size, Thickness Padding, int Spacing, TextLineBounds Bounds);
    private static readonly ConditionalWeakTable<TextBlock, Original> Originals = new();

    internal static void Apply(DependencyObject? root, bool arabic)
    {
        if (root == null || root is IconElement) return;
        if (root is TextBlock text) Text(text, arabic);
        // Controls provide the font for their native content presenters too.
        if (root is Control control && root is not TitleBar)
        {
            control.FontFamily = (FontFamily)Application.Current.Resources[arabic ? "SukutArabicFontFamily" : "ContentControlThemeFontFamily"];
        }
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(root); i++)
            Apply(VisualTreeHelper.GetChild(root, i), arabic);
    }

    internal static void Text(TextBlock text, bool arabic)
    {
        // Keep the developer's pixel wordmark and native symbol fonts intact.
        string family = text.FontFamily.Source;
        if (family.Contains("Handjet") || family.Contains("Symbol") || family.Contains("MDL2") || family.Contains("Fluent Icons")) return;
        var original = Originals.GetValue(text, t => new(t.FontFamily, t.FontSize, t.Padding, t.CharacterSpacing, t.TextLineBounds));
        bool hasArabic = text.Text.Any(c => c is >= '\u0600' and <= '\u06ff');
        bool useArabic = arabic || hasArabic;
        text.FontFamily = useArabic
            ? (FontFamily)Application.Current.Resources[text.FontWeight.Weight >= 600 ? "SukutTitleFontFamily" : "SukutArabicFontFamily"]
            : original.Face;
        // Captions 12 -> 14, body 14 -> 16. Titles keep their existing hierarchy.
        text.FontSize = arabic ? original.Size <= 12 ? 14 : Math.Max(16, original.Size) : original.Size;
        text.CharacterSpacing = useArabic ? 0 : original.Spacing;
        text.TextLineBounds = useArabic ? TextLineBounds.Full : original.Bounds;
        text.Padding = useArabic ? new Thickness(Math.Max(3, original.Padding.Left), Math.Max(2, original.Padding.Top),
            Math.Max(3, original.Padding.Right), Math.Max(2, original.Padding.Bottom)) : original.Padding;
        if (useArabic)
            foreach (var inline in text.Inlines)
            {
                inline.FontFamily = text.FontFamily;
                inline.FontWeight = text.FontWeight;
                inline.CharacterSpacing = 0;
            }
    }
}
