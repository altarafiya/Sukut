using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;

namespace Sukut;

public sealed partial class BufferingControl : UserControl
{
    private static int _instances;
    private bool _rendering, _vertical;
    private int _level;
    public int Level => _level;
    public event EventHandler? ValueCommitted;
    /// <summary>Stack the three choices as full-width rows instead of the Home card layout.</summary>
    public bool Vertical
    {
        get => _vertical;
        set { _vertical = value; if (value) StackRows(); }
    }
    private void StackRows()
    {
        Options.ColumnDefinitions.Clear(); Options.RowDefinitions.Clear();
        Options.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        for (int row = 0; row < 3; row++) Options.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        Options.ColumnSpacing = 0; Options.RowSpacing = 4; Options.HorizontalAlignment = HorizontalAlignment.Stretch;
        Frame.Padding = new Thickness(0); Frame.BorderThickness = new Thickness(0); Frame.Background = null;
        ContentStack.Spacing = 8;
        Heading.Visibility = Visibility.Collapsed;
        Row(LowestChoice, 0); Row(BalancedChoice, 1); Row(MaximumChoice, 2);
        foreach (var label in new[] { Lowest, Balanced, Maximum }) label.ClearValue(TextBlock.TextAlignmentProperty);
        Heading.HorizontalAlignment = HorizontalAlignment.Left;
        Tip.HorizontalAlignment = HorizontalAlignment.Stretch; Tip.ClearValue(TextBlock.TextAlignmentProperty);
        MinWidth = 300; MaxWidth = 360;
    }
    private static void Row(RadioButton card, int row)
    {
        Grid.SetColumn(card, 0); Grid.SetRow(card, row);
        card.Width = double.NaN;
        card.MinHeight = 40; card.Padding = new Thickness(10, 8, 10, 8);
        card.HorizontalAlignment = HorizontalAlignment.Stretch;
        card.HorizontalContentAlignment = HorizontalAlignment.Stretch;
        if (card.Content is StackPanel panel)
        {
            // A measured label column keeps Arabic glyph overhang and different
            // caption lengths from moving the icon or clipping the last letter.
            var icon = panel.Children[0]; var label = (TextBlock)panel.Children[1];
            panel.Children.Clear();
            var rowContent = new Grid { ColumnSpacing = 10, HorizontalAlignment = HorizontalAlignment.Stretch };
            rowContent.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(24) });
            rowContent.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            icon.SetValue(FrameworkElement.VerticalAlignmentProperty, VerticalAlignment.Center);
            label.HorizontalAlignment = HorizontalAlignment.Stretch;
            label.VerticalAlignment = VerticalAlignment.Center;
            Grid.SetColumn(label, 1);
            rowContent.Children.Add(icon); rowContent.Children.Add(label);
            card.Content = rowContent;
        }
    }
    public BufferingControl()
    {
        InitializeComponent();
        string group = "SukutBuffering" + (++_instances);
        LowestChoice.GroupName = BalancedChoice.GroupName = MaximumChoice.GroupName = group;
        Render(false, 0);
    }
    private void Typography_Loaded(object sender, RoutedEventArgs args)
    {
        if (sender is DependencyObject element) ArabicTypography.Apply(element, Language == "ar");
    }
    public void Render(bool arabic, int value)
    {
        _rendering = true;
        _level = Math.Clamp(value, 0, 2);
        FlowDirection = arabic ? FlowDirection.RightToLeft : FlowDirection.LeftToRight;
        Language = arabic ? "ar" : "en";
        FontFamily = (Microsoft.UI.Xaml.Media.FontFamily)Application.Current.Resources[arabic ? "SukutArabicFontFamily" : "ContentControlThemeFontFamily"];
        if (_vertical) {
            // FlowDirection already chooses the leading text edge. A local
            // Right alignment mirrors it a second time in an RTL popup.
            Tip.ClearValue(TextBlock.TextAlignmentProperty);
            foreach (var label in new[] { Lowest, Balanced, Maximum }) label.ClearValue(TextBlock.TextAlignmentProperty);
        }
        Heading.Text = arabic ? "التخزين المؤقت للصوت" : "Audio buffering";
        // Reserve a trailing advance for the final glyph in popup text layout.
        // Padding alone did not prevent Readex's final m from being clipped.
        Lowest.Text = (arabic ? "الأقل" : "Lowest") + "\u00a0";
        Balanced.Text = (arabic ? "متوازن" : "Balanced") + "\u00a0";
        Maximum.Text = (arabic ? "الأكبر" : "Maximum") + "\u00a0";
        Tip.Text = arabic
            ? "صوت جاهز احتياطيًا. زيادته تقلّل التقطّع، لكنها تؤخّر الصوت."
            : "More audio kept ready. Fewer short pauses, with a little more delay.";
        AutomationProperties.SetName(LowestChoice, Lowest.Text.TrimEnd());
        AutomationProperties.SetName(BalancedChoice, Balanced.Text.TrimEnd());
        AutomationProperties.SetName(MaximumChoice, Maximum.Text.TrimEnd());
        LowestChoice.IsChecked = _level == 0;
        BalancedChoice.IsChecked = _level == 1;
        MaximumChoice.IsChecked = _level == 2;
        ArabicTypography.Apply(Content, arabic);
        _rendering = false;
    }
    private void Choice_Checked(object sender, RoutedEventArgs args)
    {
        if (_rendering) return;
        var button = (RadioButton)sender;
        int index = ReferenceEquals(button, MaximumChoice) ? 2 : ReferenceEquals(button, BalancedChoice) ? 1 : 0;
        if (index == _level) return;
        _level = index;
        ValueCommitted?.Invoke(this, EventArgs.Empty);
    }
}
