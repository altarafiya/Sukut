using System.Numerics;
using Microsoft.UI.Composition;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Hosting;
using Microsoft.UI.Xaml.Media.Animation;
using Windows.UI.ViewManagement;
using WinColor = Windows.UI.Color;

namespace Sukut;

public sealed partial class ShellView
{
    private readonly UISettings _uiSettings = new();
    private readonly AccessibilitySettings _accessibilitySettings = new();
    private Microsoft.UI.Xaml.Controls.TextBlock? _brandOwner;
    private SpriteVisual? _brandSprite;
    private CompositionLinearGradientBrush? _brandGradient;
    private CompositionMaskBrush? _brandMask;
    private readonly List<CompositionColorGradientStop> _brandStops = [];
    private Storyboard? _sparkles;
    private bool _brandRunning;

    private void StartBrand()
    {
        if (BrandName == null || !_visible || Minimized || _setupVisible || _page != 2) return;
        if (!_uiSettings.AnimationsEnabled || _accessibilitySettings.HighContrast) { StopBrand(); return; }
        if (_brandRunning) return;
        try
        {
            _brandOwner = BrandName;
            var compositor = ElementCompositionPreview.GetElementVisual(_brandOwner).Compositor;
            _brandGradient = compositor.CreateLinearGradientBrush();
            _brandGradient.StartPoint = Vector2.Zero; _brandGradient.EndPoint = new Vector2(1, 0);
            var easing = compositor.CreateLinearEasingFunction();
            foreach (float phase in new[] { 0f, .33f, .66f })
            {
                var stop = compositor.CreateColorGradientStop(phase == 0 ? 0f : phase == .33f ? .5f : 1f, BrandHue(phase));
                var animation = compositor.CreateColorKeyFrameAnimation();
                animation.Duration = TimeSpan.FromSeconds(8.9); animation.IterationBehavior = AnimationIterationBehavior.Forever;
                animation.InterpolationColorSpace = CompositionColorSpace.Rgb;
                for (int key = 0; key <= 6; key++) animation.InsertKeyFrame(key / 6f, BrandHue((phase + key / 6.0) % 1), easing);
                _brandGradient.ColorStops.Add(stop); _brandStops.Add(stop); stop.StartAnimation("Color", animation);
                animation.Dispose();
            }
            easing.Dispose();
            _brandMask = compositor.CreateMaskBrush(); _brandMask.Mask = BrandName.GetAlphaMask(); _brandMask.Source = _brandGradient;
            _brandSprite = compositor.CreateSpriteVisual(); _brandSprite.Brush = _brandMask;
            _brandSprite.Size = new Vector2((float)BrandName.ActualWidth, (float)BrandName.ActualHeight);
            ElementCompositionPreview.SetElementChildVisual(BrandName, _brandSprite);
            _sparkles = new Storyboard();
            AddSparkle(SparkleOne, 1.1); AddSparkle(SparkleTwo, 1.5);
            _sparkles.Begin(); _brandRunning = true;
        }
        catch (Exception error)
        {
            System.Diagnostics.Debug.WriteLine("Static brand fallback: " + error.Message);
            StopBrand();
        }
    }

    private void AddSparkle(DependencyObject target, double seconds)
    {
        var animation = new DoubleAnimation { From = .35, To = 1, Duration = TimeSpan.FromSeconds(seconds), AutoReverse = true, RepeatBehavior = RepeatBehavior.Forever };
        Storyboard.SetTarget(animation, target); Storyboard.SetTargetProperty(animation, "Opacity"); _sparkles!.Children.Add(animation);
    }
    private void StopBrand()
    {
        if (_brandOwner == null && _brandSprite == null && _brandGradient == null && _sparkles == null) return;
        _sparkles?.Stop(); _sparkles = null;
        if (_brandOwner != null) ElementCompositionPreview.SetElementChildVisual(_brandOwner, null);
        foreach (var stop in _brandStops) stop.StopAnimation("Color");
        _brandStops.Clear();
        // XAML may still reference a detached visual until its next composition
        // commit. Release our references; explicitly closing these objects here
        // can leave XAML holding closed resources during deferred page unloading.
        _brandSprite = null; _brandMask = null; _brandGradient = null;
        _brandOwner = null; _brandRunning = false;
    }
    private void BrandName_SizeChanged(object sender, SizeChangedEventArgs args)
    {
        if (_brandSprite != null) _brandSprite.Size = new Vector2((float)args.NewSize.Width, (float)args.NewSize.Height);
    }
    private static WinColor BrandHue(double hue)
    {
        double k = hue * 6, x = 1 - Math.Abs(k % 2 - 1);
        (double r, double g, double b) = k < 1 ? (1d, x, 0d) : k < 2 ? (x, 1d, 0d) : k < 3 ? (0d, 1d, x) : k < 4 ? (0d, x, 1d) : k < 5 ? (x, 0d, 1d) : (1d, 0d, x);
        return WinColor.FromArgb(255, (byte)(55 + r * 200), (byte)(55 + g * 200), (byte)(55 + b * 200));
    }
}
