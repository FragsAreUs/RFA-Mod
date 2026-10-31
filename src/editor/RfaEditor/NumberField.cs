using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace RfaEditor;

/// <summary>
/// A number box with a short label in front (such as a red "X"). Type a number, or drag the label
/// left and right to change it, as in Blender and Unity (hold Shift to go ten times as fast). Used
/// for an object's position and rotation in the level editor's inspector.
/// </summary>
sealed class NumberField : Border
{
    readonly TextBox box;
    readonly string format;
    readonly double step;
    double value;
    bool showing;              // the text is being set from code, not typed
    Point? dragFrom;           // where the label was pressed, while it is dragged
    double dragStartValue;

    /// <param name="label">The text in front, e.g. "X".</param>
    /// <param name="format">How the number is shown, e.g. "0.###".</param>
    /// <param name="step">How much the number changes for each pixel the label is dragged.</param>
    public NumberField(string label, Brush labelColour, string format, double step, string tip)
    {
        this.format = format;
        this.step = step;
        Background = Theme.Brush("InputBrush");
        BorderBrush = Theme.Brush("BorderBrush");
        BorderThickness = new Thickness(1);
        CornerRadius = new CornerRadius(4);

        var grip = new TextBlock
        {
            Text = label, Foreground = labelColour, FontWeight = FontWeights.SemiBold, Width = 20, TextAlignment = TextAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center, Background = Brushes.Transparent, Cursor = Cursors.SizeWE,
            ToolTip = tip + "\nDrag left or right to change it (Shift: faster), or type a number.",
        };
        grip.MouseLeftButtonDown += OnGripPressed;
        grip.MouseMove += OnGripDragged;
        grip.MouseLeftButtonUp += OnGripReleased;

        box = new TextBox { BorderThickness = new Thickness(0), Background = Brushes.Transparent, Padding = new Thickness(2, 3, 4, 3), ToolTip = tip };
        box.TextChanged += OnTyped;
        box.KeyDown += (_, e) =>
        {
            if (e.Key != Key.Enter) return;
            Finish();
            e.Handled = true;
        };
        box.GotKeyboardFocus += (_, _) => BorderBrush = Theme.Brush("AccentBrush");
        box.LostKeyboardFocus += (_, _) =>
        {
            BorderBrush = Theme.Brush("BorderBrush");
            Finish();
        };

        var row = new DockPanel();
        DockPanel.SetDock(grip, Dock.Left);
        row.Children.Add(grip);
        row.Children.Add(box);
        Child = row;
        IsEnabledChanged += (_, _) => Opacity = IsEnabled ? 1 : 0.45;
    }

    public double Value => value;

    /// <summary>Rounds a number while the label is dragged (e.g. to the snapping step), if set.</summary>
    public Func<double, double>? Round { get; set; }

    /// <summary>Raised with each new value, while typing (valid numbers only) and dragging.</summary>
    public event Action<double>? Changed;

    /// <summary>Raised when a change is finished: the drag ends, Enter is pressed or the box is left.</summary>
    public event Action? Committed;

    /// <summary>Shows a number without raising <see cref="Changed"/>, e.g. after the object moved some other way.</summary>
    public void Show(double number)
    {
        value = number;
        if (box.IsKeyboardFocused) return;  // don't change the text under the user's typing
        showing = true;
        box.Text = number.ToString(format, CultureInfo.InvariantCulture);
        showing = false;
    }

    void OnTyped(object sender, TextChangedEventArgs e)
    {
        if (showing) return;
        if (double.TryParse(box.Text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out double typed) && double.IsFinite(typed))
        {
            box.ClearValue(Control.ForegroundProperty);
            value = typed;
            Changed?.Invoke(value);
        }
        else
        {
            box.Foreground = Theme.Brush("ErrorBrush");  // not a number: nothing is changed
        }
    }

    /// <summary>Shows the number as it is now (tidied, or the last good one) and ends the change.</summary>
    void Finish()
    {
        box.ClearValue(Control.ForegroundProperty);
        showing = true;
        box.Text = value.ToString(format, CultureInfo.InvariantCulture);
        showing = false;
        Committed?.Invoke();
    }

    void OnGripPressed(object sender, MouseButtonEventArgs e)
    {
        if (!IsEnabled) return;
        dragFrom = e.GetPosition(this);
        dragStartValue = value;
        ((UIElement)sender).CaptureMouse();
        e.Handled = true;
    }

    void OnGripDragged(object sender, MouseEventArgs e)
    {
        if (dragFrom is not Point from) return;
        double pixels = e.GetPosition(this).X - from.X;
        double fast = Keyboard.Modifiers.HasFlag(ModifierKeys.Shift) ? 10 : 1;
        double next = dragStartValue + pixels * step * fast;
        if (Round != null) next = Round(next);
        if (next == value) return;
        value = next;
        showing = true;
        box.Text = value.ToString(format, CultureInfo.InvariantCulture);
        showing = false;
        Changed?.Invoke(value);
    }

    void OnGripReleased(object sender, MouseButtonEventArgs e)
    {
        if (dragFrom == null) return;
        dragFrom = null;
        ((UIElement)sender).ReleaseMouseCapture();
        Committed?.Invoke();
    }
}
