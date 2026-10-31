using System.Numerics;
using System.Windows;
using System.Windows.Controls;
using RfaFormats.Zones;

namespace RfaEditor;

/// <summary>
/// The inspector's Transform section for a level object: where it is (X, Y, Z in metres) and, if
/// it stores a rotation, how it is turned around the X, Y and Z axes (pitch, yaw and roll, in
/// degrees). Typing a number or dragging a field's label moves or turns the object straight away.
/// </summary>
sealed class TransformFields
{
    readonly ZoneObject obj;
    readonly NumberField x, y, z, yaw, pitch, roll;
    readonly Action<Vector3> move;
    readonly Action<Matrix4x4> turn;

    /// <param name="roundMove">Rounds a position while a label is dragged (snapping), or leaves it.</param>
    /// <param name="roundTurn">Rounds an angle in degrees while a label is dragged (snapping), or leaves it.</param>
    /// <param name="move">Moves the object to a position.</param>
    /// <param name="turn">Turns the object to a rotation.</param>
    /// <param name="committed">Called when a change is finished (one undo step).</param>
    public TransformFields(ZoneObject obj, Func<double, double> roundMove, Func<double, double> roundTurn,
                           Action<Vector3> move, Action<Matrix4x4> turn, Action committed)
    {
        this.obj = obj;
        this.move = move;
        this.turn = turn;
        x = new NumberField("X", Theme.Brush("AxisXBrush"), "0.###", 0.05, "Position east-west, in metres");
        y = new NumberField("Y", Theme.Brush("AxisYBrush"), "0.###", 0.05, "Height, in metres");
        z = new NumberField("Z", Theme.Brush("AxisZBrush"), "0.###", 0.05, "Position north-south, in metres");
        pitch = new NumberField("X", Theme.Brush("AxisXBrush"), "0.##", 0.5, "Pitch: tilt around the X axis (sideways), in degrees");
        yaw = new NumberField("Y", Theme.Brush("AxisYBrush"), "0.##", 0.5, "Yaw: turn around the Y axis (up), in degrees");
        roll = new NumberField("Z", Theme.Brush("AxisZBrush"), "0.##", 0.5, "Roll: tilt around the Z axis (forward), in degrees");

        foreach (NumberField field in new[] { x, y, z })
        {
            field.Round = roundMove;
            field.Changed += _ => Move();
            field.Committed += committed;
        }
        foreach (NumberField field in new[] { yaw, pitch, roll })
        {
            field.Round = roundTurn;
            field.Changed += _ => Turn();
            field.Committed += committed;
        }

        bool canTurn = obj.CanTurn && Euler.IsPlainRotation(obj.Placement.Rotation);
        var panel = new StackPanel();
        panel.Children.Add(Row("Position", x, y, z));
        panel.Children.Add(Row("Rotation", pitch, yaw, roll));
        if (!canTurn)
        {
            foreach (NumberField field in new[] { yaw, pitch, roll }) field.IsEnabled = false;
            panel.Children.Add(new TextBlock
            {
                Text = obj.CanTurn
                    ? "Its rotation also stretches or mirrors it, so it can only be turned with the rotate tool."
                    : "It has no rotation of its own, so it can be moved but not turned.",
                Foreground = Theme.Brush("DimTextBrush"), FontSize = 11, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 2, 0, 0),
            });
        }
        View = panel;
        Refresh();
    }

    public FrameworkElement View { get; }

    /// <summary>Shows the object's position and rotation as they are now (after a drag, an undo, ...).</summary>
    public void Refresh()
    {
        (Vector3 position, Matrix4x4 rotation) = obj.Placement;
        x.Show(position.X);
        y.Show(position.Y);
        z.Show(position.Z);
        (double yawNow, double pitchNow, double rollNow) = Euler.FromRotation(rotation);
        yaw.Show(yawNow);
        pitch.Show(pitchNow);
        roll.Show(rollNow);
    }

    void Move() => move(new Vector3((float)x.Value, (float)y.Value, (float)z.Value));

    void Turn() => turn(Euler.ToRotation(yaw.Value, pitch.Value, roll.Value));

    /// <summary>A row: its name, then three fields side by side.</summary>
    static FrameworkElement Row(string name, NumberField a, NumberField b, NumberField c)
    {
        var grid = new Grid { Margin = new Thickness(0, 2, 0, 2) };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(70) });
        for (int i = 0; i < 3; i++) grid.ColumnDefinitions.Add(new ColumnDefinition());
        var label = new TextBlock { Text = name, Foreground = Theme.Brush("DimTextBrush"), VerticalAlignment = VerticalAlignment.Center };
        grid.Children.Add(label);
        NumberField[] fields = [a, b, c];
        for (int i = 0; i < 3; i++)
        {
            fields[i].Margin = new Thickness(i == 0 ? 0 : 4, 0, 0, 0);
            Grid.SetColumn(fields[i], i + 1);
            grid.Children.Add(fields[i]);
        }
        return grid;
    }
}

/// <summary>
/// Rotations as three angles in degrees, the way people think of them: yaw (turning around the up
/// axis), pitch (tilting up and down) and roll (tilting sideways). The same angles, in the same
/// order, as System.Numerics' Matrix4x4.CreateFromYawPitchRoll, whose matrices have the same
/// layout as the game's (rows: right, up, forward).
/// </summary>
static class Euler
{
    public static (double Yaw, double Pitch, double Roll) FromRotation(Matrix4x4 r)
    {
        // CreateFromYawPitchRoll makes roll · pitch · yaw, whose third row (forward) is
        // (cos p sin y, -sin p, cos p cos y): that gives the yaw. Taking the yaw back out leaves
        // roll · pitch = (cos r, ., . / -sin r, ., . / 0, -sin p, cos p), which gives the other two.
        // Pointing nearly straight up or down, the yaw is uncertain (yaw and roll then turn the
        // same way), but the roll worked out after it makes up for that, so the angles still give
        // back the same rotation.
        double yaw = Math.Abs(r.M31) + Math.Abs(r.M33) > 1e-6 ? Math.Atan2(r.M31, r.M33) : 0;
        Matrix4x4 rest = r * Matrix4x4.CreateRotationY((float)-yaw);
        double pitch = Math.Atan2(-rest.M32, rest.M33);
        double roll = Math.Atan2(-rest.M21, rest.M11);
        return (Degrees(yaw), Degrees(pitch), Degrees(roll));
    }

    public static Matrix4x4 ToRotation(double yaw, double pitch, double roll) =>
        Matrix4x4.CreateFromYawPitchRoll((float)Radians(yaw), (float)Radians(pitch), (float)Radians(roll));

    /// <summary>Whether a matrix only turns (no stretching or mirroring), so three angles can say it.</summary>
    public static bool IsPlainRotation(Matrix4x4 r)
    {
        Matrix4x4 square = r * Matrix4x4.Transpose(r);  // the identity for a plain rotation
        float[] along = [square.M11, square.M22, square.M33], across = [square.M12, square.M13, square.M23];
        return r.GetDeterminant() > 0 && along.All(v => Math.Abs(v - 1) < 0.01f) && across.All(v => Math.Abs(v) < 0.01f);
    }

    static double Degrees(double radians)
    {
        double degrees = radians * 180 / Math.PI;
        return Math.Abs(degrees) < 1e-4 ? 0 : degrees;  // no "-0"
    }

    static double Radians(double degrees) => degrees * Math.PI / 180;
}
